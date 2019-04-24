%{

#include "ast.h"
#include "symbols.h"
#include "grammar.tab.h"

%}

INT     [1-9][0-9]*|0
ID      [a-z][[:alnum:]_]*
CTORID  [A-Z][[:alnum:]_]*
WS      [ \t\f\v\r]
SYMS    [-+*/=<=(){}:\[\];_,^|@]

%%

 /* nested comments */
"/*"    {
            int c, next = 1;
            while ((c = input()) != 0) {
                if (c == '\n') {
                    ++yylineno;
                } else if (c == '*') {
                    c = input();
                    if (c == '/') {
                        if (--next == 0)
                            break;
                    } else {
                        unput(c);
                    }
                } else if (c == '/') {
                    if ((c = input()) == '*')
                        ++next;
                    else
                        unput(c);
                } else if (c < 0) {
                    return SL_TOK_ERROR;
                }
            }
        }

 /* line comments */
"//"    {
            int c;
            while ((c = input()) != 0) {
                if (c == '\n') {
                    ++yylineno;
                    break;
                } else if (c < 0) {
                    /* return error? */
                    break;
                }
            }
        }

 /* keywords */
struct  { return SL_TOK_STRUCT; }
fn      { return SL_TOK_FN; }
let     { return SL_TOK_LET; }
new     { return SL_TOK_NEW; }
return  { return SL_TOK_RETURN; }
break   { return SL_TOK_BREAK; }
loop    { return SL_TOK_LOOP; }

 /* builtin types */
{INT}       { yylval.l_int = atoll(yytext); return SL_TOK_INT; }

int         { yylval.l_sym = Atom_new(yytext, yyleng); return SL_TOK_TNAME; }
 /* TODO: other types */
{ID}        { yylval.l_sym = Atom_new(yytext, yyleng); return SL_TOK_IDENT; }
{CTORID}    { yylval.l_sym = Atom_new(yytext, yyleng); return SL_TOK_TNAME; }


 /* puctuation */
"||"    { return SL_TOK_LOR; }
"&&"    { return SL_TOK_LAND; }
"=="    { return SL_TOK_EQ; }
"!="    { return SL_TOK_NEQ; }
"<="    { return SL_TOK_LE; }
">="    { return SL_TOK_GE; }
"<<"    { return SL_TOK_LSH; }
">>"    { return SL_TOK_RSH; }
"->"    { return SL_TOK_SARROW; }
{SYMS}  { return yytext[0]; }

{WS}+   ; /* drop whitespace */
[\n]    { ++yylineno; }

.       { yylval.error_msg = strdup(yytext); return SL_TOK_ERROR; }

%%
