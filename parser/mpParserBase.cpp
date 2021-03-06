/** \file
	\brief Implementation of the muParserX engine.

	<pre>
			   __________                                 ____  ___
	 _____  __ _\______   \_____ _______  ______ __________\   \/  /
	/     \|  |  \     ___/\__  \\_  __ \/  ___// __ \_  __ \     /
	|  Y Y  \  |  /    |     / __ \|  | \/\___ \\  ___/|  | \/     \
	|__|_|  /____/|____|    (____  /__|  /____  >\___  >__| /___/\  \
		  \/                     \/           \/     \/           \_/
	Copyright (C) 2016 Ingo Berg
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	this list of conditions and the following disclaimer in the documentation
	and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
	ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
	IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
	INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
	NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
	WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
	</pre>
	*/
#include "mpParserBase.h"

#include <cmath>
#include <memory>
#include <vector>
#include <sstream>

#include "utGeneric.h"
#include "mpDefines.h"
#include "mpIfThenElse.h"
#include "mpScriptTokens.h"

using namespace std;

MUP_NAMESPACE_START

//------------------------------------------------------------------------------
const char_type *g_sCmdCode[] = {
	_T("BRCK. OPEN       "),
	_T("BRCK. CLOSE      "),
	_T("IDX OPEN         "),
	_T("IDX CLOSE        "),
	_T("CURLY BRCK. OPEN "),
	_T("CURLY BRCK. CLOSE"),
	_T("ARG_SEP          "),
	_T("IF               "),
	_T("ELSE             "),
	_T("ENDIF            "),
	_T("JMP              "),
	_T("VAL              "),
	_T("FUNC             "),
	_T("OPRT_BIN         "),
	_T("OPRT_IFX         "),
	_T("OPRT_PFX         "),
	_T("END              "),
	_T("SCR_ENDL         "),
	_T("SCR_CMT          "),
	_T("SCR_WHILE        "),
	_T("SCR_GOTO         "),
	_T("SCR_LABEL        "),
	_T("SCR_FOR          "),
	_T("SCR_IF           "),
	_T("SCR_ELSE         "),
	_T("SCR_ELIF         "),
	_T("SCR_ENDIF        "),
	_T("SCR_FUNC         "),
	_T("UNKNOWN          "),
	nullptr };

//------------------------------------------------------------------------------
bool ParserXBase::s_bDumpStack = false;
bool ParserXBase::s_bDumpRPN = false;

//------------------------------------------------------------------------------
/** \brief Identifiers for built in binary operators.

	  When defining custom binary operators with AddOprt(...) make sure not to choose
	  names conflicting with these definitions.
	  */
const char_type* ParserXBase::c_DefaultOprt[] = {
	_T("("),
	_T(")"),
	_T("["),
	_T("]"),
	_T("{"),
	_T("}"),
	_T(","),
	_T("?"),
	_T(":"),
	_T(";"),
	0 };

//------------------------------------------------------------------------------
/** \brief Default constructor. */
ParserXBase::ParserXBase()
	:m_FunDef()
	, m_PostOprtDef()
	, m_InfixOprtDef()
	, m_OprtDef()
	, m_valDef()
	, m_varDef()
	, m_pParserEngine(&ParserXBase::ParseFromString)
	, m_pTokenReader()
	, m_valDynVarShadow()
	, m_sNameChars()
	, m_sOprtChars()
	, m_sInfixOprtChars()
	, m_bIsQueryingExprVar(false)
	, m_bAutoCreateVar(false)
	, m_rpn()
	, m_vStackBuffer()
{
	InitTokenReader();
}

//---------------------------------------------------------------------------
/** \brief Copy constructor.
	  \param a_Parser Reference to the other parser object

	  Implemented by calling Assign(a_Parser)
	  */
ParserXBase::ParserXBase(const ParserXBase &a_Parser)
	:m_FunDef()
	, m_PostOprtDef()
	, m_InfixOprtDef()
	, m_OprtDef()
	, m_valDef()
	, m_varDef()
	, m_pParserEngine(&ParserXBase::ParseFromString)
	, m_pTokenReader()
	, m_valDynVarShadow()
	, m_sNameChars()
	, m_sOprtChars()
	, m_sInfixOprtChars()
	, m_bAutoCreateVar()
	, m_rpn()
	, m_vStackBuffer()
{
	m_pTokenReader.reset(new TokenReader(this));
	Assign(a_Parser);
}

//---------------------------------------------------------------------------
/** \brief Destructor.
	  \throw nothrow
	  */
ParserXBase::~ParserXBase()
{
	// It is important to release the stack buffer before
	// releasing the value cache. Since it may contain
	// Values referencing the cache.
	m_vStackBuffer.clear();
	m_cache.ReleaseAll();
}

//---------------------------------------------------------------------------
/** \brief Assignement operator.
	  \param a_Parser Object to copy to this.
	  \return *this
	  \throw nothrow

	  Implemented by calling Assign(a_Parser). Self assignement is suppressed.
	  */
ParserXBase& ParserXBase::operator=(const ParserXBase &a_Parser)
{
	Assign(a_Parser);
	return *this;
}

//---------------------------------------------------------------------------
/** \brief Copy state of a parser object to this.
	  \param a_Parser the source object.

	  Clears Variables and Functions of this parser.
	  Copies the states of all internal variables.
	  Resets parse function to string parse mode.
	  */
void ParserXBase::Assign(const ParserXBase &ref)
{
	if (&ref == this)
		return;

	// Don't copy bytecode instead cause the parser to create new bytecode
	// by resetting the parse function.
	ReInit();

	m_pTokenReader.reset(ref.m_pTokenReader->Clone(this));

	m_OprtDef = ref.m_OprtDef;
	m_FunDef = ref.m_FunDef;
	m_PostOprtDef = ref.m_PostOprtDef;
	m_InfixOprtDef = ref.m_InfixOprtDef;
	m_valDef = ref.m_valDef;
	m_valDynVarShadow = ref.m_valDynVarShadow;
	m_varDef = ref.m_varDef;             // Copy user defined variables

	// Copy charsets
	m_sNameChars = ref.m_sNameChars;
	m_sOprtChars = ref.m_sOprtChars;
	m_sInfixOprtChars = ref.m_sInfixOprtChars;

	m_bAutoCreateVar = ref.m_bAutoCreateVar;

	// Things that should not be copied:
	// - m_vStackBuffer
	// - m_cache
	// - m_rpn
}
//---------------------------------------------------------------------------
/* 
Copy state of a parser object to this
without resetting this
*/
void ParserXBase::AssignVars(const ParserXBase &ref)
{
	if (&ref == this)
		return;
	m_OprtDef = ref.m_OprtDef;
	m_FunDef = ref.m_FunDef;
	m_PostOprtDef = ref.m_PostOprtDef;
	m_InfixOprtDef = ref.m_InfixOprtDef;
	m_valDef = ref.m_valDef;
	m_valDynVarShadow = ref.m_valDynVarShadow;
	m_varDef = ref.m_varDef;             // Copy user defined variables

	// Copy charsets
	m_sNameChars = ref.m_sNameChars;
	m_sOprtChars = ref.m_sOprtChars;
	m_sInfixOprtChars = ref.m_sInfixOprtChars;

	m_bAutoCreateVar = ref.m_bAutoCreateVar;

	// Things that should not be copied:
	// - m_vStackBuffer
	// - m_cache
	// - m_rpn
}

//---------------------------------------------------------------------------
/** \brief Evaluate the expression.
	  \pre A formula must be set.
	  \pre Variables must have been set (if needed)
	  \sa SetExpr
	  \return The evaluation result
	  \throw ParseException if no Formula is set or in case of any other error related to the formula.

	  A note on const correctness:
	  I consider it important that Calc is a const function.
	  Due to caching operations Calc changes only the state of internal variables with one exception
	  m_UsedVar this is reset during string parsing and accessible from the outside. Instead of making
	  Calc non const GetExprVar is non const because it explicitely calls Eval() forcing this update.
	  */
const IValue& ParserXBase::Eval() const
{
	return (this->*m_pParserEngine)();
}

//---------------------------------------------------------------------------
/** \brief Return the strings of all Operator identifiers.
	  \return Returns a pointer to the c_DefaultOprt array of const char *.
	  \throw nothrow

	  GetOprt is a const function returning a pinter to an array of const char pointers.
	  */
const char_type** ParserXBase::GetOprtDef() const
{
	return (const char_type **)(&c_DefaultOprt[0]);
}

//---------------------------------------------------------------------------
/** \brief Define the set of valid characters to be used in names of
			  functions, variables, constants.
			  */
void ParserXBase::DefineNameChars(const char_type *a_szCharset)
{
	m_sNameChars = a_szCharset;
}

//---------------------------------------------------------------------------
/** \brief Define the set of valid characters to be used in names of
			 binary operators and postfix operators.
			 \param a_szCharset A string containing all characters that can be used
			 in operator identifiers.
			 */
void ParserXBase::DefineOprtChars(const char_type *a_szCharset)
{
	m_sOprtChars = a_szCharset;
}

//---------------------------------------------------------------------------
/** \brief Define the set of valid characters to be used in names of
			 infix operators.
			 \param a_szCharset A string containing all characters that can be used
			 in infix operator identifiers.
			 */
void ParserXBase::DefineInfixOprtChars(const char_type *a_szCharset)
{
	m_sInfixOprtChars = a_szCharset;
}

//---------------------------------------------------------------------------
/** \brief Virtual function that defines the characters allowed in name identifiers.
	  \sa #ValidOprtChars, #ValidPrefixOprtChars
	  */
const char_type* ParserXBase::ValidNameChars() const
{
	MUP_VERIFY(m_sNameChars.size());
	return m_sNameChars.c_str();
}

//---------------------------------------------------------------------------
/** \brief Virtual function that defines the characters allowed in operator definitions.
	  \sa #ValidNameChars, #ValidPrefixOprtChars
	  */
const char_type* ParserXBase::ValidOprtChars() const
{
	MUP_VERIFY(m_sOprtChars.size());
	return m_sOprtChars.c_str();
}

//---------------------------------------------------------------------------
/** \brief Virtual function that defines the characters allowed in infix operator definitions.
	  \sa #ValidNameChars, #ValidOprtChars
	  */
const char_type* ParserXBase::ValidInfixOprtChars() const
{
	MUP_VERIFY(m_sInfixOprtChars.size());
	return m_sInfixOprtChars.c_str();
}

//---------------------------------------------------------------------------
/** \brief Initialize the token reader.
	  \post m_pTokenReader.Get()!=0
	  \throw nothrow

	  Create new token reader object and submit pointers to function, operator,
	  constant and variable definitions.
	  */
void ParserXBase::InitTokenReader()
{
	m_pTokenReader.reset(new TokenReader(this));
}

//---------------------------------------------------------------------------
/** \brief Reset parser to string parsing mode and clear internal buffers.
	  \throw nothrow

	  Resets the token reader.
	  */
void ParserXBase::ReInit() const
{
	m_pParserEngine = &ParserXBase::ParseFromString;
	m_pTokenReader->ReInit();
	m_rpn.reset();
	m_vStackBuffer.clear();
	m_nPos = 0;
	Curly_Number = 0;
}

//---------------------------------------------------------------------------
/** \brief Adds a new package to the parser.

	The parser becomes the owner of the package pointer and is responsible for
	its deletion.
	*/
void ParserXBase::AddPackage(IPackage *p)
{
	p->AddToParser(this);
}

//---------------------------------------------------------------------------
/** \brief Add a value reader object to muParserX.
	  \param a_pReader Pointer to the value reader object.
	  */
void ParserXBase::AddValueReader(IValueReader *a_pReader)
{
	m_pTokenReader->AddValueReader(a_pReader);
}

//---------------------------------------------------------------------------
/** \brief Check if a given name contains invalid characters.
	  \param a_strName The name to check
	  \param a_szCharSet The characterset
	  \throw ParserException if the name contains invalid charakters.
	  */
void ParserXBase::CheckName(const string_type &a_strName,
	const string_type &a_szCharSet) const
{
	if (!a_strName.length() ||
		(a_strName.find_first_not_of(a_szCharSet) != string_type::npos) ||
		(a_strName[0] >= (char_type)'0' && a_strName[0] <= (char_type)'9'))
	{
		Error(ecINVALID_NAME);
	}
}

//---------------------------------------------------------------------------
/** \brief Set the mathematical expression.
	  \param a_sExpr String with the expression
	  \throw ParserException in case of syntax errors.

	  Triggers first time calculation thus the creation of the bytecode and
	  scanning of used variables.
	  */
void ParserXBase::SetExpr(const string_type &a_sExpr)
{
	m_pTokenReader->SetExpr(a_sExpr);
	ReInit();
}

//---------------------------------------------------------------------------
/** \brief Add a user defined variable.
	  \param a_sName The variable name
	  \param a_Var The variable to be added to muParserX
	  */
void ParserXBase::DefineVar(const string_type &ident, const Variable &var)
{
	CheckName(ident, ValidNameChars());

	CheckForEntityExistence(ident, ecVARIABLE_DEFINED);

	m_varDef[ident] = var.Clone();
}

void ParserXBase::SetVar(const string_type &ident, const Variable &var)
{
	*((Variable*)m_varDef[ident].get())->Get_Value() = *((Variable*)var.Clone().get())->Get_Value();
}
Value ParserXBase::GetVar(const string_type &ident)
{
	return *(static_pointer_cast<IValue>(m_varDef[ident]));
}

void ParserXBase::CheckForEntityExistence(const string_type &ident, EErrorCodes error_code)
{
	if (IsVarDefined(ident) ||
		IsConstDefined(ident) ||
		IsFunDefined(ident) ||
		IsOprtDefined(ident) ||
		IsPostfixOprtDefined(ident) ||
		IsInfixOprtDefined(ident))
		throw ParserError(ErrorContext(error_code, 0, ident));
}


//---------------------------------------------------------------------------
/** \brief Define a parser Constant.
		\param a_sName The name of the constant
		\param a_Val Const reference to the constants value

		Parser constants are handed over by const reference as opposed to variables
		which are handed over by reference. Consequently the parser can not change
		their value.
		*/
void ParserXBase::DefineConst(const string_type &ident, const Value &val)
{
	CheckName(ident, ValidNameChars());

	CheckForEntityExistence(ident, ecCONSTANT_DEFINED);

	m_valDef[ident] = ptr_tok_type(val.Clone());
}

void ParserXBase::SetConst(const string_type &ident, const Value &val)
{
	*((Value*)m_valDef[ident].get())->Get_Value() = *((Value*)val.Clone().get())->Get_Value();
}

Value ParserXBase::GetConst(const string_type &ident)
{
	return *(static_pointer_cast<IValue>(m_valDef[ident]));
}

//---------------------------------------------------------------------------
/** \brief Add a callback object to the parser.
		\param a_pFunc Pointer to the intance of a parser callback object
		representing the function.
		\sa GetFunDef, functions

		The parser takes ownership over the callback object.
		*/
void ParserXBase::DefineFun(const ptr_cal_type &fun)
{
	if (IsFunDefined(fun->GetIdent()))
		throw ParserError(ErrorContext(ecFUNOPRT_DEFINED, 0, fun->GetIdent()));

	fun->SetParent(this);
	m_FunDef[fun->GetIdent()] = ptr_tok_type(fun->Clone());
}

//---------------------------------------------------------------------------
/** \brief Define a binary operator.
		\param a_pCallback Pointer to the callback object
		*/
void ParserXBase::DefineOprt(const std::shared_ptr<IOprtBin> &oprt)
{
	if (IsOprtDefined(oprt->GetIdent()))
		throw ParserError(ErrorContext(ecFUNOPRT_DEFINED, 0, oprt->GetIdent()));

	oprt->SetParent(this);
	m_OprtDef[oprt->GetIdent()] = ptr_tok_type(oprt->Clone());
}

//---------------------------------------------------------------------------
/** \brief Add a user defined operator.
	  \post Will reset the Parser to string parsing mode.
	  \param a_pOprt Pointer to a unary postfix operator object. The parser will
	  become the new owner of this object hence will destroy it.
	  */
void ParserXBase::DefinePostfixOprt(const std::shared_ptr<IOprtPostfix> &oprt)
{
	if (IsPostfixOprtDefined(oprt->GetIdent()))
		throw ParserError(ErrorContext(ecFUNOPRT_DEFINED, 0, oprt->GetIdent()));

	// Operator is not added yet, add it.
	oprt->SetParent(this);
	m_PostOprtDef[oprt->GetIdent()] = ptr_tok_type(oprt->Clone());
}

//---------------------------------------------------------------------------
/** \brief Add a user defined operator.
	\param a_pOprt Pointer to a unary postfix operator object. The parser will
		   become the new owner of this object hence will destroy it.
*/
void ParserXBase::DefineInfixOprt(const std::shared_ptr<IOprtInfix> &oprt)
{
	if (IsInfixOprtDefined(oprt->GetIdent()))
		throw ParserError(ErrorContext(ecFUNOPRT_DEFINED, 0, oprt->GetIdent()));

	// Function is not added yet, add it.
	oprt->SetParent(this);
	m_InfixOprtDef[oprt->GetIdent()] = ptr_tok_type(oprt->Clone());
}

//---------------------------------------------------------------------------
void ParserXBase::RemoveVar(const string_type &ident)
{
	m_varDef.erase(ident);
	ReInit();
}

//---------------------------------------------------------------------------
void ParserXBase::RemoveConst(const string_type &ident)
{
	m_valDef.erase(ident);
	ReInit();
}

//---------------------------------------------------------------------------
void ParserXBase::RemoveFun(const string_type &ident)
{
	m_FunDef.erase(ident);
	ReInit();
}

//---------------------------------------------------------------------------
void ParserXBase::RemoveOprt(const string_type &ident)
{
	m_OprtDef.erase(ident);
	ReInit();
}

//---------------------------------------------------------------------------
void ParserXBase::RemovePostfixOprt(const string_type &ident)
{
	m_PostOprtDef.erase(ident);
	ReInit();
}

//---------------------------------------------------------------------------
void ParserXBase::RemoveInfixOprt(const string_type &ident)
{
	m_InfixOprtDef.erase(ident);
	ReInit();
}

//---------------------------------------------------------------------------
bool ParserXBase::IsVarDefined(const string_type &ident) const
{
	return m_varDef.find(ident) != m_varDef.end();
}

//---------------------------------------------------------------------------
bool ParserXBase::IsConstDefined(const string_type &ident) const
{
	return m_valDef.find(ident) != m_valDef.end();
}

//---------------------------------------------------------------------------
bool ParserXBase::IsFunDefined(const string_type &ident) const
{
	return m_FunDef.find(ident) != m_FunDef.end();
}

//---------------------------------------------------------------------------
bool ParserXBase::IsOprtDefined(const string_type &ident) const
{
	return m_OprtDef.find(ident) != m_OprtDef.end();
}

//---------------------------------------------------------------------------
bool ParserXBase::IsPostfixOprtDefined(const string_type &ident) const
{
	return m_PostOprtDef.find(ident) != m_PostOprtDef.end();
}

//---------------------------------------------------------------------------
bool ParserXBase::IsInfixOprtDefined(const string_type &ident) const
{
	return m_InfixOprtDef.find(ident) != m_InfixOprtDef.end();
}

//---------------------------------------------------------------------------
/** \brief Return a map containing the used variables only. */
const var_maptype& ParserXBase::GetExprVar() const
{
	utils::scoped_setter<bool> guard(m_bIsQueryingExprVar, true);

	// Create RPN,  but do not compute the result or switch to RPN
	// parsing mode. The expression may contain yet to be defined variables.
	CreateRPN();
	return m_pTokenReader->GetUsedVar();
}

//---------------------------------------------------------------------------
/** \brief Return a map containing the used variables only. */
const var_maptype& ParserXBase::GetVar() const
{
	return m_varDef;
}

//---------------------------------------------------------------------------
/** \brief Return a map containing all parser constants. */
const val_maptype& ParserXBase::GetConst() const
{
	return m_valDef;
}

//---------------------------------------------------------------------------
/** \brief Return prototypes of all parser functions.
	  \return #m_FunDef
	  \sa FunProt, functions
	  \throw nothrow

	  The return type is a map of the public type #funmap_type containing the prototype
	  definitions for all numerical parser functions. String functions are not part of
	  this map. The Prototype definition is encapsulated in objects of the class FunProt
	  one per parser function each associated with function names via a map construct.
	  */
const fun_maptype& ParserXBase::GetFunDef() const
{
	return m_FunDef;
}

//---------------------------------------------------------------------------
/** \brief Retrieve the mathematical expression. */
const string_type& ParserXBase::GetExpr() const
{
	return m_pTokenReader->GetExpr();
}

//---------------------------------------------------------------------------
/** \brief Get the version number of muParserX.
	  \return A string containing the version number of muParserX.
	  */
string_type ParserXBase::GetVersion()
{
	return MUP_PARSER_VERSION;
}

//---------------------------------------------------------------------------
void ParserXBase::ApplyRemainingOprt(Stack<ptr_tok_type> &stOpt) const

{
	while (stOpt.size() &&
		stOpt.top()->GetCode() != cmBO  &&
		stOpt.top()->GetCode() != cmIO  &&
		stOpt.top()->GetCode() != cmCBO &&
		stOpt.top()->GetCode() != cmIF)
	{
		ptr_tok_type &op = stOpt.top();
		switch (op->GetCode())
		{
		case  cmOPRT_INFIX:
		case  cmOPRT_BIN:    ApplyFunc(stOpt, 2);   break;
		case  cmELSE:        ApplyIfElse(stOpt);    break;
		default:             Error(ecINTERNAL_ERROR);
		} // switch operator token type
	} // While operator stack not empty
}

//---------------------------------------------------------------------------
/** \brief Simulates the call of a parser function with its corresponding arguments.
	  \param a_stOpt The operator stack
	  \param a_stVal The value stack
	  \param a_iArgCount The number of function arguments
	  */
void ParserXBase::ApplyFunc(Stack<ptr_tok_type> &a_stOpt,
	int a_iArgCount) const
{
	if (a_stOpt.empty())
		return;

	ptr_tok_type tok = a_stOpt.pop();
	ICallback *pFun = tok->AsICallback();

	int iArgCount = (pFun->GetArgc() >= 0) ? pFun->GetArgc() : a_iArgCount;
	pFun->SetNumArgsPresent(iArgCount);

	m_nPos -= (iArgCount - 1);
	m_rpn.Add(tok);
}

//---------------------------------------------------------------------------
/** \brief Simulates the effect of the execution of an if-then-else block.
*/
void ParserXBase::ApplyIfElse(Stack<ptr_tok_type> &a_stOpt) const
{
	while (a_stOpt.size() && a_stOpt.top()->GetCode() == cmELSE)
	{
		MUP_VERIFY(a_stOpt.size() > 0);
		MUP_VERIFY(m_nPos >= 3);
		MUP_VERIFY(a_stOpt.top()->GetCode() == cmELSE);

		ptr_tok_type opElse = a_stOpt.pop();
		ptr_tok_type opIf = a_stOpt.pop();
		MUP_VERIFY(opElse->GetCode() == cmELSE)
		MUP_VERIFY(opIf->GetCode() == cmIF)

		// If then else hat 3 argumente und erzeugt einen rückgabewert (3-1=2)
		m_nPos -= 2;
		m_rpn.Add(ptr_tok_type(new TokenIfThenElse(cmENDIF)));
	}
}

//---------------------------------------------------------------------------
void ParserXBase::DumpRPN() const
{
	m_rpn.AsciiDump();
}

void ParserXBase::Count_Max_Nesting_Level() const
{
	int If_Nest_Level_Max = 0;
	int Loop_Level_Max = 0;
	ECmdCode eCmd;
	for (;;)
	{
		eCmd = m_pTokenReader->CheckIfsAndLoops();
		switch (eCmd)
		{
		case cmSCRIPT_IF:
			If_Nest_Level_Max++;
			break;
		case cmSCRIPT_LOOP:
			Loop_Level_Max++;
			break;
		default:
			break;
		}
		if (eCmd == cmEOE)
			break;
	}
	if (!If_Nest_Level_Max)
		If_Nest_Level_Max = 1;
	Number_Of_Curlys_At_IfElse_Level.resize(If_Nest_Level_Max);
	Parse_If_Or_Else.resize(If_Nest_Level_Max);
	if (!Loop_Level_Max)
		Loop_Level_Max = 1;
	Number_Of_Curlys_At_Loop_Level.resize(Loop_Level_Max+1);	// UnsureJedi note: "+1" is because I messed indices in CreateRPN() a bit so they are shifted by +1. For now I'm too lazy to fix it properly so I just added an extra level
}

//---------------------------------------------------------------------------
void ParserXBase::CreateRPN() const
{
	if (!m_pTokenReader->GetExpr().length())
		Error(ecUNEXPECTED_EOF, 0);

	// The Stacks take the ownership over the tokens
	Stack<ptr_tok_type> stOpt;
	Stack<int>  stArgCount;
	Stack<int>  stIdxCount;
	ptr_tok_type pTok, pTokPrev;
	Value val;

	ReInit();
	Count_Max_Nesting_Level();
	ReInit();
	Parse_If_Condition = 0;
	If_Nest_Level = 0;
	Parse_If_Or_Else[0] = 0;
	Bracket_Number = 0;
	Curly_Number = 0;
	Loop_Level = 0;
	for (;;)
	{
		pTokPrev = pTok;
		pTok = m_pTokenReader->ReadNextToken();

#if defined(MUP_DUMP_TOKENS)
		console() << pTok->AsciiDump() << endl;
#endif

		ECmdCode eCmd = pTok->GetCode();
		switch (eCmd)
		{
		case  cmVAL:
			m_nPos++;
			m_rpn.Add(pTok);
			break;

		case  cmCBC:
			Curly_Number--;
			// Check if this curly bracket closes Loop body
			if (Loop_Level && Number_Of_Curlys_At_Loop_Level[Loop_Level] == Curly_Number + 1)
			{
				Loop_Level--;

				ApplyRemainingOprt(stOpt);

				m_rpn.Add(pTok);
				m_nPos--;
				break;
			}
			// Check if this curly bracket closes If body
			if (Parse_If_Or_Else[If_Nest_Level] == 1 && Number_Of_Curlys_At_IfElse_Level[If_Nest_Level] == Curly_Number + 1)
			{
				ApplyRemainingOprt(stOpt);

				m_rpn.Add(pTok);
				m_nPos--;

				m_pTokenReader->m_nSynFlags = sfALLOW_NONE;
				//only "Else", EOE, or newline statement is allowed next
				m_pTokenReader->m_nSynFlags ^= noSCRIPT_ELSE;
				m_pTokenReader->m_nSynFlags ^= noNEWLINE;
				m_pTokenReader->m_nSynFlags ^= noEND;
				// In case there is no Else body following, decrease the nesting level and reset the checker
				Parse_If_Or_Else[If_Nest_Level] = 0;
				If_Nest_Level--;		
				break;
			}
			// Check if this curly bracket closes Else body
			if (Parse_If_Or_Else[If_Nest_Level] == 2 && Number_Of_Curlys_At_IfElse_Level[If_Nest_Level] == Curly_Number + 1)
			{
				Parse_If_Or_Else[If_Nest_Level] = 0;
				If_Nest_Level--;

				ApplyRemainingOprt(stOpt);

				m_rpn.Add(pTok);
				m_nPos--;

				break;
			}
		case  cmIC:
		{
			ECmdCode eStarter = (ECmdCode)(eCmd - 1);
			MUP_VERIFY(eStarter == cmCBO || eStarter == cmIO);

			// The argument count for parameterless functions is zero
			// by default an opening bracket sets parameter count to 1
			// in preparation of arguments to come. If the last token
			// was an opening bracket we know better...
			if (pTokPrev.get() != nullptr && pTokPrev->GetCode() == eStarter)
				--stArgCount.top();

			ApplyRemainingOprt(stOpt);

			// if opt is "]" and opta is "[" the bracket content has been evaluated.
			// Now check whether there is an index operator on the stack.
			if (stOpt.size() && stOpt.top()->GetCode() == eStarter)
			{
				//
				// Find out how many dimensions were used in the index operator.
				//
				int iArgc = stArgCount.pop();
				stOpt.pop(); // Take opening bracket from stack

				MUP_VERIFY(pTok->AsICallback() != nullptr);

				static_pointer_cast<ICallback>(pTok)->SetNumArgsPresent(iArgc);
				m_rpn.Add(pTok);

				// If this is an index operator there must be something else in the register (the variable to index)
				MUP_VERIFY(eCmd != cmIC || m_nPos >= (int)iArgc + 1);

				// Reduce the index into the value registers accordingly
				m_nPos -= iArgc;

				if (eCmd == cmCBC)
				{
					++m_nPos;
				}
			} // if opening index bracket is on top of operator stack
		}
		break;

		case  cmBC:
		{
			Bracket_Number--;
			// Check if closing bracket closes If-condition
			if (Parse_If_Condition)
			{
				if (!Bracket_Number)
				{
					ApplyRemainingOprt(stOpt);

					m_rpn.Add(pTok);
					m_nPos--;

					Parse_If_Condition = 0;
					m_pTokenReader->m_nSynFlags = sfALLOW_NONE;
					// After condition brackets only curly bracket is allowed for If body
					m_pTokenReader->m_nSynFlags ^= ~noCBO;				
					Parse_If_Or_Else[If_Nest_Level] = 1;	// We're done parsing condition, move on to parsing If body
					break;
				}
			}
			// The argument count for parameterless functions is zero
			// by default an opening bracket sets parameter count to 1
			// in preparation of arguments to come. If the last token
			// was an opening bracket we know better...
			if (pTokPrev.get() != nullptr && pTokPrev->GetCode() == cmBO)
				--stArgCount.top();

			ApplyRemainingOprt(stOpt);

			// if opt is ")" and opta is "(" the bracket content has been evaluated.
			// Now its time to check if there is either a function or a sign pending.
			// - Neither the opening nor the closing bracket will be pushed back to
			//   the operator stack
			// - Check if a function is standing in front of the opening bracket,
			//   if so evaluate it afterwards to apply an infix operator.
			if (stOpt.size() && stOpt.top()->GetCode() == cmBO)
			{
				//
				// Here is the stuff to evaluate a function token
				//
				int iArgc = stArgCount.pop();

				stOpt.pop(); // Take opening bracket from stack
				if (stOpt.empty())
					break;

				if ((stOpt.top()->GetCode() != cmFUNC) && (stOpt.top()->GetCode() != cmOPRT_INFIX))
					break;

				ICallback *pFun = stOpt.top()->AsICallback();

				if (pFun->GetArgc() != -1 && iArgc > pFun->GetArgc())
					Error(ecTOO_MANY_PARAMS, pTok->GetExprPos(), pFun);

				if (iArgc < pFun->GetArgc())
					Error(ecTOO_FEW_PARAMS, pTok->GetExprPos(), pFun);

				// Apply function, if present
				if (stOpt.size() &&
					stOpt.top()->GetCode() != cmOPRT_INFIX &&
					stOpt.top()->GetCode() != cmOPRT_BIN)
				{
					ApplyFunc(stOpt, iArgc);
				}
			}
		}
		break;

		case  cmELSE:
			ApplyRemainingOprt(stOpt);
			m_rpn.Add(pTok);
			stOpt.push(pTok);
			break;

		case  cmSCRIPT_NEWLINE:
			ApplyRemainingOprt(stOpt);
			m_rpn.AddNewline(pTok, m_nPos);
			stOpt.clear();
			m_nPos = 0;
			break;

		case cmSCRIPT_IF:
			If_Nest_Level++;
			Parse_If_Condition = 1;
			m_nPos++;
			m_rpn.Add(pTok);
			break;
		case cmSCRIPT_ELSE:
			// Else body is optional, so the closing curly bracket for If body has decreased If_Nest_Level and reset the checker for that level. 
			// If we detect "Else" keyword then we need to increse If_Nest_Level again and set the checker to expect Else body
			If_Nest_Level++;
			Parse_If_Or_Else[If_Nest_Level] = 2;
		case cmSCRIPT_LOOP:
		case cmSCRIPT_BREAK:
			m_nPos++;
			m_rpn.Add(pTok);
			break;

		case  cmENDIF:				
			// Redem: first, pop the stacks that have been pushed by
			// preceding curly brackets
			stOpt.pop();
			stArgCount.pop();

			pTokPrev = pTok;
			pTok = m_pTokenReader->ReadSemicolonStringToken();
			m_nPos++;
			m_rpn.Add(pTok);
			break;

		case  cmARG_SEP:
			if (stArgCount.empty())
				Error(ecUNEXPECTED_COMMA, m_pTokenReader->GetPos() - 1);

			++stArgCount.top();

			ApplyRemainingOprt(stOpt);
			break;

		case  cmEOE:
			ApplyRemainingOprt(stOpt);
			m_rpn.Finalize();
			break;

		case  cmIF:
		case  cmOPRT_BIN:
		{
			while (stOpt.size() &&
				stOpt.top()->GetCode() != cmBO   &&
				stOpt.top()->GetCode() != cmIO   &&
				stOpt.top()->GetCode() != cmCBO  &&
				stOpt.top()->GetCode() != cmELSE &&
				stOpt.top()->GetCode() != cmIF)
			{
				IToken *pOprt1 = stOpt.top().get();
				IToken *pOprt2 = pTok.get();
				MUP_VERIFY(pOprt1 != nullptr && pOprt2 != nullptr);
				MUP_VERIFY(pOprt1->AsIPrecedence() && pOprt2->AsIPrecedence());

				int nPrec1 = pOprt1->AsIPrecedence()->GetPri(),
					nPrec2 = pOprt2->AsIPrecedence()->GetPri();

				if (pOprt1->GetCode() == pOprt2->GetCode())
				{
					// Deal with operator associativity
					EOprtAsct eOprtAsct = pOprt1->AsIPrecedence()->GetAssociativity();
					if ((eOprtAsct == oaRIGHT && (nPrec1 <= nPrec2)) ||
						(eOprtAsct == oaLEFT && (nPrec1 < nPrec2)))
					{
						break;
					}
				}
				else if (nPrec1 < nPrec2)
				{
					break;
				}

				// apply the operator now
				// (binary operators are identic to functions with two arguments)
				ApplyFunc(stOpt, 2);
			} // while ( ... )

			if (pTok->GetCode() == cmIF)
				m_rpn.Add(pTok);

			stOpt.push(pTok);
		}
		break;

		//
		//  Postfix Operators
		//
		case  cmOPRT_POSTFIX:
			MUP_VERIFY(m_nPos);
			m_rpn.Add(pTok);
			break;

		case  cmBO:		
			if (m_nPos && pTokPrev->GetCode() == cmSCRIPT_IF)
			{
				m_rpn.Add(pTok);
			}
			Bracket_Number++;
			stOpt.push(pTok);
			stArgCount.push(1);
			break;
		case  cmCBO:
			Curly_Number++;
			m_rpn.Add(pTok);
			// Check for opening bracket for Loop body
			if (m_nPos && pTokPrev->GetCode() == cmSCRIPT_LOOP)	// Redem note: change the if not to rely on m_nPos
			{
				Loop_Level++;
				Number_Of_Curlys_At_Loop_Level[Loop_Level] = Curly_Number;
				break;
			}
			// Check for opening bracket for If body
			if (Parse_If_Or_Else[If_Nest_Level] == 1)
			{
				Number_Of_Curlys_At_IfElse_Level[If_Nest_Level] = Curly_Number;
				break;
			}
			// Check for opening bracket for Else body
			if (Parse_If_Or_Else[If_Nest_Level] == 2)
			{
				Number_Of_Curlys_At_IfElse_Level[If_Nest_Level] = Curly_Number;
				break;
			}
		case  cmIO:    
			stOpt.push(pTok);
			stArgCount.push(1);
			break;

			//
			// Functions
			//
		case  cmOPRT_INFIX:
		case  cmFUNC:
		{
			MUP_VERIFY(pTok->AsICallback());
			stOpt.push(pTok);
		}
		break;

		default:
			Error(ecINTERNAL_ERROR);
		} // switch Code

		if (ParserXBase::s_bDumpStack)
		{
			StackDump(stOpt);
		}

		if (pTok->GetCode() == cmEOE)
			break;
	} // for (all tokens)

	if (ParserXBase::s_bDumpRPN)
	{
		m_rpn.AsciiDump();
	}

	if (m_nPos > 1)
	{
		Error(ecUNEXPECTED_COMMA, -1);
	}
}

//---------------------------------------------------------------------------
/** \brief One of the two main parse functions.
	  \sa ParseCmdCode(), ParseValue()

	  Parse expression from input string. Perform syntax checking and create bytecode.
	  After parsing the string and creating the bytecode the function pointer
	  #m_pParseFormula will be changed to the second parse routine the uses bytecode instead of string parsing.
	  */
const IValue& ParserXBase::ParseFromString() const
{
	CreateRPN();

	// Umsachalten auf RPN
	m_vStackBuffer.assign(m_rpn.GetRequiredStackSize(), ptr_val_type());
	for (std::size_t i = 0; i < m_vStackBuffer.size(); ++i)
	{
		Value *pValue = new Value;
		pValue->BindToCache(&m_cache);
		m_vStackBuffer[i].reset(pValue, Value_Deleter);
	}
	Preconnect_Curlies_and_Keywords_RPN();
	m_pParserEngine = &ParserXBase::ParseFromRPN;

	return (this->*m_pParserEngine)();
}

// The role of this function is to tie the opening and closing curly brackets together in RPN
// to allow fast jumps between them. Also connect Break statements to corresponding "}"
void ParserXBase::Preconnect_Curlies_and_Keywords_RPN() const
{
	Closing_Curly.resize(m_rpn.GetSize());
	Opening_Curly.resize(m_rpn.GetSize());
	Break_Closing_Curly.resize(m_rpn.GetSize());
	Loop_Curly.resize(m_rpn.GetSize());
	IfElse_Curly.resize(m_rpn.GetSize());

	Curly_Number = 0;
	int j;
	int Curly_Open_Pos;
	ECmdCode eCode = cmUNKNOWN;
	std::size_t lenRPN = m_rpn.GetSize();

	// Initialize the curly identifiers
	for (j = 0; j < m_rpn.GetSize(); j++)
	{
		Loop_Curly[j] = 0;
		IfElse_Curly[j] = 0;
	}

	for (std::size_t i = 0; i < lenRPN; ++i)
	{
		IToken *pTok = m_rpn.GetData()[i].get();
		eCode = pTok->GetCode();

		switch (eCode)
		{
		case cmSCRIPT_BREAK:
			j = i;
			// We should go backwards from current position because all preceding curly brackets have been connected, unlike the following ones
			for (; j > 0; j--)
			{
				if ((m_rpn.GetData()[j].get())->GetCode() == cmCBC)
				{
					// If this is closing bracket for another loop, then skip its body
					if ((m_rpn.GetData()[Opening_Curly[j] - 1].get())->GetCode() == cmSCRIPT_LOOP)
						j = Opening_Curly[j] - 2;
				}
				else if ((m_rpn.GetData()[j].get())->GetCode() == cmCBO)
				{
					// If this opening curly is Loop one, then it's the one we are looking for, because other loops have been skipped by keeping track of closing curlies.
					if ((m_rpn.GetData()[j - 1].get())->GetCode() == cmSCRIPT_LOOP)
					{
						Break_Closing_Curly[i] = Closing_Curly[j];
						break;
					}
				}
			}
			if (j)
				break;
			else
				Error(ecUNEXPECTED_EOF, j);
		
		case cmCBO:
			// At given "{" set the curly number to 1 (this "{") because we will look for it's closing "}" immediately in this "case"
			Curly_Number = 1;		
			Curly_Open_Pos = i;
			j = i + 1;
			for (; j < lenRPN; j++)
			{
				if ((m_rpn.GetData()[j].get())->GetCode() == cmCBO)
					Curly_Number++;
				if ((m_rpn.GetData()[j].get())->GetCode() == cmCBC)
				{
					Curly_Number--;
					if (!Curly_Number)
					{
						// Now let's tie these curlies together
						Opening_Curly[j] = Curly_Open_Pos;
						Closing_Curly[Curly_Open_Pos] = j;
						if (Opening_Curly[j] && (m_rpn.GetData()[Opening_Curly[j] - 1].get())->GetCode() == cmSCRIPT_LOOP)	// Redem note: might be a bug source, if Loop and "{" are separated by newline
						{
							Loop_Curly[j] = Loop_Curly[Opening_Curly[j]] = 1;
						}
						else if (Opening_Curly[j] && ((m_rpn.GetData()[Opening_Curly[j] - 1].get())->GetCode() == cmSCRIPT_ELSE || (m_rpn.GetData()[Opening_Curly[j] - 1].get())->GetCode() == cmBC))
						{
							IfElse_Curly[j] = IfElse_Curly[Opening_Curly[j]] = 1;
						}

						break;
					}
				}
			}
			if (!Curly_Number)
				break;
			else
				Error(ecUNEXPECTED_EOF, j);
		case cmBO:
		case cmSCRIPT_NEWLINE:
		case cmVAL:
		case cmIC:
		case cmBC:
		case cmSCRIPT_IF:
		case cmSCRIPT_ELSE:
		case cmSCRIPT_LOOP:
		case cmCBC:
		case cmOPRT_POSTFIX:
		case cmFUNC:
		case cmOPRT_BIN:
		case cmOPRT_INFIX:
		case cmIF:
		case cmELSE:
		case cmJMP:
		case cmENDIF:
			continue;

		default:
			Error(ecINTERNAL_ERROR);
		} // switch token
	} // for all RPN tokens
}

//---------------------------------------------------------------------------
const IValue& ParserXBase::ParseFromRPN() const
{
	//ptr_val_type *pStack = &(m_vStackBuffer[0]);	// Redem note for myself: pStack gets filled with IValues here, in ParseFromRPN() when values and variables are encountered
	if (m_rpn.GetSize() == 0)
	{
		// Passiert bei leeren strings oder solchen, die nur Leerzeichen enthalten
		ErrorContext err;
		err.Expr = m_pTokenReader->GetExpr();
		err.Errc = ecUNEXPECTED_EOF;
		err.Pos = 0;
		throw ParserError(err);
	}

	const ptr_tok_type *pRPN;

	int sidx = -1;
	ECmdCode eCode = cmUNKNOWN;
	bool If_Condition = 0;
	std::size_t lenRPN = m_rpn.GetSize();
	for (std::size_t i = 0; i < lenRPN; ++i)
	{
		IToken *pTok = m_rpn.GetData()[i].get();
		eCode = pTok->GetCode();

		switch (eCode)
		{
		case cmSCRIPT_NEWLINE:
			sidx = -1;
			continue;

		case cmVAL:
		{
			IValue *pVal = static_cast<IValue*>(pTok);

			sidx++;
			MUP_VERIFY(sidx < (int)m_vStackBuffer.size());
			if (pVal->IsVariable())
			{
				m_vStackBuffer[sidx] = static_pointer_cast<IValue>(m_rpn.GetData()[i]);
			}
			else
			{
				ptr_val_type &val = m_vStackBuffer[sidx];
				if (val->IsVariable())
					val.reset(m_cache.CreateFromCache(), Value_Deleter);

				*val = *(static_cast<IValue*>(pTok));
			}
		}
		continue;
		/*
		// Deal with:
		//   - Index operator:             [,,,]
		//   - Array constrution operator: {,,,}
		case  cmCBC:
		{
		ICallback *pFun = static_cast<ICallback*>(pTok);
		int nArgs = pFun->GetArgsPresent();
		sidx -= nArgs - 1;
		MUP_VERIFY(sidx >= 0);

		ptr_val_type &val = pStack[sidx];   // Pointer to the variable or value beeing indexed
		if (val->IsVariable())
		{
		ptr_val_type buf(m_cache.CreateFromCache());
		pFun->Eval(buf, &val, nArgs);
		val = buf;
		}
		else
		{
		pFun->Eval(val, &val, nArgs);
		}
		}
		continue;
		*/
		case  cmIC:
		{
			ICallback *pIdxOprt = static_cast<ICallback*>(pTok);
			int nArgs = pIdxOprt->GetArgsPresent();
			sidx -= nArgs - 1;
			MUP_VERIFY(sidx >= 0);

			ptr_val_type &idx = m_vStackBuffer[sidx];   // Pointer to the first index
			ptr_val_type &val = m_vStackBuffer[--sidx];   // Pointer to the variable or value beeing indexed
			pIdxOprt->Eval(val, &idx, nArgs);
		}
		continue;

		case cmSCRIPT_BREAK:
			i = Break_Closing_Curly[i];
			break;
		case cmSCRIPT_IF:
			Parse_If_Condition = 1;
		case cmSCRIPT_ELSE:
		case cmSCRIPT_LOOP:
			continue;

		case  cmBO:
			continue;
		case  cmBC:
			Parse_If_Condition = 0;
			if (If_Condition)
			{
				// Redem note: of course, not changing "i" this assumes that next token is "{", so I must make sure newlines don't affect the flow
				break;
			}
			else
			{
				i += 1;	// "{" for If body
				i = Closing_Curly[i] + 1; // "{" for Else body after considering "++i" increment
				break;
			}

		case cmCBO:
			continue;
		case cmCBC:
			// Check if this curly bracket closes Loop body
			if (Loop_Curly[i])
			{
				i = Opening_Curly[i];	// jump to start of this Loop body
				break;
			}
			// Check if this curly bracket closes If or Else body
			else if (IfElse_Curly[i])
			{
				// Skip the Else body, if present
				if (IfElse_Curly[i + 2])	// Redem note: again, can be a bug source if there are newlines between If body "}" and Else body "{"
				{
					i = Closing_Curly[i + 2];
				}
				break;
			}
		case cmOPRT_POSTFIX:
		case cmFUNC:
		case cmOPRT_BIN:
		case cmOPRT_INFIX:
		{
			ICallback *pFun = static_cast<ICallback*>(pTok);
			int nArgs = pFun->GetArgsPresent();
			sidx -= nArgs - 1;
			MUP_VERIFY(sidx >= 0);

			ptr_val_type &val = m_vStackBuffer[sidx];
			try
			{
				if (val->IsVariable())
				{
					ptr_val_type buf(m_cache.CreateFromCache(), Value_Deleter);
					pFun->Eval(buf, &val, nArgs);
					val = buf;
				}
				else
				{
					pFun->Eval(val, &val, nArgs);
				}
				if (Parse_If_Condition)
					If_Condition = val->GetBool();
			}
			catch (ParserError &exc)
			{
				// <ibg 20130131> Not too happy about that:
				// Multiarg functions may throw specific error codes when evaluating.
				// These codes would be converted to ecEVAL here. I omit the conversion
				// for certain handpicked errors. (The reason this catch block exists is
				// that not all exceptions contain proper metadata when thrown out of
				// a function.)
				if (exc.GetCode() == ecTOO_FEW_PARAMS ||
					exc.GetCode() == ecDOMAIN_ERROR ||
					exc.GetCode() == ecOVERFLOW ||
					exc.GetCode() == ecINVALID_NUMBER_OF_PARAMETERS ||
					exc.GetCode() == ecASSIGNEMENT_TO_VALUE)
				{
					exc.GetContext().Pos = pFun->GetExprPos();
					throw;
				}
				// </ibg>
				else
				{
					ErrorContext err;
					err.Expr = m_pTokenReader->GetExpr();
					err.Ident = pFun->GetIdent();
					err.Errc = ecEVAL;
					err.Pos = pFun->GetExprPos();
					err.Hint = exc.GetMsg();
					throw ParserError(err);
				}
			}
			catch (MatrixError & /*exc*/)
			{
				ErrorContext err;
				err.Expr = m_pTokenReader->GetExpr();
				err.Ident = pFun->GetIdent();
				err.Errc = ecMATRIX_DIMENSION_MISMATCH;
				err.Pos = pFun->GetExprPos();
				throw ParserError(err);
			}
		}
		continue;

		case cmIF:
			MUP_VERIFY(sidx >= 0);
			if (m_vStackBuffer[sidx--]->GetBool() == false)
				i += static_cast<TokenIfThenElse*>(pTok)->GetOffset();
			continue;

		case cmELSE:
		case cmJMP:
			i += static_cast<TokenIfThenElse*>(pTok)->GetOffset();
			continue;

		case cmENDIF:
			continue;

		default:
			Error(ecINTERNAL_ERROR);
		} // switch token
	} // for all RPN tokens

	return *m_vStackBuffer[0];
}

//---------------------------------------------------------------------------
void  ParserXBase::Error(EErrorCodes a_iErrc, int a_iPos, const IToken *a_pTok) const
{
	ErrorContext err;
	err.Errc = a_iErrc;
	err.Pos = a_iPos;
	err.Expr = m_pTokenReader->GetExpr();
	err.Ident = (a_pTok) ? a_pTok->GetIdent() : _T("");
	throw ParserError(err);
}

//------------------------------------------------------------------------------
/** \brief Clear all user defined variables.
	  \throw nothrow

	  Resets the parser to string parsing mode by calling #ReInit.
	  */
void ParserXBase::ClearVar()
{
	m_varDef.clear();
	m_valDynVarShadow.clear();
	ReInit();
}

//------------------------------------------------------------------------------
/** \brief Clear the expression.
	  \throw nothrow

	  Clear the expression and existing bytecode.
	  */
void ParserXBase::ClearExpr()
{
	m_pTokenReader->SetExpr(_T(""));
	ReInit();
}

//------------------------------------------------------------------------------
/** \brief Clear all function definitions.
	  \throw nothrow
	  */
void ParserXBase::ClearFun()
{
	m_FunDef.clear();
	ReInit();
}

//------------------------------------------------------------------------------
/** \brief Clear all user defined constants.
	  \throw nothrow

	  Both numeric and string constants will be removed from the internal storage.
	  */
void ParserXBase::ClearConst()
{
	m_valDef.clear();
	ReInit();
}

//------------------------------------------------------------------------------
/** \brief Clear all user defined postfix operators.
	  \throw nothrow
	  */
void ParserXBase::ClearPostfixOprt()
{
	m_PostOprtDef.clear();
	ReInit();
}

//------------------------------------------------------------------------------
/** \brief Clear all user defined binary operators.
	  \throw nothrow
	  */
void ParserXBase::ClearOprt()
{
	m_OprtDef.clear();
	ReInit();
}

//------------------------------------------------------------------------------
/** \brief Clear the user defined Prefix operators.
	  \throw nothrow
	  */
void ParserXBase::ClearInfixOprt()
{
	m_InfixOprtDef.clear();
	ReInit();
}

//------------------------------------------------------------------------------
void ParserXBase::EnableAutoCreateVar(bool bStat)
{
	m_bAutoCreateVar = bStat;
}

//------------------------------------------------------------------------------
void ParserXBase::EnableOptimizer(bool bStat)
{
	m_rpn.EnableOptimizer(bStat);
}

//---------------------------------------------------------------------------
/** \brief Enable the dumping of bytecode amd stack content on the console.
	  \param bDumpCmd Flag to enable dumping of the current bytecode to the console.
	  \param bDumpStack Flag to enable dumping of the stack content is written to the console.

	  This function is for debug purposes only!
	  */
void ParserXBase::EnableDebugDump(bool bDumpRPN, bool bDumpStack)
{
	ParserXBase::s_bDumpRPN = bDumpRPN;
	ParserXBase::s_bDumpStack = bDumpStack;
}

//------------------------------------------------------------------------------
bool ParserXBase::IsAutoCreateVarEnabled() const
{
	return m_bAutoCreateVar;
}

//------------------------------------------------------------------------------
/** \brief Dump stack content.

	  This function is used for debugging only.
	  */
void ParserXBase::StackDump(const Stack<ptr_tok_type> &a_stOprt) const
{
	using std::cout;
	Stack<ptr_tok_type>  stOprt(a_stOprt);

	string_type sInfo = _T("StackDump>  ");
	console() << sInfo;

	if (stOprt.empty())
		console() << _T("\n") << sInfo << _T("Operator stack is empty.\n");
	else
		console() << _T("\n") << sInfo << _T("Operator stack:\n");

	while (!stOprt.empty())
	{
		ptr_tok_type tok = stOprt.pop();
		console() << sInfo << _T(" ") << g_sCmdCode[tok->GetCode()] << _T(" \"") << tok->GetIdent() << _T("\" \n");
	}

	console() << endl;
}
} // namespace mu
