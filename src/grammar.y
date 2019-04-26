%{
#include <stdio.h> // fopen
#include <string.h> // strcmp
#include <stdlib.h>
#include "ast.h"

extern int yylex();
extern int yylineno;
extern FILE* yyin;
int yywrap();

static void yyerror(const char* msg);

static sl_decl_t* parse_tree_root;

#define YYERROR_VERBOSE

%}

%union {
    sl_decl_t* l_decl;
    sl_expr_t* l_expr;
    sl_type_t* l_type;

/* terminals */
    long long l_int;
    char* l_str;
    sl_sym_t l_sym;
    int l_tok;
    const char* error_msg;
}

%token SL_TOK_STRUCT
%token SL_TOK_FN
%token SL_TOK_LET
%token SL_TOK_NEW
%token SL_TOK_RETURN
%token SL_TOK_BREAK
%token SL_TOK_LOOP

%token <l_tok> '+' '-' '*' '/'
%token SL_TOK_SARROW

%token <l_int> SL_TOK_INT
%token <l_sym> SL_TOK_IDENT
%token <error> SL_TOK_ERROR

%left SL_TOK_RETURN
%left ','
%right '='
%nonassoc ':'
%left SL_TOK_LOR
%left SL_TOK_LAND
%left '|'
%left '^'
%left '&'
%left SL_TOK_EQ SL_TOK_NEQ
%nonassoc '<' SL_TOK_LE '>' SL_TOK_GE
%left SL_TOK_LSH SL_TOK_RSH
%left '+' '-'
%left '*' '/' '%'
/* TODO deref */
%left '(' ')'


%type <l_decl> program
%type <l_decl> declaration_list
%type <l_decl> declaration
%type <l_decl> struct_decl
%type <l_decl> func_decl
%type <l_decl> param_list
%type <l_decl> param

%type <l_type> type_expr

%type <l_expr> stmt_list
%type <l_expr> expr
%type <l_expr> expr_opt
%type <l_expr> expr_list

%%

program:
        declaration_list { parse_tree_root = $$ = $1 }
    ;

declaration_list:
        declaration                     { $$ = $1 }
    |   declaration_list declaration    { $$ = dl_append($1, $2) }
    ;

declaration:
        struct_decl { $$ = $1 }
    |   func_decl   { $$ = $1 }
    ;

struct_decl:
        SL_TOK_STRUCT SL_TOK_IDENT '{' param_list '}'
        {
            $$ = dl_struct($2, $4)
        }
    ;

param_list:
        /* empty */             { $$ = NULL }
    |   param                   { $$ = $1 }
    |   param_list ',' param    { $$ = dl_append($1, $3) }
    |   param_list ','          { $$ = $1 }
    ;

param:
        SL_TOK_IDENT ':' type_expr   { $$ = dl_param($1, $3) }
    ;

type_expr:
        SL_TOK_IDENT    { $$ = ty_type_name($1) }
    |   '*' type_expr   { $$ = ty_type_pointer($2) }
    ;

func_decl:
        SL_TOK_FN SL_TOK_IDENT '(' param_list ')' SL_TOK_SARROW type_expr
            '{' stmt_list '}'
        {
            $$ = dl_func($2, $4, $7, $9)
        }
    ;

stmt_list:
        expr                { $$ = $1 }
    |   stmt_list ';' expr  { $$ = ex_append($1, $3)  }
    |   stmt_list ';'       { $$ = $1 }
    ;

expr:
        '(' expr ')'                    { $$ = $2 }
    |   SL_TOK_INT                      { $$ = sl_expr_int($1) }
    |   expr '+' expr                   { $$ = sl_expr_binop($2, $1, $3) }
    |   expr '-' expr                   { $$ = sl_expr_binop($2, $1, $3) }
    |   expr '*' expr                   { $$ = sl_expr_binop($2, $1, $3) }
    |   expr '/' expr                   { $$ = sl_expr_binop($2, $1, $3) }
    |   SL_TOK_LET SL_TOK_IDENT ':' type_expr '=' expr
        {
            $$ = sl_expr_let($2, $4, $6)
        }
    |   SL_TOK_IDENT '(' expr_list ')'  { $$ = sl_expr_call($1, $3) }
    |   SL_TOK_IDENT                    { $$ = sl_expr_var($1) }
    |   SL_TOK_RETURN expr_opt          { $$ = sl_expr_return($2) }
    |   SL_TOK_BREAK                    { $$ = sl_expr_break() }
    |   SL_TOK_LOOP '{' stmt_list '}'   { $$ = sl_expr_loop($3) }
    |   '*' expr                        { $$ = sl_expr_deref($2) }
    |   SL_TOK_NEW SL_TOK_IDENT '{' expr_list '}'
                                        { $$ = sl_expr_new($2, $4) }
    ;

expr_opt:
        /* empty */         { $$ = NULL }
    |   expr                { $$ = $1 }
    ;

    /* this looks wrong..., looks like it would allow ",e" */
expr_list:
        /* empty */         { $$ = NULL }
    |   expr                { $$ = $1 }
    |   expr_list ',' expr  { $$ = ex_append($1, $3) }
    ;
%%

int yywrap()
{
    return 1;
}

static const char* yyfilename;

static void yyerror(const char* msg)
{
    const char* fn;
    if (yyin == stdin) {
        fn = "<stdin>";
    } else {
        fn = yyfilename;
    }

    fprintf(stderr, "%s:%d: error: %s\n", fn, yylineno, msg);
    extern const char* yytext;
    fprintf(stderr, "	yytext = %s\n", yytext);
}

sl_decl_t* parse_file(const char* filename)
{
    if (strcmp(filename, "-") == 0) {
        yyin = stdin; // default anyway
    } else {
        yyin = fopen(filename, "r");
        if (!yyin) {
            perror(filename);
            return NULL;
        }
        yyfilename = filename;
    }
    parse_tree_root = NULL;
    // TODO: check meaning of yyparse return value
    yyparse();
    yyfilename = NULL;
    return parse_tree_root;
}
