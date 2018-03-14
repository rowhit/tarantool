/*
** 2000-05-29
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Driver template for the LEMON parser generator.
**
** The "lemon" program processes an LALR(1) input grammar file, then uses
** this template to construct a parser.  The "lemon" program inserts text
** at each "%%" line.  Also, any "P-a-r-s-e" identifer prefix (without the
** interstitial "-" characters) contained in this template is changed into
** the value of the %name directive from the grammar.  Otherwise, the content
** of this template is copied straight through into the generate parser
** source file.
**
** The following is the concatenation of all %include directives from the
** input grammar file:
*/
#include <stdio.h>
#include <stdbool.h>
/************ Begin %include sections from the grammar ************************/
#line 52 "parse.y"

#include "sqliteInt.h"

/*
** Disable all error recovery processing in the parser push-down
** automaton.
*/
#define YYNOERRORRECOVERY 1

/*
** Make yytestcase() the same as testcase()
*/
#define yytestcase(X) testcase(X)

/*
** Indicate that sqlite3ParserFree() will never be called with a null
** pointer.
*/
#define YYPARSEFREENEVERNULL 1

/*
** Alternative datatype for the argument to the malloc() routine passed
** into sqlite3ParserAlloc().  The default is size_t.
*/
#define YYMALLOCARGTYPE  u64

/*
** An instance of this structure holds information about the
** LIMIT clause of a SELECT statement.
*/
struct LimitVal {
  Expr *pLimit;    /* The LIMIT expression.  NULL if there is no limit */
  Expr *pOffset;   /* The OFFSET expression.  NULL if there is none */
};

/*
** An instance of the following structure describes the event of a
** TRIGGER.  "a" is the event type, one of TK_UPDATE, TK_INSERT,
** TK_DELETE, or TK_INSTEAD.  If the event is of the form
**
**      UPDATE ON (a,b,c)
**
** Then the "b" IdList records the list "a,b,c".
*/
struct TrigEvent { int a; IdList * b; };

/*
** Disable lookaside memory allocation for objects that might be
** shared across database connections.
*/
static void disableLookaside(Parse *pParse){
  pParse->disableLookaside++;
  pParse->db->lookaside.bDisable++;
}

#line 392 "parse.y"

  /*
  ** For a compound SELECT statement, make sure p->pPrior->pNext==p for
  ** all elements in the list.  And make sure list length does not exceed
  ** SQLITE_LIMIT_COMPOUND_SELECT.
  */
  static void parserDoubleLinkSelect(Parse *pParse, Select *p){
    if( p->pPrior ){
      Select *pNext = 0, *pLoop;
      int mxSelect, cnt = 0;
      for(pLoop=p; pLoop; pNext=pLoop, pLoop=pLoop->pPrior, cnt++){
        pLoop->pNext = pNext;
        pLoop->selFlags |= SF_Compound;
      }
      if( (p->selFlags & SF_MultiValue)==0 && 
        (mxSelect = pParse->db->aLimit[SQLITE_LIMIT_COMPOUND_SELECT])>0 &&
        cnt>mxSelect
      ){
        sqlite3ErrorMsg(pParse, "Too many UNION or EXCEPT or INTERSECT operations");
      }
    }
  }
#line 831 "parse.y"

  /* This is a utility routine used to set the ExprSpan.zStart and
  ** ExprSpan.zEnd values of pOut so that the span covers the complete
  ** range of text beginning with pStart and going to the end of pEnd.
  */
  static void spanSet(ExprSpan *pOut, Token *pStart, Token *pEnd){
    pOut->zStart = pStart->z;
    pOut->zEnd = &pEnd->z[pEnd->n];
  }

  /* Construct a new Expr object from a single identifier.  Use the
  ** new Expr to populate pOut.  Set the span of pOut to be the identifier
  ** that created the expression.
  */
  static void spanExpr(ExprSpan *pOut, Parse *pParse, int op, Token t){
    Expr *p = sqlite3DbMallocRawNN(pParse->db, sizeof(Expr)+t.n+1);
    if( p ){
      memset(p, 0, sizeof(Expr));
      p->op = (u8)op;
      p->flags = EP_Leaf;
      p->iAgg = -1;
      p->u.zToken = (char*)&p[1];
      memcpy(p->u.zToken, t.z, t.n);
      p->u.zToken[t.n] = 0;
      if (op != TK_VARIABLE){
        sqlite3NormalizeName(p->u.zToken);
      }
#if SQLITE_MAX_EXPR_DEPTH>0
      p->nHeight = 1;
#endif  
    }
    pOut->pExpr = p;
    pOut->zStart = t.z;
    pOut->zEnd = &t.z[t.n];
  }
#line 939 "parse.y"

  /* This routine constructs a binary expression node out of two ExprSpan
  ** objects and uses the result to populate a new ExprSpan object.
  */
  static void spanBinaryExpr(
    Parse *pParse,      /* The parsing context.  Errors accumulate here */
    int op,             /* The binary operation */
    ExprSpan *pLeft,    /* The left operand, and output */
    ExprSpan *pRight    /* The right operand */
  ){
    pLeft->pExpr = sqlite3PExpr(pParse, op, pLeft->pExpr, pRight->pExpr);
    pLeft->zEnd = pRight->zEnd;
  }

  /* If doNot is true, then add a TK_NOT Expr-node wrapper around the
  ** outside of *ppExpr.
  */
  static void exprNot(Parse *pParse, int doNot, ExprSpan *pSpan){
    if( doNot ){
      pSpan->pExpr = sqlite3PExpr(pParse, TK_NOT, pSpan->pExpr, 0);
    }
  }
#line 1013 "parse.y"

  /* Construct an expression node for a unary postfix operator
  */
  static void spanUnaryPostfix(
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand, and output */
    Token *pPostOp         /* The operand token for setting the span */
  ){
    pOperand->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0);
    pOperand->zEnd = &pPostOp->z[pPostOp->n];
  }                           
#line 1030 "parse.y"

  /* A routine to convert a binary TK_IS or TK_ISNOT expression into a
  ** unary TK_ISNULL or TK_NOTNULL expression. */
  static void binaryToUnaryIfNull(Parse *pParse, Expr *pY, Expr *pA, int op){
    sqlite3 *db = pParse->db;
    if( pA && pY && pY->op==TK_NULL ){
      pA->op = (u8)op;
      sqlite3ExprDelete(db, pA->pRight);
      pA->pRight = 0;
    }
  }
#line 1058 "parse.y"

  /* Construct an expression node for a unary prefix operator
  */
  static void spanUnaryPrefix(
    ExprSpan *pOut,        /* Write the new expression node here */
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand */
    Token *pPreOp         /* The operand token for setting the span */
  ){
    pOut->zStart = pPreOp->z;
    pOut->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0);
    pOut->zEnd = pOperand->zEnd;
  }
#line 1263 "parse.y"

  /* Add a single new term to an ExprList that is used to store a
  ** list of identifiers.  Report an error if the ID list contains
  ** a COLLATE clause or an ASC or DESC keyword, except ignore the
  ** error while parsing a legacy schema.
  */
  static ExprList *parserAddExprIdListTerm(
    Parse *pParse,
    ExprList *pPrior,
    Token *pIdToken,
    int hasCollate,
    int sortOrder
  ){
    ExprList *p = sqlite3ExprListAppend(pParse, pPrior, 0);
    if( (hasCollate || sortOrder!=SQLITE_SO_UNDEFINED)
        && pParse->db->init.busy==0
    ){
      sqlite3ErrorMsg(pParse, "syntax error after column name \"%.*s\"",
                         pIdToken->n, pIdToken->z);
    }
    sqlite3ExprListSetName(pParse, p, pIdToken, 1);
    return p;
  }
#line 231 "parse.c"
/**************** End of %include directives **********************************/
/* These constants specify the various numeric values for terminal symbols
** in a format understandable to "makeheaders".  This section is blank unless
** "lemon" is run with the "-m" command-line option.
***************** Begin makeheaders token definitions *************************/
/**************** End makeheaders token definitions ***************************/

/* The next sections is a series of control #defines.
** various aspects of the generated parser.
**    YYCODETYPE         is the data type used to store the integer codes
**                       that represent terminal and non-terminal symbols.
**                       "unsigned char" is used if there are fewer than
**                       256 symbols.  Larger types otherwise.
**    YYNOCODE           is a number of type YYCODETYPE that is not used for
**                       any terminal or nonterminal symbol.
**    YYFALLBACK         If defined, this indicates that one or more tokens
**                       (also known as: "terminal symbols") have fall-back
**                       values which should be used if the original symbol
**                       would not parse.  This permits keywords to sometimes
**                       be used as identifiers, for example.
**    YYACTIONTYPE       is the data type used for "action codes" - numbers
**                       that indicate what to do in response to the next
**                       token.
**    sqlite3ParserTOKENTYPE     is the data type used for minor type for terminal
**                       symbols.  Background: A "minor type" is a semantic
**                       value associated with a terminal or non-terminal
**                       symbols.  For example, for an "ID" terminal symbol,
**                       the minor type might be the name of the identifier.
**                       Each non-terminal can have a different minor type.
**                       Terminal symbols all have the same minor type, though.
**                       This macros defines the minor type for terminal 
**                       symbols.
**    YYMINORTYPE        is the data type used for all minor types.
**                       This is typically a union of many types, one of
**                       which is sqlite3ParserTOKENTYPE.  The entry in the union
**                       for terminal symbols is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    sqlite3ParserARG_SDECL     A static variable declaration for the %extra_argument
**    sqlite3ParserARG_PDECL     A parameter declaration for the %extra_argument
**    sqlite3ParserARG_STORE     Code to store %extra_argument into yypParser
**    sqlite3ParserARG_FETCH     Code to extract %extra_argument from yypParser
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YY_MAX_SHIFT       Maximum value for shift actions
**    YY_MIN_SHIFTREDUCE Minimum value for shift-reduce actions
**    YY_MAX_SHIFTREDUCE Maximum value for shift-reduce actions
**    YY_MIN_REDUCE      Maximum value for reduce actions
**    YY_ERROR_ACTION    The yy_action[] code for syntax error
**    YY_ACCEPT_ACTION   The yy_action[] code for accept
**    YY_NO_ACTION       The yy_action[] code for no-op
*/
#ifndef INTERFACE
# define INTERFACE 1
#endif
/************* Begin control #defines *****************************************/
#define YYCODETYPE unsigned char
#define YYNOCODE 230
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 73
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  SrcList* yy41;
  struct LimitVal yy76;
  ExprList* yy162;
  struct TrigEvent yy184;
  Select* yy203;
  ExprSpan yy266;
  With* yy273;
  IdList* yy306;
  struct {int value; int mask;} yy331;
  Expr* yy396;
  int yy444;
  TriggerStep* yy451;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYFALLBACK 1
#define YYNSTATE             409
#define YYNRULE              297
#define YY_MAX_SHIFT         408
#define YY_MIN_SHIFTREDUCE   604
#define YY_MAX_SHIFTREDUCE   900
#define YY_MIN_REDUCE        901
#define YY_MAX_REDUCE        1197
#define YY_ERROR_ACTION      1198
#define YY_ACCEPT_ACTION     1199
#define YY_NO_ACTION         1200
/************* End control #defines *******************************************/

/* Define the yytestcase() macro to be a no-op if is not already defined
** otherwise.
**
** Applications can choose to define yytestcase() in the %include section
** to a macro that can assist in verifying code coverage.  For production
** code the yytestcase() macro should be turned off.  But it is useful
** for testing.
*/
#ifndef yytestcase
# define yytestcase(X)
#endif


/* Next are the tables used to determine what action to take based on the
** current state and lookahead token.  These tables are used to implement
** functions that take a state number and lookahead value and return an
** action integer.  
**
** Suppose the action integer is N.  Then the action is determined as
** follows
**
**   0 <= N <= YY_MAX_SHIFT             Shift N.  That is, push the lookahead
**                                      token onto the stack and goto state N.
**
**   N between YY_MIN_SHIFTREDUCE       Shift to an arbitrary state then
**     and YY_MAX_SHIFTREDUCE           reduce by rule N-YY_MIN_SHIFTREDUCE.
**
**   N between YY_MIN_REDUCE            Reduce by rule N-YY_MIN_REDUCE
**     and YY_MAX_REDUCE
**
**   N == YY_ERROR_ACTION               A syntax error has occurred.
**
**   N == YY_ACCEPT_ACTION              The parser accepts its input.
**
**   N == YY_NO_ACTION                  No such action.  Denotes unused
**                                      slots in the yy_action[] table.
**
** The action table is constructed as a single large table named yy_action[].
** Given state S and lookahead X, the action is computed as either:
**
**    (A)   N = yy_action[ yy_shift_ofst[S] + X ]
**    (B)   N = yy_default[S]
**
** The (A) formula is preferred.  The B formula is used instead if:
**    (1)  The yy_shift_ofst[S]+X value is out of range, or
**    (2)  yy_lookahead[yy_shift_ofst[S]+X] is not equal to X, or
**    (3)  yy_shift_ofst[S] equal YY_SHIFT_USE_DFLT.
** (Implementation note: YY_SHIFT_USE_DFLT is chosen so that
** YY_SHIFT_USE_DFLT+X will be out of range for all possible lookaheads X.
** Hence only tests (1) and (2) need to be evaluated.)
**
** The formulas above are for computing the action when the lookahead is
** a terminal symbol.  If the lookahead is a non-terminal (as occurs after
** a reduce action) then the yy_reduce_ofst[] array is used in place of
** the yy_shift_ofst[] array and YY_REDUCE_USE_DFLT is used in place of
** YY_SHIFT_USE_DFLT.
**
** The following are the tables generated in this section:
**
**  yy_action[]        A single table containing all actions.
**  yy_lookahead[]     A table containing the lookahead for each entry in
**                     yy_action.  Used to detect hash collisions.
**  yy_shift_ofst[]    For each state, the offset into yy_action for
**                     shifting terminals.
**  yy_reduce_ofst[]   For each state, the offset into yy_action for
**                     shifting non-terminals after a reduce.
**  yy_default[]       Default action for each state.
**
*********** Begin parsing tables **********************************************/
#define YY_ACTTAB_COUNT (1404)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    91,   92,  285,   82,  771,  771,  783,  786,  775,  775,
 /*    10 */    89,   89,   90,   90,   90,   90,  307,   88,   88,   88,
 /*    20 */    88,   87,   87,   86,   86,   86,   85,  307,   90,   90,
 /*    30 */    90,   90,   83,   88,   88,   88,   88,   87,   87,   86,
 /*    40 */    86,   86,   85,  307,  208,  870,  885,   90,   90,   90,
 /*    50 */    90,  122,   88,   88,   88,   88,   87,   87,   86,   86,
 /*    60 */    86,   85,  307,   86,   86,   86,   85,  307,  870,  885,
 /*    70 */  1199,  408,    3,  605,  310,   91,   92,  285,   82,  771,
 /*    80 */   771,  783,  786,  775,  775,   89,   89,   90,   90,   90,
 /*    90 */    90,  630,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   100 */    86,   85,  307,   91,   92,  285,   82,  771,  771,  783,
 /*   110 */   786,  775,  775,   89,   89,   90,   90,   90,   90,  633,
 /*   120 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   130 */   307,  371,   91,   92,  285,   82,  771,  771,  783,  786,
 /*   140 */   775,  775,   89,   89,   90,   90,   90,   90,   67,   88,
 /*   150 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  307,
 /*   160 */   772,  772,  784,  787,  109,   93,   87,   87,   86,   86,
 /*   170 */    86,   85,  307,  305,  304,  243,  264,  721,  722,  162,
 /*   180 */   174,   91,   92,  285,   82,  771,  771,  783,  786,  775,
 /*   190 */   775,   89,   89,   90,   90,   90,   90,  299,   88,   88,
 /*   200 */    88,   88,   87,   87,   86,   86,   86,   85,  307,   88,
 /*   210 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  307,
 /*   220 */   666,  645,  296,  365,  638,  684,  684,  229,  776,  666,
 /*   230 */    91,   92,  285,   82,  771,  771,  783,  786,  775,  775,
 /*   240 */    89,   89,   90,   90,   90,   90,  692,   88,   88,   88,
 /*   250 */    88,   87,   87,   86,   86,   86,   85,  307,   84,   81,
 /*   260 */   176,  896,  398,  896,  317,  217,  155,  253,  359,  248,
 /*   270 */   358,  203,  761,  631,  754,  339,  228,  749,  246,   91,
 /*   280 */    92,  285,   82,  771,  771,  783,  786,  775,  775,   89,
 /*   290 */    89,   90,   90,   90,   90,  632,   88,   88,   88,   88,
 /*   300 */    87,   87,   86,   86,   86,   85,  307,  227,  404,  226,
 /*   310 */   344,  407,  407,  341,  753,  753,  755,  218,  720,  119,
 /*   320 */   122,  719,  762,  679,  740,   48,   48,  824,   91,   92,
 /*   330 */   285,   82,  771,  771,  783,  786,  775,  775,   89,   89,
 /*   340 */    90,   90,   90,   90,  265,   88,   88,   88,   88,   87,
 /*   350 */    87,   86,   86,   86,   85,  307,   22,  109,  743,  188,
 /*   360 */   382,  367,   84,   81,  176,   84,   81,  176,   84,   81,
 /*   370 */   176,  748,  649,  236,  330,  235,  294,   91,   92,  285,
 /*   380 */    82,  771,  771,  783,  786,  775,  775,   89,   89,   90,
 /*   390 */    90,   90,   90,  877,   88,   88,   88,   88,   87,   87,
 /*   400 */    86,   86,   86,   85,  307,   91,   92,  285,   82,  771,
 /*   410 */   771,  783,  786,  775,  775,   89,   89,   90,   90,   90,
 /*   420 */    90,  122,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   430 */    86,   85,  307,   91,   92,  285,   82,  771,  771,  783,
 /*   440 */   786,  775,  775,   89,   89,   90,   90,   90,   90,  648,
 /*   450 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   460 */   307,   91,   92,  285,   82,  771,  771,  783,  786,  775,
 /*   470 */   775,   89,   89,   90,   90,   90,   90,  145,   88,   88,
 /*   480 */    88,   88,   87,   87,   86,   86,   86,   85,  307, 1150,
 /*   490 */  1150,   85,  307,   70,   92,  285,   82,  771,  771,  783,
 /*   500 */   786,  775,  775,   89,   89,   90,   90,   90,   90,  647,
 /*   510 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   520 */   307,   73,  216,  366,  122,  624,  624,   91,   80,  285,
 /*   530 */    82,  771,  771,  783,  786,  775,  775,   89,   89,   90,
 /*   540 */    90,   90,   90,  747,   88,   88,   88,   88,   87,   87,
 /*   550 */    86,   86,   86,   85,  307,  285,   82,  771,  771,  783,
 /*   560 */   786,  775,  775,   89,   89,   90,   90,   90,   90,   78,
 /*   570 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   580 */   307,  886,  886,  743, 1173,  404,  661,  404,   75,   76,
 /*   590 */   402,  402,  402,  383,  747,   77,  624,  624,  236,  330,
 /*   600 */   235,  339,   48,   48,   47,   47,  324,  143,  400,    2,
 /*   610 */  1097,  404,  280,  308,  308,  278,  277,  276,  220,  274,
 /*   620 */   199,   78,  618,  356,  353,  352,  109,  743,   48,   48,
 /*   630 */   389,  404,  887,  708,  761,  351,  754,  382,  384,  749,
 /*   640 */    75,   76,  236,  320,  224,  873,  122,   77,   48,   48,
 /*   650 */   306,  306,  306,  640,  404,  216,  366,  290,  263,  315,
 /*   660 */   400,    2,  363,  300,  751,  308,  308,  305,  304,  286,
 /*   670 */   843,   30,   30,  315,  314,  846,  753,  753,  755,  756,
 /*   680 */   403,   18,  389,  382,  372,  234,  761,  376,  754,  847,
 /*   690 */   699,  749,  624,  624,  122,  109,  404,  848,  672,   78,
 /*   700 */   177,  177,  182,  109,  207,  673,  364,  107,  180,  313,
 /*   710 */   122,  628,  369,   48,   48,  891,  751,  895,   75,   76,
 /*   720 */   693,  624,  624,  404,  893,   77,  894,  331,  753,  753,
 /*   730 */   755,  756,  403,   18,  628,  315,  404,  747,  400,    2,
 /*   740 */    48,   48,  164,  308,  308,  340,  122,  264,  382,  381,
 /*   750 */   747,   78,  265,   10,   10,  896,  328,  896,  199,   68,
 /*   760 */   389,  356,  353,  352,  761,  404,  754,  293,  843,  749,
 /*   770 */    75,   76,  316,  351,  747,  382,  362,   77,   75,   76,
 /*   780 */   671,  671,   48,   48,  303,   77,  816,  818,  886,  886,
 /*   790 */   400,    2,  669,  669,  751,  308,  308,  404,  400,    2,
 /*   800 */   291,  109,  387,  308,  308,  404,  753,  753,  755,  756,
 /*   810 */   403,   18,  389,  292,   10,   10,  761,  377,  754,  373,
 /*   820 */   389,  749,   10,   10,  761,  331,  754,  705,  295,  749,
 /*   830 */   157,  156,  348,  394,  201,  370,  198,  301,   23,  887,
 /*   840 */   707,  241,   74,  329,   72,  172,  751,  706,  816,  623,
 /*   850 */   288,   95,    9,    9,  751,  634,  634,  404,  753,  753,
 /*   860 */   755,  756,  403,   18,  374,  312,  753,  753,  755,  756,
 /*   870 */   403,   18,  333,  141,   10,   10,  761,  806,  754,  177,
 /*   880 */   177,  749,  284,  652,  830,  830,  325,  141,  379,  357,
 /*   890 */   240,  369,  333,  273,  653,  167,  281,  404,  361,  165,
 /*   900 */   706,  200,    5,  200,  208,  318,  885,  335,  287,  404,
 /*   910 */   846,  297,  705,  246,   34,   34,  401,  288,  753,  753,
 /*   920 */   755,  404,  752,  232,  847,  404,   35,   35,  404,  885,
 /*   930 */   404,  255,  848,  388,  404,  705,  183,  404,   36,   36,
 /*   940 */   706,  404,   37,   37,  404,   38,   38,   26,   26,  404,
 /*   950 */   223,   27,   27,  404,   29,   29,  189,  404,   39,   39,
 /*   960 */   404,   40,   40,  404,  689,  404,   41,   41,  404,  688,
 /*   970 */    11,   11,  404,  184,   42,   42,  681,   97,   97,  404,
 /*   980 */    43,   43,   44,   44,  404,   31,   31,  369,  404,   45,
 /*   990 */    45,  404,  165,  706,  404,  163,   46,   46,  404,  262,
 /*  1000 */   404,   32,   32,  404,  146,  112,  112,   20,  113,  113,
 /*  1010 */   404,  114,  114,  841,  404,   52,   52,   33,   33,  404,
 /*  1020 */    98,   98,  404,  339,  404,  137,  404,   49,   49,  624,
 /*  1030 */   624,   99,   99,  404,  175,  174,  100,  100,   19,   96,
 /*  1040 */    96,  111,  111,  108,  108,  404,  109,  404,  169,  404,
 /*  1050 */   104,  104,  404,  201,  404,  745,  404,  206,  404,  721,
 /*  1060 */   722,  404,  103,  103,  101,  101,  102,  102,  694,   51,
 /*  1070 */    51,   53,   53,   50,   50,   25,   25,  705,   28,   28,
 /*  1080 */   138,  827,  705,  826,  624,  624,  208,  705,  885,  323,
 /*  1090 */    24,  624,  624,    1,  624,  624,  705,  624,  624,  161,
 /*  1100 */   160,  159,  705,  705,  215,  677,  622,  191,  337,  391,
 /*  1110 */    64,  885,  395,  399,   54,  185,   66,  884,  689,  110,
 /*  1120 */   849,  149,  332,  688,  206,  321,  334,  287,  206,  237,
 /*  1130 */   349,   66,  212,  244,  678,   66,  651,  650,  109,  109,
 /*  1140 */   674,  231,  109,  641,  641,  109,  109,  658,  643,  823,
 /*  1150 */   252,  823,  686,  338,   69,  715,  822,  206,  822,  813,
 /*  1160 */   813,  251,  809,  626,  212,  106,  178,  257,  153,  757,
 /*  1170 */   757,  259,    7,  852,  851,  233,  820,  735,  289,  354,
 /*  1180 */   319,  225,  840,  814,  336,  158,  645,  168,  250,  838,
 /*  1190 */   837,  342,  343,  239,  617,  242,  662,  646,  247,  713,
 /*  1200 */   659,  746,  261,  695,  272,  390,  266,  267,  154,  135,
 /*  1210 */   629,  124,  615,  614,  866,  117,  732,  616,  863,  921,
 /*  1220 */   811,   64,  322,   55,  810,  327,  825,  230,  347,  147,
 /*  1230 */   360,  187,  144,  298,  643,  375,   63,  194,    6,  126,
 /*  1240 */   195,  196,  703,  209,  302,   71,   94,  210,  380,   65,
 /*  1250 */   791,   21,  378,  864,  222,  345,  128,  610,  129,  130,
 /*  1260 */   282,  665,  131,  139,  742,  636,  664,  663,  656,  607,
 /*  1270 */   309,  637,  179,  311,  842,  406,  283,  805,  249,  123,
 /*  1280 */   279,  635,  655,  875,  219,  405,  612,  221,  819,  611,
 /*  1290 */   608,  393,  817,  397,  181,  741,  120,  254,  115,  125,
 /*  1300 */   704,  127,  675,  186,  828,  206,  116,  132,  898,  133,
 /*  1310 */   326,  836,  134,   56,   57,  105,  202,  136,  256,   58,
 /*  1320 */    59,  271,  269,  685,  702,  258,  270,  701,  260,  268,
 /*  1330 */   121,  839,  190,  192,  835,    8,   12,  193,  238,  148,
 /*  1340 */   620,  346,  197,  140,  350,  211,  251,   60,  355,   13,
 /*  1350 */   204,  118,  170,  245,   14,   61,  683,  760,  604,  759,
 /*  1360 */   654,  789,   62,   15,  368,  687,    4,  714,  171,  173,
 /*  1370 */   205,  142,  709,   69,   16,   66,   17,  804,  790,  788,
 /*  1380 */   845,  793, 1155,  214,  844,  386,  166,  392,  856,  150,
 /*  1390 */   903,  213,  385,  857,  151,  396,  792,  152,  275,  758,
 /*  1400 */   627,   79,  903,  621,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*    10 */    15,   16,   17,   18,   19,   20,   32,   22,   23,   24,
 /*    20 */    25,   26,   27,   28,   29,   30,   31,   32,   17,   18,
 /*    30 */    19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
 /*    40 */    29,   30,   31,   32,   49,   51,   51,   17,   18,   19,
 /*    50 */    20,  132,   22,   23,   24,   25,   26,   27,   28,   29,
 /*    60 */    30,   31,   32,   28,   29,   30,   31,   32,   74,   74,
 /*    70 */   135,  136,  137,    1,    2,    5,    6,    7,    8,    9,
 /*    80 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*    90 */    20,  159,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   100 */    30,   31,   32,    5,    6,    7,    8,    9,   10,   11,
 /*   110 */    12,   13,   14,   15,   16,   17,   18,   19,   20,  159,
 /*   120 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   130 */    32,  150,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   140 */    13,   14,   15,   16,   17,   18,   19,   20,   50,   22,
 /*   150 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   160 */     9,   10,   11,   12,  183,   67,   26,   27,   28,   29,
 /*   170 */    30,   31,   32,   26,   27,   48,  142,  107,  108,  198,
 /*   180 */   199,    5,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   190 */    14,   15,   16,   17,   18,   19,   20,    7,   22,   23,
 /*   200 */    24,   25,   26,   27,   28,   29,   30,   31,   32,   22,
 /*   210 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   220 */   166,  167,   32,   93,   48,   95,   96,   43,   77,  175,
 /*   230 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*   240 */    15,   16,   17,   18,   19,   20,  197,   22,   23,   24,
 /*   250 */    25,   26,   27,   28,   29,   30,   31,   32,  209,  210,
 /*   260 */   211,  114,  228,  116,   76,   75,   76,   77,   78,   79,
 /*   270 */    80,   81,   72,   48,   74,  142,   92,   77,   88,    5,
 /*   280 */     6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
 /*   290 */    16,   17,   18,   19,   20,  159,   22,   23,   24,   25,
 /*   300 */    26,   27,   28,   29,   30,   31,   32,  123,  142,  125,
 /*   310 */   216,  138,  139,  219,  114,  115,  116,  144,  162,  146,
 /*   320 */   132,  162,   48,  150,  150,  159,  160,   38,    5,    6,
 /*   330 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   340 */    17,   18,   19,   20,  142,   22,   23,   24,   25,   26,
 /*   350 */    27,   28,   29,   30,   31,   32,  183,  183,   69,  226,
 /*   360 */   194,  195,  209,  210,  211,  209,  210,  211,  209,  210,
 /*   370 */   211,   48,  168,   84,   85,   86,  174,    5,    6,    7,
 /*   380 */     8,    9,   10,   11,   12,   13,   14,   15,   16,   17,
 /*   390 */    18,   19,   20,  172,   22,   23,   24,   25,   26,   27,
 /*   400 */    28,   29,   30,   31,   32,    5,    6,    7,    8,    9,
 /*   410 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   420 */    20,  132,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   430 */    30,   31,   32,    5,    6,    7,    8,    9,   10,   11,
 /*   440 */    12,   13,   14,   15,   16,   17,   18,   19,   20,  168,
 /*   450 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   460 */    32,    5,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   470 */    14,   15,   16,   17,   18,   19,   20,   49,   22,   23,
 /*   480 */    24,   25,   26,   27,   28,   29,   30,   31,   32,   97,
 /*   490 */    98,   31,   32,  121,    6,    7,    8,    9,   10,   11,
 /*   500 */    12,   13,   14,   15,   16,   17,   18,   19,   20,  168,
 /*   510 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   520 */    32,  121,   97,   98,  132,   51,   52,    5,    6,    7,
 /*   530 */     8,    9,   10,   11,   12,   13,   14,   15,   16,   17,
 /*   540 */    18,   19,   20,  142,   22,   23,   24,   25,   26,   27,
 /*   550 */    28,   29,   30,   31,   32,    7,    8,    9,   10,   11,
 /*   560 */    12,   13,   14,   15,   16,   17,   18,   19,   20,    7,
 /*   570 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   580 */    32,   51,   52,   69,   48,  142,   50,  142,   26,   27,
 /*   590 */   155,  156,  157,  150,  142,   33,   51,   52,   84,   85,
 /*   600 */    86,  142,  159,  160,  159,  160,  205,  133,   46,   47,
 /*   610 */    48,  142,   34,   51,   52,   37,   38,   39,   40,   41,
 /*   620 */    75,    7,   44,   78,   79,   80,  183,   69,  159,  160,
 /*   630 */    68,  142,  102,  103,   72,   90,   74,  194,  195,   77,
 /*   640 */    26,   27,   84,   85,   86,  158,  132,   33,  159,  160,
 /*   650 */   155,  156,  157,  166,  142,   97,   98,  205,  213,  142,
 /*   660 */    46,   47,  150,  194,  102,   51,   52,   26,   27,   91,
 /*   670 */   150,  159,  160,  156,  157,   39,  114,  115,  116,  117,
 /*   680 */   118,  119,   68,  194,  195,  226,   72,  204,   74,   53,
 /*   690 */   200,   77,   51,   52,  132,  183,  142,   61,   62,    7,
 /*   700 */   181,  182,  124,  183,  197,   69,  194,   47,  130,  131,
 /*   710 */   132,   51,  193,  159,  160,   74,  102,   76,   26,   27,
 /*   720 */    28,   51,   52,  142,   83,   33,   85,  207,  114,  115,
 /*   730 */   116,  117,  118,  119,   74,  218,  142,  142,   46,   47,
 /*   740 */   159,  160,  204,   51,   52,  225,  132,  142,  194,  195,
 /*   750 */   142,    7,  142,  159,  160,  114,  222,  116,   75,    7,
 /*   760 */    68,   78,   79,   80,   72,  142,   74,  173,  150,   77,
 /*   770 */    26,   27,  142,   90,  142,  194,  195,   33,   26,   27,
 /*   780 */   177,  178,  159,  160,  174,   33,  156,  157,   51,   52,
 /*   790 */    46,   47,  177,  178,  102,   51,   52,  142,   46,   47,
 /*   800 */   205,  183,  178,   51,   52,  142,  114,  115,  116,  117,
 /*   810 */   118,  119,   68,  205,  159,  160,   72,  194,   74,    7,
 /*   820 */    68,   77,  159,  160,   72,  207,   74,  142,  173,   77,
 /*   830 */    26,   27,    7,  228,    9,  142,  173,  205,  220,  102,
 /*   840 */   103,   43,  120,  225,  122,   48,  102,   50,  218,  153,
 /*   850 */   154,   47,  159,  160,  102,   51,   52,  142,  114,  115,
 /*   860 */   116,  117,  118,  119,   52,  180,  114,  115,  116,  117,
 /*   870 */   118,  119,  142,  142,  159,  160,   72,   79,   74,  181,
 /*   880 */   182,   77,  151,   59,   84,   85,   86,  142,  173,   65,
 /*   890 */    92,  193,  142,  148,   70,  221,  151,  142,   28,  102,
 /*   900 */   103,  170,   47,  172,   49,  207,   51,  142,   83,  142,
 /*   910 */    39,   87,  142,   88,  159,  160,  153,  154,  114,  115,
 /*   920 */   116,  142,  142,  125,   53,  142,  159,  160,  142,   74,
 /*   930 */   142,  197,   61,   62,  142,  142,  206,  142,  159,  160,
 /*   940 */    50,  142,  159,  160,  142,  159,  160,  159,  160,  142,
 /*   950 */   180,  159,  160,  142,  159,  160,  206,  142,  159,  160,
 /*   960 */   142,  159,  160,  142,   94,  142,  159,  160,  142,   99,
 /*   970 */   159,  160,  142,  180,  159,  160,  182,  159,  160,  142,
 /*   980 */   159,  160,  159,  160,  142,  159,  160,  193,  142,  159,
 /*   990 */   160,  142,  102,  103,  142,  142,  159,  160,  142,  142,
 /*  1000 */   142,  159,  160,  142,  184,  159,  160,   16,  159,  160,
 /*  1010 */   142,  159,  160,  150,  142,  159,  160,  159,  160,  142,
 /*  1020 */   159,  160,  142,  142,  142,   47,  142,  159,  160,   51,
 /*  1030 */    52,  159,  160,  142,  198,  199,  159,  160,   47,  159,
 /*  1040 */   160,  159,  160,  159,  160,  142,  183,  142,   50,  142,
 /*  1050 */   159,  160,  142,    9,  142,   48,  142,   50,  142,  107,
 /*  1060 */   108,  142,  159,  160,  159,  160,  159,  160,   28,  159,
 /*  1070 */   160,  159,  160,  159,  160,  159,  160,  142,  159,  160,
 /*  1080 */    47,   56,  142,   58,   51,   52,   49,  142,   51,   64,
 /*  1090 */    47,   51,   52,   47,   51,   52,  142,   51,   52,   84,
 /*  1100 */    85,   86,  142,  142,  186,  150,  150,  226,    7,  150,
 /*  1110 */   112,   74,  150,  150,  196,  180,   50,   50,   94,   47,
 /*  1120 */   180,   49,   48,   99,   50,  180,   48,   83,   50,   48,
 /*  1130 */    48,   50,   50,   48,  180,   50,   76,   77,  183,  183,
 /*  1140 */   180,  180,  183,   51,   52,  183,  183,   36,   82,  114,
 /*  1150 */    77,  116,   48,   52,   50,   48,  114,   50,  116,   51,
 /*  1160 */    52,   88,   48,   48,   50,   50,   47,  197,  101,   51,
 /*  1170 */    52,  197,  185,  142,  142,  227,  142,  188,  142,  163,
 /*  1180 */   201,  201,  188,  142,  227,  171,  167,  142,  162,  142,
 /*  1190 */   142,  142,  142,  142,  142,  142,  142,  142,  142,  142,
 /*  1200 */    89,  142,  201,  142,  187,  215,  142,  142,  185,   47,
 /*  1210 */   142,  208,  142,  142,  145,    5,  188,  142,  142,  100,
 /*  1220 */   162,  112,   45,  120,  162,  127,  224,  223,   45,  208,
 /*  1230 */    83,  147,   47,   63,   82,  105,   83,  147,   47,  176,
 /*  1240 */   147,  147,  203,  214,   32,  120,  111,  217,  106,  110,
 /*  1250 */   212,   50,  109,   40,   35,  164,  179,   36,  179,  179,
 /*  1260 */   164,  161,  179,  176,  176,  163,  161,  161,  169,    4,
 /*  1270 */     3,  161,   42,   71,  188,  141,  164,  188,  161,   43,
 /*  1280 */   140,  161,  169,  161,  143,  149,  141,  143,   48,  141,
 /*  1290 */   141,  164,   48,  164,  100,   98,   87,  202,  152,  113,
 /*  1300 */   203,  101,   46,   83,  126,   50,  152,  126,  129,   83,
 /*  1310 */   128,    1,  101,   16,   16,  165,  165,  113,  202,   16,
 /*  1320 */    16,  188,  190,  192,  203,  202,  189,  203,  202,  191,
 /*  1330 */    87,   52,  104,  100,    1,   34,   47,   83,  123,   49,
 /*  1340 */    46,    7,   81,   47,   66,  217,   88,   47,   66,   47,
 /*  1350 */    66,   60,  100,   48,   47,   47,   94,   48,    1,   48,
 /*  1360 */    54,   48,   50,   47,   50,   48,   47,   52,   48,   48,
 /*  1370 */   104,   47,  103,   50,  104,   50,  104,   48,   48,   48,
 /*  1380 */    48,   38,    0,  100,   48,   50,   47,   49,   48,   47,
 /*  1390 */   229,   50,   74,   48,   47,   49,   48,   47,   42,   48,
 /*  1400 */    48,   47,  229,   48,
};
#define YY_SHIFT_USE_DFLT (1404)
#define YY_SHIFT_COUNT    (408)
#define YY_SHIFT_MIN      (-81)
#define YY_SHIFT_MAX      (1382)
static const short yy_shift_ofst[] = {
 /*     0 */    72,  562,  614,  578,  744,  744,  744,  744,  514,   -5,
 /*    10 */    70,   70,  744,  744,  744,  744,  744,  744,  744,  641,
 /*    20 */   641,  545,  558,  289,  392,   98,  127,  176,  225,  274,
 /*    30 */   323,  372,  400,  428,  456,  456,  456,  456,  456,  456,
 /*    40 */   456,  456,  456,  456,  456,  456,  456,  456,  456,  522,
 /*    50 */   456,  488,  548,  548,  692,  744,  744,  744,  744,  744,
 /*    60 */   744,  744,  744,  744,  744,  744,  744,  744,  744,  744,
 /*    70 */   744,  744,  744,  744,  744,  744,  744,  744,  744,  744,
 /*    80 */   744,  744,  752,  744,  744,  744,  744,  744,  744,  744,
 /*    90 */   744,  744,  744,  744,  744,  744,   11,   30,   30,   30,
 /*   100 */    30,   30,  187,  140,   35,  825,  147,  147,  460,  425,
 /*   110 */   670,  -16, 1404, 1404, 1404,  190,  190,  636,  636,  798,
 /*   120 */   978,  978,  474,  670,  188,  670,  670,  670,  670,  670,
 /*   130 */   670,  670,  670,  670,  670,  670,  670,  670,  670,  670,
 /*   140 */   670,   -6,  670,  670,  670,   -6,  425,  -81,  -81,  -81,
 /*   150 */   -81,  -81,  -81, 1404, 1404,  804,  200,  200,  683,  824,
 /*   160 */   824,  824,  797,  855,  530,  737,  871,  800, 1025, 1033,
 /*   170 */  1040, 1037, 1037, 1037, 1043,  890, 1046,  130,  870,  670,
 /*   180 */   670,  670,  670,  998,  812,  812,  670,  670, 1101,  998,
 /*   190 */   670, 1101,  670,  670,  670,  670,  670,  670, 1066,  670,
 /*   200 */   536,  670, 1044,  670,  952,  670,  670,  812,  670,  722,
 /*   210 */   952,  952,  670,  670,  670, 1067, 1024,  670, 1072,  670,
 /*   220 */   670,  670,  670, 1162, 1210, 1109, 1177, 1177, 1177, 1177,
 /*   230 */  1103, 1098, 1183, 1109, 1162, 1210, 1210, 1109, 1183, 1185,
 /*   240 */  1183, 1183, 1185, 1147, 1147, 1147, 1170, 1185, 1147, 1152,
 /*   250 */  1147, 1170, 1147, 1147, 1130, 1153, 1130, 1153, 1130, 1153,
 /*   260 */  1130, 1153, 1191, 1125, 1185, 1212, 1212, 1185, 1135, 1142,
 /*   270 */  1139, 1143, 1109, 1201, 1213, 1213, 1219, 1219, 1219, 1219,
 /*   280 */  1221, 1404, 1404, 1404, 1404,  151,  184, 1015,  660,  991,
 /*   290 */  1007, 1074, 1078, 1081, 1082, 1085, 1092, 1060, 1111, 1073,
 /*   300 */  1104, 1107, 1108, 1114, 1035, 1042, 1115, 1118, 1119, 1265,
 /*   310 */  1267, 1230, 1202, 1236, 1240, 1244, 1194, 1197, 1186, 1209,
 /*   320 */  1200, 1220, 1256, 1178, 1255, 1181, 1179, 1182, 1226, 1310,
 /*   330 */  1211, 1204, 1297, 1298, 1303, 1304, 1243, 1279, 1228, 1233,
 /*   340 */  1333, 1301, 1289, 1254, 1215, 1290, 1294, 1334, 1258, 1261,
 /*   350 */  1296, 1278, 1300, 1302, 1305, 1307, 1282, 1306, 1308, 1284,
 /*   360 */  1291, 1309, 1311, 1313, 1312, 1262, 1316, 1317, 1319, 1314,
 /*   370 */  1252, 1320, 1321, 1315, 1266, 1324, 1269, 1323, 1270, 1325,
 /*   380 */  1272, 1329, 1323, 1330, 1331, 1332, 1318, 1335, 1336, 1339,
 /*   390 */  1343, 1340, 1342, 1338, 1341, 1345, 1347, 1346, 1341, 1348,
 /*   400 */  1350, 1351, 1352, 1354, 1283, 1355, 1356, 1357, 1382,
};
#define YY_REDUCE_USE_DFLT (-69)
#define YY_REDUCE_COUNT (284)
#define YY_REDUCE_MIN   (-68)
#define YY_REDUCE_MAX   (1154)
static const short yy_reduce_ofst[] = {
 /*     0 */   -65,  443,  512,  173,  166,  489,  554,  581,  618,   49,
 /*    10 */   156,  159,  594,  655,  663,  469,  623,  715,  445,  517,
 /*    20 */   630,  731,  698,  520,  -19,  153,  153,  153,  153,  153,
 /*    30 */   153,  153,  153,  153,  153,  153,  153,  153,  153,  153,
 /*    40 */   153,  153,  153,  153,  153,  153,  153,  153,  153,  153,
 /*    50 */   153,  153,  153,  153,  693,  755,  767,  779,  783,  786,
 /*    60 */   788,  792,  795,  799,  802,  807,  811,  815,  818,  821,
 /*    70 */   823,  826,  830,  837,  842,  846,  849,  852,  856,  858,
 /*    80 */   861,  868,  872,  877,  880,  882,  884,  891,  903,  905,
 /*    90 */   907,  910,  912,  914,  916,  919,  153,  153,  153,  153,
 /*   100 */   153,  153,  153,  153,  153,   54,  435,  495,  153,  519,
 /*   110 */   745,  153,  153,  153,  153,  487,  487,  603,  615,   94,
 /*   120 */   730,  750,   34,  685,  174,  770,  793,  935,  940,  945,
 /*   130 */   954,  960,  401,  961,  133,  452,  459,  595,  608,  881,
 /*   140 */   202,  696,  632,  605,  610,  763,  794,  863,  955,  956,
 /*   150 */   959,  962,  963,  836,  918,  -68,  -40,  136,  221,  204,
 /*   160 */   281,  341,  490,  507,  483,  538,  624,  534,  674,  765,
 /*   170 */   780,  734,  970,  974,  853,  490,  857,  820,  987, 1031,
 /*   180 */  1032, 1034, 1036,  989,  979,  980, 1041, 1045,  948,  994,
 /*   190 */  1047,  957, 1048, 1049, 1050, 1051, 1052, 1053, 1016, 1054,
 /*   200 */  1014, 1055, 1019, 1056, 1026, 1057, 1059, 1001, 1061,  990,
 /*   210 */  1058, 1062, 1064, 1065,  780, 1017, 1023, 1068, 1069, 1070,
 /*   220 */  1071, 1075, 1076, 1003, 1063, 1028, 1077, 1079, 1080, 1083,
 /*   230 */  1002, 1004, 1084, 1086, 1021, 1087, 1088, 1089, 1090, 1091,
 /*   240 */  1093, 1094, 1096, 1100, 1105, 1106, 1099, 1112, 1110, 1102,
 /*   250 */  1117, 1113, 1120, 1122, 1039, 1095, 1097, 1116, 1121, 1123,
 /*   260 */  1124, 1126, 1038, 1029, 1127, 1030, 1128, 1129, 1131, 1138,
 /*   270 */  1132, 1137, 1133, 1136, 1141, 1144, 1134, 1145, 1148, 1149,
 /*   280 */  1140, 1146, 1150, 1151, 1154,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1156, 1150, 1150, 1150, 1097, 1097, 1097, 1097, 1150,  993,
 /*    10 */  1020, 1020, 1198, 1198, 1198, 1198, 1198, 1198, 1096, 1198,
 /*    20 */  1198, 1198, 1198, 1150,  997, 1026, 1198, 1198, 1198, 1098,
 /*    30 */  1099, 1198, 1198, 1198, 1131, 1036, 1035, 1034, 1033, 1007,
 /*    40 */  1031, 1024, 1028, 1098, 1092, 1093, 1091, 1095, 1099, 1198,
 /*    50 */  1027, 1061, 1076, 1060, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    60 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    70 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    80 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    90 */  1198, 1198, 1198, 1198, 1198, 1198, 1070, 1075, 1082, 1074,
 /*   100 */  1071, 1063, 1062, 1064, 1065,  964, 1198, 1198, 1066, 1198,
 /*   110 */  1198, 1067, 1079, 1078, 1077, 1165, 1164, 1198, 1198, 1104,
 /*   120 */  1198, 1198, 1198, 1198, 1150, 1198, 1198, 1198, 1198, 1198,
 /*   130 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   140 */  1198,  922, 1198, 1198, 1198,  922, 1198, 1150, 1150, 1150,
 /*   150 */  1150, 1150, 1150,  997,  988, 1198, 1198, 1198, 1198, 1198,
 /*   160 */  1198, 1198, 1198,  993, 1198, 1198, 1198, 1198, 1126, 1198,
 /*   170 */  1198,  993,  993,  993, 1198,  995, 1198,  977,  987, 1198,
 /*   180 */  1147, 1198, 1118, 1030, 1009, 1009, 1198, 1198, 1197, 1030,
 /*   190 */  1198, 1197, 1198, 1198, 1198, 1198, 1198, 1198,  939, 1198,
 /*   200 */  1176, 1198,  936, 1198, 1020, 1198, 1198, 1009, 1198, 1094,
 /*   210 */  1020, 1020, 1198, 1198, 1198,  994,  987, 1198, 1198, 1198,
 /*   220 */  1198, 1198, 1159, 1041,  967, 1030,  973,  973,  973,  973,
 /*   230 */  1130, 1194,  916, 1030, 1041,  967,  967, 1030,  916, 1105,
 /*   240 */   916,  916, 1105,  965,  965,  965,  954, 1105,  965,  939,
 /*   250 */   965,  954,  965,  965, 1013, 1008, 1013, 1008, 1013, 1008,
 /*   260 */  1013, 1008, 1100, 1198, 1105, 1109, 1109, 1105, 1025, 1014,
 /*   270 */  1023, 1021, 1030,  957, 1162, 1162, 1158, 1158, 1158, 1158,
 /*   280 */   906, 1171,  941,  941, 1171, 1198, 1198, 1198, 1166, 1112,
 /*   290 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   300 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1047, 1198,
 /*   310 */   903, 1198, 1198, 1198, 1198, 1198, 1189, 1198, 1198, 1198,
 /*   320 */  1198, 1198, 1198, 1198, 1129, 1128, 1198, 1198, 1198, 1198,
 /*   330 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1196,
 /*   340 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   350 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   360 */  1198, 1198, 1198, 1198, 1198,  979, 1198, 1198, 1198, 1180,
 /*   370 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1022, 1198, 1015,
 /*   380 */  1198, 1198, 1186, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   390 */  1198, 1198, 1198, 1198, 1152, 1198, 1198, 1198, 1151, 1198,
 /*   400 */  1198, 1198, 1198, 1198, 1198, 1198,  910, 1198, 1198,
};
/********** End of lemon-generated parsing tables *****************************/

/* The next table maps tokens (terminal symbols) into fallback tokens.  
** If a construct like the following:
** 
**      %fallback ID X Y Z.
**
** appears in the grammar, then ID becomes a fallback token for X, Y,
** and Z.  Whenever one of the tokens X, Y, or Z is input to the parser
** but it does not parse, the type of the token is changed to ID and
** the parse is retried before an error is thrown.
**
** This feature can be used, for example, to cause some keywords in a language
** to revert to identifiers if they keyword does not apply in the context where
** it appears.
*/
#ifdef YYFALLBACK
static const YYCODETYPE yyFallback[] = {
    0,  /*          $ => nothing */
    0,  /*       SEMI => nothing */
    0,  /*    EXPLAIN => nothing */
   51,  /*      QUERY => ID */
   51,  /*       PLAN => ID */
    0,  /*         OR => nothing */
    0,  /*        AND => nothing */
    0,  /*        NOT => nothing */
    0,  /*         IS => nothing */
   51,  /*      MATCH => ID */
    0,  /*    LIKE_KW => nothing */
    0,  /*    BETWEEN => nothing */
    0,  /*         IN => nothing */
   51,  /*     ISNULL => ID */
   51,  /*    NOTNULL => ID */
    0,  /*         NE => nothing */
    0,  /*         EQ => nothing */
    0,  /*         GT => nothing */
    0,  /*         LE => nothing */
    0,  /*         LT => nothing */
    0,  /*         GE => nothing */
    0,  /*     ESCAPE => nothing */
    0,  /*     BITAND => nothing */
    0,  /*      BITOR => nothing */
    0,  /*     LSHIFT => nothing */
    0,  /*     RSHIFT => nothing */
    0,  /*       PLUS => nothing */
    0,  /*      MINUS => nothing */
    0,  /*       STAR => nothing */
    0,  /*      SLASH => nothing */
    0,  /*        REM => nothing */
    0,  /*     CONCAT => nothing */
    0,  /*    COLLATE => nothing */
    0,  /*     BITNOT => nothing */
    0,  /*      BEGIN => nothing */
    0,  /* TRANSACTION => nothing */
   51,  /*   DEFERRED => ID */
    0,  /*     COMMIT => nothing */
   51,  /*        END => ID */
    0,  /*   ROLLBACK => nothing */
    0,  /*  SAVEPOINT => nothing */
   51,  /*    RELEASE => ID */
    0,  /*         TO => nothing */
    0,  /*      TABLE => nothing */
    0,  /*     CREATE => nothing */
   51,  /*         IF => ID */
    0,  /*     EXISTS => nothing */
    0,  /*         LP => nothing */
    0,  /*         RP => nothing */
    0,  /*         AS => nothing */
    0,  /*      COMMA => nothing */
    0,  /*         ID => nothing */
   51,  /*    INDEXED => ID */
   51,  /*      ABORT => ID */
   51,  /*     ACTION => ID */
   51,  /*        ADD => ID */
   51,  /*      AFTER => ID */
   51,  /* AUTOINCREMENT => ID */
   51,  /*     BEFORE => ID */
   51,  /*    CASCADE => ID */
   51,  /*   CONFLICT => ID */
   51,  /*       FAIL => ID */
   51,  /*     IGNORE => ID */
   51,  /*  INITIALLY => ID */
   51,  /*    INSTEAD => ID */
   51,  /*         NO => ID */
   51,  /*        KEY => ID */
   51,  /*     OFFSET => ID */
   51,  /*      RAISE => ID */
   51,  /*    REPLACE => ID */
   51,  /*   RESTRICT => ID */
   51,  /*     RENAME => ID */
   51,  /*   CTIME_KW => ID */
};
#endif /* YYFALLBACK */

/* The following structure represents a single element of the
** parser's stack.  Information stored includes:
**
**   +  The state number for the parser at this level of the stack.
**
**   +  The value of the token stored at this level of the stack.
**      (In other words, the "major" token.)
**
**   +  The semantic value stored at this level of the stack.  This is
**      the information used by the action routines in the grammar.
**      It is sometimes called the "minor" token.
**
** After the "shift" half of a SHIFTREDUCE action, the stateno field
** actually contains the reduce action for the second half of the
** SHIFTREDUCE.
*/
struct yyStackEntry {
  YYACTIONTYPE stateno;  /* The state-number, or reduce action in SHIFTREDUCE */
  YYCODETYPE major;      /* The major token value.  This is the code
                         ** number for the token at this stack level */
  YYMINORTYPE minor;     /* The user-supplied minor token value.  This
                         ** is the value of the token  */
};
typedef struct yyStackEntry yyStackEntry;

/* The state of the parser is completely contained in an instance of
** the following structure */
struct yyParser {
  yyStackEntry *yytos;          /* Pointer to top element of the stack */
#ifdef YYTRACKMAXSTACKDEPTH
  int yyhwm;                    /* High-water mark of the stack */
#endif
#ifndef YYNOERRORRECOVERY
  int yyerrcnt;                 /* Shifts left before out of the error */
#endif
  bool is_fallback_failed;      /* Shows if fallback failed or not */
  sqlite3ParserARG_SDECL                /* A place to hold %extra_argument */
#if YYSTACKDEPTH<=0
  int yystksz;                  /* Current side of the stack */
  yyStackEntry *yystack;        /* The parser's stack */
  yyStackEntry yystk0;          /* First stack entry */
#else
  yyStackEntry yystack[YYSTACKDEPTH];  /* The parser's stack */
#endif
};
typedef struct yyParser yyParser;

#ifndef NDEBUG
#include <stdio.h>
static FILE *yyTraceFILE = 0;
static char *yyTracePrompt = 0;
#endif /* NDEBUG */

#ifndef NDEBUG
/* 
** Turn parser tracing on by giving a stream to which to write the trace
** and a prompt to preface each trace message.  Tracing is turned off
** by making either argument NULL 
**
** Inputs:
** <ul>
** <li> A FILE* to which trace output should be written.
**      If NULL, then tracing is turned off.
** <li> A prefix string written at the beginning of every
**      line of trace output.  If NULL, then tracing is
**      turned off.
** </ul>
**
** Outputs:
** None.
*/
void sqlite3ParserTrace(FILE *TraceFILE, char *zTracePrompt){
  yyTraceFILE = TraceFILE;
  yyTracePrompt = zTracePrompt;
  if( yyTraceFILE==0 ) yyTracePrompt = 0;
  else if( yyTracePrompt==0 ) yyTraceFILE = 0;
}
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing shifts, the names of all terminals and nonterminals
** are required.  The following table supplies these names */
static const char *const yyTokenName[] = { 
  "$",             "SEMI",          "EXPLAIN",       "QUERY",       
  "PLAN",          "OR",            "AND",           "NOT",         
  "IS",            "MATCH",         "LIKE_KW",       "BETWEEN",     
  "IN",            "ISNULL",        "NOTNULL",       "NE",          
  "EQ",            "GT",            "LE",            "LT",          
  "GE",            "ESCAPE",        "BITAND",        "BITOR",       
  "LSHIFT",        "RSHIFT",        "PLUS",          "MINUS",       
  "STAR",          "SLASH",         "REM",           "CONCAT",      
  "COLLATE",       "BITNOT",        "BEGIN",         "TRANSACTION", 
  "DEFERRED",      "COMMIT",        "END",           "ROLLBACK",    
  "SAVEPOINT",     "RELEASE",       "TO",            "TABLE",       
  "CREATE",        "IF",            "EXISTS",        "LP",          
  "RP",            "AS",            "COMMA",         "ID",          
  "INDEXED",       "ABORT",         "ACTION",        "ADD",         
  "AFTER",         "AUTOINCREMENT",  "BEFORE",        "CASCADE",     
  "CONFLICT",      "FAIL",          "IGNORE",        "INITIALLY",   
  "INSTEAD",       "NO",            "KEY",           "OFFSET",      
  "RAISE",         "REPLACE",       "RESTRICT",      "RENAME",      
  "CTIME_KW",      "ANY",           "STRING",        "CONSTRAINT",  
  "DEFAULT",       "NULL",          "PRIMARY",       "UNIQUE",      
  "CHECK",         "REFERENCES",    "AUTOINCR",      "ON",          
  "INSERT",        "DELETE",        "UPDATE",        "SET",         
  "DEFERRABLE",    "IMMEDIATE",     "FOREIGN",       "DROP",        
  "VIEW",          "UNION",         "ALL",           "EXCEPT",      
  "INTERSECT",     "SELECT",        "VALUES",        "DISTINCT",    
  "DOT",           "FROM",          "JOIN_KW",       "JOIN",        
  "BY",            "USING",         "ORDER",         "ASC",         
  "DESC",          "GROUP",         "HAVING",        "LIMIT",       
  "WHERE",         "INTO",          "FLOAT",         "BLOB",        
  "INTEGER",       "VARIABLE",      "CAST",          "CASE",        
  "WHEN",          "THEN",          "ELSE",          "INDEX",       
  "PRAGMA",        "TRIGGER",       "OF",            "FOR",         
  "EACH",          "ROW",           "ANALYZE",       "ALTER",       
  "WITH",          "RECURSIVE",     "error",         "input",       
  "ecmd",          "explain",       "cmdx",          "cmd",         
  "transtype",     "trans_opt",     "nm",            "savepoint_opt",
  "create_table",  "create_table_args",  "createkw",      "ifnotexists", 
  "columnlist",    "conslist_opt",  "select",        "columnname",  
  "carglist",      "typetoken",     "typename",      "signed",      
  "plus_num",      "minus_num",     "ccons",         "term",        
  "expr",          "onconf",        "sortorder",     "autoinc",     
  "eidlist_opt",   "refargs",       "defer_subclause",  "refarg",      
  "refact",        "init_deferred_pred_opt",  "conslist",      "tconscomma",  
  "tcons",         "sortlist",      "eidlist",       "defer_subclause_opt",
  "orconf",        "resolvetype",   "raisetype",     "ifexists",    
  "fullname",      "selectnowith",  "oneselect",     "with",        
  "multiselect_op",  "distinct",      "selcollist",    "from",        
  "where_opt",     "groupby_opt",   "having_opt",    "orderby_opt", 
  "limit_opt",     "values",        "nexprlist",     "exprlist",    
  "sclp",          "as",            "seltablist",    "stl_prefix",  
  "joinop",        "indexed_opt",   "on_opt",        "using_opt",   
  "join_nm",       "idlist",        "setlist",       "insert_cmd",  
  "idlist_opt",    "likeop",        "between_op",    "in_op",       
  "paren_exprlist",  "case_operand",  "case_exprlist",  "case_else",   
  "uniqueflag",    "collate",       "nmnum",         "trigger_decl",
  "trigger_cmd_list",  "trigger_time",  "trigger_event",  "foreach_clause",
  "when_clause",   "trigger_cmd",   "trnm",          "tridxby",     
  "wqlist",      
};
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing reduce actions, the names of all rules are required.
*/
static const char *const yyRuleName[] = {
 /*   0 */ "ecmd ::= explain cmdx SEMI",
 /*   1 */ "ecmd ::= SEMI",
 /*   2 */ "explain ::= EXPLAIN",
 /*   3 */ "explain ::= EXPLAIN QUERY PLAN",
 /*   4 */ "cmd ::= BEGIN transtype trans_opt",
 /*   5 */ "transtype ::=",
 /*   6 */ "transtype ::= DEFERRED",
 /*   7 */ "cmd ::= COMMIT trans_opt",
 /*   8 */ "cmd ::= END trans_opt",
 /*   9 */ "cmd ::= ROLLBACK trans_opt",
 /*  10 */ "cmd ::= SAVEPOINT nm",
 /*  11 */ "cmd ::= RELEASE savepoint_opt nm",
 /*  12 */ "cmd ::= ROLLBACK trans_opt TO savepoint_opt nm",
 /*  13 */ "create_table ::= createkw TABLE ifnotexists nm",
 /*  14 */ "createkw ::= CREATE",
 /*  15 */ "ifnotexists ::=",
 /*  16 */ "ifnotexists ::= IF NOT EXISTS",
 /*  17 */ "create_table_args ::= LP columnlist conslist_opt RP",
 /*  18 */ "create_table_args ::= AS select",
 /*  19 */ "columnname ::= nm typetoken",
 /*  20 */ "nm ::= ID|INDEXED",
 /*  21 */ "typetoken ::=",
 /*  22 */ "typetoken ::= typename LP signed RP",
 /*  23 */ "typetoken ::= typename LP signed COMMA signed RP",
 /*  24 */ "typename ::= typename ID|STRING",
 /*  25 */ "ccons ::= CONSTRAINT nm",
 /*  26 */ "ccons ::= DEFAULT term",
 /*  27 */ "ccons ::= DEFAULT LP expr RP",
 /*  28 */ "ccons ::= DEFAULT PLUS term",
 /*  29 */ "ccons ::= DEFAULT MINUS term",
 /*  30 */ "ccons ::= DEFAULT ID|INDEXED",
 /*  31 */ "ccons ::= NOT NULL onconf",
 /*  32 */ "ccons ::= PRIMARY KEY sortorder onconf autoinc",
 /*  33 */ "ccons ::= UNIQUE onconf",
 /*  34 */ "ccons ::= CHECK LP expr RP",
 /*  35 */ "ccons ::= REFERENCES nm eidlist_opt refargs",
 /*  36 */ "ccons ::= defer_subclause",
 /*  37 */ "ccons ::= COLLATE ID|INDEXED",
 /*  38 */ "autoinc ::=",
 /*  39 */ "autoinc ::= AUTOINCR",
 /*  40 */ "refargs ::=",
 /*  41 */ "refargs ::= refargs refarg",
 /*  42 */ "refarg ::= MATCH nm",
 /*  43 */ "refarg ::= ON INSERT refact",
 /*  44 */ "refarg ::= ON DELETE refact",
 /*  45 */ "refarg ::= ON UPDATE refact",
 /*  46 */ "refact ::= SET NULL",
 /*  47 */ "refact ::= SET DEFAULT",
 /*  48 */ "refact ::= CASCADE",
 /*  49 */ "refact ::= RESTRICT",
 /*  50 */ "refact ::= NO ACTION",
 /*  51 */ "defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt",
 /*  52 */ "defer_subclause ::= DEFERRABLE init_deferred_pred_opt",
 /*  53 */ "init_deferred_pred_opt ::=",
 /*  54 */ "init_deferred_pred_opt ::= INITIALLY DEFERRED",
 /*  55 */ "init_deferred_pred_opt ::= INITIALLY IMMEDIATE",
 /*  56 */ "conslist_opt ::=",
 /*  57 */ "tconscomma ::= COMMA",
 /*  58 */ "tcons ::= CONSTRAINT nm",
 /*  59 */ "tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf",
 /*  60 */ "tcons ::= UNIQUE LP sortlist RP onconf",
 /*  61 */ "tcons ::= CHECK LP expr RP onconf",
 /*  62 */ "tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt",
 /*  63 */ "defer_subclause_opt ::=",
 /*  64 */ "onconf ::=",
 /*  65 */ "onconf ::= ON CONFLICT resolvetype",
 /*  66 */ "orconf ::=",
 /*  67 */ "orconf ::= OR resolvetype",
 /*  68 */ "resolvetype ::= IGNORE",
 /*  69 */ "resolvetype ::= REPLACE",
 /*  70 */ "cmd ::= DROP TABLE ifexists fullname",
 /*  71 */ "ifexists ::= IF EXISTS",
 /*  72 */ "ifexists ::=",
 /*  73 */ "cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select",
 /*  74 */ "cmd ::= DROP VIEW ifexists fullname",
 /*  75 */ "cmd ::= select",
 /*  76 */ "select ::= with selectnowith",
 /*  77 */ "selectnowith ::= selectnowith multiselect_op oneselect",
 /*  78 */ "multiselect_op ::= UNION",
 /*  79 */ "multiselect_op ::= UNION ALL",
 /*  80 */ "multiselect_op ::= EXCEPT|INTERSECT",
 /*  81 */ "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
 /*  82 */ "values ::= VALUES LP nexprlist RP",
 /*  83 */ "values ::= values COMMA LP exprlist RP",
 /*  84 */ "distinct ::= DISTINCT",
 /*  85 */ "distinct ::= ALL",
 /*  86 */ "distinct ::=",
 /*  87 */ "sclp ::=",
 /*  88 */ "selcollist ::= sclp expr as",
 /*  89 */ "selcollist ::= sclp STAR",
 /*  90 */ "selcollist ::= sclp nm DOT STAR",
 /*  91 */ "as ::= AS nm",
 /*  92 */ "as ::=",
 /*  93 */ "from ::=",
 /*  94 */ "from ::= FROM seltablist",
 /*  95 */ "stl_prefix ::= seltablist joinop",
 /*  96 */ "stl_prefix ::=",
 /*  97 */ "seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt",
 /*  98 */ "seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt",
 /*  99 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
 /* 100 */ "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
 /* 101 */ "fullname ::= nm",
 /* 102 */ "joinop ::= COMMA|JOIN",
 /* 103 */ "joinop ::= JOIN_KW JOIN",
 /* 104 */ "joinop ::= JOIN_KW join_nm JOIN",
 /* 105 */ "joinop ::= JOIN_KW join_nm join_nm JOIN",
 /* 106 */ "on_opt ::= ON expr",
 /* 107 */ "on_opt ::=",
 /* 108 */ "indexed_opt ::=",
 /* 109 */ "indexed_opt ::= INDEXED BY nm",
 /* 110 */ "indexed_opt ::= NOT INDEXED",
 /* 111 */ "using_opt ::= USING LP idlist RP",
 /* 112 */ "using_opt ::=",
 /* 113 */ "orderby_opt ::=",
 /* 114 */ "orderby_opt ::= ORDER BY sortlist",
 /* 115 */ "sortlist ::= sortlist COMMA expr sortorder",
 /* 116 */ "sortlist ::= expr sortorder",
 /* 117 */ "sortorder ::= ASC",
 /* 118 */ "sortorder ::= DESC",
 /* 119 */ "sortorder ::=",
 /* 120 */ "groupby_opt ::=",
 /* 121 */ "groupby_opt ::= GROUP BY nexprlist",
 /* 122 */ "having_opt ::=",
 /* 123 */ "having_opt ::= HAVING expr",
 /* 124 */ "limit_opt ::=",
 /* 125 */ "limit_opt ::= LIMIT expr",
 /* 126 */ "limit_opt ::= LIMIT expr OFFSET expr",
 /* 127 */ "limit_opt ::= LIMIT expr COMMA expr",
 /* 128 */ "cmd ::= with DELETE FROM fullname indexed_opt where_opt",
 /* 129 */ "where_opt ::=",
 /* 130 */ "where_opt ::= WHERE expr",
 /* 131 */ "cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt",
 /* 132 */ "setlist ::= setlist COMMA nm EQ expr",
 /* 133 */ "setlist ::= setlist COMMA LP idlist RP EQ expr",
 /* 134 */ "setlist ::= nm EQ expr",
 /* 135 */ "setlist ::= LP idlist RP EQ expr",
 /* 136 */ "cmd ::= with insert_cmd INTO fullname idlist_opt select",
 /* 137 */ "cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES",
 /* 138 */ "insert_cmd ::= INSERT orconf",
 /* 139 */ "insert_cmd ::= REPLACE",
 /* 140 */ "idlist_opt ::=",
 /* 141 */ "idlist_opt ::= LP idlist RP",
 /* 142 */ "idlist ::= idlist COMMA nm",
 /* 143 */ "idlist ::= nm",
 /* 144 */ "expr ::= LP expr RP",
 /* 145 */ "term ::= NULL",
 /* 146 */ "expr ::= ID|INDEXED",
 /* 147 */ "expr ::= JOIN_KW",
 /* 148 */ "expr ::= nm DOT nm",
 /* 149 */ "term ::= FLOAT|BLOB",
 /* 150 */ "term ::= STRING",
 /* 151 */ "term ::= INTEGER",
 /* 152 */ "expr ::= VARIABLE",
 /* 153 */ "expr ::= expr COLLATE ID|INDEXED",
 /* 154 */ "expr ::= CAST LP expr AS typetoken RP",
 /* 155 */ "expr ::= ID|INDEXED LP distinct exprlist RP",
 /* 156 */ "expr ::= ID|INDEXED LP STAR RP",
 /* 157 */ "term ::= CTIME_KW",
 /* 158 */ "expr ::= LP nexprlist COMMA expr RP",
 /* 159 */ "expr ::= expr AND expr",
 /* 160 */ "expr ::= expr OR expr",
 /* 161 */ "expr ::= expr LT|GT|GE|LE expr",
 /* 162 */ "expr ::= expr EQ|NE expr",
 /* 163 */ "expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr",
 /* 164 */ "expr ::= expr PLUS|MINUS expr",
 /* 165 */ "expr ::= expr STAR|SLASH|REM expr",
 /* 166 */ "expr ::= expr CONCAT expr",
 /* 167 */ "likeop ::= LIKE_KW|MATCH",
 /* 168 */ "likeop ::= NOT LIKE_KW|MATCH",
 /* 169 */ "expr ::= expr likeop expr",
 /* 170 */ "expr ::= expr likeop expr ESCAPE expr",
 /* 171 */ "expr ::= expr ISNULL|NOTNULL",
 /* 172 */ "expr ::= expr NOT NULL",
 /* 173 */ "expr ::= expr IS expr",
 /* 174 */ "expr ::= expr IS NOT expr",
 /* 175 */ "expr ::= NOT expr",
 /* 176 */ "expr ::= BITNOT expr",
 /* 177 */ "expr ::= MINUS expr",
 /* 178 */ "expr ::= PLUS expr",
 /* 179 */ "between_op ::= BETWEEN",
 /* 180 */ "between_op ::= NOT BETWEEN",
 /* 181 */ "expr ::= expr between_op expr AND expr",
 /* 182 */ "in_op ::= IN",
 /* 183 */ "in_op ::= NOT IN",
 /* 184 */ "expr ::= expr in_op LP exprlist RP",
 /* 185 */ "expr ::= LP select RP",
 /* 186 */ "expr ::= expr in_op LP select RP",
 /* 187 */ "expr ::= expr in_op nm paren_exprlist",
 /* 188 */ "expr ::= EXISTS LP select RP",
 /* 189 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 190 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 191 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 192 */ "case_else ::= ELSE expr",
 /* 193 */ "case_else ::=",
 /* 194 */ "case_operand ::= expr",
 /* 195 */ "case_operand ::=",
 /* 196 */ "exprlist ::=",
 /* 197 */ "nexprlist ::= nexprlist COMMA expr",
 /* 198 */ "nexprlist ::= expr",
 /* 199 */ "paren_exprlist ::=",
 /* 200 */ "paren_exprlist ::= LP exprlist RP",
 /* 201 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt",
 /* 202 */ "uniqueflag ::= UNIQUE",
 /* 203 */ "uniqueflag ::=",
 /* 204 */ "eidlist_opt ::=",
 /* 205 */ "eidlist_opt ::= LP eidlist RP",
 /* 206 */ "eidlist ::= eidlist COMMA nm collate sortorder",
 /* 207 */ "eidlist ::= nm collate sortorder",
 /* 208 */ "collate ::=",
 /* 209 */ "collate ::= COLLATE ID|INDEXED",
 /* 210 */ "cmd ::= DROP INDEX ifexists fullname ON nm",
 /* 211 */ "cmd ::= PRAGMA nm",
 /* 212 */ "cmd ::= PRAGMA nm EQ nmnum",
 /* 213 */ "cmd ::= PRAGMA nm LP nmnum RP",
 /* 214 */ "cmd ::= PRAGMA nm EQ minus_num",
 /* 215 */ "cmd ::= PRAGMA nm LP minus_num RP",
 /* 216 */ "cmd ::= PRAGMA nm EQ nm DOT nm",
 /* 217 */ "cmd ::= PRAGMA",
 /* 218 */ "plus_num ::= PLUS INTEGER|FLOAT",
 /* 219 */ "minus_num ::= MINUS INTEGER|FLOAT",
 /* 220 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
 /* 221 */ "trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause",
 /* 222 */ "trigger_time ::= BEFORE",
 /* 223 */ "trigger_time ::= AFTER",
 /* 224 */ "trigger_time ::= INSTEAD OF",
 /* 225 */ "trigger_time ::=",
 /* 226 */ "trigger_event ::= DELETE|INSERT",
 /* 227 */ "trigger_event ::= UPDATE",
 /* 228 */ "trigger_event ::= UPDATE OF idlist",
 /* 229 */ "when_clause ::=",
 /* 230 */ "when_clause ::= WHEN expr",
 /* 231 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
 /* 232 */ "trigger_cmd_list ::= trigger_cmd SEMI",
 /* 233 */ "trnm ::= nm DOT nm",
 /* 234 */ "tridxby ::= INDEXED BY nm",
 /* 235 */ "tridxby ::= NOT INDEXED",
 /* 236 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 237 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
 /* 238 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 239 */ "trigger_cmd ::= select",
 /* 240 */ "expr ::= RAISE LP IGNORE RP",
 /* 241 */ "expr ::= RAISE LP raisetype COMMA STRING RP",
 /* 242 */ "raisetype ::= ROLLBACK",
 /* 243 */ "raisetype ::= ABORT",
 /* 244 */ "raisetype ::= FAIL",
 /* 245 */ "cmd ::= DROP TRIGGER ifexists fullname",
 /* 246 */ "cmd ::= ANALYZE",
 /* 247 */ "cmd ::= ANALYZE nm",
 /* 248 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
 /* 249 */ "with ::=",
 /* 250 */ "with ::= WITH wqlist",
 /* 251 */ "with ::= WITH RECURSIVE wqlist",
 /* 252 */ "wqlist ::= nm eidlist_opt AS LP select RP",
 /* 253 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
 /* 254 */ "input ::= ecmd",
 /* 255 */ "explain ::=",
 /* 256 */ "cmdx ::= cmd",
 /* 257 */ "trans_opt ::=",
 /* 258 */ "trans_opt ::= TRANSACTION",
 /* 259 */ "trans_opt ::= TRANSACTION nm",
 /* 260 */ "savepoint_opt ::= SAVEPOINT",
 /* 261 */ "savepoint_opt ::=",
 /* 262 */ "cmd ::= create_table create_table_args",
 /* 263 */ "columnlist ::= columnlist COMMA columnname carglist",
 /* 264 */ "columnlist ::= columnname carglist",
 /* 265 */ "typetoken ::= typename",
 /* 266 */ "typename ::= ID|STRING",
 /* 267 */ "signed ::= plus_num",
 /* 268 */ "signed ::= minus_num",
 /* 269 */ "carglist ::= carglist ccons",
 /* 270 */ "carglist ::=",
 /* 271 */ "ccons ::= NULL onconf",
 /* 272 */ "conslist_opt ::= COMMA conslist",
 /* 273 */ "conslist ::= conslist tconscomma tcons",
 /* 274 */ "conslist ::= tcons",
 /* 275 */ "tconscomma ::=",
 /* 276 */ "defer_subclause_opt ::= defer_subclause",
 /* 277 */ "resolvetype ::= raisetype",
 /* 278 */ "selectnowith ::= oneselect",
 /* 279 */ "oneselect ::= values",
 /* 280 */ "sclp ::= selcollist COMMA",
 /* 281 */ "as ::= ID|STRING",
 /* 282 */ "join_nm ::= ID|INDEXED",
 /* 283 */ "join_nm ::= JOIN_KW",
 /* 284 */ "expr ::= term",
 /* 285 */ "exprlist ::= nexprlist",
 /* 286 */ "nmnum ::= plus_num",
 /* 287 */ "nmnum ::= STRING",
 /* 288 */ "nmnum ::= nm",
 /* 289 */ "nmnum ::= ON",
 /* 290 */ "nmnum ::= DELETE",
 /* 291 */ "nmnum ::= DEFAULT",
 /* 292 */ "plus_num ::= INTEGER|FLOAT",
 /* 293 */ "foreach_clause ::=",
 /* 294 */ "foreach_clause ::= FOR EACH ROW",
 /* 295 */ "trnm ::= nm",
 /* 296 */ "tridxby ::=",
};
#endif /* NDEBUG */


#if YYSTACKDEPTH<=0
/*
** Try to increase the size of the parser stack.  Return the number
** of errors.  Return 0 on success.
*/
static int yyGrowStack(yyParser *p){
  int newSize;
  int idx;
  yyStackEntry *pNew;

  newSize = p->yystksz*2 + 100;
  idx = p->yytos ? (int)(p->yytos - p->yystack) : 0;
  if( p->yystack==&p->yystk0 ){
    pNew = malloc(newSize*sizeof(pNew[0]));
    if( pNew ) pNew[0] = p->yystk0;
  }else{
    pNew = realloc(p->yystack, newSize*sizeof(pNew[0]));
  }
  if( pNew ){
    p->yystack = pNew;
    p->yytos = &p->yystack[idx];
#ifndef NDEBUG
    if( yyTraceFILE ){
      fprintf(yyTraceFILE,"%sStack grows from %d to %d entries.\n",
              yyTracePrompt, p->yystksz, newSize);
    }
#endif
    p->yystksz = newSize;
  }
  return pNew==0; 
}
#endif

/* Datatype of the argument to the memory allocated passed as the
** second argument to sqlite3ParserAlloc() below.  This can be changed by
** putting an appropriate #define in the %include section of the input
** grammar.
*/
#ifndef YYMALLOCARGTYPE
# define YYMALLOCARGTYPE size_t
#endif

/* 
** This function allocates a new parser.
** The only argument is a pointer to a function which works like
** malloc.
**
** Inputs:
** A pointer to the function used to allocate memory.
**
** Outputs:
** A pointer to a parser.  This pointer is used in subsequent calls
** to sqlite3Parser and sqlite3ParserFree.
*/
void *sqlite3ParserAlloc(void *(*mallocProc)(YYMALLOCARGTYPE)){
  yyParser *pParser;
  pParser = (yyParser*)(*mallocProc)( (YYMALLOCARGTYPE)sizeof(yyParser) );
  if( pParser ){
#ifdef YYTRACKMAXSTACKDEPTH
    pParser->yyhwm = 0;
    pParser->is_fallback_failed = false;
#endif
#if YYSTACKDEPTH<=0
    pParser->yytos = NULL;
    pParser->yystack = NULL;
    pParser->yystksz = 0;
    if( yyGrowStack(pParser) ){
      pParser->yystack = &pParser->yystk0;
      pParser->yystksz = 1;
    }
#endif
#ifndef YYNOERRORRECOVERY
    pParser->yyerrcnt = -1;
#endif
    pParser->yytos = pParser->yystack;
    pParser->yystack[0].stateno = 0;
    pParser->yystack[0].major = 0;
  }
  return pParser;
}

/* The following function deletes the "minor type" or semantic value
** associated with a symbol.  The symbol can be either a terminal
** or nonterminal. "yymajor" is the symbol code, and "yypminor" is
** a pointer to the value to be deleted.  The code used to do the 
** deletions is derived from the %destructor and/or %token_destructor
** directives of the input grammar.
*/
static void yy_destructor(
  yyParser *yypParser,    /* The parser */
  YYCODETYPE yymajor,     /* Type code for object to destroy */
  YYMINORTYPE *yypminor   /* The object to be destroyed */
){
  sqlite3ParserARG_FETCH;
  switch( yymajor ){
    /* Here is inserted the actions which take place when a
    ** terminal or non-terminal is destroyed.  This can happen
    ** when the symbol is popped from the stack during a
    ** reduce or during error processing or when a parser is 
    ** being destroyed before it is finished parsing.
    **
    ** Note: during a reduce, the only symbols destroyed are those
    ** which appear on the RHS of the rule, but which are *not* used
    ** inside the C code.
    */
/********* Begin destructor definitions ***************************************/
    case 150: /* select */
    case 181: /* selectnowith */
    case 182: /* oneselect */
    case 193: /* values */
{
#line 386 "parse.y"
sqlite3SelectDelete(pParse->db, (yypminor->yy203));
#line 1468 "parse.c"
}
      break;
    case 159: /* term */
    case 160: /* expr */
{
#line 829 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy266).pExpr);
#line 1476 "parse.c"
}
      break;
    case 164: /* eidlist_opt */
    case 173: /* sortlist */
    case 174: /* eidlist */
    case 186: /* selcollist */
    case 189: /* groupby_opt */
    case 191: /* orderby_opt */
    case 194: /* nexprlist */
    case 195: /* exprlist */
    case 196: /* sclp */
    case 206: /* setlist */
    case 212: /* paren_exprlist */
    case 214: /* case_exprlist */
{
#line 1261 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy162));
#line 1494 "parse.c"
}
      break;
    case 180: /* fullname */
    case 187: /* from */
    case 198: /* seltablist */
    case 199: /* stl_prefix */
{
#line 613 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy41));
#line 1504 "parse.c"
}
      break;
    case 183: /* with */
    case 228: /* wqlist */
{
#line 1511 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy273));
#line 1512 "parse.c"
}
      break;
    case 188: /* where_opt */
    case 190: /* having_opt */
    case 202: /* on_opt */
    case 213: /* case_operand */
    case 215: /* case_else */
    case 224: /* when_clause */
{
#line 738 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy396));
#line 1524 "parse.c"
}
      break;
    case 203: /* using_opt */
    case 205: /* idlist */
    case 208: /* idlist_opt */
{
#line 650 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy306));
#line 1533 "parse.c"
}
      break;
    case 220: /* trigger_cmd_list */
    case 225: /* trigger_cmd */
{
#line 1384 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy451));
#line 1541 "parse.c"
}
      break;
    case 222: /* trigger_event */
{
#line 1370 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy184).b);
#line 1548 "parse.c"
}
      break;
/********* End destructor definitions *****************************************/
    default:  break;   /* If no destructor action specified: do nothing */
  }
}

/*
** Pop the parser's stack once.
**
** If there is a destructor routine associated with the token which
** is popped from the stack, then call it.
*/
static void yy_pop_parser_stack(yyParser *pParser){
  yyStackEntry *yytos;
  assert( pParser->yytos!=0 );
  assert( pParser->yytos > pParser->yystack );
  yytos = pParser->yytos--;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sPopping %s\n",
      yyTracePrompt,
      yyTokenName[yytos->major]);
  }
#endif
  yy_destructor(pParser, yytos->major, &yytos->minor);
}

/* 
** Deallocate and destroy a parser.  Destructors are called for
** all stack elements before shutting the parser down.
**
** If the YYPARSEFREENEVERNULL macro exists (for example because it
** is defined in a %include section of the input grammar) then it is
** assumed that the input pointer is never NULL.
*/
void sqlite3ParserFree(
  void *p,                    /* The parser to be deleted */
  void (*freeProc)(void*)     /* Function used to reclaim memory */
){
  yyParser *pParser = (yyParser*)p;
#ifndef YYPARSEFREENEVERNULL
  if( pParser==0 ) return;
#endif
  while( pParser->yytos>pParser->yystack ) yy_pop_parser_stack(pParser);
#if YYSTACKDEPTH<=0
  if( pParser->yystack!=&pParser->yystk0 ) free(pParser->yystack);
#endif
  (*freeProc)((void*)pParser);
}

/*
** Return the peak depth of the stack for a parser.
*/
#ifdef YYTRACKMAXSTACKDEPTH
int sqlite3ParserStackPeak(void *p){
  yyParser *pParser = (yyParser*)p;
  return pParser->yyhwm;
}
#endif

/*
** Find the appropriate action for a parser given the terminal
** look-ahead token iLookAhead.
*/
static unsigned int yy_find_shift_action(
  yyParser *pParser,        /* The parser */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
  int stateno = pParser->yytos->stateno;
 
  if( stateno>=YY_MIN_REDUCE ) return stateno;
  assert( stateno <= YY_SHIFT_COUNT );
  do{
    i = yy_shift_ofst[stateno];
    assert( iLookAhead!=YYNOCODE );
    i += iLookAhead;
    if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
#ifdef YYFALLBACK
      YYCODETYPE iFallback = -1;            /* Fallback token */
      if( iLookAhead<sizeof(yyFallback)/sizeof(yyFallback[0])
             && (iFallback = yyFallback[iLookAhead])!=0 ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE, "%sFALLBACK %s => %s\n",
             yyTracePrompt, yyTokenName[iLookAhead], yyTokenName[iFallback]);
        }
#endif
        assert( yyFallback[iFallback]==0 ); /* Fallback loop must terminate */
        iLookAhead = iFallback;
        continue;
      } else if ( iFallback==0 ) {
        pParser->is_fallback_failed = true;
      }
#endif
#ifdef YYWILDCARD
      {
        int j = i - iLookAhead + YYWILDCARD;
        if( 
#if YY_SHIFT_MIN+YYWILDCARD<0
          j>=0 &&
#endif
#if YY_SHIFT_MAX+YYWILDCARD>=YY_ACTTAB_COUNT
          j<YY_ACTTAB_COUNT &&
#endif
          yy_lookahead[j]==YYWILDCARD && iLookAhead>0
        ){
#ifndef NDEBUG
          if( yyTraceFILE ){
            fprintf(yyTraceFILE, "%sWILDCARD %s => %s\n",
               yyTracePrompt, yyTokenName[iLookAhead],
               yyTokenName[YYWILDCARD]);
          }
#endif /* NDEBUG */
          return yy_action[j];
        }
      }
#endif /* YYWILDCARD */
      return yy_default[stateno];
    }else{
      return yy_action[i];
    }
  }while(1);
}

/*
** Find the appropriate action for a parser given the non-terminal
** look-ahead token iLookAhead.
*/
static int yy_find_reduce_action(
  int stateno,              /* Current state number */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
#ifdef YYERRORSYMBOL
  if( stateno>YY_REDUCE_COUNT ){
    return yy_default[stateno];
  }
#else
  assert( stateno<=YY_REDUCE_COUNT );
#endif
  i = yy_reduce_ofst[stateno];
  assert( i!=YY_REDUCE_USE_DFLT );
  assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
#ifdef YYERRORSYMBOL
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    return yy_default[stateno];
  }
#else
  assert( i>=0 && i<YY_ACTTAB_COUNT );
  assert( yy_lookahead[i]==iLookAhead );
#endif
  return yy_action[i];
}

/*
** The following routine is called if the stack overflows.
*/
static void yyStackOverflow(yyParser *yypParser){
   sqlite3ParserARG_FETCH;
#ifndef NDEBUG
   if( yyTraceFILE ){
     fprintf(yyTraceFILE,"%sStack Overflow!\n",yyTracePrompt);
   }
#endif
   while( yypParser->yytos>yypParser->yystack ) yy_pop_parser_stack(yypParser);
   /* Here code is inserted which will execute if the parser
   ** stack every overflows */
/******** Begin %stack_overflow code ******************************************/
#line 41 "parse.y"

  sqlite3ErrorMsg(pParse, "parser stack overflow");
#line 1723 "parse.c"
/******** End %stack_overflow code ********************************************/
   sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument var */
}

/*
** Print tracing information for a SHIFT action
*/
#ifndef NDEBUG
static void yyTraceShift(yyParser *yypParser, int yyNewState){
  if( yyTraceFILE ){
    if( yyNewState<YYNSTATE ){
      fprintf(yyTraceFILE,"%sShift '%s', go to state %d\n",
         yyTracePrompt,yyTokenName[yypParser->yytos->major],
         yyNewState);
    }else{
      fprintf(yyTraceFILE,"%sShift '%s'\n",
         yyTracePrompt,yyTokenName[yypParser->yytos->major]);
    }
  }
}
#else
# define yyTraceShift(X,Y)
#endif

/*
** Perform a shift action.
*/
static void yy_shift(
  yyParser *yypParser,          /* The parser to be shifted */
  int yyNewState,               /* The new state to shift in */
  int yyMajor,                  /* The major token to shift in */
  sqlite3ParserTOKENTYPE yyMinor        /* The minor token to shift in */
){
  yyStackEntry *yytos;
  yypParser->yytos++;
#ifdef YYTRACKMAXSTACKDEPTH
  if( (int)(yypParser->yytos - yypParser->yystack)>yypParser->yyhwm ){
    yypParser->yyhwm++;
    assert( yypParser->yyhwm == (int)(yypParser->yytos - yypParser->yystack) );
  }
#endif
#if YYSTACKDEPTH>0 
  if( yypParser->yytos>=&yypParser->yystack[YYSTACKDEPTH] ){
    yypParser->yytos--;
    yyStackOverflow(yypParser);
    return;
  }
#else
  if( yypParser->yytos>=&yypParser->yystack[yypParser->yystksz] ){
    if( yyGrowStack(yypParser) ){
      yypParser->yytos--;
      yyStackOverflow(yypParser);
      return;
    }
  }
#endif
  if( yyNewState > YY_MAX_SHIFT ){
    yyNewState += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
  }
  yytos = yypParser->yytos;
  yytos->stateno = (YYACTIONTYPE)yyNewState;
  yytos->major = (YYCODETYPE)yyMajor;
  yytos->minor.yy0 = yyMinor;
  yyTraceShift(yypParser, yyNewState);
}

/* The following table contains information about every rule that
** is used during the reduce.
*/
static const struct {
  YYCODETYPE lhs;         /* Symbol on the left-hand side of the rule */
  unsigned char nrhs;     /* Number of right-hand side symbols in the rule */
} yyRuleInfo[] = {
  { 136, 3 },
  { 136, 1 },
  { 137, 1 },
  { 137, 3 },
  { 139, 3 },
  { 140, 0 },
  { 140, 1 },
  { 139, 2 },
  { 139, 2 },
  { 139, 2 },
  { 139, 2 },
  { 139, 3 },
  { 139, 5 },
  { 144, 4 },
  { 146, 1 },
  { 147, 0 },
  { 147, 3 },
  { 145, 4 },
  { 145, 2 },
  { 151, 2 },
  { 142, 1 },
  { 153, 0 },
  { 153, 4 },
  { 153, 6 },
  { 154, 2 },
  { 158, 2 },
  { 158, 2 },
  { 158, 4 },
  { 158, 3 },
  { 158, 3 },
  { 158, 2 },
  { 158, 3 },
  { 158, 5 },
  { 158, 2 },
  { 158, 4 },
  { 158, 4 },
  { 158, 1 },
  { 158, 2 },
  { 163, 0 },
  { 163, 1 },
  { 165, 0 },
  { 165, 2 },
  { 167, 2 },
  { 167, 3 },
  { 167, 3 },
  { 167, 3 },
  { 168, 2 },
  { 168, 2 },
  { 168, 1 },
  { 168, 1 },
  { 168, 2 },
  { 166, 3 },
  { 166, 2 },
  { 169, 0 },
  { 169, 2 },
  { 169, 2 },
  { 149, 0 },
  { 171, 1 },
  { 172, 2 },
  { 172, 7 },
  { 172, 5 },
  { 172, 5 },
  { 172, 10 },
  { 175, 0 },
  { 161, 0 },
  { 161, 3 },
  { 176, 0 },
  { 176, 2 },
  { 177, 1 },
  { 177, 1 },
  { 139, 4 },
  { 179, 2 },
  { 179, 0 },
  { 139, 7 },
  { 139, 4 },
  { 139, 1 },
  { 150, 2 },
  { 181, 3 },
  { 184, 1 },
  { 184, 2 },
  { 184, 1 },
  { 182, 9 },
  { 193, 4 },
  { 193, 5 },
  { 185, 1 },
  { 185, 1 },
  { 185, 0 },
  { 196, 0 },
  { 186, 3 },
  { 186, 2 },
  { 186, 4 },
  { 197, 2 },
  { 197, 0 },
  { 187, 0 },
  { 187, 2 },
  { 199, 2 },
  { 199, 0 },
  { 198, 6 },
  { 198, 8 },
  { 198, 7 },
  { 198, 7 },
  { 180, 1 },
  { 200, 1 },
  { 200, 2 },
  { 200, 3 },
  { 200, 4 },
  { 202, 2 },
  { 202, 0 },
  { 201, 0 },
  { 201, 3 },
  { 201, 2 },
  { 203, 4 },
  { 203, 0 },
  { 191, 0 },
  { 191, 3 },
  { 173, 4 },
  { 173, 2 },
  { 162, 1 },
  { 162, 1 },
  { 162, 0 },
  { 189, 0 },
  { 189, 3 },
  { 190, 0 },
  { 190, 2 },
  { 192, 0 },
  { 192, 2 },
  { 192, 4 },
  { 192, 4 },
  { 139, 6 },
  { 188, 0 },
  { 188, 2 },
  { 139, 8 },
  { 206, 5 },
  { 206, 7 },
  { 206, 3 },
  { 206, 5 },
  { 139, 6 },
  { 139, 7 },
  { 207, 2 },
  { 207, 1 },
  { 208, 0 },
  { 208, 3 },
  { 205, 3 },
  { 205, 1 },
  { 160, 3 },
  { 159, 1 },
  { 160, 1 },
  { 160, 1 },
  { 160, 3 },
  { 159, 1 },
  { 159, 1 },
  { 159, 1 },
  { 160, 1 },
  { 160, 3 },
  { 160, 6 },
  { 160, 5 },
  { 160, 4 },
  { 159, 1 },
  { 160, 5 },
  { 160, 3 },
  { 160, 3 },
  { 160, 3 },
  { 160, 3 },
  { 160, 3 },
  { 160, 3 },
  { 160, 3 },
  { 160, 3 },
  { 209, 1 },
  { 209, 2 },
  { 160, 3 },
  { 160, 5 },
  { 160, 2 },
  { 160, 3 },
  { 160, 3 },
  { 160, 4 },
  { 160, 2 },
  { 160, 2 },
  { 160, 2 },
  { 160, 2 },
  { 210, 1 },
  { 210, 2 },
  { 160, 5 },
  { 211, 1 },
  { 211, 2 },
  { 160, 5 },
  { 160, 3 },
  { 160, 5 },
  { 160, 4 },
  { 160, 4 },
  { 160, 5 },
  { 214, 5 },
  { 214, 4 },
  { 215, 2 },
  { 215, 0 },
  { 213, 1 },
  { 213, 0 },
  { 195, 0 },
  { 194, 3 },
  { 194, 1 },
  { 212, 0 },
  { 212, 3 },
  { 139, 11 },
  { 216, 1 },
  { 216, 0 },
  { 164, 0 },
  { 164, 3 },
  { 174, 5 },
  { 174, 3 },
  { 217, 0 },
  { 217, 2 },
  { 139, 6 },
  { 139, 2 },
  { 139, 4 },
  { 139, 5 },
  { 139, 4 },
  { 139, 5 },
  { 139, 6 },
  { 139, 1 },
  { 156, 2 },
  { 157, 2 },
  { 139, 5 },
  { 219, 9 },
  { 221, 1 },
  { 221, 1 },
  { 221, 2 },
  { 221, 0 },
  { 222, 1 },
  { 222, 1 },
  { 222, 3 },
  { 224, 0 },
  { 224, 2 },
  { 220, 3 },
  { 220, 2 },
  { 226, 3 },
  { 227, 3 },
  { 227, 2 },
  { 225, 7 },
  { 225, 5 },
  { 225, 5 },
  { 225, 1 },
  { 160, 4 },
  { 160, 6 },
  { 178, 1 },
  { 178, 1 },
  { 178, 1 },
  { 139, 4 },
  { 139, 1 },
  { 139, 2 },
  { 139, 6 },
  { 183, 0 },
  { 183, 2 },
  { 183, 3 },
  { 228, 6 },
  { 228, 8 },
  { 135, 1 },
  { 137, 0 },
  { 138, 1 },
  { 141, 0 },
  { 141, 1 },
  { 141, 2 },
  { 143, 1 },
  { 143, 0 },
  { 139, 2 },
  { 148, 4 },
  { 148, 2 },
  { 153, 1 },
  { 154, 1 },
  { 155, 1 },
  { 155, 1 },
  { 152, 2 },
  { 152, 0 },
  { 158, 2 },
  { 149, 2 },
  { 170, 3 },
  { 170, 1 },
  { 171, 0 },
  { 175, 1 },
  { 177, 1 },
  { 181, 1 },
  { 182, 1 },
  { 196, 2 },
  { 197, 1 },
  { 204, 1 },
  { 204, 1 },
  { 160, 1 },
  { 195, 1 },
  { 218, 1 },
  { 218, 1 },
  { 218, 1 },
  { 218, 1 },
  { 218, 1 },
  { 218, 1 },
  { 156, 1 },
  { 223, 0 },
  { 223, 3 },
  { 226, 1 },
  { 227, 0 },
};

static void yy_accept(yyParser*);  /* Forward Declaration */

/*
** Perform a reduce action and the shift that must immediately
** follow the reduce.
*/
static void yy_reduce(
  yyParser *yypParser,         /* The parser */
  unsigned int yyruleno        /* Number of the rule by which to reduce */
){
  int yygoto;                     /* The next state */
  int yyact;                      /* The next action */
  yyStackEntry *yymsp;            /* The top of the parser's stack */
  int yysize;                     /* Amount to pop the stack */
  sqlite3ParserARG_FETCH;
  yymsp = yypParser->yytos;
#ifndef NDEBUG
  if( yyTraceFILE && yyruleno<(int)(sizeof(yyRuleName)/sizeof(yyRuleName[0])) ){
    yysize = yyRuleInfo[yyruleno].nrhs;
    fprintf(yyTraceFILE, "%sReduce [%s], go to state %d.\n", yyTracePrompt,
      yyRuleName[yyruleno], yymsp[-yysize].stateno);
  }
#endif /* NDEBUG */

  /* Check that the stack is large enough to grow by a single entry
  ** if the RHS of the rule is empty.  This ensures that there is room
  ** enough on the stack to push the LHS value */
  if( yyRuleInfo[yyruleno].nrhs==0 ){
#ifdef YYTRACKMAXSTACKDEPTH
    if( (int)(yypParser->yytos - yypParser->yystack)>yypParser->yyhwm ){
      yypParser->yyhwm++;
      assert( yypParser->yyhwm == (int)(yypParser->yytos - yypParser->yystack));
    }
#endif
#if YYSTACKDEPTH>0 
    if( yypParser->yytos>=&yypParser->yystack[YYSTACKDEPTH-1] ){
      yyStackOverflow(yypParser);
      return;
    }
#else
    if( yypParser->yytos>=&yypParser->yystack[yypParser->yystksz-1] ){
      if( yyGrowStack(yypParser) ){
        yyStackOverflow(yypParser);
        return;
      }
      yymsp = yypParser->yytos;
    }
#endif
  }

  switch( yyruleno ){
  /* Beginning here are the reduction cases.  A typical example
  ** follows:
  **   case 0:
  **  #line <lineno> <grammarfile>
  **     { ... }           // User supplied code
  **  #line <lineno> <thisfile>
  **     break;
  */
/********** Begin reduce actions **********************************************/
        YYMINORTYPE yylhsminor;
      case 0: /* ecmd ::= explain cmdx SEMI */
#line 111 "parse.y"
{ sqlite3FinishCoding(pParse); }
#line 2160 "parse.c"
        break;
      case 1: /* ecmd ::= SEMI */
#line 112 "parse.y"
{
  sqlite3ErrorMsg(pParse, "syntax error: empty request");
}
#line 2167 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN */
#line 117 "parse.y"
{ pParse->explain = 1; }
#line 2172 "parse.c"
        break;
      case 3: /* explain ::= EXPLAIN QUERY PLAN */
#line 118 "parse.y"
{ pParse->explain = 2; }
#line 2177 "parse.c"
        break;
      case 4: /* cmd ::= BEGIN transtype trans_opt */
#line 150 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy444);}
#line 2182 "parse.c"
        break;
      case 5: /* transtype ::= */
#line 155 "parse.y"
{yymsp[1].minor.yy444 = TK_DEFERRED;}
#line 2187 "parse.c"
        break;
      case 6: /* transtype ::= DEFERRED */
#line 156 "parse.y"
{yymsp[0].minor.yy444 = yymsp[0].major; /*A-overwrites-X*/}
#line 2192 "parse.c"
        break;
      case 7: /* cmd ::= COMMIT trans_opt */
      case 8: /* cmd ::= END trans_opt */ yytestcase(yyruleno==8);
#line 157 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2198 "parse.c"
        break;
      case 9: /* cmd ::= ROLLBACK trans_opt */
#line 159 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2203 "parse.c"
        break;
      case 10: /* cmd ::= SAVEPOINT nm */
#line 163 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2210 "parse.c"
        break;
      case 11: /* cmd ::= RELEASE savepoint_opt nm */
#line 166 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2217 "parse.c"
        break;
      case 12: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 169 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2224 "parse.c"
        break;
      case 13: /* create_table ::= createkw TABLE ifnotexists nm */
#line 176 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[0].minor.yy0,yymsp[-1].minor.yy444);
}
#line 2231 "parse.c"
        break;
      case 14: /* createkw ::= CREATE */
#line 179 "parse.y"
{disableLookaside(pParse);}
#line 2236 "parse.c"
        break;
      case 15: /* ifnotexists ::= */
      case 38: /* autoinc ::= */ yytestcase(yyruleno==38);
      case 53: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==53);
      case 63: /* defer_subclause_opt ::= */ yytestcase(yyruleno==63);
      case 72: /* ifexists ::= */ yytestcase(yyruleno==72);
      case 86: /* distinct ::= */ yytestcase(yyruleno==86);
      case 208: /* collate ::= */ yytestcase(yyruleno==208);
#line 182 "parse.y"
{yymsp[1].minor.yy444 = 0;}
#line 2247 "parse.c"
        break;
      case 16: /* ifnotexists ::= IF NOT EXISTS */
#line 183 "parse.y"
{yymsp[-2].minor.yy444 = 1;}
#line 2252 "parse.c"
        break;
      case 17: /* create_table_args ::= LP columnlist conslist_opt RP */
#line 185 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,0,0);
}
#line 2259 "parse.c"
        break;
      case 18: /* create_table_args ::= AS select */
#line 188 "parse.y"
{
  sqlite3EndTable(pParse,0,0,0,yymsp[0].minor.yy203);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy203);
}
#line 2267 "parse.c"
        break;
      case 19: /* columnname ::= nm typetoken */
#line 194 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2272 "parse.c"
        break;
      case 20: /* nm ::= ID|INDEXED */
#line 225 "parse.y"
{
  if(yymsp[0].minor.yy0.isReserved) {
    sqlite3ErrorMsg(pParse, "keyword \"%T\" is reserved", &yymsp[0].minor.yy0);
  }
}
#line 2281 "parse.c"
        break;
      case 21: /* typetoken ::= */
      case 56: /* conslist_opt ::= */ yytestcase(yyruleno==56);
      case 92: /* as ::= */ yytestcase(yyruleno==92);
#line 236 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2288 "parse.c"
        break;
      case 22: /* typetoken ::= typename LP signed RP */
#line 238 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2295 "parse.c"
        break;
      case 23: /* typetoken ::= typename LP signed COMMA signed RP */
#line 241 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2302 "parse.c"
        break;
      case 24: /* typename ::= typename ID|STRING */
#line 246 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2307 "parse.c"
        break;
      case 25: /* ccons ::= CONSTRAINT nm */
      case 58: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==58);
#line 255 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2313 "parse.c"
        break;
      case 26: /* ccons ::= DEFAULT term */
      case 28: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==28);
#line 256 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy266);}
#line 2319 "parse.c"
        break;
      case 27: /* ccons ::= DEFAULT LP expr RP */
#line 257 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy266);}
#line 2324 "parse.c"
        break;
      case 29: /* ccons ::= DEFAULT MINUS term */
#line 259 "parse.y"
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy266.pExpr, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy266.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2335 "parse.c"
        break;
      case 30: /* ccons ::= DEFAULT ID|INDEXED */
#line 266 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2344 "parse.c"
        break;
      case 31: /* ccons ::= NOT NULL onconf */
#line 276 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy444);}
#line 2349 "parse.c"
        break;
      case 32: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 278 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy444,yymsp[0].minor.yy444,yymsp[-2].minor.yy444);}
#line 2354 "parse.c"
        break;
      case 33: /* ccons ::= UNIQUE onconf */
#line 279 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[0].minor.yy444,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2360 "parse.c"
        break;
      case 34: /* ccons ::= CHECK LP expr RP */
#line 281 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy266.pExpr);}
#line 2365 "parse.c"
        break;
      case 35: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 283 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy162,yymsp[0].minor.yy444);}
#line 2370 "parse.c"
        break;
      case 36: /* ccons ::= defer_subclause */
#line 284 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy444);}
#line 2375 "parse.c"
        break;
      case 37: /* ccons ::= COLLATE ID|INDEXED */
#line 285 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2380 "parse.c"
        break;
      case 39: /* autoinc ::= AUTOINCR */
#line 290 "parse.y"
{yymsp[0].minor.yy444 = 1;}
#line 2385 "parse.c"
        break;
      case 40: /* refargs ::= */
#line 298 "parse.y"
{ yymsp[1].minor.yy444 = ON_CONFLICT_ACTION_NONE*0x0101; /* EV: R-19803-45884 */}
#line 2390 "parse.c"
        break;
      case 41: /* refargs ::= refargs refarg */
#line 299 "parse.y"
{ yymsp[-1].minor.yy444 = (yymsp[-1].minor.yy444 & ~yymsp[0].minor.yy331.mask) | yymsp[0].minor.yy331.value; }
#line 2395 "parse.c"
        break;
      case 42: /* refarg ::= MATCH nm */
#line 301 "parse.y"
{ yymsp[-1].minor.yy331.value = 0;     yymsp[-1].minor.yy331.mask = 0x000000; }
#line 2400 "parse.c"
        break;
      case 43: /* refarg ::= ON INSERT refact */
#line 302 "parse.y"
{ yymsp[-2].minor.yy331.value = 0;     yymsp[-2].minor.yy331.mask = 0x000000; }
#line 2405 "parse.c"
        break;
      case 44: /* refarg ::= ON DELETE refact */
#line 303 "parse.y"
{ yymsp[-2].minor.yy331.value = yymsp[0].minor.yy444;     yymsp[-2].minor.yy331.mask = 0x0000ff; }
#line 2410 "parse.c"
        break;
      case 45: /* refarg ::= ON UPDATE refact */
#line 304 "parse.y"
{ yymsp[-2].minor.yy331.value = yymsp[0].minor.yy444<<8;  yymsp[-2].minor.yy331.mask = 0x00ff00; }
#line 2415 "parse.c"
        break;
      case 46: /* refact ::= SET NULL */
#line 306 "parse.y"
{ yymsp[-1].minor.yy444 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2420 "parse.c"
        break;
      case 47: /* refact ::= SET DEFAULT */
#line 307 "parse.y"
{ yymsp[-1].minor.yy444 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2425 "parse.c"
        break;
      case 48: /* refact ::= CASCADE */
#line 308 "parse.y"
{ yymsp[0].minor.yy444 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2430 "parse.c"
        break;
      case 49: /* refact ::= RESTRICT */
#line 309 "parse.y"
{ yymsp[0].minor.yy444 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2435 "parse.c"
        break;
      case 50: /* refact ::= NO ACTION */
#line 310 "parse.y"
{ yymsp[-1].minor.yy444 = ON_CONFLICT_ACTION_NONE;     /* EV: R-33326-45252 */}
#line 2440 "parse.c"
        break;
      case 51: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 312 "parse.y"
{yymsp[-2].minor.yy444 = 0;}
#line 2445 "parse.c"
        break;
      case 52: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 67: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==67);
      case 138: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==138);
#line 313 "parse.y"
{yymsp[-1].minor.yy444 = yymsp[0].minor.yy444;}
#line 2452 "parse.c"
        break;
      case 54: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 71: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==71);
      case 180: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==180);
      case 183: /* in_op ::= NOT IN */ yytestcase(yyruleno==183);
      case 209: /* collate ::= COLLATE ID|INDEXED */ yytestcase(yyruleno==209);
#line 316 "parse.y"
{yymsp[-1].minor.yy444 = 1;}
#line 2461 "parse.c"
        break;
      case 55: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 317 "parse.y"
{yymsp[-1].minor.yy444 = 0;}
#line 2466 "parse.c"
        break;
      case 57: /* tconscomma ::= COMMA */
#line 323 "parse.y"
{pParse->constraintName.n = 0;}
#line 2471 "parse.c"
        break;
      case 59: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 327 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy162,yymsp[0].minor.yy444,yymsp[-2].minor.yy444,0);}
#line 2476 "parse.c"
        break;
      case 60: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 329 "parse.y"
{sqlite3CreateIndex(pParse,0,0,yymsp[-2].minor.yy162,yymsp[0].minor.yy444,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2482 "parse.c"
        break;
      case 61: /* tcons ::= CHECK LP expr RP onconf */
#line 332 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy266.pExpr);}
#line 2487 "parse.c"
        break;
      case 62: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 334 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy162, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy162, yymsp[-1].minor.yy444);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy444);
}
#line 2495 "parse.c"
        break;
      case 64: /* onconf ::= */
      case 66: /* orconf ::= */ yytestcase(yyruleno==66);
#line 348 "parse.y"
{yymsp[1].minor.yy444 = ON_CONFLICT_ACTION_DEFAULT;}
#line 2501 "parse.c"
        break;
      case 65: /* onconf ::= ON CONFLICT resolvetype */
#line 349 "parse.y"
{yymsp[-2].minor.yy444 = yymsp[0].minor.yy444;}
#line 2506 "parse.c"
        break;
      case 68: /* resolvetype ::= IGNORE */
#line 353 "parse.y"
{yymsp[0].minor.yy444 = ON_CONFLICT_ACTION_IGNORE;}
#line 2511 "parse.c"
        break;
      case 69: /* resolvetype ::= REPLACE */
      case 139: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==139);
#line 354 "parse.y"
{yymsp[0].minor.yy444 = ON_CONFLICT_ACTION_REPLACE;}
#line 2517 "parse.c"
        break;
      case 70: /* cmd ::= DROP TABLE ifexists fullname */
#line 358 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy41, 0, yymsp[-1].minor.yy444);
}
#line 2524 "parse.c"
        break;
      case 73: /* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 369 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-6].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy162, yymsp[0].minor.yy203, yymsp[-4].minor.yy444);
}
#line 2531 "parse.c"
        break;
      case 74: /* cmd ::= DROP VIEW ifexists fullname */
#line 372 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy41, 1, yymsp[-1].minor.yy444);
}
#line 2538 "parse.c"
        break;
      case 75: /* cmd ::= select */
#line 379 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy203, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy203);
}
#line 2547 "parse.c"
        break;
      case 76: /* select ::= with selectnowith */
#line 416 "parse.y"
{
  Select *p = yymsp[0].minor.yy203;
  if( p ){
    p->pWith = yymsp[-1].minor.yy273;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, yymsp[-1].minor.yy273);
  }
  yymsp[-1].minor.yy203 = p; /*A-overwrites-W*/
}
#line 2561 "parse.c"
        break;
      case 77: /* selectnowith ::= selectnowith multiselect_op oneselect */
#line 429 "parse.y"
{
  Select *pRhs = yymsp[0].minor.yy203;
  Select *pLhs = yymsp[-2].minor.yy203;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,&x,pRhs,0,0);
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)yymsp[-1].minor.yy444;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( yymsp[-1].minor.yy444!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  yymsp[-2].minor.yy203 = pRhs;
}
#line 2587 "parse.c"
        break;
      case 78: /* multiselect_op ::= UNION */
      case 80: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==80);
#line 452 "parse.y"
{yymsp[0].minor.yy444 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2593 "parse.c"
        break;
      case 79: /* multiselect_op ::= UNION ALL */
#line 453 "parse.y"
{yymsp[-1].minor.yy444 = TK_ALL;}
#line 2598 "parse.c"
        break;
      case 81: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 457 "parse.y"
{
#ifdef SELECTTRACE_ENABLED
  Token s = yymsp[-8].minor.yy0; /*A-overwrites-S*/
#endif
  yymsp[-8].minor.yy203 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy162,yymsp[-5].minor.yy41,yymsp[-4].minor.yy396,yymsp[-3].minor.yy162,yymsp[-2].minor.yy396,yymsp[-1].minor.yy162,yymsp[-7].minor.yy444,yymsp[0].minor.yy76.pLimit,yymsp[0].minor.yy76.pOffset);
#ifdef SELECTTRACE_ENABLED
  /* Populate the Select.zSelName[] string that is used to help with
  ** query planner debugging, to differentiate between multiple Select
  ** objects in a complex query.
  **
  ** If the SELECT keyword is immediately followed by a C-style comment
  ** then extract the first few alphanumeric characters from within that
  ** comment to be the zSelName value.  Otherwise, the label is #N where
  ** is an integer that is incremented with each SELECT statement seen.
  */
  if( yymsp[-8].minor.yy203!=0 ){
    const char *z = s.z+6;
    int i;
    sqlite3_snprintf(sizeof(yymsp[-8].minor.yy203->zSelName), yymsp[-8].minor.yy203->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(yymsp[-8].minor.yy203->zSelName), yymsp[-8].minor.yy203->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
#line 2632 "parse.c"
        break;
      case 82: /* values ::= VALUES LP nexprlist RP */
#line 491 "parse.y"
{
  yymsp[-3].minor.yy203 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy162,0,0,0,0,0,SF_Values,0,0);
}
#line 2639 "parse.c"
        break;
      case 83: /* values ::= values COMMA LP exprlist RP */
#line 494 "parse.y"
{
  Select *pRight, *pLeft = yymsp[-4].minor.yy203;
  pRight = sqlite3SelectNew(pParse,yymsp[-1].minor.yy162,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    yymsp[-4].minor.yy203 = pRight;
  }else{
    yymsp[-4].minor.yy203 = pLeft;
  }
}
#line 2655 "parse.c"
        break;
      case 84: /* distinct ::= DISTINCT */
#line 511 "parse.y"
{yymsp[0].minor.yy444 = SF_Distinct;}
#line 2660 "parse.c"
        break;
      case 85: /* distinct ::= ALL */
#line 512 "parse.y"
{yymsp[0].minor.yy444 = SF_All;}
#line 2665 "parse.c"
        break;
      case 87: /* sclp ::= */
      case 113: /* orderby_opt ::= */ yytestcase(yyruleno==113);
      case 120: /* groupby_opt ::= */ yytestcase(yyruleno==120);
      case 196: /* exprlist ::= */ yytestcase(yyruleno==196);
      case 199: /* paren_exprlist ::= */ yytestcase(yyruleno==199);
      case 204: /* eidlist_opt ::= */ yytestcase(yyruleno==204);
#line 525 "parse.y"
{yymsp[1].minor.yy162 = 0;}
#line 2675 "parse.c"
        break;
      case 88: /* selcollist ::= sclp expr as */
#line 526 "parse.y"
{
   yymsp[-2].minor.yy162 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy162, yymsp[-1].minor.yy266.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy162, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy162,&yymsp[-1].minor.yy266);
}
#line 2684 "parse.c"
        break;
      case 89: /* selcollist ::= sclp STAR */
#line 531 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy162 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy162, p);
}
#line 2692 "parse.c"
        break;
      case 90: /* selcollist ::= sclp nm DOT STAR */
#line 535 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy162 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy162, pDot);
}
#line 2702 "parse.c"
        break;
      case 91: /* as ::= AS nm */
      case 218: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==218);
      case 219: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==219);
#line 546 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2709 "parse.c"
        break;
      case 93: /* from ::= */
#line 560 "parse.y"
{yymsp[1].minor.yy41 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy41));}
#line 2714 "parse.c"
        break;
      case 94: /* from ::= FROM seltablist */
#line 561 "parse.y"
{
  yymsp[-1].minor.yy41 = yymsp[0].minor.yy41;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy41);
}
#line 2722 "parse.c"
        break;
      case 95: /* stl_prefix ::= seltablist joinop */
#line 569 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy41 && yymsp[-1].minor.yy41->nSrc>0) ) yymsp[-1].minor.yy41->a[yymsp[-1].minor.yy41->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy444;
}
#line 2729 "parse.c"
        break;
      case 96: /* stl_prefix ::= */
#line 572 "parse.y"
{yymsp[1].minor.yy41 = 0;}
#line 2734 "parse.c"
        break;
      case 97: /* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 574 "parse.y"
{
  yymsp[-5].minor.yy41 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-5].minor.yy41,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy396,yymsp[0].minor.yy306);
  sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy41, &yymsp[-2].minor.yy0);
}
#line 2742 "parse.c"
        break;
      case 98: /* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 579 "parse.y"
{
  yymsp[-7].minor.yy41 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-7].minor.yy41,&yymsp[-6].minor.yy0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy396,yymsp[0].minor.yy306);
  sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy41, yymsp[-4].minor.yy162);
}
#line 2750 "parse.c"
        break;
      case 99: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 585 "parse.y"
{
    yymsp[-6].minor.yy41 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy41,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy203,yymsp[-1].minor.yy396,yymsp[0].minor.yy306);
  }
#line 2757 "parse.c"
        break;
      case 100: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 589 "parse.y"
{
    if( yymsp[-6].minor.yy41==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy396==0 && yymsp[0].minor.yy306==0 ){
      yymsp[-6].minor.yy41 = yymsp[-4].minor.yy41;
    }else if( yymsp[-4].minor.yy41->nSrc==1 ){
      yymsp[-6].minor.yy41 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy41,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy396,yymsp[0].minor.yy306);
      if( yymsp[-6].minor.yy41 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy41->a[yymsp[-6].minor.yy41->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy41->a;
        pNew->zName = pOld->zName;
        pNew->pSelect = pOld->pSelect;
        pOld->zName =  0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy41);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy41);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy41,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy41 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy41,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy396,yymsp[0].minor.yy306);
    }
  }
#line 2782 "parse.c"
        break;
      case 101: /* fullname ::= nm */
#line 615 "parse.y"
{yymsp[0].minor.yy41 = sqlite3SrcListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 2787 "parse.c"
        break;
      case 102: /* joinop ::= COMMA|JOIN */
#line 621 "parse.y"
{ yymsp[0].minor.yy444 = JT_INNER; }
#line 2792 "parse.c"
        break;
      case 103: /* joinop ::= JOIN_KW JOIN */
#line 623 "parse.y"
{yymsp[-1].minor.yy444 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2797 "parse.c"
        break;
      case 104: /* joinop ::= JOIN_KW join_nm JOIN */
#line 625 "parse.y"
{yymsp[-2].minor.yy444 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2802 "parse.c"
        break;
      case 105: /* joinop ::= JOIN_KW join_nm join_nm JOIN */
#line 627 "parse.y"
{yymsp[-3].minor.yy444 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2807 "parse.c"
        break;
      case 106: /* on_opt ::= ON expr */
      case 123: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==123);
      case 130: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==130);
      case 192: /* case_else ::= ELSE expr */ yytestcase(yyruleno==192);
#line 631 "parse.y"
{yymsp[-1].minor.yy396 = yymsp[0].minor.yy266.pExpr;}
#line 2815 "parse.c"
        break;
      case 107: /* on_opt ::= */
      case 122: /* having_opt ::= */ yytestcase(yyruleno==122);
      case 129: /* where_opt ::= */ yytestcase(yyruleno==129);
      case 193: /* case_else ::= */ yytestcase(yyruleno==193);
      case 195: /* case_operand ::= */ yytestcase(yyruleno==195);
#line 632 "parse.y"
{yymsp[1].minor.yy396 = 0;}
#line 2824 "parse.c"
        break;
      case 108: /* indexed_opt ::= */
#line 645 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2829 "parse.c"
        break;
      case 109: /* indexed_opt ::= INDEXED BY nm */
#line 646 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2834 "parse.c"
        break;
      case 110: /* indexed_opt ::= NOT INDEXED */
#line 647 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2839 "parse.c"
        break;
      case 111: /* using_opt ::= USING LP idlist RP */
#line 651 "parse.y"
{yymsp[-3].minor.yy306 = yymsp[-1].minor.yy306;}
#line 2844 "parse.c"
        break;
      case 112: /* using_opt ::= */
      case 140: /* idlist_opt ::= */ yytestcase(yyruleno==140);
#line 652 "parse.y"
{yymsp[1].minor.yy306 = 0;}
#line 2850 "parse.c"
        break;
      case 114: /* orderby_opt ::= ORDER BY sortlist */
      case 121: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==121);
#line 666 "parse.y"
{yymsp[-2].minor.yy162 = yymsp[0].minor.yy162;}
#line 2856 "parse.c"
        break;
      case 115: /* sortlist ::= sortlist COMMA expr sortorder */
#line 667 "parse.y"
{
  yymsp[-3].minor.yy162 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy162,yymsp[-1].minor.yy266.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy162,yymsp[0].minor.yy444);
}
#line 2864 "parse.c"
        break;
      case 116: /* sortlist ::= expr sortorder */
#line 671 "parse.y"
{
  yymsp[-1].minor.yy162 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy266.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy162,yymsp[0].minor.yy444);
}
#line 2872 "parse.c"
        break;
      case 117: /* sortorder ::= ASC */
#line 678 "parse.y"
{yymsp[0].minor.yy444 = SQLITE_SO_ASC;}
#line 2877 "parse.c"
        break;
      case 118: /* sortorder ::= DESC */
#line 679 "parse.y"
{yymsp[0].minor.yy444 = SQLITE_SO_DESC;}
#line 2882 "parse.c"
        break;
      case 119: /* sortorder ::= */
#line 680 "parse.y"
{yymsp[1].minor.yy444 = SQLITE_SO_UNDEFINED;}
#line 2887 "parse.c"
        break;
      case 124: /* limit_opt ::= */
#line 705 "parse.y"
{yymsp[1].minor.yy76.pLimit = 0; yymsp[1].minor.yy76.pOffset = 0;}
#line 2892 "parse.c"
        break;
      case 125: /* limit_opt ::= LIMIT expr */
#line 706 "parse.y"
{yymsp[-1].minor.yy76.pLimit = yymsp[0].minor.yy266.pExpr; yymsp[-1].minor.yy76.pOffset = 0;}
#line 2897 "parse.c"
        break;
      case 126: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 708 "parse.y"
{yymsp[-3].minor.yy76.pLimit = yymsp[-2].minor.yy266.pExpr; yymsp[-3].minor.yy76.pOffset = yymsp[0].minor.yy266.pExpr;}
#line 2902 "parse.c"
        break;
      case 127: /* limit_opt ::= LIMIT expr COMMA expr */
#line 710 "parse.y"
{yymsp[-3].minor.yy76.pOffset = yymsp[-2].minor.yy266.pExpr; yymsp[-3].minor.yy76.pLimit = yymsp[0].minor.yy266.pExpr;}
#line 2907 "parse.c"
        break;
      case 128: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 727 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy273, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy41, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy41,yymsp[0].minor.yy396);
}
#line 2919 "parse.c"
        break;
      case 131: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 760 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy273, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy41, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy162,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Update(pParse,yymsp[-4].minor.yy41,yymsp[-1].minor.yy162,yymsp[0].minor.yy396,yymsp[-5].minor.yy444);
}
#line 2932 "parse.c"
        break;
      case 132: /* setlist ::= setlist COMMA nm EQ expr */
#line 774 "parse.y"
{
  yymsp[-4].minor.yy162 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy162, yymsp[0].minor.yy266.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy162, &yymsp[-2].minor.yy0, 1);
}
#line 2940 "parse.c"
        break;
      case 133: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 778 "parse.y"
{
  yymsp[-6].minor.yy162 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy162, yymsp[-3].minor.yy306, yymsp[0].minor.yy266.pExpr);
}
#line 2947 "parse.c"
        break;
      case 134: /* setlist ::= nm EQ expr */
#line 781 "parse.y"
{
  yylhsminor.yy162 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy266.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy162, &yymsp[-2].minor.yy0, 1);
}
#line 2955 "parse.c"
  yymsp[-2].minor.yy162 = yylhsminor.yy162;
        break;
      case 135: /* setlist ::= LP idlist RP EQ expr */
#line 785 "parse.y"
{
  yymsp[-4].minor.yy162 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy306, yymsp[0].minor.yy266.pExpr);
}
#line 2963 "parse.c"
        break;
      case 136: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 791 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy273, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-2].minor.yy41, yymsp[0].minor.yy203, yymsp[-1].minor.yy306, yymsp[-4].minor.yy444);
}
#line 2974 "parse.c"
        break;
      case 137: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 799 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy273, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-3].minor.yy41, 0, yymsp[-2].minor.yy306, yymsp[-5].minor.yy444);
}
#line 2985 "parse.c"
        break;
      case 141: /* idlist_opt ::= LP idlist RP */
#line 817 "parse.y"
{yymsp[-2].minor.yy306 = yymsp[-1].minor.yy306;}
#line 2990 "parse.c"
        break;
      case 142: /* idlist ::= idlist COMMA nm */
#line 819 "parse.y"
{yymsp[-2].minor.yy306 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy306,&yymsp[0].minor.yy0);}
#line 2995 "parse.c"
        break;
      case 143: /* idlist ::= nm */
#line 821 "parse.y"
{yymsp[0].minor.yy306 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3000 "parse.c"
        break;
      case 144: /* expr ::= LP expr RP */
#line 870 "parse.y"
{spanSet(&yymsp[-2].minor.yy266,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy266.pExpr = yymsp[-1].minor.yy266.pExpr;}
#line 3005 "parse.c"
        break;
      case 145: /* term ::= NULL */
      case 149: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==149);
      case 150: /* term ::= STRING */ yytestcase(yyruleno==150);
#line 871 "parse.y"
{spanExpr(&yymsp[0].minor.yy266,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3012 "parse.c"
        break;
      case 146: /* expr ::= ID|INDEXED */
      case 147: /* expr ::= JOIN_KW */ yytestcase(yyruleno==147);
#line 872 "parse.y"
{spanExpr(&yymsp[0].minor.yy266,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3018 "parse.c"
        break;
      case 148: /* expr ::= nm DOT nm */
#line 874 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy266,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3028 "parse.c"
        break;
      case 151: /* term ::= INTEGER */
#line 882 "parse.y"
{
  yylhsminor.yy266.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy266.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy266.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy266.pExpr ) yylhsminor.yy266.pExpr->flags |= EP_Leaf;
}
#line 3038 "parse.c"
  yymsp[0].minor.yy266 = yylhsminor.yy266;
        break;
      case 152: /* expr ::= VARIABLE */
#line 888 "parse.y"
{
  if( !(yymsp[0].minor.yy0.z[0]=='#' && sqlite3Isdigit(yymsp[0].minor.yy0.z[1])) ){
    u32 n = yymsp[0].minor.yy0.n;
    spanExpr(&yymsp[0].minor.yy266, pParse, TK_VARIABLE, yymsp[0].minor.yy0);
    sqlite3ExprAssignVarNumber(pParse, yymsp[0].minor.yy266.pExpr, n);
  }else{
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    Token t = yymsp[0].minor.yy0; /*A-overwrites-X*/
    assert( t.n>=2 );
    spanSet(&yymsp[0].minor.yy266, &t, &t);
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &t);
      yymsp[0].minor.yy266.pExpr = 0;
    }else{
      yymsp[0].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0);
      if( yymsp[0].minor.yy266.pExpr ) sqlite3GetInt32(&t.z[1], &yymsp[0].minor.yy266.pExpr->iTable);
    }
  }
}
#line 3064 "parse.c"
        break;
      case 153: /* expr ::= expr COLLATE ID|INDEXED */
#line 909 "parse.y"
{
  yymsp[-2].minor.yy266.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy266.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy266.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3072 "parse.c"
        break;
      case 154: /* expr ::= CAST LP expr AS typetoken RP */
#line 914 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy266,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy266.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy266.pExpr, yymsp[-3].minor.yy266.pExpr, 0);
}
#line 3081 "parse.c"
        break;
      case 155: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 920 "parse.y"
{
  if( yymsp[-1].minor.yy162 && yymsp[-1].minor.yy162->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yylhsminor.yy266.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy162, &yymsp[-4].minor.yy0);
  spanSet(&yylhsminor.yy266,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy444==SF_Distinct && yylhsminor.yy266.pExpr ){
    yylhsminor.yy266.pExpr->flags |= EP_Distinct;
  }
}
#line 3095 "parse.c"
  yymsp[-4].minor.yy266 = yylhsminor.yy266;
        break;
      case 156: /* expr ::= ID|INDEXED LP STAR RP */
#line 930 "parse.y"
{
  yylhsminor.yy266.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy266,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3104 "parse.c"
  yymsp[-3].minor.yy266 = yylhsminor.yy266;
        break;
      case 157: /* term ::= CTIME_KW */
#line 934 "parse.y"
{
  yylhsminor.yy266.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy266, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3113 "parse.c"
  yymsp[0].minor.yy266 = yylhsminor.yy266;
        break;
      case 158: /* expr ::= LP nexprlist COMMA expr RP */
#line 963 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy162, yymsp[-1].minor.yy266.pExpr);
  yylhsminor.yy266.pExpr = sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
  if( yylhsminor.yy266.pExpr ){
    yylhsminor.yy266.pExpr->x.pList = pList;
    spanSet(&yylhsminor.yy266, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  }
}
#line 3128 "parse.c"
  yymsp[-4].minor.yy266 = yylhsminor.yy266;
        break;
      case 159: /* expr ::= expr AND expr */
      case 160: /* expr ::= expr OR expr */ yytestcase(yyruleno==160);
      case 161: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==161);
      case 162: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==162);
      case 163: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==163);
      case 164: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==164);
      case 165: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==165);
      case 166: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==166);
#line 974 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy266,&yymsp[0].minor.yy266);}
#line 3141 "parse.c"
        break;
      case 167: /* likeop ::= LIKE_KW|MATCH */
#line 987 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3146 "parse.c"
        break;
      case 168: /* likeop ::= NOT LIKE_KW|MATCH */
#line 988 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3151 "parse.c"
        break;
      case 169: /* expr ::= expr likeop expr */
#line 989 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
  yymsp[-1].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy266.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy266.pExpr);
  yymsp[-2].minor.yy266.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-2].minor.yy266);
  yymsp[-2].minor.yy266.zEnd = yymsp[0].minor.yy266.zEnd;
  if( yymsp[-2].minor.yy266.pExpr ) yymsp[-2].minor.yy266.pExpr->flags |= EP_InfixFunc;
}
#line 3166 "parse.c"
        break;
      case 170: /* expr ::= expr likeop expr ESCAPE expr */
#line 1000 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
  yymsp[-3].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy266.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy266.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy266.pExpr);
  yymsp[-4].minor.yy266.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-4].minor.yy266);
  yymsp[-4].minor.yy266.zEnd = yymsp[0].minor.yy266.zEnd;
  if( yymsp[-4].minor.yy266.pExpr ) yymsp[-4].minor.yy266.pExpr->flags |= EP_InfixFunc;
}
#line 3182 "parse.c"
        break;
      case 171: /* expr ::= expr ISNULL|NOTNULL */
#line 1027 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy266,&yymsp[0].minor.yy0);}
#line 3187 "parse.c"
        break;
      case 172: /* expr ::= expr NOT NULL */
#line 1028 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy266,&yymsp[0].minor.yy0);}
#line 3192 "parse.c"
        break;
      case 173: /* expr ::= expr IS expr */
#line 1049 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy266,&yymsp[0].minor.yy266);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy266.pExpr, yymsp[-2].minor.yy266.pExpr, TK_ISNULL);
}
#line 3200 "parse.c"
        break;
      case 174: /* expr ::= expr IS NOT expr */
#line 1053 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy266,&yymsp[0].minor.yy266);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy266.pExpr, yymsp[-3].minor.yy266.pExpr, TK_NOTNULL);
}
#line 3208 "parse.c"
        break;
      case 175: /* expr ::= NOT expr */
      case 176: /* expr ::= BITNOT expr */ yytestcase(yyruleno==176);
#line 1077 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy266,pParse,yymsp[-1].major,&yymsp[0].minor.yy266,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3214 "parse.c"
        break;
      case 177: /* expr ::= MINUS expr */
#line 1081 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy266,pParse,TK_UMINUS,&yymsp[0].minor.yy266,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3219 "parse.c"
        break;
      case 178: /* expr ::= PLUS expr */
#line 1083 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy266,pParse,TK_UPLUS,&yymsp[0].minor.yy266,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3224 "parse.c"
        break;
      case 179: /* between_op ::= BETWEEN */
      case 182: /* in_op ::= IN */ yytestcase(yyruleno==182);
#line 1086 "parse.y"
{yymsp[0].minor.yy444 = 0;}
#line 3230 "parse.c"
        break;
      case 181: /* expr ::= expr between_op expr AND expr */
#line 1088 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy266.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy266.pExpr);
  yymsp[-4].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy266.pExpr, 0);
  if( yymsp[-4].minor.yy266.pExpr ){
    yymsp[-4].minor.yy266.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, yymsp[-3].minor.yy444, &yymsp[-4].minor.yy266);
  yymsp[-4].minor.yy266.zEnd = yymsp[0].minor.yy266.zEnd;
}
#line 3246 "parse.c"
        break;
      case 184: /* expr ::= expr in_op LP exprlist RP */
#line 1104 "parse.y"
{
    if( yymsp[-1].minor.yy162==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      sqlite3ExprDelete(pParse->db, yymsp[-4].minor.yy266.pExpr);
      yymsp[-4].minor.yy266.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER,&sqlite3IntTokens[yymsp[-3].minor.yy444],1);
    }else if( yymsp[-1].minor.yy162->nExpr==1 ){
      /* Expressions of the form:
      **
      **      expr1 IN (?1)
      **      expr1 NOT IN (?2)
      **
      ** with exactly one value on the RHS can be simplified to something
      ** like this:
      **
      **      expr1 == ?1
      **      expr1 <> ?2
      **
      ** But, the RHS of the == or <> is marked with the EP_Generic flag
      ** so that it may not contribute to the computation of comparison
      ** affinity or the collating sequence to use for comparison.  Otherwise,
      ** the semantics would be subtly different from IN or NOT IN.
      */
      Expr *pRHS = yymsp[-1].minor.yy162->a[0].pExpr;
      yymsp[-1].minor.yy162->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy162);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      yymsp[-4].minor.yy266.pExpr = sqlite3PExpr(pParse, yymsp[-3].minor.yy444 ? TK_NE : TK_EQ, yymsp[-4].minor.yy266.pExpr, pRHS);
    }else{
      yymsp[-4].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy266.pExpr, 0);
      if( yymsp[-4].minor.yy266.pExpr ){
        yymsp[-4].minor.yy266.pExpr->x.pList = yymsp[-1].minor.yy162;
        sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy266.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy162);
      }
      exprNot(pParse, yymsp[-3].minor.yy444, &yymsp[-4].minor.yy266);
    }
    yymsp[-4].minor.yy266.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3301 "parse.c"
        break;
      case 185: /* expr ::= LP select RP */
#line 1155 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy266,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy266.pExpr, yymsp[-1].minor.yy203);
  }
#line 3310 "parse.c"
        break;
      case 186: /* expr ::= expr in_op LP select RP */
#line 1160 "parse.y"
{
    yymsp[-4].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy266.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy266.pExpr, yymsp[-1].minor.yy203);
    exprNot(pParse, yymsp[-3].minor.yy444, &yymsp[-4].minor.yy266);
    yymsp[-4].minor.yy266.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3320 "parse.c"
        break;
      case 187: /* expr ::= expr in_op nm paren_exprlist */
#line 1166 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy162 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy162);
    yymsp[-3].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy266.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-3].minor.yy266.pExpr, pSelect);
    exprNot(pParse, yymsp[-2].minor.yy444, &yymsp[-3].minor.yy266);
    yymsp[-3].minor.yy266.zEnd = &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
#line 3333 "parse.c"
        break;
      case 188: /* expr ::= EXISTS LP select RP */
#line 1175 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy266,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy203);
  }
#line 3343 "parse.c"
        break;
      case 189: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1184 "parse.y"
{
  spanSet(&yymsp[-4].minor.yy266,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-C*/
  yymsp[-4].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy396, 0);
  if( yymsp[-4].minor.yy266.pExpr ){
    yymsp[-4].minor.yy266.pExpr->x.pList = yymsp[-1].minor.yy396 ? sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy162,yymsp[-1].minor.yy396) : yymsp[-2].minor.yy162;
    sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy266.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy162);
    sqlite3ExprDelete(pParse->db, yymsp[-1].minor.yy396);
  }
}
#line 3358 "parse.c"
        break;
      case 190: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1197 "parse.y"
{
  yymsp[-4].minor.yy162 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy162, yymsp[-2].minor.yy266.pExpr);
  yymsp[-4].minor.yy162 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy162, yymsp[0].minor.yy266.pExpr);
}
#line 3366 "parse.c"
        break;
      case 191: /* case_exprlist ::= WHEN expr THEN expr */
#line 1201 "parse.y"
{
  yymsp[-3].minor.yy162 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy266.pExpr);
  yymsp[-3].minor.yy162 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy162, yymsp[0].minor.yy266.pExpr);
}
#line 3374 "parse.c"
        break;
      case 194: /* case_operand ::= expr */
#line 1211 "parse.y"
{yymsp[0].minor.yy396 = yymsp[0].minor.yy266.pExpr; /*A-overwrites-X*/}
#line 3379 "parse.c"
        break;
      case 197: /* nexprlist ::= nexprlist COMMA expr */
#line 1222 "parse.y"
{yymsp[-2].minor.yy162 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy162,yymsp[0].minor.yy266.pExpr);}
#line 3384 "parse.c"
        break;
      case 198: /* nexprlist ::= expr */
#line 1224 "parse.y"
{yymsp[0].minor.yy162 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy266.pExpr); /*A-overwrites-Y*/}
#line 3389 "parse.c"
        break;
      case 200: /* paren_exprlist ::= LP exprlist RP */
      case 205: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==205);
#line 1232 "parse.y"
{yymsp[-2].minor.yy162 = yymsp[-1].minor.yy162;}
#line 3395 "parse.c"
        break;
      case 201: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1239 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0), yymsp[-2].minor.yy162, yymsp[-9].minor.yy444,
                      &yymsp[-10].minor.yy0, yymsp[0].minor.yy396, SQLITE_SO_ASC, yymsp[-7].minor.yy444, SQLITE_IDXTYPE_APPDEF);
}
#line 3404 "parse.c"
        break;
      case 202: /* uniqueflag ::= UNIQUE */
      case 243: /* raisetype ::= ABORT */ yytestcase(yyruleno==243);
#line 1246 "parse.y"
{yymsp[0].minor.yy444 = ON_CONFLICT_ACTION_ABORT;}
#line 3410 "parse.c"
        break;
      case 203: /* uniqueflag ::= */
#line 1247 "parse.y"
{yymsp[1].minor.yy444 = ON_CONFLICT_ACTION_NONE;}
#line 3415 "parse.c"
        break;
      case 206: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1290 "parse.y"
{
  yymsp[-4].minor.yy162 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy162, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy444, yymsp[0].minor.yy444);
}
#line 3422 "parse.c"
        break;
      case 207: /* eidlist ::= nm collate sortorder */
#line 1293 "parse.y"
{
  yymsp[-2].minor.yy162 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy444, yymsp[0].minor.yy444); /*A-overwrites-Y*/
}
#line 3429 "parse.c"
        break;
      case 210: /* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1304 "parse.y"
{
    sqlite3DropIndex(pParse, yymsp[-2].minor.yy41, &yymsp[0].minor.yy0, yymsp[-3].minor.yy444);
}
#line 3436 "parse.c"
        break;
      case 211: /* cmd ::= PRAGMA nm */
#line 1311 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[0].minor.yy0,0,0,0,0);
}
#line 3443 "parse.c"
        break;
      case 212: /* cmd ::= PRAGMA nm EQ nmnum */
#line 1314 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,0);
}
#line 3450 "parse.c"
        break;
      case 213: /* cmd ::= PRAGMA nm LP nmnum RP */
#line 1317 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,0);
}
#line 3457 "parse.c"
        break;
      case 214: /* cmd ::= PRAGMA nm EQ minus_num */
#line 1320 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,1);
}
#line 3464 "parse.c"
        break;
      case 215: /* cmd ::= PRAGMA nm LP minus_num RP */
#line 1323 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,1);
}
#line 3471 "parse.c"
        break;
      case 216: /* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1326 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,0,&yymsp[0].minor.yy0,&yymsp[-2].minor.yy0,0);
}
#line 3478 "parse.c"
        break;
      case 217: /* cmd ::= PRAGMA */
#line 1329 "parse.y"
{
    sqlite3Pragma(pParse, 0,0,0,0,0);
}
#line 3485 "parse.c"
        break;
      case 220: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1349 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy451, &all);
}
#line 3495 "parse.c"
        break;
      case 221: /* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1358 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy444, yymsp[-4].minor.yy184.a, yymsp[-4].minor.yy184.b, yymsp[-2].minor.yy41, yymsp[0].minor.yy396, yymsp[-7].minor.yy444);
  yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0; /*yymsp[-8].minor.yy0-overwrites-T*/
}
#line 3503 "parse.c"
        break;
      case 222: /* trigger_time ::= BEFORE */
#line 1364 "parse.y"
{ yymsp[0].minor.yy444 = TK_BEFORE; }
#line 3508 "parse.c"
        break;
      case 223: /* trigger_time ::= AFTER */
#line 1365 "parse.y"
{ yymsp[0].minor.yy444 = TK_AFTER;  }
#line 3513 "parse.c"
        break;
      case 224: /* trigger_time ::= INSTEAD OF */
#line 1366 "parse.y"
{ yymsp[-1].minor.yy444 = TK_INSTEAD;}
#line 3518 "parse.c"
        break;
      case 225: /* trigger_time ::= */
#line 1367 "parse.y"
{ yymsp[1].minor.yy444 = TK_BEFORE; }
#line 3523 "parse.c"
        break;
      case 226: /* trigger_event ::= DELETE|INSERT */
      case 227: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==227);
#line 1371 "parse.y"
{yymsp[0].minor.yy184.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy184.b = 0;}
#line 3529 "parse.c"
        break;
      case 228: /* trigger_event ::= UPDATE OF idlist */
#line 1373 "parse.y"
{yymsp[-2].minor.yy184.a = TK_UPDATE; yymsp[-2].minor.yy184.b = yymsp[0].minor.yy306;}
#line 3534 "parse.c"
        break;
      case 229: /* when_clause ::= */
#line 1380 "parse.y"
{ yymsp[1].minor.yy396 = 0; }
#line 3539 "parse.c"
        break;
      case 230: /* when_clause ::= WHEN expr */
#line 1381 "parse.y"
{ yymsp[-1].minor.yy396 = yymsp[0].minor.yy266.pExpr; }
#line 3544 "parse.c"
        break;
      case 231: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1385 "parse.y"
{
  assert( yymsp[-2].minor.yy451!=0 );
  yymsp[-2].minor.yy451->pLast->pNext = yymsp[-1].minor.yy451;
  yymsp[-2].minor.yy451->pLast = yymsp[-1].minor.yy451;
}
#line 3553 "parse.c"
        break;
      case 232: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1390 "parse.y"
{ 
  assert( yymsp[-1].minor.yy451!=0 );
  yymsp[-1].minor.yy451->pLast = yymsp[-1].minor.yy451;
}
#line 3561 "parse.c"
        break;
      case 233: /* trnm ::= nm DOT nm */
#line 1401 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3571 "parse.c"
        break;
      case 234: /* tridxby ::= INDEXED BY nm */
#line 1413 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3580 "parse.c"
        break;
      case 235: /* tridxby ::= NOT INDEXED */
#line 1418 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3589 "parse.c"
        break;
      case 236: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1431 "parse.y"
{yymsp[-6].minor.yy451 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy162, yymsp[0].minor.yy396, yymsp[-5].minor.yy444);}
#line 3594 "parse.c"
        break;
      case 237: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1435 "parse.y"
{yymsp[-4].minor.yy451 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy306, yymsp[0].minor.yy203, yymsp[-4].minor.yy444);/*A-overwrites-R*/}
#line 3599 "parse.c"
        break;
      case 238: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1439 "parse.y"
{yymsp[-4].minor.yy451 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy396);}
#line 3604 "parse.c"
        break;
      case 239: /* trigger_cmd ::= select */
#line 1443 "parse.y"
{yymsp[0].minor.yy451 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy203); /*A-overwrites-X*/}
#line 3609 "parse.c"
        break;
      case 240: /* expr ::= RAISE LP IGNORE RP */
#line 1446 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy266,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy266.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy266.pExpr ){
    yymsp[-3].minor.yy266.pExpr->affinity = ON_CONFLICT_ACTION_IGNORE;
  }
}
#line 3620 "parse.c"
        break;
      case 241: /* expr ::= RAISE LP raisetype COMMA STRING RP */
#line 1453 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy266,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy266.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy266.pExpr ) {
    yymsp[-5].minor.yy266.pExpr->affinity = (char)yymsp[-3].minor.yy444;
  }
}
#line 3631 "parse.c"
        break;
      case 242: /* raisetype ::= ROLLBACK */
#line 1463 "parse.y"
{yymsp[0].minor.yy444 = ON_CONFLICT_ACTION_ROLLBACK;}
#line 3636 "parse.c"
        break;
      case 244: /* raisetype ::= FAIL */
#line 1465 "parse.y"
{yymsp[0].minor.yy444 = ON_CONFLICT_ACTION_FAIL;}
#line 3641 "parse.c"
        break;
      case 245: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1470 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy41,yymsp[-1].minor.yy444);
}
#line 3648 "parse.c"
        break;
      case 246: /* cmd ::= ANALYZE */
#line 1485 "parse.y"
{sqlite3Analyze(pParse, 0);}
#line 3653 "parse.c"
        break;
      case 247: /* cmd ::= ANALYZE nm */
#line 1486 "parse.y"
{sqlite3Analyze(pParse, &yymsp[0].minor.yy0);}
#line 3658 "parse.c"
        break;
      case 248: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1491 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy41,&yymsp[0].minor.yy0);
}
#line 3665 "parse.c"
        break;
      case 249: /* with ::= */
#line 1514 "parse.y"
{yymsp[1].minor.yy273 = 0;}
#line 3670 "parse.c"
        break;
      case 250: /* with ::= WITH wqlist */
#line 1516 "parse.y"
{ yymsp[-1].minor.yy273 = yymsp[0].minor.yy273; }
#line 3675 "parse.c"
        break;
      case 251: /* with ::= WITH RECURSIVE wqlist */
#line 1517 "parse.y"
{ yymsp[-2].minor.yy273 = yymsp[0].minor.yy273; }
#line 3680 "parse.c"
        break;
      case 252: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1519 "parse.y"
{
  yymsp[-5].minor.yy273 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy162, yymsp[-1].minor.yy203); /*A-overwrites-X*/
}
#line 3687 "parse.c"
        break;
      case 253: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1522 "parse.y"
{
  yymsp[-7].minor.yy273 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy273, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy162, yymsp[-1].minor.yy203);
}
#line 3694 "parse.c"
        break;
      default:
      /* (254) input ::= ecmd */ yytestcase(yyruleno==254);
      /* (255) explain ::= */ yytestcase(yyruleno==255);
      /* (256) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno!=256);
      /* (257) trans_opt ::= */ yytestcase(yyruleno==257);
      /* (258) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==258);
      /* (259) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==259);
      /* (260) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==260);
      /* (261) savepoint_opt ::= */ yytestcase(yyruleno==261);
      /* (262) cmd ::= create_table create_table_args */ yytestcase(yyruleno==262);
      /* (263) columnlist ::= columnlist COMMA columnname carglist */ yytestcase(yyruleno==263);
      /* (264) columnlist ::= columnname carglist */ yytestcase(yyruleno==264);
      /* (265) typetoken ::= typename */ yytestcase(yyruleno==265);
      /* (266) typename ::= ID|STRING */ yytestcase(yyruleno==266);
      /* (267) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=267);
      /* (268) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=268);
      /* (269) carglist ::= carglist ccons */ yytestcase(yyruleno==269);
      /* (270) carglist ::= */ yytestcase(yyruleno==270);
      /* (271) ccons ::= NULL onconf */ yytestcase(yyruleno==271);
      /* (272) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==272);
      /* (273) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==273);
      /* (274) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=274);
      /* (275) tconscomma ::= */ yytestcase(yyruleno==275);
      /* (276) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=276);
      /* (277) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=277);
      /* (278) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=278);
      /* (279) oneselect ::= values */ yytestcase(yyruleno==279);
      /* (280) sclp ::= selcollist COMMA */ yytestcase(yyruleno==280);
      /* (281) as ::= ID|STRING */ yytestcase(yyruleno==281);
      /* (282) join_nm ::= ID|INDEXED */ yytestcase(yyruleno==282);
      /* (283) join_nm ::= JOIN_KW */ yytestcase(yyruleno==283);
      /* (284) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=284);
      /* (285) exprlist ::= nexprlist */ yytestcase(yyruleno==285);
      /* (286) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=286);
      /* (287) nmnum ::= STRING */ yytestcase(yyruleno==287);
      /* (288) nmnum ::= nm */ yytestcase(yyruleno==288);
      /* (289) nmnum ::= ON */ yytestcase(yyruleno==289);
      /* (290) nmnum ::= DELETE */ yytestcase(yyruleno==290);
      /* (291) nmnum ::= DEFAULT */ yytestcase(yyruleno==291);
      /* (292) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno==292);
      /* (293) foreach_clause ::= */ yytestcase(yyruleno==293);
      /* (294) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==294);
      /* (295) trnm ::= nm */ yytestcase(yyruleno==295);
      /* (296) tridxby ::= */ yytestcase(yyruleno==296);
        break;
/********** End reduce actions ************************************************/
  };
  assert( yyruleno<sizeof(yyRuleInfo)/sizeof(yyRuleInfo[0]) );
  yygoto = yyRuleInfo[yyruleno].lhs;
  yysize = yyRuleInfo[yyruleno].nrhs;
  yyact = yy_find_reduce_action(yymsp[-yysize].stateno,(YYCODETYPE)yygoto);
  if( yyact <= YY_MAX_SHIFTREDUCE ){
    if( yyact>YY_MAX_SHIFT ){
      yyact += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
    }
    yymsp -= yysize-1;
    yypParser->yytos = yymsp;
    yymsp->stateno = (YYACTIONTYPE)yyact;
    yymsp->major = (YYCODETYPE)yygoto;
    yyTraceShift(yypParser, yyact);
  }else{
    assert( yyact == YY_ACCEPT_ACTION );
    yypParser->yytos -= yysize;
    yy_accept(yypParser);
  }
}

/*
** The following code executes when the parse fails
*/
#ifndef YYNOERRORRECOVERY
static void yy_parse_failed(
  yyParser *yypParser           /* The parser */
){
  sqlite3ParserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sFail!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yytos>yypParser->yystack ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
/************ Begin %parse_failure code ***************************************/
/************ End %parse_failure code *****************************************/
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}
#endif /* YYNOERRORRECOVERY */

/*
** The following code executes when a syntax error first occurs.
*/
static void yy_syntax_error(
  yyParser *yypParser,           /* The parser */
  int yymajor,                   /* The major type of the error token */
  sqlite3ParserTOKENTYPE yyminor         /* The minor type of the error token */
){
  sqlite3ParserARG_FETCH;
#define TOKEN yyminor
/************ Begin %syntax_error code ****************************************/
#line 32 "parse.y"

  UNUSED_PARAMETER(yymajor);  /* Silence some compiler warnings */
  assert( TOKEN.z[0] );  /* The tokenizer always gives us a token */
  if (yypParser->is_fallback_failed && TOKEN.isReserved) {
    sqlite3ErrorMsg(pParse, "keyword \"%T\" is reserved", &TOKEN);
  } else {
    sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &TOKEN);
  }
#line 3805 "parse.c"
/************ End %syntax_error code ******************************************/
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  sqlite3ParserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sAccept!\n",yyTracePrompt);
  }
#endif
#ifndef YYNOERRORRECOVERY
  yypParser->yyerrcnt = -1;
#endif
  assert( yypParser->yytos==yypParser->yystack );
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
/*********** Begin %parse_accept code *****************************************/
/*********** End %parse_accept code *******************************************/
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "sqlite3ParserAlloc" which describes the current state of the parser.
** The second argument is the major token number.  The third is
** the minor token.  The fourth optional argument is whatever the
** user wants (and specified in the grammar) and is available for
** use by the action routines.
**
** Inputs:
** <ul>
** <li> A pointer to the parser (an opaque structure.)
** <li> The major token number.
** <li> The minor token number.
** <li> An option argument of a grammar-specified type.
** </ul>
**
** Outputs:
** None.
*/
void sqlite3Parser(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  sqlite3ParserTOKENTYPE yyminor       /* The value for the token */
  sqlite3ParserARG_PDECL               /* Optional %extra_argument parameter */
){
  YYMINORTYPE yyminorunion;
  unsigned int yyact;   /* The parser action. */
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  int yyendofinput;     /* True if we are at the end of input */
#endif
#ifdef YYERRORSYMBOL
  int yyerrorhit = 0;   /* True if yymajor has invoked an error */
#endif
  yyParser *yypParser;  /* The parser */

  yypParser = (yyParser*)yyp;
  assert( yypParser->yytos!=0 );
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  yyendofinput = (yymajor==0);
#endif
  sqlite3ParserARG_STORE;

#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sInput '%s'\n",yyTracePrompt,yyTokenName[yymajor]);
  }
#endif

  do{
    yyact = yy_find_shift_action(yypParser,(YYCODETYPE)yymajor);
    if( yyact <= YY_MAX_SHIFTREDUCE ){
      yy_shift(yypParser,yyact,yymajor,yyminor);
#ifndef YYNOERRORRECOVERY
      yypParser->yyerrcnt--;
#endif
      yymajor = YYNOCODE;
    }else if( yyact <= YY_MAX_REDUCE ){
      yy_reduce(yypParser,yyact-YY_MIN_REDUCE);
    }else{
      assert( yyact == YY_ERROR_ACTION );
      yyminorunion.yy0 = yyminor;
#ifdef YYERRORSYMBOL
      int yymx;
#endif
#ifndef NDEBUG
      if( yyTraceFILE ){
        fprintf(yyTraceFILE,"%sSyntax Error!\n",yyTracePrompt);
      }
#endif
#ifdef YYERRORSYMBOL
      /* A syntax error has occurred.
      ** The response to an error depends upon whether or not the
      ** grammar defines an error token "ERROR".  
      **
      ** This is what we do if the grammar does define ERROR:
      **
      **  * Call the %syntax_error function.
      **
      **  * Begin popping the stack until we enter a state where
      **    it is legal to shift the error symbol, then shift
      **    the error symbol.
      **
      **  * Set the error count to three.
      **
      **  * Begin accepting and shifting new tokens.  No new error
      **    processing will occur until three tokens have been
      **    shifted successfully.
      **
      */
      if( yypParser->yyerrcnt<0 ){
        yy_syntax_error(yypParser,yymajor,yyminor);
      }
      yymx = yypParser->yytos->major;
      if( yymx==YYERRORSYMBOL || yyerrorhit ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE,"%sDiscard input token %s\n",
             yyTracePrompt,yyTokenName[yymajor]);
        }
#endif
        yy_destructor(yypParser, (YYCODETYPE)yymajor, &yyminorunion);
        yymajor = YYNOCODE;
      }else{
        while( yypParser->yytos >= yypParser->yystack
            && yymx != YYERRORSYMBOL
            && (yyact = yy_find_reduce_action(
                        yypParser->yytos->stateno,
                        YYERRORSYMBOL)) >= YY_MIN_REDUCE
        ){
          yy_pop_parser_stack(yypParser);
        }
        if( yypParser->yytos < yypParser->yystack || yymajor==0 ){
          yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
          yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
          yypParser->yyerrcnt = -1;
#endif
          yymajor = YYNOCODE;
        }else if( yymx!=YYERRORSYMBOL ){
          yy_shift(yypParser,yyact,YYERRORSYMBOL,yyminor);
        }
      }
      yypParser->yyerrcnt = 3;
      yyerrorhit = 1;
#elif defined(YYNOERRORRECOVERY)
      /* If the YYNOERRORRECOVERY macro is defined, then do not attempt to
      ** do any kind of error recovery.  Instead, simply invoke the syntax
      ** error routine and continue going as if nothing had happened.
      **
      ** Applications can set this macro (for example inside %include) if
      ** they intend to abandon the parse upon the first syntax error seen.
      */
      yy_syntax_error(yypParser,yymajor, yyminor);
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      yymajor = YYNOCODE;
      
#else  /* YYERRORSYMBOL is not defined */
      /* This is what we do if the grammar does not define ERROR:
      **
      **  * Report an error message, and throw away the input token.
      **
      **  * If the input token is $, then fail the parse.
      **
      ** As before, subsequent error messages are suppressed until
      ** three input tokens have been successfully shifted.
      */
      if( yypParser->yyerrcnt<=0 ){
        yy_syntax_error(yypParser,yymajor, yyminor);
      }
      yypParser->yyerrcnt = 3;
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      if( yyendofinput ){
        yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
        yypParser->yyerrcnt = -1;
#endif
      }
      yymajor = YYNOCODE;
#endif
    }
  }while( yymajor!=YYNOCODE && yypParser->yytos>yypParser->yystack );
#ifndef NDEBUG
  if( yyTraceFILE ){
    yyStackEntry *i;
    char cDiv = '[';
    fprintf(yyTraceFILE,"%sReturn. Stack=",yyTracePrompt);
    for(i=&yypParser->yystack[1]; i<=yypParser->yytos; i++){
      fprintf(yyTraceFILE,"%c%s", cDiv, yyTokenName[i->major]);
      cDiv = ' ';
    }
    fprintf(yyTraceFILE,"]\n");
  }
#endif
  return;
}
