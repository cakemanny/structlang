%{

#include "ast.h"
#include "symbols.h"
#include "grammar.tab.h"

%}

INT     [-]?[1-9][0-9]*|0
ID      [a-zA-Z_][[:alnum:]_]*
WS      [ \t\f\v\r]
SYMS    [-+*/=<>(){}:\[\];.,^|@]

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
if      { return SL_TOK_IF; }
else    { return SL_TOK_ELSE; }

 /* builtin types */
true    { return SL_TOK_TRUE; }
false   { return SL_TOK_FALSE; }
{INT}   { yylval.l_int = atoll(yytext); return SL_TOK_INT; }

{ID}    { yylval.l_sym = Atom_new(yytext, yyleng); return SL_TOK_IDENT; }


 /* puctuation */
"||"    { return (yylval.l_tok = SL_TOK_LOR); }
"&&"    { return (yylval.l_tok = SL_TOK_LAND); }
"=="    { return (yylval.l_tok = SL_TOK_EQ); }
"!="    { return (yylval.l_tok = SL_TOK_NEQ); }
"<="    { return (yylval.l_tok = SL_TOK_LE); }
">="    { return (yylval.l_tok = SL_TOK_GE); }
"<<"    { return (yylval.l_tok = SL_TOK_LSH); }
">>"    { return (yylval.l_tok = SL_TOK_RSH); }
"->"    { return (yylval.l_tok = SL_TOK_SARROW); }
{SYMS}  { return (yylval.l_tok = yytext[0]); }

{WS}+   ; /* drop whitespace */
[\n]    { ++yylineno; }

.       { yylval.error_msg = strdup(yytext); return SL_TOK_ERROR; }

%%
