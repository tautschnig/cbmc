%{

#undef yylex
#include "simplify_unparse.h"
#define yylex() lexer.yylex()


#include "std_expr.h"

#include <list>

#include <iostream>
void yyerror(
    simplify_unparset &lexer,
    const exprt& expr,
    exprt& dest,
    std::list<exprt> &temps,
    char const* error)
{
  lexer.abort_expr();

  if(expr.id()!=ID_constant)
  {
    std::cerr << "Simplify parse failed: " << error << std::endl;
    std::cerr << "While parsing " << expr.pretty() << std::endl;
  }
}

%}

%error-verbose

%parse-param { simplify_unparset &lexer }
%parse-param { const exprt& expr }
%parse-param { exprt& dest }
%parse-param { std::list<exprt> &temps }

%token TOK_expr      "other-expr"

%token TOK_not       "not"
%token TOK_constant  "constant"
%token TOK_symbol    "symbol"
%token TOK_typecast  "typecast"
%token TOK_infinity  "infinity"

%start init

%%

init: expr
    {
      if($1!=0)
      {
        exprt tmp(*$1);
        dest.swap(tmp);
      }
    }
    ;

expr: not_expr
    | typecast_expr
    | nary_expr
    | error
    {
      $$=0;

      yyclearin;
      yyerrok;
    }
    ;

exprs: exprs expr
     {
       // TODO: broken
       temps.push_back(*$1);
       temps.back().copy_to_operands(*$2);
       $$=&(temps.back());
     }
     | expr
     {
       if($1!=0)
       {
         temps.push_back(exprt());
         temps.back().copy_to_operands(*$1);
         $$=&(temps.back());
       }
       else
         $$=0;
     }
     ;

nary_expr: TOK_expr '(' exprs ')'
         {
           temps.push_back(*$3);
           temps.back().id(($1)->id());
           $$=&(temps.back());
         }
         ;

not_expr: TOK_not '(' TOK_not '(' expr ')' ')'
        {
          if($5!=0)
            $$=$5;
          else
            $$=&(($3)->op0());
        }
        | TOK_not '(' not_expr ')'
        {
          if($3!=0)
          {
            temps.push_back(exprt());
            not_exprt n(*$3);
            n.swap(temps.back());
            $$=&(temps.back());
          }
          else
            $$=0;
        }
        | error
        {
          $$=0;

          yyclearin;
          yyerrok;
        }
        ;

typecast_expr: TOK_typecast '(' TOK_infinity ')'
             {
               $$=0;
             }
             | TOK_typecast '(' TOK_constant ')'
             {
               $$=0;
             }
             | TOK_typecast '(' TOK_typecast '(' expr ')' ')'
             {
               $$=0;
             }
             | TOK_typecast '(' expr ')'
             {
               $$=0;
             }
             ;

%%
