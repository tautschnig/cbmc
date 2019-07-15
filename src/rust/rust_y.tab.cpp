// A Bison parser, made by GNU Bison 3.0.4.

// Skeleton implementation for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2015 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.

// Take the name prefix into account.
#define yylex   yyrustlex

// First part of user declarations.
#line 1 "parser.y" // lalr1.cc:404

#define YYERROR_VERBOSE
#define YYSTYPE unsigned int
#define PARSER rust_parser
#define YYSTYPE_IS_TRIVIAL 1

#include <iostream>
#include <util/std_expr.h>
#include <util/c_types.h>
#include <util/arith_tools.h>
#include <util/ieee_float.h>
#include <ansi-c/literals/convert_float_literal.h>
#include <ansi-c/gcc_types.h>
#include <ansi-c/literals/parse_float.h>
#include "rust_parser.h"
#include "rust_parseassert.h"
#include "rust_types.h"

// Make sure this matches the #define for YY_DECL in scanner.l
extern int yylex(unsigned int* yylval);
extern void yyerror(char const *s);
extern void push_back(char c);
extern char const* yyrusttext;

symbol_exprt symbol_exprt_typeless_empty(irep_idt const& id)
{
  return symbol_exprt(id, typet(ID_empty));
}

#line 68 "rust_y.tab.cpp" // lalr1.cc:404

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

#include "rust_y.tab.hpp"

// User implementation prologue.

#line 82 "rust_y.tab.cpp" // lalr1.cc:412


#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif



// Suppress unused-variable warnings by "using" E.
#define YYUSE(E) ((void) (E))

// Enable debugging if requested.
#if YYDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << std::endl;                  \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yystack_print_ ();                \
  } while (false)

#else // !YYDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YYUSE(Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void>(0)
# define YY_STACK_PRINT()                static_cast<void>(0)

#endif // !YYDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyla.clear ())

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)


namespace yyrust {
#line 149 "rust_y.tab.cpp" // lalr1.cc:479

  /// Build a parser object.
  parser::parser ()
#if YYDEBUG
     :yydebug_ (false),
      yycdebug_ (&std::cerr)
#endif
  {}

  parser::~parser ()
  {}


  /*---------------.
  | Symbol types.  |
  `---------------*/

  inline
  parser::syntax_error::syntax_error (const std::string& m)
    : std::runtime_error (m)
  {}

  // basic_symbol.
  template <typename Base>
  inline
  parser::basic_symbol<Base>::basic_symbol ()
    : value ()
  {}

  template <typename Base>
  inline
  parser::basic_symbol<Base>::basic_symbol (const basic_symbol& other)
    : Base (other)
    , value ()
  {
    value = other.value;
  }


  template <typename Base>
  inline
  parser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, const semantic_type& v)
    : Base (t)
    , value (v)
  {}


  /// Constructor for valueless symbols.
  template <typename Base>
  inline
  parser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t)
    : Base (t)
    , value ()
  {}

  template <typename Base>
  inline
  parser::basic_symbol<Base>::~basic_symbol ()
  {
    clear ();
  }

  template <typename Base>
  inline
  void
  parser::basic_symbol<Base>::clear ()
  {
    Base::clear ();
  }

  template <typename Base>
  inline
  bool
  parser::basic_symbol<Base>::empty () const
  {
    return Base::type_get () == empty_symbol;
  }

  template <typename Base>
  inline
  void
  parser::basic_symbol<Base>::move (basic_symbol& s)
  {
    super_type::move(s);
    value = s.value;
  }

  // by_type.
  inline
  parser::by_type::by_type ()
    : type (empty_symbol)
  {}

  inline
  parser::by_type::by_type (const by_type& other)
    : type (other.type)
  {}

  inline
  parser::by_type::by_type (token_type t)
    : type (yytranslate_ (t))
  {}

  inline
  void
  parser::by_type::clear ()
  {
    type = empty_symbol;
  }

  inline
  void
  parser::by_type::move (by_type& that)
  {
    type = that.type;
    that.clear ();
  }

  inline
  int
  parser::by_type::type_get () const
  {
    return type;
  }


  // by_state.
  inline
  parser::by_state::by_state ()
    : state (empty_state)
  {}

  inline
  parser::by_state::by_state (const by_state& other)
    : state (other.state)
  {}

  inline
  void
  parser::by_state::clear ()
  {
    state = empty_state;
  }

  inline
  void
  parser::by_state::move (by_state& that)
  {
    state = that.state;
    that.clear ();
  }

  inline
  parser::by_state::by_state (state_type s)
    : state (s)
  {}

  inline
  parser::symbol_number_type
  parser::by_state::type_get () const
  {
    if (state == empty_state)
      return empty_symbol;
    else
      return yystos_[state];
  }

  inline
  parser::stack_symbol_type::stack_symbol_type ()
  {}


  inline
  parser::stack_symbol_type::stack_symbol_type (state_type s, symbol_type& that)
    : super_type (s)
  {
    value = that.value;
    // that is emptied.
    that.type = empty_symbol;
  }

  inline
  parser::stack_symbol_type&
  parser::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
    value = that.value;
    return *this;
  }


  template <typename Base>
  inline
  void
  parser::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);

    // User destructor.
    YYUSE (yysym.type_get ());
  }

#if YYDEBUG
  template <typename Base>
  void
  parser::yy_print_ (std::ostream& yyo,
                                     const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YYUSE (yyoutput);
    symbol_number_type yytype = yysym.type_get ();
    // Avoid a (spurious) G++ 4.8 warning about "array subscript is
    // below array bounds".
    if (yysym.empty ())
      std::abort ();
    yyo << (yytype < yyntokens_ ? "token" : "nterm")
        << ' ' << yytname_[yytype] << " (";
    YYUSE (yytype);
    yyo << ')';
  }
#endif

  inline
  void
  parser::yypush_ (const char* m, state_type s, symbol_type& sym)
  {
    stack_symbol_type t (s, sym);
    yypush_ (m, t);
  }

  inline
  void
  parser::yypush_ (const char* m, stack_symbol_type& s)
  {
    if (m)
      YY_SYMBOL_PRINT (m, s);
    yystack_.push (s);
  }

  inline
  void
  parser::yypop_ (unsigned int n)
  {
    yystack_.pop (n);
  }

#if YYDEBUG
  std::ostream&
  parser::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  parser::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  parser::debug_level_type
  parser::debug_level () const
  {
    return yydebug_;
  }

  void
  parser::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // YYDEBUG

  inline parser::state_type
  parser::yy_lr_goto_state_ (state_type yystate, int yysym)
  {
    int yyr = yypgoto_[yysym - yyntokens_] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yysym - yyntokens_];
  }

  inline bool
  parser::yy_pact_value_is_default_ (int yyvalue)
  {
    return yyvalue == yypact_ninf_;
  }

  inline bool
  parser::yy_table_value_is_error_ (int yyvalue)
  {
    return yyvalue == yytable_ninf_;
  }

  int
  parser::parse ()
  {
    // State.
    int yyn;
    /// Length of the RHS of the rule being reduced.
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The return value of parse ().
    int yyresult;

    // FIXME: This shoud be completely indented.  It is not yet to
    // avoid gratuitous conflicts when merging into the master branch.
    try
      {
    YYCDEBUG << "Starting parse" << std::endl;


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULLPTR, 0, yyla);

    // A new symbol was pushed on the stack.
  yynewstate:
    YYCDEBUG << "Entering state " << yystack_[0].state << std::endl;

    // Accept?
    if (yystack_[0].state == yyfinal_)
      goto yyacceptlab;

    goto yybackup;

    // Backup.
  yybackup:

    // Try to take a decision without lookahead.
    yyn = yypact_[yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyla.empty ())
      {
        YYCDEBUG << "Reading a token: ";
        try
          {
            yyla.type = yytranslate_ (yylex (&yyla.value));
          }
        catch (const syntax_error& yyexc)
          {
            error (yyexc);
            goto yyerrlab1;
          }
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.type_get ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.type_get ())
      goto yydefault;

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", yyn, yyla);
    goto yynewstate;

  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;

  /*-----------------------------.
  | yyreduce -- Do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    {
      stack_symbol_type yylhs;
      yylhs.state = yy_lr_goto_state_(yystack_[yylen].state, yyr1_[yyn]);
      /* If YYLEN is nonzero, implement the default value of the
         action: '$$ = $1'.  Otherwise, use the top of the stack.

         Otherwise, the following line sets YYLHS.VALUE to garbage.
         This behavior is undocumented and Bison users should not rely
         upon it.  */
      if (yylen)
        yylhs.value = yystack_[yylen - 1].value;
      else
        yylhs.value = yystack_[0].value;


      // Perform the reduction.
      YY_REDUCE_PRINT (yyn);
      try
        {
          switch (yyn)
            {
  case 2:
#line 228 "parser.y" // lalr1.cc:859
    { /*{o}{td}mk_node("crate", 2, $2, $3);*/ }
#line 574 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 3:
#line 229 "parser.y" // lalr1.cc:859
    { /*{o}{td}mk_node("crate", 1, $2);*/ }
#line 580 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 7:
#line 239 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 586 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 8:
#line 243 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("InnerAttrs", 1, $1);*/ }
#line 592 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 9:
#line 244 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 598 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 10:
#line 248 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("InnerAttr", 1, $3);*/ }
#line 604 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 11:
#line 249 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("InnerAttr", 1, mk_node("doc-comment", 1, mk_atom(yyrusttext)));*/ }
#line 610 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 13:
#line 254 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 616 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 14:
#line 258 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("OuterAttrs", 1, $1);*/ }
#line 622 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 15:
#line 259 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 628 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 16:
#line 263 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $3;*/ }
#line 634 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 17:
#line 264 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("doc-comment", 1, mk_atom(yyrusttext));*/ }
#line 640 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 18:
#line 268 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("MetaWord", 1, $1);*/ }
#line 646 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 19:
#line 269 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("MetaNameValue", 2, $1, $3);*/ }
#line 652 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 20:
#line 270 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("MetaList", 2, $1, $3);*/ }
#line 658 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 21:
#line 271 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("MetaList", 2, $1, $3);*/ }
#line 664 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 22:
#line 275 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 670 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 23:
#line 276 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("MetaItems", 1, $1);*/ }
#line 676 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 24:
#line 277 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 682 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 26:
#line 282 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 688 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 27:
#line 286 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Items", 1, $1);*/ }
#line 694 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 28:
#line 287 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 700 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 29:
#line 291 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("AttrsAndVis", 2, $1, $2);*/ }
#line 706 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 30:
#line 295 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Item", 2, $1, $2);*/ }
#line 712 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 38:
#line 314 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemStatic", 3, $2, $4, $6);*/ }
#line 718 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 39:
#line 315 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemStatic", 3, $3, $5, $7);*/ }
#line 724 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 40:
#line 319 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemConst", 3, $2, $4, $6);*/ }
#line 730 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 41:
#line 323 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemMacro", 3, $1, $3, $4);*/ }
#line 736 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 42:
#line 324 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemMacro", 3, $1, $3, $4);*/ }
#line 742 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 43:
#line 325 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemMacro", 3, $1, $3, $4);*/ }
#line 748 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 46:
#line 331 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewItemExternCrate", 1, $3);*/ }
#line 754 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 47:
#line 332 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewItemExternCrate", 2, $3, $5);*/ }
#line 760 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 48:
#line 336 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewItemExternFn", 2, $2, $3);*/ }
#line 766 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 49:
#line 340 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewItemUse", 1, $2);*/ }
#line 772 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 50:
#line 344 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathSimple", 1, $1);*/ }
#line 778 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 51:
#line 345 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 2, $1, mk_atom("ViewPathListEmpty"));*/ }
#line 784 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 52:
#line 346 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 1, mk_atom("ViewPathListEmpty"));*/ }
#line 790 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 53:
#line 347 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 2, $1, $4);*/ }
#line 796 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 54:
#line 348 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 1, $3);*/ }
#line 802 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 55:
#line 349 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 2, $1, $4);*/ }
#line 808 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 56:
#line 350 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 1, $3);*/ }
#line 814 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 57:
#line 351 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathGlob", 1, $1);*/ }
#line 820 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 58:
#line 352 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("ViewPathGlob");*/ }
#line 826 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 59:
#line 353 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("ViewPathGlob");*/ }
#line 832 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 60:
#line 354 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("ViewPathListEmpty");*/ }
#line 838 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 61:
#line 355 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 1, $2);*/ }
#line 844 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 62:
#line 356 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathList", 1, $2);*/ }
#line 850 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 63:
#line 357 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPathSimple", 2, $1, $3);*/ }
#line 856 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 67:
#line 364 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemForeignMod", 1, $1);*/ }
#line 862 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 73:
#line 373 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 868 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 74:
#line 374 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(exprt("ID_emptytyascript")); }
#line 874 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 75:
#line 378 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 880 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 76:
#line 379 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(exprt("ID_emptyinitexpr")); }
#line 886 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 77:
#line 385 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemStruct", 4, $2, $3, $4, $5);*/
}
#line 894 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 78:
#line 389 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemStruct", 4, $2, $3, $4, $5);*/
}
#line 902 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 79:
#line 393 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemStruct", 3, $2, $3, $4);*/
}
#line 910 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 80:
#line 399 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 916 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 81:
#line 400 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 922 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 82:
#line 404 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 928 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 83:
#line 405 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 934 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 84:
#line 409 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("StructFields", 1, $1);*/ }
#line 940 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 85:
#line 410 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 946 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 86:
#line 411 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 952 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 87:
#line 415 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("StructField", 3, $1, $2, $4);*/ }
#line 958 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 88:
#line 419 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("StructFields", 1, $1);*/ }
#line 964 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 89:
#line 420 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 970 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 90:
#line 421 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 976 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 91:
#line 425 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("StructField", 2, $1, $2);*/ }
#line 982 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 92:
#line 430 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemEnum", 0);*/ }
#line 988 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 93:
#line 431 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemEnum", 0);*/ }
#line 994 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 94:
#line 435 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("EnumDefs", 1, $1);*/ }
#line 1000 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 95:
#line 436 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 1006 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 96:
#line 437 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1012 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 97:
#line 441 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("EnumDef", 3, $1, $2, $3);*/ }
#line 1018 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 98:
#line 445 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("EnumArgs", 1, $2);*/ }
#line 1024 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 99:
#line 446 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("EnumArgs", 1, $2);*/ }
#line 1030 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 100:
#line 447 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("EnumArgs", 1, $2);*/ }
#line 1036 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 101:
#line 448 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("EnumArgs", 1, $2);*/ }
#line 1042 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 102:
#line 449 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1048 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 103:
#line 454 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemUnion", 0);*/ }
#line 1054 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 104:
#line 455 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemUnion", 0);*/ }
#line 1060 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 105:
#line 458 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemMod", 1, $2);*/ }
#line 1066 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 106:
#line 459 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemMod", 2, $2, $4);*/ }
#line 1072 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 107:
#line 460 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemMod", 3, $2, $4, $5);*/ }
#line 1078 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 108:
#line 464 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemForeignMod", 1, $4);*/ }
#line 1084 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 109:
#line 465 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemForeignMod", 2, $4, $5);*/ }
#line 1090 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 111:
#line 470 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1096 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 113:
#line 475 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1102 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 114:
#line 479 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForeignItems", 1, $1);*/ }
#line 1108 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 115:
#line 480 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 1114 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 116:
#line 484 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForeignItem", 2, $1, $3);*/ }
#line 1120 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 117:
#line 485 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForeignItem", 2, $1, $2);*/ }
#line 1126 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 118:
#line 486 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForeignItem", 2, $1, $3);*/ }
#line 1132 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 119:
#line 490 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("StaticItem", 3, $1, $2, $4);*/ }
#line 1138 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 120:
#line 494 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForeignFn", 4, $2, $3, $4, $5);*/ }
#line 1144 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 121:
#line 498 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("FnDecl", 2, $1, $2);*/ }
#line 1150 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 122:
#line 502 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1156 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 123:
#line 503 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1162 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 124:
#line 504 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1168 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 125:
#line 505 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1174 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 126:
#line 509 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("Public");*/ }
#line 1180 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 127:
#line 510 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("Inherited");*/ }
#line 1186 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 128:
#line 514 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("IdentsOrSelf", 1, $1);*/ }
#line 1192 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 129:
#line 515 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("IdentsOrSelf", 2, $1, $3);*/ }
#line 1198 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 130:
#line 516 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 1204 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 132:
#line 521 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 1210 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 133:
#line 525 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ItemTy", 4, $2, $3, $4, $6);*/ }
#line 1216 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 134:
#line 529 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForSized", 1, $3);*/ }
#line 1222 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 135:
#line 530 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForSized", 1, $2);*/ }
#line 1228 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 136:
#line 531 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1234 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 137:
#line 536 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemTrait", 7, $1, $3, $4, $5, $6, $7, $9);*/
}
#line 1242 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 139:
#line 543 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1248 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 140:
#line 547 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TraitItems", 1, $1);*/ }
#line 1254 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 141:
#line 548 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 1260 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 145:
#line 555 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TraitMacroItem", 2, $1, $2);*/ }
#line 1266 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 146:
#line 559 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ConstTraitItem", 4, $1, $3, $4, $5);*/ }
#line 1272 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 147:
#line 563 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ConstDefault", 1, $2);*/ }
#line 1278 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 148:
#line 564 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1284 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 149:
#line 568 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TypeTraitItem", 2, $1, $3);*/ }
#line 1290 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 150:
#line 572 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("Unsafe");*/ }
#line 1296 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 151:
#line 573 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1302 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 152:
#line 577 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("DefaultUnsafe");*/ }
#line 1308 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 153:
#line 578 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("Default");*/ }
#line 1314 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 154:
#line 579 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("Unsafe");*/ }
#line 1320 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 155:
#line 580 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1326 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 156:
#line 583 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Required", 1, $1);*/ }
#line 1332 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 157:
#line 584 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Provided", 1, $1);*/ }
#line 1338 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 158:
#line 589 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("TypeMethod", 6, $1, $2, $4, $5, $6, $7);*/
}
#line 1346 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 159:
#line 593 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("TypeMethod", 6, $1, $3, $5, $6, $7, $8);*/
}
#line 1354 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 160:
#line 597 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("TypeMethod", 7, $1, $2, $4, $6, $7, $8, $9);*/
}
#line 1362 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 161:
#line 604 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("Method", 7, $1, $2, $4, $5, $6, $7, $8);*/
}
#line 1370 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 162:
#line 608 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("Method", 7, $1, $3, $5, $6, $7, $8, $9);*/
}
#line 1378 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 163:
#line 612 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("Method", 8, $1, $2, $4, $6, $7, $8, $9, $10);*/
}
#line 1386 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 164:
#line 619 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("Method", 8, $1, $2, $3, $5, $6, $7, $8, $9);*/
}
#line 1394 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 165:
#line 623 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("Method", 8, $1, $2, $4, $6, $7, $8, $9, $10);*/
}
#line 1402 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 166:
#line 627 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("Method", 9, $1, $2, $3, $5, $7, $8, $9, $10, $11);*/
}
#line 1410 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 167:
#line 649 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemImpl", 6, $1, $3, $4, $5, $7, $8);*/
}
#line 1418 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 168:
#line 653 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemImpl", 6, $1, $3, 5, $6, $9, $10);*/
}
#line 1426 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 169:
#line 657 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemImpl", 6, $3, $4, $6, $7, $9, $10);*/
}
#line 1434 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 170:
#line 661 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemImplNeg", 7, $1, $3, $5, $7, $8, $10, $11);*/
}
#line 1442 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 171:
#line 665 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemImplDefault", 3, $1, $3, $4);*/
}
#line 1450 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 172:
#line 669 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemImplDefaultNeg", 3, $1, $3, $4);*/
}
#line 1458 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 174:
#line 676 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1464 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 175:
#line 680 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ImplItems", 1, $1);*/ }
#line 1470 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 176:
#line 681 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 1476 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 178:
#line 686 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ImplMacroItem", 2, $1, $2);*/ }
#line 1482 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 181:
#line 692 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("Default");*/ }
#line 1488 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 182:
#line 693 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1494 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 183:
#line 697 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ImplConst", 3, $1, $2, $3);*/ }
#line 1500 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 184:
#line 701 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ImplType", 5, $1, $2, $4, $5, $7);*/ }
#line 1506 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 185:
#line 706 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemFn", 5, $2, $3, $4, $5, $6);*/
  // implied return value
  // if function has a return type
  if(parser_stack((yystack_[2].value)).type().id() != ID_empty)
  {
    code_blockt& body = to_code_block(to_code(parser_stack((yystack_[0].value))));
    codet& last_statement = to_code(body.operands().back());
    irep_idt statement = last_statement.get_statement();
    if(statement == ID_expression)
    {
      body.operands().back() = code_returnt(to_code_expression(last_statement).expression());
    }
    else if(statement == ID_block)
    {
      side_effect_exprt rhs(ID_statement_expression, exprt::operandst(), typet(), source_locationt());
      rhs.add_to_operands(to_code_block(last_statement));
      body.operands().back() = code_returnt(rhs);
    }
  }

  // actual function definition
  code_function_callt x(parser_stack((yystack_[4].value)), code_function_callt::argumentst(parser_stack((yystack_[2].value)).operands()));
  x.type() = parser_stack((yystack_[2].value)).type();
  newstack((yylhs.value)).swap(x);

  //TODO_main: This might not belong here long-term, if crates need packaging up especially
  rust_declarationt a;
  code_typet::parameterst params;
  Forall_operands(it, parser_stack((yystack_[2].value)))
  {
    symbol_exprt& symbol = to_symbol_expr(*it);
    params.push_back(code_typet::parametert(symbol.type()));
    params.back().set_identifier(symbol.get_identifier());
  }
  parser_stack((yystack_[4].value)).type() = code_typet(params, parser_stack((yystack_[2].value)).type());
  a.add_declarator(to_symbol_expr(parser_stack((yystack_[4].value))));
  a.add_value(to_code_block(to_code(parser_stack((yystack_[0].value)))));
  PARSER.parse_tree.items.push_back(std::move(a));
}
#line 1551 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 186:
#line 747 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemFn", 5, $3, $4, $5, $6, $7);*/
}
#line 1559 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 187:
#line 754 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemUnsafeFn", 5, $3, $4, $5, $6, $7);*/
}
#line 1567 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 188:
#line 758 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemUnsafeFn", 5, $4, $5, $6, $7, $8);*/
}
#line 1575 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 189:
#line 762 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ItemUnsafeFn", 6, $3, $5, $6, $7, $8, $9);*/
}
#line 1583 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 190:
#line 768 "parser.y" // lalr1.cc:859
    { parser_stack((yystack_[1].value)).type() = rusttype_to_ctype(to_symbol_expr(parser_stack((yystack_[0].value))).get_identifier());
                       (yylhs.value) = (yystack_[1].value); }
#line 1590 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 191:
#line 773 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("FnDecl", 2, $1, $2);*/ }
#line 1596 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 192:
#line 777 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("FnDecl", 2, $1, $2);*/ }
#line 1602 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 193:
#line 781 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[1].value); }
#line 1608 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 194:
#line 785 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($2, 1, $3);*/ }
#line 1614 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 195:
#line 786 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1620 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 196:
#line 790 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfLower", 3, $2, $4, $5);*/ }
#line 1626 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 197:
#line 791 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfRegion", 3, $3, $5, $6);*/ }
#line 1632 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 198:
#line 792 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfRegion", 4, $3, $4, $6, $7);*/ }
#line 1638 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 199:
#line 793 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfStatic", 1, $2);*/ }
#line 1644 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 200:
#line 797 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfLower", 3, $2, $4, $5);*/ }
#line 1650 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 201:
#line 798 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfRegion", 3, $3, $5, $6);*/ }
#line 1656 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 202:
#line 799 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfRegion", 4, $3, $4, $6, $7);*/ }
#line 1662 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 203:
#line 800 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfStatic", 1, $2);*/ }
#line 1668 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 204:
#line 804 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 1674 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 205:
#line 805 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[1].value); }
#line 1680 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 206:
#line 806 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(multi_ary_exprt(ID_parameters, exprt::operandst(), typet())); }
#line 1686 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 207:
#line 810 "parser.y" // lalr1.cc:859
    { exprt::operandst params;
                         params.push_back(parser_stack((yystack_[0].value)));
                         newstack((yylhs.value)).swap(multi_ary_exprt(ID_parameters, params, typet())); }
#line 1694 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 208:
#line 813 "parser.y" // lalr1.cc:859
    { parser_stack((yystack_[2].value)).operands().push_back(parser_stack((yystack_[0].value))); }
#line 1700 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 209:
#line 817 "parser.y" // lalr1.cc:859
    { //TODO_lifetimes: This will have to change to support lifetimes unless they're integrated into the rusttype_to_ctype function
                     parser_stack((yystack_[2].value)).type() = rusttype_to_ctype(to_symbol_expr(parser_stack((yystack_[0].value))).get_identifier());
                     (yylhs.value) = (yystack_[2].value); }
#line 1708 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 210:
#line 823 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("InferrableParams", 1, $1);*/ }
#line 1714 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 211:
#line 824 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 1720 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 212:
#line 828 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("InferrableParam", 2, $1, $2);*/ }
#line 1726 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 213:
#line 832 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1732 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 214:
#line 833 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1738 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 215:
#line 834 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1744 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 216:
#line 835 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1750 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 217:
#line 839 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1756 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 218:
#line 840 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1762 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 219:
#line 841 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1768 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 220:
#line 842 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1774 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 223:
#line 848 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1780 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 224:
#line 852 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Args", 1, $1);*/ }
#line 1786 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 225:
#line 853 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 1792 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 226:
#line 859 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Arg", 2, $1, $3);*/ }
#line 1798 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 228:
#line 864 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1804 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 229:
#line 865 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Args", 2, $2, $3);*/ }
#line 1810 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 230:
#line 866 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1816 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 232:
#line 871 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("PatWild");*/ }
#line 1822 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 233:
#line 872 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1828 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 234:
#line 873 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("PatWild");*/ }
#line 1834 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 235:
#line 874 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1840 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 236:
#line 875 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("PatWild");*/ }
#line 1846 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 237:
#line 876 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 1852 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 238:
#line 880 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty("")); }
#line 1858 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 239:
#line 881 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 1864 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 240:
#line 882 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty("")); }
#line 1870 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 241:
#line 886 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Generics", 2, mk_none(), mk_none());*/ }
#line 1876 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 242:
#line 887 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Generics", 2, $2, mk_none());*/ }
#line 1882 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 243:
#line 888 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Generics", 2, $2, mk_none());*/ }
#line 1888 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 244:
#line 889 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = mk_node("Generics", 2, $2, mk_none());*/ }
#line 1894 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 245:
#line 890 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = mk_node("Generics", 2, $2, mk_none());*/ }
#line 1900 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 246:
#line 891 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Generics", 2, $2, $4);*/ }
#line 1906 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 247:
#line 892 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Generics", 2, $2, $4);*/ }
#line 1912 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 248:
#line 893 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = mk_node("Generics", 2, $2, $4);*/ }
#line 1918 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 249:
#line 894 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = mk_node("Generics", 2, $2, $4);*/ }
#line 1924 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 250:
#line 895 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Generics", 2, mk_none(), $2);*/ }
#line 1930 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 251:
#line 896 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Generics", 2, mk_none(), $2);*/ }
#line 1936 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 252:
#line 897 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = mk_node("Generics", 2, mk_none(), $2);*/ }
#line 1942 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 253:
#line 898 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = mk_node("Generics", 2, mk_none(), $2);*/ }
#line 1948 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 254:
#line 899 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)); }
#line 1954 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 255:
#line 903 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 1960 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 257:
#line 908 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("WhereClause", 1, $2);*/ }
#line 1966 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 258:
#line 909 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("WhereClause", 1, $2);*/ }
#line 1972 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 259:
#line 913 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("WherePredicates", 1, $1);*/ }
#line 1978 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 260:
#line 914 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 1984 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 261:
#line 918 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("WherePredicate", 3, $1, $2, $4);*/ }
#line 1990 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 262:
#line 919 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("WherePredicate", 3, $1, $2, $4);*/ }
#line 1996 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 263:
#line 923 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2002 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 264:
#line 924 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2008 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 265:
#line 927 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyParams", 1, $1);*/ }
#line 2014 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 266:
#line 928 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2020 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 267:
#line 936 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPath", 1, $1);*/ }
#line 2026 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 268:
#line 937 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPath", 1, $2);*/ }
#line 2032 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 269:
#line 938 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPath", 1, mk_atom("Self"));*/ }
#line 2038 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 270:
#line 939 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPath", 1, mk_atom("Self"));*/ }
#line 2044 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 271:
#line 940 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPath", 1, mk_atom("Super"));*/ }
#line 2050 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 272:
#line 941 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ViewPath", 1, mk_atom("Super"));*/ }
#line 2056 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 273:
#line 942 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2062 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 274:
#line 957 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("components", 1, $1);*/
                                                                                (yylhs.value) = (yystack_[0].value); }
#line 2069 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 275:
#line 959 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("components", 2, $1, $2);*/ }
#line 2075 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 276:
#line 960 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("components", 2, $1, $3);*/ }
#line 2081 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 277:
#line 961 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2087 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 278:
#line 962 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 2, $3, $4);*/ }
#line 2093 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 279:
#line 964 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 2, $3, $5);*/ }
#line 2099 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 280:
#line 968 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2105 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 281:
#line 969 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = $2;*/ }
#line 2111 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 282:
#line 970 "parser.y" // lalr1.cc:859
    { push_back('='); /*{o}$$ = $2;*/ }
#line 2117 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 283:
#line 971 "parser.y" // lalr1.cc:859
    { push_back('>'); push_back('='); /*{o}$$ = $2;*/ }
#line 2123 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 284:
#line 976 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2129 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 285:
#line 977 "parser.y" // lalr1.cc:859
    { push_back('>'); /*{o}$$ = $2;*/ }
#line 2135 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 286:
#line 978 "parser.y" // lalr1.cc:859
    { push_back('='); /*{o}$$ = $2;*/ }
#line 2141 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 287:
#line 979 "parser.y" // lalr1.cc:859
    { push_back('>'); push_back('='); /*{o}$$ = $2;*/ }
#line 2147 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 288:
#line 983 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("GenericValues", 1, $1);*/ }
#line 2153 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 291:
#line 989 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TySumsAndBindings", 2, $1, $3);*/ }
#line 2159 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 294:
#line 992 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2165 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 295:
#line 996 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2171 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 296:
#line 997 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2177 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 297:
#line 1005 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("PatWild");*/ }
#line 2183 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 298:
#line 1006 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatRegion", 1, $2);*/ }
#line 2189 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 299:
#line 1007 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatRegion", 1, $3);*/ }
#line 2195 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 300:
#line 1008 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatRegion", 1, mk_node("PatRegion", 1, $2));*/ }
#line 2201 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 301:
#line 1009 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("PatUnit");*/ }
#line 2207 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 302:
#line 1010 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 1, $2);*/ }
#line 2213 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 303:
#line 1011 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 1, $2);*/ }
#line 2219 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 305:
#line 1013 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatRange", 2, $1, $3);*/ }
#line 2225 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 306:
#line 1014 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatStruct", 2, $1, $3);*/ }
#line 2231 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 307:
#line 1015 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatEnum", 2, $1, mk_none());*/ }
#line 2237 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 308:
#line 1016 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatEnum", 2, $1, $3);*/ }
#line 2243 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 309:
#line 1017 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatMac", 3, $1, $3, $4);*/ }
#line 2249 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 310:
#line 1018 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatIdent", 2, $1, $2);*/
                                                    //TODO: ignoring mut. if other binding modes are important, add them
                                                    (yylhs.value) = (yystack_[0].value); }
#line 2257 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 311:
#line 1021 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatIdent", 3, mk_node("BindByValue", 1, mk_atom("MutImmutable")), $1, $3);*/ }
#line 2263 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 312:
#line 1022 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatIdent", 3, $1, $2, $4);*/ }
#line 2269 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 313:
#line 1023 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatUniq", 1, $2);*/ }
#line 2275 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 314:
#line 1024 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatQualifiedPath", 3, $2, $3, $6);*/ }
#line 2281 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 315:
#line 1026 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("PatQualifiedPath", 3, mk_node("PatQualifiedPath", 3, $2, $3, $6), $7, $10);*/
}
#line 2289 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 316:
#line 1032 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Pats", 1, $1);*/ }
#line 2295 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 317:
#line 1033 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2301 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 318:
#line 1037 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("BindByRef", 1, mk_atom("MutImmutable"));*/ }
#line 2307 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 319:
#line 1038 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("BindByRef", 1, mk_atom("MutMutable"));*/ }
#line 2313 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 320:
#line 1039 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("BindByValue", 1, mk_atom("MutMutable"));*/ }
#line 2319 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 321:
#line 1043 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 2325 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 322:
#line 1044 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 2331 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 323:
#line 1045 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatLit", 1, $2);*/ }
#line 2337 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 324:
#line 1049 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatField", 1, $1);*/ }
#line 2343 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 325:
#line 1050 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatField", 2, $1, $2);*/ }
#line 2349 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 326:
#line 1051 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatField", 2, mk_atom("box"), $2);*/ }
#line 2355 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 327:
#line 1052 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatField", 3, mk_atom("box"), $2, $3);*/ }
#line 2361 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 328:
#line 1053 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatField", 2, $1, $3);*/ }
#line 2367 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 329:
#line 1054 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatField", 3, $1, $2, $4);*/ }
#line 2373 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 330:
#line 1055 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatField", 2, mk_atom(yyrusttext), $3);*/ }
#line 2379 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 331:
#line 1059 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatFields", 1, $1);*/ }
#line 2385 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 332:
#line 1060 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2391 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 333:
#line 1064 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatStruct", 2, $1, mk_atom("false"));*/ }
#line 2397 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 334:
#line 1065 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatStruct", 2, $1, mk_atom("false"));*/ }
#line 2403 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 335:
#line 1066 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatStruct", 2, $1, mk_atom("true"));*/ }
#line 2409 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 336:
#line 1067 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatStruct", 1, mk_atom("true"));*/ }
#line 2415 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 337:
#line 1068 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatStruct", 1, mk_none());*/ }
#line 2421 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 338:
#line 1072 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, mk_none());*/ }
#line 2427 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 339:
#line 1073 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, mk_none());*/ }
#line 2433 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 340:
#line 1074 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, mk_none());*/ }
#line 2439 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 341:
#line 1075 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, mk_none());*/ }
#line 2445 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 342:
#line 1076 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, $4);*/ }
#line 2451 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 343:
#line 1077 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, $4);*/ }
#line 2457 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 344:
#line 1078 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, $5);*/ }
#line 2463 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 345:
#line 1079 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, $1, $5);*/ }
#line 2469 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 346:
#line 1080 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, mk_none(), $3);*/ }
#line 2475 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 347:
#line 1081 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, mk_none(), $3);*/ }
#line 2481 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 348:
#line 1082 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTup", 2, mk_none(), mk_none());*/ }
#line 2487 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 349:
#line 1086 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatTupElts", 1, $1);*/ }
#line 2493 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 350:
#line 1087 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2499 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 351:
#line 1091 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, mk_none());*/ }
#line 2505 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 352:
#line 1092 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, mk_none());*/ }
#line 2511 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 353:
#line 1093 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, mk_none());*/ }
#line 2517 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 354:
#line 1094 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, mk_none());*/ }
#line 2523 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 355:
#line 1095 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, $4);*/ }
#line 2529 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 356:
#line 1096 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, $4);*/ }
#line 2535 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 357:
#line 1097 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, $5);*/ }
#line 2541 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 358:
#line 1098 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, $1, $5);*/ }
#line 2547 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 359:
#line 1099 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, mk_none(), $3);*/ }
#line 2553 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 360:
#line 1100 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, mk_none(), $3);*/ }
#line 2559 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 361:
#line 1101 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, mk_none(), mk_none());*/ }
#line 2565 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 362:
#line 1102 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVec", 2, mk_none(), mk_none());*/ }
#line 2571 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 363:
#line 1106 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PatVecElts", 1, $1);*/ }
#line 2577 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 364:
#line 1107 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2583 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 367:
#line 1117 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyQualifiedPath", 3, $2, $3, $6);*/ }
#line 2589 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 368:
#line 1118 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyQualifiedPath", 3, mk_node("TyQualifiedPath", 3, $2, $3, $6), $7, $10);*/ }
#line 2595 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 369:
#line 1119 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyTup", 1, $2);*/ }
#line 2601 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 370:
#line 1120 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyTup", 1, $2);*/ }
#line 2607 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 371:
#line 1121 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("TyNil");*/ }
#line 2613 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 372:
#line 1125 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyPath", 2, mk_node("global", 1, mk_atom("false")), $1);*/
                                                                                               (yylhs.value) = (yystack_[0].value); }
#line 2620 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 373:
#line 1127 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyPath", 2, mk_node("global", 1, mk_atom("true")), $2);*/ }
#line 2626 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 374:
#line 1128 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyPath", 2, mk_node("self", 1, mk_atom("true")), $3);*/ }
#line 2632 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 375:
#line 1129 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyMacro", 3, $1, $3, $4);*/ }
#line 2638 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 376:
#line 1130 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyMacro", 3, $2, $4, $5);*/ }
#line 2644 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 377:
#line 1131 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyBox", 1, $2);*/ }
#line 2650 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 378:
#line 1132 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyPtr", 2, $2, $3);*/ }
#line 2656 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 379:
#line 1133 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyRptr", 2, mk_atom("MutImmutable"), $2);*/ }
#line 2662 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 380:
#line 1134 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyRptr", 2, mk_atom("MutMutable"), $3);*/ }
#line 2668 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 381:
#line 1135 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyRptr", 1, mk_node("TyRptr", 2, mk_atom("MutImmutable"), $2));*/ }
#line 2674 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 382:
#line 1136 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyRptr", 1, mk_node("TyRptr", 2, mk_atom("MutMutable"), $3));*/ }
#line 2680 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 383:
#line 1137 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyRptr", 3, $2, $3, $4);*/ }
#line 2686 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 384:
#line 1138 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyRptr", 1, mk_node("TyRptr", 3, $2, $3, $4));*/ }
#line 2692 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 385:
#line 1139 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyVec", 1, $2);*/ }
#line 2698 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 386:
#line 1140 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyFixedLengthVec", 2, $2, $5);*/ }
#line 2704 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 387:
#line 1141 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyFixedLengthVec", 2, $2, $4);*/ }
#line 2710 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 388:
#line 1142 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyTypeof", 1, $3);*/ }
#line 2716 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 389:
#line 1143 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("TyInfer");*/ }
#line 2722 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 392:
#line 1149 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2728 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 393:
#line 1150 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $3;*/ }
#line 2734 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 394:
#line 1151 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $4;*/ }
#line 2740 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 395:
#line 1152 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $5;*/ }
#line 2746 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 396:
#line 1156 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyFnDecl", 3, $1, $2, $3);*/ }
#line 2752 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 397:
#line 1160 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyClosure", 3, $3, $5, $6);*/ }
#line 2758 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 398:
#line 1161 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyClosure", 3, $2, $4, $5);*/ }
#line 2764 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 399:
#line 1162 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyClosure", 2, $3, $4);*/ }
#line 2770 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 400:
#line 1163 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyClosure", 2, $2, $3);*/ }
#line 2776 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 401:
#line 1167 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ForInType", 2, $3, $5);*/ }
#line 2782 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 405:
#line 1177 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("MutMutable");*/ }
#line 2788 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 406:
#line 1178 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("MutImmutable");*/ }
#line 2794 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 407:
#line 1182 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("MutMutable");*/ }
#line 2800 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 408:
#line 1183 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("MutImmutable");*/ }
#line 2806 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 409:
#line 1184 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("MutImmutable");*/ }
#line 2812 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 410:
#line 1189 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("GenericValues", 3, mk_none(), mk_node("TySums", 1, mk_node("TySum", 1, $1)), $2);*/
}
#line 2820 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 411:
#line 1193 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("GenericValues", 3, mk_none(), mk_node("TySums", 2, $1, $3), $4);*/
}
#line 2828 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 412:
#line 1199 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyQualifiedPath", 3, $1, $3, $6);*/ }
#line 2834 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 413:
#line 1200 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyQualifiedPath", 3, $1, $3, $6);*/ }
#line 2840 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 416:
#line 1206 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2846 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 417:
#line 1210 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TySums", 1, $1);*/ }
#line 2852 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 418:
#line 1211 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2858 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 419:
#line 1215 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TySum", 1, $1);*/
                          (yylhs.value) = (yystack_[0].value); }
#line 2865 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 420:
#line 1217 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2871 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 423:
#line 1226 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TySum", 1, $1);*/ }
#line 2877 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 424:
#line 1227 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2883 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 427:
#line 1236 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2889 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 428:
#line 1237 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2895 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 430:
#line 1242 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2901 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 432:
#line 1247 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2907 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 433:
#line 1251 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PolyBound", 2, $3, $5);*/ }
#line 2913 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 435:
#line 1253 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("PolyBound", 2, $4, $6);*/ }
#line 2919 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 436:
#line 1254 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2925 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 437:
#line 1258 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Bindings", 1, $1);*/ }
#line 2931 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 438:
#line 1259 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2937 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 439:
#line 1263 "parser.y" // lalr1.cc:859
    { /*{o}mk_node("Binding", 2, $1, $3);*/ }
#line 2943 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 440:
#line 1267 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyParam", 3, $1, $2, $3);*/ }
#line 2949 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 441:
#line 1268 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyParam", 4, $1, $3, $4, $5);*/ }
#line 2955 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 442:
#line 1273 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2961 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 443:
#line 1274 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2967 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 444:
#line 1278 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("bounds", 1, $1);*/ }
#line 2973 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 445:
#line 1279 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 2979 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 448:
#line 1289 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 2985 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 449:
#line 1290 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 2991 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 450:
#line 1294 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ltbounds", 1, $1);*/ }
#line 2997 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 451:
#line 1295 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 3003 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 452:
#line 1299 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TyDefault", 1, $2);*/ }
#line 3009 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 453:
#line 1300 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 3015 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 456:
#line 1306 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 3021 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 457:
#line 1310 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Lifetimes", 1, $1);*/ }
#line 3027 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 458:
#line 1311 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 3033 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 459:
#line 1315 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("lifetime", 2, mk_atom(yyrusttext), $2);*/ }
#line 3039 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 460:
#line 1316 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("static_lifetime");*/ }
#line 3045 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 461:
#line 1320 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("lifetime", 1, mk_atom(yyrusttext));*/ }
#line 3051 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 462:
#line 1321 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("static_lifetime");*/ }
#line 3057 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 464:
#line 1326 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 3063 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 465:
#line 1335 "parser.y" // lalr1.cc:859
    {
    /*{o}$$ = mk_node("ExprBlock", 2, $2, $3);*/
    code_blockt a = to_code_block(to_code(parser_stack((yystack_[1].value))));
    newstack((yylhs.value)).swap(a);
  }
#line 3073 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 466:
#line 1344 "parser.y" // lalr1.cc:859
    {
    /*{o}$$ = mk_node("ExprBlock", 1, $2);*/
    (yylhs.value) = (yystack_[1].value);
  }
#line 3082 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 468:
#line 1352 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/
                        code_blockt a;
                        a.append(to_code_block(to_code(parser_stack((yystack_[1].value)))));
                        if(parser_stack((yystack_[0].value)).id() == ID_code)
                          a.add(to_code(parser_stack((yystack_[0].value))));
                        else
                          a.add(code_expressiont(parser_stack((yystack_[0].value))));
                        newstack((yylhs.value)).swap(a); }
#line 3095 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 469:
#line 1360 "parser.y" // lalr1.cc:859
    { code_blockt a;
                        if(parser_stack((yystack_[0].value)).id() == ID_code)
                          a.add(to_code(parser_stack((yystack_[0].value))));
                        else
                          a.add(code_expressiont(parser_stack((yystack_[0].value))));
                        newstack((yylhs.value)).swap(a); }
#line 3106 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 470:
#line 1366 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/
                        newstack((yylhs.value)).swap(code_blockt()); }
#line 3113 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 471:
#line 1395 "parser.y" // lalr1.cc:859
    { code_blockt a;
                   a.add(to_code(parser_stack((yystack_[0].value))));
                   newstack((yylhs.value)).swap(a); }
#line 3121 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 472:
#line 1398 "parser.y" // lalr1.cc:859
    { code_blockt a = to_code_block(to_code(parser_stack((yystack_[1].value))));
                   a.add(to_code(parser_stack((yystack_[0].value))));
                   newstack((yylhs.value)).swap(a); }
#line 3129 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 473:
#line 1405 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = $2;*/
  /*TODO: Handle maybe_outer_attrs(#[...]s)*/
  // pass along the let statement
  (yylhs.value) = (yystack_[0].value);
}
#line 3140 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 475:
#line 1412 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 3146 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 476:
#line 1413 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 3152 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 477:
#line 1414 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $3;*/ }
#line 3158 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 479:
#line 1416 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/
                              (yylhs.value) = (yystack_[0].value); }
#line 3165 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 480:
#line 1418 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[1].value); }
#line 3171 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 481:
#line 1420 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 3177 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 482:
#line 1421 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 3183 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 485:
#line 1427 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/
           newstack((yylhs.value)).swap(exprt()); }
#line 3190 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 487:
#line 1433 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ 
           newstack((yylhs.value)).swap(exprt()); }
#line 3197 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 488:
#line 1438 "parser.y" // lalr1.cc:859
    { multi_ary_exprt a("ID_onlyexpr", exprt::operandst(), typet());
                           a.add_to_operands(parser_stack((yystack_[0].value)));
                           newstack((yylhs.value)).swap(a); }
#line 3205 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 489:
#line 1441 "parser.y" // lalr1.cc:859
    { parser_stack((yystack_[2].value)).add_to_operands(parser_stack((yystack_[0].value)));
                           (yylhs.value) = (yystack_[2].value); }
#line 3212 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 491:
#line 1447 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 3218 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 492:
#line 1448 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("SelfPath", 1, $3);*/ }
#line 3224 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 493:
#line 1457 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("components", 1, $1);*/
                                                       (yylhs.value) = (yystack_[0].value); }
#line 3231 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 494:
#line 1459 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom("Super");*/ }
#line 3237 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 495:
#line 1460 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 3243 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 496:
#line 1461 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, mk_atom("Super"));*/ }
#line 3249 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 497:
#line 1462 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 3255 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 498:
#line 1467 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("MacroExpr", 3, $1, $3, $4);*/ 
                                                             // special case for asserts
                                                             if(to_symbol_expr(parser_stack((yystack_[3].value))).get_identifier() == "assert")
                                                             {
                                                               //TODO_q: how to get source location?
                                                               // condition is always first parameter
                                                               exprt condition = parse_token_tree(to_multi_ary_expr(parser_stack((yystack_[0].value))));
                                                               source_locationt loc;
                                                               code_blockt a = create_fatal_assertion(condition, loc);
                                                               newstack((yylhs.value)).swap(a);
                                                             }
                                                             // TODO: Normal macro
                                                             else
                                                             {
                                                             }
                                                           }
#line 3276 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 499:
#line 1483 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("MacroExpr", 3, $1, $3, $4);*/ }
#line 3282 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 500:
#line 1487 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 3288 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 501:
#line 1488 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 3294 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 502:
#line 1489 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprPath", 1, mk_node("ident", 1, mk_atom("self")));*/ }
#line 3300 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 503:
#line 1490 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 3306 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 504:
#line 1491 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprStruct", 2, $1, $3);*/ }
#line 3312 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 505:
#line 1492 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTry", 1, $1);*/ }
#line 3318 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 506:
#line 1493 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprField", 2, $1, $3);*/ }
#line 3324 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 507:
#line 1494 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTupleIndex", 1, $1);*/ }
#line 3330 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 508:
#line 1495 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIndex", 2, $1, $3);*/ }
#line 3336 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 509:
#line 1496 "parser.y" // lalr1.cc:859
    { // assumes parser_stack($1) is a symbol
                                                     symbol_exprt a = to_symbol_expr(parser_stack((yystack_[3].value)));
                                                     code_typet::parameterst params;
                                                     Forall_operands(it, parser_stack((yystack_[1].value)))
                                                     {
                                                       params.push_back(code_typet::parametert((*it).type()));
                                                       params.back().add_to_operands((*it));
                                                     }
                                                     a.type() = code_typet(params, typet());
                                                     newstack((yylhs.value)).swap(code_expressiont(
                                                                       side_effect_expr_function_callt(a,
                                                                                                       parser_stack((yystack_[1].value)).operands(),
                                                                                                       typet(),
                                                                                                       source_locationt()))); }
#line 3355 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 510:
#line 1510 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprVec", 1, $2);*/ }
#line 3361 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 511:
#line 1511 "parser.y" // lalr1.cc:859
    { multi_ary_exprt& a = to_multi_ary_expr(parser_stack((yystack_[1].value)));
                                                    code_blockt b;
                                                    for(exprt::operandst::iterator it=(a).operands().begin(); it!=(a).operands().end(); ++it)
                                                    {
                                                      if((*it).id() == ID_code)
                                                        b.add_to_operands(to_code(*it));
                                                      else
                                                      {
                                                        code_expressiont c(*it);
                                                        c.type() = (*it).type();
                                                        b.add_to_operands(c);
                                                      }
                                                    }
                                                    //TODO: right now only one operand  has been important, I don't know what multiple would be used for yet
                                                    if(b.operands().size() == 1)
                                                      newstack((yylhs.value)).swap(b.op0());
                                                    else
                                                      newstack((yylhs.value)).swap(b); }
#line 3384 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 512:
#line 1529 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAgain", 0);*/ }
#line 3390 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 513:
#line 1530 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAgain", 1, $2);*/ }
#line 3396 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 514:
#line 1531 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(code_returnt()); }
#line 3402 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 515:
#line 1532 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(code_returnt(parser_stack((yystack_[0].value)))); }
#line 3408 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 516:
#line 1533 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(code_breakt()); }
#line 3414 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 517:
#line 1534 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBreak", 1, $2);*/ }
#line 3420 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 518:
#line 1535 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprYield", 0);*/ }
#line 3426 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 519:
#line 1536 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprYield", 1, $2);*/ }
#line 3432 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 520:
#line 1537 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3438 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 521:
#line 1538 "parser.y" // lalr1.cc:859
    { shl_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3445 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 522:
#line 1540 "parser.y" // lalr1.cc:859
    { lshr_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3452 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 523:
#line 1542 "parser.y" // lalr1.cc:859
    { minus_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3459 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 524:
#line 1544 "parser.y" // lalr1.cc:859
    { bitand_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3466 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 525:
#line 1546 "parser.y" // lalr1.cc:859
    { bitor_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3473 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 526:
#line 1548 "parser.y" // lalr1.cc:859
    { plus_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3480 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 527:
#line 1550 "parser.y" // lalr1.cc:859
    { mult_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3487 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 528:
#line 1552 "parser.y" // lalr1.cc:859
    { div_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3494 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 529:
#line 1554 "parser.y" // lalr1.cc:859
    { bitxor_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3501 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 530:
#line 1556 "parser.y" // lalr1.cc:859
    { mod_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                    newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3508 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 531:
#line 1558 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(or_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3514 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 532:
#line 1559 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(and_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3520 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 533:
#line 1560 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(equal_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3526 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 534:
#line 1561 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(notequal_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3532 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 535:
#line 1562 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_lt, parser_stack((yystack_[0].value)))); }
#line 3538 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 536:
#line 1563 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_gt, parser_stack((yystack_[0].value)))); }
#line 3544 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 537:
#line 1564 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_le, parser_stack((yystack_[0].value)))); }
#line 3550 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 538:
#line 1565 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_ge, parser_stack((yystack_[0].value)))); }
#line 3556 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 539:
#line 1566 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitor_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3562 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 540:
#line 1567 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitxor_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3568 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 541:
#line 1568 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitand_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3574 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 542:
#line 1569 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(shl_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3580 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 543:
#line 1570 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(lshr_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3586 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 544:
#line 1571 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(plus_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3592 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 545:
#line 1572 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(minus_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3598 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 546:
#line 1573 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(mult_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3604 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 547:
#line 1574 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(div_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3610 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 548:
#line 1575 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(mod_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3616 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 549:
#line 1576 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, $1, mk_none());*/ }
#line 3622 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 550:
#line 1577 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, $1, $3);*/ }
#line 3628 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 551:
#line 1578 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, mk_none(), $2);*/ }
#line 3634 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 552:
#line 1579 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, mk_none(), mk_none());*/ }
#line 3640 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 553:
#line 1580 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprCast", 2, $1, $3);*/ }
#line 3646 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 554:
#line 1581 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTypeAscr", 2, $1, $3);*/ }
#line 3652 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 555:
#line 1582 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBox", 1, $2);*/ }
#line 3658 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 558:
#line 1588 "parser.y" // lalr1.cc:859
    { (yylhs.value) = (yystack_[0].value); }
#line 3664 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 559:
#line 1589 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprPath", 1, $1);*/ }
#line 3670 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 560:
#line 1590 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprPath", 1, mk_node("ident", 1, mk_atom("self")));*/ }
#line 3676 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 561:
#line 1591 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprMac", 1, $1);*/ }
#line 3682 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 562:
#line 1592 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprStruct", 2, $1, $3);*/ }
#line 3688 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 563:
#line 1593 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTry", 1, $1);*/ }
#line 3694 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 564:
#line 1594 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprField", 2, $1, $3);*/ }
#line 3700 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 565:
#line 1595 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTupleIndex", 1, $1);*/ }
#line 3706 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 566:
#line 1596 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIndex", 2, $1, $3);*/ }
#line 3712 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 567:
#line 1597 "parser.y" // lalr1.cc:859
    { // assumes parser_stack($1) is a symbol
                                               symbol_exprt a = to_symbol_expr(parser_stack((yystack_[3].value)));
                                               code_typet::parameterst params;
                                               Forall_operands(it, parser_stack((yystack_[1].value)))
                                               {
                                                 params.push_back(code_typet::parametert((*it).type()));
                                                 params.back().add_to_operands((*it));
                                               }
                                               a.type() = code_typet(params, typet());
                                               newstack((yylhs.value)).swap(code_expressiont(
                                                                 side_effect_expr_function_callt(a,
                                                                                                 parser_stack((yystack_[1].value)).operands(),
                                                                                                 typet(),
                                                                                                 source_locationt()))); }
#line 3731 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 568:
#line 1611 "parser.y" // lalr1.cc:859
    { //TODO: assumes expressions in parentheses will reduce to a single expression. If not the case, fix this
                                               newstack((yylhs.value)).swap(parser_stack((yystack_[1].value)).op0()); }
#line 3738 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 569:
#line 1613 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprVec", 1, $2);*/ }
#line 3744 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 570:
#line 1614 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAgain", 0);*/ }
#line 3750 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 571:
#line 1615 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAgain", 1, $2);*/ }
#line 3756 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 572:
#line 1616 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRet", 0);*/ }
#line 3762 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 573:
#line 1617 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRet", 1, $2);*/ }
#line 3768 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 574:
#line 1618 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBreak", 0);*/ }
#line 3774 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 575:
#line 1619 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBreak", 1, $2);*/ }
#line 3780 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 576:
#line 1620 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprYield", 0);*/ }
#line 3786 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 577:
#line 1621 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprYield", 1, $2);*/ }
#line 3792 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 578:
#line 1622 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3798 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 579:
#line 1623 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignShl", 2, $1, $3);*/
                                               shl_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3806 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 580:
#line 1626 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignShr", 2, $1, $3);*/
                                               lshr_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3814 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 581:
#line 1629 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignSub", 2, $1, $3);*/
                                               minus_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3822 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 582:
#line 1632 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignBitAnd", 2, $1, $3);*/ 
                                               bitand_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3830 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 583:
#line 1635 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignBitOr", 2, $1, $3);*/ 
                                               bitor_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3838 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 584:
#line 1638 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignAdd", 2, $1, $3);*/
                                               plus_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3846 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 585:
#line 1641 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignMul", 2, $1, $3);*/
                                               mult_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3854 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 586:
#line 1644 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignDiv", 2, $1, $3);*/
                                               div_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3862 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 587:
#line 1647 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignBitXor", 2, $1, $3);*/ 
                                               bitxor_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3870 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 588:
#line 1650 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignRem", 2, $1, $3);*/ 
                                               mod_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                               newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 3878 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 589:
#line 1653 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(or_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3884 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 590:
#line 1654 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(and_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3890 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 591:
#line 1655 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(equal_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3896 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 592:
#line 1656 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(notequal_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3902 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 593:
#line 1657 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_lt, parser_stack((yystack_[0].value)))); }
#line 3908 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 594:
#line 1658 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_gt, parser_stack((yystack_[0].value)))); }
#line 3914 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 595:
#line 1659 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_le, parser_stack((yystack_[0].value)))); }
#line 3920 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 596:
#line 1660 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_ge, parser_stack((yystack_[0].value)))); }
#line 3926 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 597:
#line 1661 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitor_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3932 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 598:
#line 1662 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitxor_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3938 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 599:
#line 1663 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitand_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3944 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 600:
#line 1664 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(shl_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3950 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 601:
#line 1665 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(lshr_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3956 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 602:
#line 1666 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(plus_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3962 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 603:
#line 1667 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(minus_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3968 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 604:
#line 1668 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(mult_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3974 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 605:
#line 1669 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(div_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3980 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 606:
#line 1670 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(mod_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 3986 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 607:
#line 1671 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, $1, mk_none());*/ }
#line 3992 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 608:
#line 1672 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, $1, $3);*/ }
#line 3998 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 609:
#line 1673 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, mk_none(), $2);*/ }
#line 4004 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 610:
#line 1674 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, mk_none(), mk_none());*/ }
#line 4010 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 611:
#line 1675 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprCast", 2, $1, $3);*/ }
#line 4016 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 612:
#line 1676 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTypeAscr", 2, $1, $3);*/ }
#line 4022 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 613:
#line 1677 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBox", 1, $2);*/ }
#line 4028 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 618:
#line 1685 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprLit", 1, $1);*/
                                                        (yylhs.value) = (yystack_[0].value); }
#line 4035 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 619:
#line 1688 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprPath", 1, $1);*/ }
#line 4041 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 620:
#line 1689 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprPath", 1, mk_node("ident", 1, mk_atom("self")));*/ }
#line 4047 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 621:
#line 1690 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprMac", 1, $1);*/ }
#line 4053 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 622:
#line 1691 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTry", 1, $1);*/ }
#line 4059 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 623:
#line 1692 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprField", 2, $1, $3);*/ }
#line 4065 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 624:
#line 1693 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTupleIndex", 1, $1);*/ }
#line 4071 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 625:
#line 1694 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIndex", 2, $1, $3);*/ }
#line 4077 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 626:
#line 1695 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprCall", 2, $1, $3);*/ }
#line 4083 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 627:
#line 1696 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprVec", 1, $2);*/ }
#line 4089 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 628:
#line 1697 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprParen", 1, $2);*/
                                                        //TODO: assumes expressions in parentheses will reduce to a single expression. If not the case, fix this
                                                        newstack((yylhs.value)).swap(parser_stack((yystack_[1].value)).op0()); }
#line 4097 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 629:
#line 1700 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAgain", 0);*/ }
#line 4103 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 630:
#line 1701 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAgain", 1, $2);*/ }
#line 4109 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 631:
#line 1702 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRet", 0);*/ }
#line 4115 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 632:
#line 1703 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRet", 1, $2);*/ }
#line 4121 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 633:
#line 1704 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBreak", 0);*/ }
#line 4127 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 634:
#line 1705 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBreak", 1, $2);*/ }
#line 4133 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 635:
#line 1706 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprYield", 0);*/ }
#line 4139 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 636:
#line 1707 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprYield", 1, $2);*/ }
#line 4145 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 637:
#line 1708 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssign", 2, $1, $3);*/ }
#line 4151 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 638:
#line 1709 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignShl", 2, $1, $3);*/
                                                        shl_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4159 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 639:
#line 1712 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignShr", 2, $1, $3);*/
                                                        lshr_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4167 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 640:
#line 1715 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignSub", 2, $1, $3);*/
                                                        minus_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4175 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 641:
#line 1718 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignBitAnd", 2, $1, $3);*/ 
                                                        bitand_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4183 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 642:
#line 1721 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignBitOr", 2, $1, $3);*/ 
                                                        bitor_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4191 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 643:
#line 1724 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignAdd", 2, $1, $3);*/
                                                        plus_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4199 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 644:
#line 1727 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignMul", 2, $1, $3);*/
                                                        mult_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4207 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 645:
#line 1730 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignDiv", 2, $1, $3);*/
                                                        div_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4215 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 646:
#line 1733 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignBitXor", 2, $1, $3);*/ 
                                                        bitxor_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4223 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 647:
#line 1736 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAssignRem", 2, $1, $3);*/ 
                                                        mod_exprt a(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)));
                                                        newstack((yylhs.value)).swap(code_assignt(parser_stack((yystack_[2].value)), a)); }
#line 4231 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 648:
#line 1739 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(or_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4237 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 649:
#line 1740 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(and_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4243 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 650:
#line 1741 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(equal_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4249 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 651:
#line 1742 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(notequal_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4255 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 652:
#line 1743 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_lt, parser_stack((yystack_[0].value)))); }
#line 4261 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 653:
#line 1744 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_gt, parser_stack((yystack_[0].value)))); }
#line 4267 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 654:
#line 1745 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_le, parser_stack((yystack_[0].value)))); }
#line 4273 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 655:
#line 1746 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(binary_relation_exprt(parser_stack((yystack_[2].value)), ID_ge, parser_stack((yystack_[0].value)))); }
#line 4279 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 656:
#line 1747 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitor_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4285 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 657:
#line 1748 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitxor_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4291 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 658:
#line 1749 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitand_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4297 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 659:
#line 1750 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(shl_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4303 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 660:
#line 1751 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(lshr_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4309 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 661:
#line 1752 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(plus_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4315 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 662:
#line 1753 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(minus_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4321 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 663:
#line 1754 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(mult_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4327 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 664:
#line 1755 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(div_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4333 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 665:
#line 1756 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(mod_exprt(parser_stack((yystack_[2].value)), parser_stack((yystack_[0].value)))); }
#line 4339 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 666:
#line 1757 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, $1, mk_none());*/ }
#line 4345 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 667:
#line 1758 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, $1, $3);*/ }
#line 4351 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 668:
#line 1759 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, mk_none(), $2);*/ }
#line 4357 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 669:
#line 1760 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprRange", 2, mk_none(), mk_none());*/ }
#line 4363 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 670:
#line 1761 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprCast", 2, $1, $3);*/ }
#line 4369 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 671:
#line 1762 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTypeAscr", 2, $1, $3);*/ }
#line 4375 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 672:
#line 1763 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprBox", 1, $2);*/ }
#line 4381 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 677:
#line 1771 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(unary_minus_exprt(parser_stack((yystack_[0].value)), parser_stack((yystack_[0].value)).type())); }
#line 4387 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 678:
#line 1772 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitnot_exprt(parser_stack((yystack_[0].value)))); }
#line 4393 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 679:
#line 1773 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(dereference_exprt(parser_stack((yystack_[0].value)))); }
#line 4399 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 680:
#line 1774 "parser.y" // lalr1.cc:859
    { //TODO: handle maybe_mut if necessary. It might not be: the actual Rust compiler should stop altering of non-mut things without CBMC
                                              newstack((yylhs.value)).swap(address_of_exprt(parser_stack((yystack_[1].value)))); }
#line 4406 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 681:
#line 1776 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAddrOf", 1, mk_node("ExprAddrOf", 2, $2, $3));*/ }
#line 4412 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 683:
#line 1778 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 4418 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 684:
#line 1782 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(unary_minus_exprt(parser_stack((yystack_[0].value)), parser_stack((yystack_[0].value)).type())); }
#line 4424 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 685:
#line 1783 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(bitnot_exprt(parser_stack((yystack_[0].value)))); }
#line 4430 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 686:
#line 1784 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(dereference_exprt(parser_stack((yystack_[0].value)))); }
#line 4436 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 687:
#line 1785 "parser.y" // lalr1.cc:859
    { //TODO: handle maybe_mut if necessary. It might not be: the actual Rust compiler should stop altering of non-mut things without CBMC
                                     newstack((yylhs.value)).swap(address_of_exprt(parser_stack((yystack_[1].value)))); }
#line 4443 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 688:
#line 1787 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprAddrOf", 1, mk_node("ExprAddrOf", 2, $2, $3));*/ }
#line 4449 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 690:
#line 1789 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 4455 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 691:
#line 1794 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ExprQualifiedPath", 4, $2, $3, $6, $7);*/
}
#line 4463 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 692:
#line 1798 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ExprQualifiedPath", 3, mk_node("ExprQualifiedPath", 3, $2, $3, $6), $7, $10);*/
}
#line 4471 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 693:
#line 1802 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ExprQualifiedPath", 3, mk_node("ExprQualifiedPath", 4, $2, $3, $6, $7), $8, $11);*/
}
#line 4479 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 694:
#line 1806 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ExprQualifiedPath", 4, mk_node("ExprQualifiedPath", 3, $2, $3, $6), $7, $10, $11);*/
}
#line 4487 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 695:
#line 1810 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("ExprQualifiedPath", 4, mk_node("ExprQualifiedPath", 4, $2, $3, $6, $7), $8, $11, $12);*/
}
#line 4495 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 696:
#line 1815 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 4501 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 697:
#line 1816 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 4507 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 698:
#line 1820 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 4513 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 699:
#line 1821 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 4519 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 700:
#line 1826 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, mk_none(), $2, $3);*/ }
#line 4525 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 701:
#line 1828 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, mk_none(), $3, $4);*/ }
#line 4531 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 702:
#line 1830 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, $2, $4, $5);*/ }
#line 4537 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 703:
#line 1832 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, $2, mk_none(), $4);*/ }
#line 4543 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 704:
#line 1837 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, mk_none(), $2, $3);*/ }
#line 4549 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 705:
#line 1839 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, $1, $3, $4);*/ }
#line 4555 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 706:
#line 1841 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, $1, mk_none(), $3);*/ }
#line 4561 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 707:
#line 1846 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 2, mk_none(), $2);*/ }
#line 4567 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 708:
#line 1848 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, mk_none(), $3, $4);*/ }
#line 4573 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 709:
#line 1850 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 2, $2, $4);*/ }
#line 4579 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 710:
#line 1852 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, $2, mk_none(), $4);*/ }
#line 4585 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 711:
#line 1857 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, mk_none(), $2, $3);*/ }
#line 4591 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 712:
#line 1859 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, $1, $3, $4);*/ }
#line 4597 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 713:
#line 1861 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprFnBlock", 3, $1, mk_none(), $3);*/ }
#line 4603 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 715:
#line 1866 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("VecRepeat", 2, $1, $3);*/ }
#line 4609 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 718:
#line 1872 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 4615 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 719:
#line 1873 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 4621 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 722:
#line 1879 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 4627 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 723:
#line 1883 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("FieldInits", 1, $1);*/ }
#line 4633 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 724:
#line 1884 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $3);*/ }
#line 4639 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 725:
#line 1888 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("FieldInit", 1, $1);*/ }
#line 4645 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 726:
#line 1889 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("FieldInit", 2, $1, $3);*/ }
#line 4651 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 727:
#line 1890 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("FieldInit", 2, mk_atom(yyrusttext), $3);*/ }
#line 4657 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 728:
#line 1894 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("DefaultFieldInit", 1, $2);*/ }
#line 4663 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 736:
#line 1905 "parser.y" // lalr1.cc:859
    { //TODO_unsafe: can unsafe be ignored? maybe.
                                                           (yylhs.value) = (yystack_[0].value); }
#line 4670 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 737:
#line 1907 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Macro", 3, $1, $3, $4);*/ }
#line 4676 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 740:
#line 1916 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprField", 2, $1, $3);*/ }
#line 4682 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 741:
#line 1917 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprField", 2, $1, $3);*/ }
#line 4688 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 742:
#line 1918 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIndex", 3, $1, $3, $5);*/ }
#line 4694 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 743:
#line 1919 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIndex", 3, $1, $3, $5);*/ }
#line 4700 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 744:
#line 1920 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprCall", 3, $1, $3, $5);*/ }
#line 4706 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 745:
#line 1921 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprCall", 3, $1, $3, $5);*/ }
#line 4712 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 746:
#line 1922 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTupleIndex", 1, $1);*/ }
#line 4718 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 747:
#line 1923 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprTupleIndex", 1, $1);*/ }
#line 4724 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 748:
#line 1927 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprMatch", 1, $2);*/ }
#line 4730 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 749:
#line 1928 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprMatch", 2, $2, $4);*/ }
#line 4736 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 750:
#line 1929 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprMatch", 2, $2, ext_node($4, 1, $5));*/ }
#line 4742 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 751:
#line 1930 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprMatch", 2, $2, mk_node("Arms", 1, $4));*/ }
#line 4748 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 752:
#line 1934 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("Arms", 1, $1);*/ }
#line 4754 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 753:
#line 1935 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/ }
#line 4760 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 757:
#line 1945 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ArmNonblock", 4, $1, $2, $3, $5);*/ }
#line 4766 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 758:
#line 1946 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ArmNonblock", 4, $1, $2, $3, $5);*/ }
#line 4772 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 759:
#line 1950 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ArmBlock", 4, $1, $2, $3, $5);*/ }
#line 4778 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 760:
#line 1951 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ArmBlock", 4, $1, $2, $3, $5);*/ }
#line 4784 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 761:
#line 1955 "parser.y" // lalr1.cc:859
    { /*{o}$$ = $2;*/ }
#line 4790 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 762:
#line 1956 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 4796 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 763:
#line 1960 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIf", 2, $2, $3);*/
                                              newstack((yylhs.value)).swap(code_ifthenelset(parser_stack((yystack_[1].value)), to_code(parser_stack((yystack_[0].value))))); }
#line 4803 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 764:
#line 1962 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIf", 3, $2, $3, $5);*/
                                              newstack((yylhs.value)).swap(code_ifthenelset(parser_stack((yystack_[3].value)), to_code(parser_stack((yystack_[2].value))), to_code(parser_stack((yystack_[0].value))))); }
#line 4810 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 765:
#line 1967 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIfLet", 3, $3, $5, $6);*/ }
#line 4816 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 766:
#line 1968 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprIfLet", 4, $3, $5, $6, $8);*/ }
#line 4822 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 770:
#line 1978 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprWhile", 3, $1, $3, $4);*/
                                                        newstack((yylhs.value)).swap(code_whilet(parser_stack((yystack_[1].value)), to_code(parser_stack((yystack_[0].value)))));  }
#line 4829 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 771:
#line 1983 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprWhileLet", 4, $1, $4, $6, $7);*/ }
#line 4835 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 772:
#line 1987 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprLoop", 2, $1, $3);*/
                                                        newstack((yylhs.value)).swap(code_whilet(true_exprt(), to_code(parser_stack((yystack_[0].value))))); }
#line 4842 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 773:
#line 1992 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ExprForLoop", 4, $1, $3, $5, $6);*/ }
#line 4848 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 775:
#line 1997 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_none();*/ }
#line 4854 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 776:
#line 2002 "parser.y" // lalr1.cc:859
    {
    /*{o}$$ = mk_node("DeclLocal", 3, $2, $3, $4);*/
    
    if(parser_stack((yystack_[1].value)).id() != "ID_emptyinitexpr") // if given assignment expression
    {
      codet declblock(ID_decl_block);
      codet decl(ID_decl);

      // if type ascription given
      if(parser_stack((yystack_[2].value)).id() != "ID_emptytyascript") 
      {
        irep_idt symbolName(to_symbol_expr(parser_stack((yystack_[3].value))).get_identifier());
        irep_idt typeName(to_symbol_expr(parser_stack((yystack_[2].value))).get_identifier());
        symbol_exprt symbol(symbol_exprt(symbolName, rusttype_to_ctype(typeName)));

        decl.add_to_operands(symbol);
      }
      else // no type ascription given
      {
        decl.add_to_operands(parser_stack((yystack_[3].value)));
      }

      // if assignment is a code expression
      if(parser_stack((yystack_[1].value)).id() == ID_code) 
      {
        codet& code = to_code(parser_stack((yystack_[1].value)));
        irep_idt statement = code.get_statement();
        side_effect_exprt rhs(ID_statement_expression, exprt::operandst(), typet(), source_locationt());
        if(statement == ID_block)
        {
          rhs.add_to_operands(to_code_block(code));
        }
        // if already a wrapped side effect(namely function calls)
        else if(statement == ID_expression && code.op0().id() == ID_side_effect)
        {
          rhs = to_side_effect_expr(code.op0());
        }
        else // general case
        {
          code_blockt wrapper_block;
          wrapper_block.add_to_operands(code);
          rhs.add_to_operands(wrapper_block);
        }
        code_assignt assignment(parser_stack((yystack_[3].value)), rhs);
        declblock.add_to_operands(decl);
        declblock.add_to_operands(assignment);
      }
      else
      {
        code_assignt assignment(parser_stack((yystack_[3].value)), parser_stack((yystack_[1].value)));
        declblock.add_to_operands(decl);
        declblock.add_to_operands(assignment);
      }
      
      newstack((yylhs.value)).swap(declblock);
    }
    else // if assignment was not specified, type must have been
    {
      irep_idt symbolName(to_symbol_expr(parser_stack((yystack_[3].value))).get_identifier());
      irep_idt typeName(to_symbol_expr(parser_stack((yystack_[2].value))).get_identifier());
      symbol_exprt symbol(symbol_exprt(symbolName, rusttype_to_ctype(typeName)));
      code_declt decl(symbol);
      newstack((yylhs.value)).swap(decl);
    }
  }
#line 4924 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 777:
#line 2074 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitByte", 1, mk_atom(yyrusttext));*/ }
#line 4930 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 778:
#line 2075 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitChar", 1, mk_atom(yyrusttext));*/
                  constant_exprt a(yyrusttext, char_type());
                  newstack((yylhs.value)).swap(a); }
#line 4938 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 779:
#line 2078 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(from_integer(string2integer(yyrusttext), unsigned_int_type())); }
#line 4944 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 780:
#line 2079 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitFloat", 1, mk_atom(yyrusttext));*/
                  parse_floatt parsed_float(yyrusttext);
                  floatbv_typet type = float_type();
                  ieee_floatt a(type);
                  a.from_base10(parsed_float.significand, parsed_float.exponent);
                  constant_exprt result = a.to_expr();
                  result.type() = type;
                  newstack((yylhs.value)).swap(result); }
#line 4957 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 781:
#line 2087 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitBool", 1, mk_atom(yyrusttext));*/
                  newstack((yylhs.value)).swap(true_exprt()); }
#line 4964 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 782:
#line 2089 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitBool", 1, mk_atom(yyrusttext));*/
                  newstack((yylhs.value)).swap(false_exprt()); }
#line 4971 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 784:
#line 2095 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitStr", 1, mk_atom(yyrusttext), mk_atom("CookedStr"));*/ }
#line 4977 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 785:
#line 2096 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitStr", 1, mk_atom(yyrusttext), mk_atom("RawStr"));*/ }
#line 4983 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 786:
#line 2097 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitByteStr", 1, mk_atom(yyrusttext), mk_atom("ByteStr"));*/ }
#line 4989 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 787:
#line 2098 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("LitByteStr", 1, mk_atom(yyrusttext), mk_atom("RawByteStr"));*/ }
#line 4995 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 788:
#line 2102 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty("")); }
#line 5001 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 790:
#line 2107 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5007 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 791:
#line 2109 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ident", 1, mk_atom(yyrusttext));*/ }
#line 5013 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 792:
#line 2110 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ident", 1, mk_atom(yyrusttext));*/ }
#line 5019 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 793:
#line 2111 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("ident", 1, mk_atom(yyrusttext));*/ }
#line 5025 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 794:
#line 2116 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_shl))); }
#line 5031 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 795:
#line 2117 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_shr))); }
#line 5037 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 796:
#line 2118 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_le))); }
#line 5043 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 797:
#line 2119 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_equal))); }
#line 5049 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 798:
#line 2120 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_notequal))); }
#line 5055 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 799:
#line 2121 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_ge))); }
#line 5061 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 800:
#line 2122 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_and))); }
#line 5067 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 801:
#line 2123 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_or))); }
#line 5073 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 802:
#line 2124 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5079 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 803:
#line 2125 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5085 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 804:
#line 2126 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5091 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 805:
#line 2127 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5097 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 806:
#line 2128 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5103 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 807:
#line 2129 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5109 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 808:
#line 2130 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5115 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 809:
#line 2131 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5121 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 810:
#line 2132 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5127 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 811:
#line 2133 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5133 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 812:
#line 2134 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5139 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 813:
#line 2135 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5145 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 814:
#line 2136 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5151 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 815:
#line 2137 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5157 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 816:
#line 2138 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5163 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 817:
#line 2139 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5169 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 818:
#line 2140 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5175 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 819:
#line 2141 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, char_type())); }
#line 5181 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 820:
#line 2142 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, signed_int_type())); }
#line 5187 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 821:
#line 2143 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, float_type())); }
#line 5193 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 822:
#line 2144 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5199 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 823:
#line 2145 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5205 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 824:
#line 2146 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5211 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 825:
#line 2147 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5217 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 826:
#line 2148 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5223 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 827:
#line 2149 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5229 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 828:
#line 2150 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5235 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 829:
#line 2151 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5241 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 830:
#line 2152 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5247 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 831:
#line 2153 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5253 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 832:
#line 2154 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5259 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 833:
#line 2155 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5265 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 834:
#line 2156 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5271 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 835:
#line 2157 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, c_bool_type()));  }
#line 5277 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 836:
#line 2158 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5283 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 837:
#line 2159 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5289 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 838:
#line 2160 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5295 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 839:
#line 2161 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5301 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 840:
#line 2162 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5307 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 841:
#line 2163 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5313 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 842:
#line 2164 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5319 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 843:
#line 2165 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, c_bool_type())); }
#line 5325 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 844:
#line 2166 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5331 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 845:
#line 2167 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5337 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 846:
#line 2168 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5343 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 847:
#line 2169 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5349 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 848:
#line 2170 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5355 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 849:
#line 2171 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5361 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 850:
#line 2172 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5367 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 851:
#line 2173 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext)); }
#line 5373 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 852:
#line 2174 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5379 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 853:
#line 2175 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5385 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 854:
#line 2176 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5391 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 855:
#line 2177 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5397 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 856:
#line 2178 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5403 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 857:
#line 2179 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5409 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 858:
#line 2180 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5415 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 859:
#line 2181 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5421 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 860:
#line 2182 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5427 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 861:
#line 2183 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5433 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 862:
#line 2184 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5439 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 863:
#line 2185 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5445 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 864:
#line 2186 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5451 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 865:
#line 2187 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5457 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 866:
#line 2188 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5463 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 867:
#line 2189 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, c_bool_type())); }
#line 5469 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 868:
#line 2190 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5475 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 869:
#line 2191 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5481 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 870:
#line 2192 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5487 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 871:
#line 2193 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5493 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 872:
#line 2194 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5499 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 873:
#line 2195 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5505 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 874:
#line 2196 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5511 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 875:
#line 2197 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt_typeless_empty(yyrusttext));  }
#line 5517 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 876:
#line 2198 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5523 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 877:
#line 2199 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5529 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 878:
#line 2200 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5535 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 879:
#line 2201 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5541 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 880:
#line 2202 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5547 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 881:
#line 2203 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5553 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 882:
#line 2204 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5559 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 883:
#line 2205 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5565 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 884:
#line 2206 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5571 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 885:
#line 2207 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5577 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 886:
#line 2208 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5583 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 887:
#line 2209 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5589 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 888:
#line 2210 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5595 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 889:
#line 2211 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5601 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 890:
#line 2212 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5607 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 891:
#line 2213 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5613 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 892:
#line 2214 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5619 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 893:
#line 2215 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5625 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 894:
#line 2216 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5631 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 895:
#line 2217 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5637 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 896:
#line 2218 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_atom(yyrusttext);*/ }
#line 5643 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 897:
#line 2219 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_bitnot))); }
#line 5649 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 898:
#line 2220 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_lt))); }
#line 5655 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 899:
#line 2221 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_gt))); }
#line 5661 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 900:
#line 2222 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_minus))); }
#line 5667 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 901:
#line 2223 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_bitand))); }
#line 5673 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 902:
#line 2224 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_bitor))); }
#line 5679 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 903:
#line 2225 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_plus))); }
#line 5685 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 904:
#line 2226 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_mult))); }
#line 5691 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 905:
#line 2227 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_div))); }
#line 5697 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 906:
#line 2228 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_bitxor))); }
#line 5703 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 907:
#line 2229 "parser.y" // lalr1.cc:859
    { newstack((yylhs.value)).swap(symbol_exprt(yyrusttext, typet(ID_mod))); }
#line 5709 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 908:
#line 2233 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TokenTrees", 0);*/
                               multi_ary_exprt empty("ID_token_tree", exprt::operandst(), typet());
                               newstack((yylhs.value)).swap(empty); }
#line 5717 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 909:
#line 2236 "parser.y" // lalr1.cc:859
    { /*{o}$$ = ext_node($1, 1, $2);*/
                               multi_ary_exprt tokTree = to_multi_ary_expr(parser_stack((yystack_[1].value)));
                               tokTree.add_to_operands(parser_stack((yystack_[0].value)));
                               newstack((yylhs.value)).swap(tokTree); }
#line 5726 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 911:
#line 2244 "parser.y" // lalr1.cc:859
    { /*{o}$$ = mk_node("TTTok", 1, $1);*/
                           (yylhs.value) = (yystack_[0].value); }
#line 5733 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 915:
#line 2256 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("TTDelim", 3, mk_node("TTTok", 1, mk_atom("(")), $2, mk_node("TTTok", 1, mk_atom(")")));*/
  multi_ary_exprt parenthesized("ID_token_tree", exprt::operandst(), typet());
  parenthesized.add_to_operands(symbol_exprt_typeless_empty("("));
  parenthesized.add_to_operands(parser_stack((yystack_[1].value)));
  parenthesized.add_to_operands(symbol_exprt_typeless_empty(")"));
  newstack((yylhs.value)).swap(parenthesized);
}
#line 5746 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 916:
#line 2268 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("TTDelim", 3, mk_node("TTTok", 1, mk_atom("{")), $2, mk_node("TTTok", 1, mk_atom("}")));*/
}
#line 5754 "rust_y.tab.cpp" // lalr1.cc:859
    break;

  case 917:
#line 2275 "parser.y" // lalr1.cc:859
    {
  /*{o}$$ = mk_node("TTDelim", 3, mk_node("TTTok", 1, mk_atom("[")), $2, mk_node("TTTok", 1, mk_atom("]")));*/
}
#line 5762 "rust_y.tab.cpp" // lalr1.cc:859
    break;


#line 5766 "rust_y.tab.cpp" // lalr1.cc:859
            default:
              break;
            }
        }
      catch (const syntax_error& yyexc)
        {
          error (yyexc);
          YYERROR;
        }
      YY_SYMBOL_PRINT ("-> $$ =", yylhs);
      yypop_ (yylen);
      yylen = 0;
      YY_STACK_PRINT ();

      // Shift the result of the reduction.
      yypush_ (YY_NULLPTR, yylhs);
    }
    goto yynewstate;

  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        error (yysyntax_error_ (yystack_[0].state, yyla));
      }


    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.type_get () == yyeof_)
          YYABORT;
        else if (!yyla.empty ())
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyla.clear ();
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:

    /* Pacify compilers like GCC when the user code never invokes
       YYERROR and the label yyerrorlab therefore never appears in user
       code.  */
    if (false)
      goto yyerrorlab;
    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    goto yyerrlab1;

  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    {
      stack_symbol_type error_token;
      for (;;)
        {
          yyn = yypact_[yystack_[0].state];
          if (!yy_pact_value_is_default_ (yyn))
            {
              yyn += yyterror_;
              if (0 <= yyn && yyn <= yylast_ && yycheck_[yyn] == yyterror_)
                {
                  yyn = yytable_[yyn];
                  if (0 < yyn)
                    break;
                }
            }

          // Pop the current state because it cannot handle the error token.
          if (yystack_.size () == 1)
            YYABORT;

          yy_destroy_ ("Error: popping", yystack_[0]);
          yypop_ ();
          YY_STACK_PRINT ();
        }


      // Shift the error token.
      error_token.state = yyn;
      yypush_ ("Shifting", error_token);
    }
    goto yynewstate;

    // Accept.
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;

    // Abort.
  yyabortlab:
    yyresult = 1;
    goto yyreturn;

  yyreturn:
    if (!yyla.empty ())
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack"
                 << std::endl;
        // Do not try to display the values of the reclaimed symbols,
        // as their printer might throw an exception.
        if (!yyla.empty ())
          yy_destroy_ (YY_NULLPTR, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULLPTR, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
  }

  void
  parser::error (const syntax_error& yyexc)
  {
    error (yyexc.what());
  }

  // Generate an error message.
  std::string
  parser::yysyntax_error_ (state_type, const symbol_type&) const
  {
    return YY_("syntax error");
  }


  const short int parser::yypact_ninf_ = -1336;

  const short int parser::yytable_ninf_ = -793;

  const short int
  parser::yypact_[] =
  {
      21, -1336,   177,   111, -1336, -1336, -1336,   182,   193,   111,
   -1336,   254,    26, -1336, -1336,    92,  2942, -1336,  1211,  1211,
   -1336, -1336, -1336, -1336, -1336, -1336,   856, -1336,   326,  1089,
   -1336,  1211,   990,  1211,  1211,  1211, -1336,  1211,  1211,   765,
     361,   630,   766, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,   354,
     413, -1336, -1336, -1336,   352,   456, -1336, -1336, -1336,   380,
      47,   431,   456,   856,  1211,   419,   465, -1336, -1336, -1336,
   -1336,  1211,   231, -1336,   465,   305,   465,   465,   465,   773,
    1211, -1336,   839, -1336, -1336, -1336,   446,   502,    94, -1336,
    1211,   537,   540,  1211,   465,  1211,   264, -1336,  2008,  1211,
   -1336,   456,   565,  8929,   960,   546,    59,   601,    30, -1336,
     551,    33, -1336,    24,   546,   546,   643,   465, -1336, -1336,
   -1336,   603, -1336, -1336, -1336,   237, -1336, -1336, -1336,   635,
    1211,   465,  1211,  8929,   465,  1701,   569, -1336,  8634, -1336,
    8634, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336,   220,  8929,  8634,  8250,   608,  1211, -1336,   694,
     773,   465,   620,   125,  8929,   612,  8634,  8811,  8314,    49,
    8929,  1822,   174,   634, -1336, -1336, -1336, -1336,   105,   685,
   -1336, -1336,    84, -1336,    89, -1336,   397,   734,   682, -1336,
    1211, -1336,    30,   444,   690,   288, -1336,  7986,   546,   800,
      33,   713,   494,   546,   457,   728,   743,  1211,   551, -1336,
     318,  1211,   703, -1336, -1336,   757, -1336, -1336,   551,   465,
     770,   829,  1211, -1336,   395, -1336,   574,  8929,   281, -1336,
      25, -1336, -1336,   849, -1336, -1336, -1336,   792, -1336,   809,
   -1336,   258,   821,    52, -1336, -1336,   286, -1336,   824,   787,
     830, -1336,   142, -1336,   392,   833,    58,  8929, -1336,   890,
     576,   800,   253,  1211,   902,   842, -1336,    83,   608,   773,
     465,  8811, -1336,  7142,    58,  8378,   858,  1211,  8442,   252,
   -1336,   901, -1336,   134,  8929, -1336,   890, -1336, -1336,  8929,
     918, -1336,   303,  1211,  1211,  7142,  8634, -1336,   255, -1336,
   -1336, -1336,   309, -1336, -1336,   360,  1107,  1211,   897,   903,
     885, -1336,  8634,   358,   895,   889,   890,  1211,   972, -1336,
   -1336, -1336,  8634,  7986, -1336, -1336,   962,  7986,  8634,  8049,
    2008,  7455,   599,   906,   909, -1336,   929,  1211,  1011,   542,
   -1336,   914,   930,   450, -1336,   922, -1336,  8634,   625, -1336,
     925,   469, -1336, -1336,   469,  8634,   465,   546,   788, -1336,
   -1336, -1336, -1336, -1336,   368,   546,   551,  7142,   697,   950,
     289,  1211,  1028,   999,   936,  2536,   944,  8506,  4582,  4759,
    5033, -1336, -1336, -1336, -1336, -1336, -1336,  8634, -1336,   574,
    8634, -1336, -1336, -1336, -1336,  8634,  1211,  8929, -1336, -1336,
    7142,   574,   955, -1336, -1336,  8929,   952, -1336, -1336, -1336,
   -1336,  1211,  1028,   465,  5458,   800,   957,   945,   800,  1018,
   -1336,   311,  8634,   890,   800,  6602,   326,  1211,  6926,  7250,
      79,  6818,   959,  6818,  1211,  7142,  8634,  7518,   890,  7142,
    7142,  7142,  4886,  5665,  5370,   977, -1336,   490, -1336,  4180,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336,   102, -1336,   974,   984,   136, -1336,   992,   148,   608,
    8811,  8929, -1336,  8929, -1336, -1336,  1071,  7142, -1336,  3193,
     126,   569,  2414,   985,   986,  1001, -1336, -1336, -1336, -1336,
   -1336, -1336,    98, -1336,  1004,  1610, -1336,  1003, -1336, -1336,
     950,  8634, -1336,    83,   409,  1013,  1024,  1211,   422, -1336,
   -1336, -1336, -1336,  1211,   465, -1336,    58, -1336, -1336, -1336,
      58,  7986, -1336, -1336,  1006, -1336,  1008,    74,  1007, -1336,
   -1336,  1016,    97, -1336,  7986,  8634,  1000,  3625,  1211,  1891,
    2170,  7986,   553, -1336, -1336, -1336, -1336,   787, -1336,   185,
   -1336,  1211,   555, -1336,   579,   394,   551,   930, -1336,   835,
   -1336,   930,   546,  2653,  1211,  1037,   546,  1028,  8570,   546,
   -1336,   553,  1026,   295, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336, -1336, -1336, -1336, -1336,  1019,  1021,  1042,
   -1336,   787,  1021, -1336,  1046, -1336,  3337, -1336,  1126, -1336,
     576,   569, -1336, -1336,  1032, -1336,  1615,    83, -1336,   465,
     608,    58,  7142,  7142,  6262, -1336,   890,  7250,  6710,   326,
    1211,  7986,    93,  6818,  6818,  1211,  7142,  7581,   890,  7250,
    7250,  7250,  5665,  5370, -1336,  1044, -1336,  5961, -1336, -1336,
   -1336, -1336, -1336,  6032, -1336,  6421, -1336,  6421, -1336,  6306,
      58,   800,   149, -1336,  1051,  7142,  1005,   645,   645,  6602,
     326,   255,  1972,  6818,   498,  6818,   255,  7142,  5665,  5370,
   -1336,    41,  5557, -1336,  1038,  5154, -1336,   755, -1336,  3523,
   -1336, -1336,  1050, -1336,  1052, -1336, -1336,   706,  6076,  1054,
    1053,  1041, -1336,  1211,   742,  7142,  7142,  7142,  7142,  7142,
    7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,
    7142,  7142,  7142,  6602,  8929,  8929, -1336,  7142,  7142,  7142,
    7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,  5773,  5370,
    1130, -1336,  7986,   959,  7034,  1148,   800, -1336, -1336, -1336,
    7142,  5890, -1336,  8634, -1336, -1336, -1336,   800,  8634,   255,
   -1336, -1336,   351,    83,  1073, -1336,  1107,   897,   787,   317,
   -1336,  1107,   576,   834,   381, -1336,  1078,  1062,  1085,  1090,
   -1336,  7986, -1336,  1058,  7644,  7986, -1336,  1084,  7707, -1336,
     787,  7986, -1336, -1336,   569, -1336,  1096,   841,  1211, -1336,
    1087,  1083,  1101, -1336,  1093, -1336,  4886,   553, -1336, -1336,
    1112,   423, -1336,   619, -1336, -1336,   546, -1336, -1336, -1336,
     930, -1336, -1336, -1336,  1108,  1110,   295,  1111,   689,  1102,
    1113,  8634, -1336,  1211,  1206, -1336,  1211, -1336, -1336,  8693,
    1109, -1336, -1336, -1336, -1336, -1336,   800,  1147,  2001,  6076,
    7250,  6147,  6377, -1336,  1151, -1336,  6421,  6421, -1336,  6306,
     800,   155,  7250,  1160,   772,   772,  1138,  1139,  7250,  7250,
    7250,  7250,  7250,  7250,  7250,  7250,  7250,  7250,  7250,  7250,
    7250,  7250,  7250,  7250,  7250,  7250,  9137,  8929,  8929, -1336,
    7250,  7250,  7250,  7250,  7250,  7250,  7250,  7250,  7250,  7250,
    7250,  5773,  5370,  1213,  1214,   710,  1155,  7142,  7770,   800,
    7986,  8634, -1336,   978,  6262, -1336,  1211,  1181, -1336,  6421,
    6421, -1336,  6306,  1146,  1145,  7986, -1336, -1336,  1972, -1336,
     794,  4015, -1336, -1336,  3523,   742,  7142,  7142,  7142,  7142,
    7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,
    7142,  7142,  7142,  7142,  6602,  8929,  8929, -1336,  7142,  7142,
    7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,  7142,  5773,
    5370,  1494, -1336,  1568,  1667,  5262,  7142, -1336, -1336,   569,
    1174,  1156,  1258,   121, -1336,  1182,   900,   900,  1260,   749,
     749,  1260,  2001,  1212,  6421,  6421,  6421,  6421,  6421,  6421,
    6421,  6421,  6421,  6421,  6262, -1336, -1336,  6421,  1260,  1260,
     467,   658,   978,  1005,  1005,   645,   645,   645,  1164,  6076,
    1163, -1336,   456,  1228, -1336,  7986,  5961,  1211, -1336,  5909,
   -1336,  1168, -1336, -1336, -1336, -1336,  1186,    83, -1336, -1336,
   -1336,    83, -1336,   952,  7142,   469,  8634, -1336, -1336, -1336,
    8929,  2826,   546,   800,  1268,  1270,  1173,  7986,  1175, -1336,
    1176,  7986,  1178, -1336, -1336, -1336,  7986,  1211, -1336,  1205,
    1912, -1336,  7986, -1336,  1183,  8634, -1336, -1336, -1336,   930,
   -1336,   715,  1184,  1195,   553,  1027, -1336,  1187, -1336,   723,
   -1336, -1336, -1336,   553,  1211,  1289, -1336,  1032, -1336, -1336,
    1287,  2633,  7250,  7250,  7833,  7250,  1076, -1336, -1336,   988,
     988,  1322,  1128,  1128,  1322,  2633,  2263,  6492,  6492,  6492,
    6492,  6492,  6492,  6492,  6492,  6492,  6492,  6377, -1336, -1336,
    6492,  1322,  1322,   582,  1572,  1076,  1160,  1160,   772,   772,
     772,  1193,  1218, -1336,   456,   238, -1336,  7986,   774, -1336,
     602,  1219,  1310,  6076,   800,   158, -1336,  7142, -1336,   787,
   -1336, -1336,  1051, -1336,  1211, -1336,  1220,   900,   900,  1260,
     749,   749,  1260,  2001,  1212,  6421,  6421,  6421,  6421,  6421,
    6421,  6421,  6421,  6421,  6421,  6262, -1336, -1336,  6421,  1260,
    1260,   467,   658,   978,  1005,  1005,   645,   645,   645,  1223,
    1224, -1336,   456, -1336,   284, -1336,   291,  6076,  6076, -1336,
   -1336, -1336,  7142, -1336,  7142, -1336,  1474,  7142, -1336, -1336,
    7250,  1244, -1336, -1336, -1336,   800,   576,  1245,  6076,   659,
    1227,  1229, -1336,   799,  1230, -1336,  1211,  1211,  7986,  1232,
    7986,  7986,  1234,  7986, -1336, -1336,  7986, -1336, -1336, -1336,
   -1336,   787, -1336,  1375,  1226,   803, -1336, -1336, -1336, -1336,
   -1336, -1336, -1336,   553,   689,   260, -1336,   447, -1336, -1336,
     689,  1243,  1253, -1336,  1211,  5961,  6147,   800,   160, -1336,
    6147, -1336, -1336, -1336, -1336, -1336, -1336, -1336,    66, -1336,
   -1336,   726, -1336, -1336, -1336,  1211,  7142,  7770,   800,  6076,
    1256,   810, -1336, -1336, -1336,  5773,  5370,  5773,  5370,  6076,
    6076, -1336,  6076,  5961,  7250, -1336, -1336,   576,   813, -1336,
   -1336, -1336, -1336,  4297, -1336,  1289, -1336,  7986,  1237,  7986,
    1239, -1336,  1211, -1336,   636, -1336,   489, -1336, -1336,   689,
    1238,  1211,   636, -1336,   511,  1249,  1107,  1351,   269,  1330,
    7250,  7833,   800,  7250,  7986,  1353, -1336,  1357,  6076, -1336,
    7142,  7142,  1259,  1262,  1264,  1267,  1269, -1336,  5961, -1336,
   -1336,  1271, -1336,  1284,  7986,  7986,  1266,  1341,  1051,   773,
    1211,  1272, -1336,   465,  1342,   773,  1211, -1336, -1336,  1211,
    1289,  1293,   238,  6147, -1336,  7250,  6147, -1336,  7358,   166,
   -1336,  6076,  6076, -1336, -1336, -1336, -1336, -1336, -1336, -1336,
    1376, -1336,  1211,  1298,  1348,   465, -1336,  1300,  1211,  1350,
     465, -1336,  1303,  1381, -1336,  6147, -1336,  6191,  1050,  1052,
   -1336,  1211,   465,  7142,  1286,  1211,  1296,  8634,   465,  1211,
    1299,  1391,  1211, -1336,  1296,  6076, -1336,   465,  8870,   546,
     800,   399,  1299,   465,  8112,   546,   800,  1211,   166,   546,
    1296,  1211,  8752,  1297,  1302,  1384,   482, -1336, -1336,   546,
    1299,  1387,  7896,  1305,  1397,   930, -1336,   166, -1336,   484,
     546,  9047,  1398,   890, -1336,  8811,  1051, -1336, -1336,   930,
     546,  8175,  1402,   890, -1336,  1051, -1336, -1336, -1336, -1336,
     487,  1051,  8988,  1319, -1336,   930,  1051,  1404,  1321, -1336,
   -1336,  1319,   272,  8811,  1325, -1336,  1321,  1051,  7986,  1331,
    1336,  1319,  1329, -1336,  1340,  1321,  1344, -1336, -1336,  1352,
    8811, -1336,  1359,  7986, -1336, -1336
  };

  const unsigned short int
  parser::yydefact_[] =
  {
       5,     4,     0,    13,     1,    11,    17,     0,     0,    13,
       8,   127,    12,    14,     3,    13,   151,    27,     0,     0,
       9,     2,   126,    29,    15,    28,     0,   790,     0,     0,
     791,     0,   111,     0,     0,     0,   494,   793,     0,   150,
     792,     0,     0,    30,    31,    33,    34,    32,    37,    45,
      44,    36,    68,    69,    70,    66,    67,    35,    71,     0,
       0,    72,    64,    65,     0,   490,   493,   793,   792,     0,
      18,     0,   491,     0,     0,     0,   254,   784,   785,   786,
     787,     0,     0,   110,   254,     0,   254,   254,   254,   111,
       0,   152,     0,   269,   271,    59,     0,     0,    50,   267,
       0,     0,     0,     0,   254,   788,     0,    10,     0,    22,
      16,   492,     0,     0,     0,   255,     0,     0,    13,    48,
       0,    13,   105,   255,   255,   255,     0,   254,   270,   272,
      58,     0,   268,   132,    60,     0,   128,   131,    49,     0,
       0,   254,     0,     0,   254,     0,     0,   789,     0,   496,
     294,   497,   495,   777,   778,   779,   780,   782,   781,    19,
     783,    23,     0,     0,     0,     0,   443,     0,   389,     0,
     111,   254,     0,     0,     0,     0,     0,     0,     0,   409,
       0,     0,   372,     0,   365,   390,   366,   391,   274,   449,
     460,   241,     0,   265,     0,   457,   428,   264,     0,   256,
       0,    46,    13,     0,     0,    13,   114,   206,   255,   240,
      13,     0,    13,   255,     0,     0,     0,     0,     0,    52,
       0,     0,     0,    61,    57,     0,   273,    63,     0,   254,
       0,   136,     0,   461,     0,   462,     0,     0,   372,   425,
     255,   423,   426,     0,   908,   908,   908,     0,    42,     0,
     421,     0,   296,     0,   419,   422,     0,   288,   289,   417,
     292,   437,   274,    20,     0,     0,   699,     0,   381,   406,
       0,   240,   373,     0,     0,     0,   392,   456,   443,   111,
     254,     0,   377,   775,   699,     0,   389,     0,     0,     0,
     224,     0,   227,   274,     0,   379,   406,   407,   408,     0,
       0,   371,     0,     0,   788,   775,   416,   275,     0,   459,
     252,   250,     0,   244,   242,     0,   430,     0,   453,     0,
     257,   259,     0,    13,     0,     0,   406,     0,     0,   117,
     108,   115,     0,     0,   297,   320,   318,     0,     0,     0,
       0,   362,     0,     0,   204,   207,     0,     0,   304,   321,
     322,   493,     0,     0,   190,     0,   106,     0,     0,    88,
       0,    13,    79,    77,    13,     0,   254,   255,     0,    54,
     129,    62,   130,    51,     0,   255,     0,   775,     0,   428,
     373,     0,   463,     0,     0,     0,     0,     0,     0,     0,
       0,    41,    43,   285,   286,   287,   284,     0,   410,     0,
       0,   281,   282,   283,   280,   290,   293,     0,    21,    24,
     775,     0,     0,   382,   405,     0,   442,   444,   446,   447,
     400,   788,   374,   254,     0,   240,     0,   454,   240,     0,
     393,     0,     0,   406,   240,   610,   560,   574,   775,   775,
       0,   572,     0,   576,   570,   775,     0,     0,   406,   775,
     775,   775,   775,   775,   775,     0,   616,   559,   561,     0,
     617,   614,   689,   615,   729,   730,   731,   732,   733,   734,
     735,     0,   558,     0,   389,   274,   237,   389,   274,   443,
       0,     0,   380,     0,   378,   385,     0,   775,   369,     0,
     277,     0,     0,     0,   414,   448,   450,   253,   251,   266,
     245,   243,     0,   458,     0,     0,   427,   429,   431,   434,
     428,     0,   440,     0,   264,     0,     0,     0,     0,    94,
      47,   109,   116,     0,   254,   118,   699,   300,   319,   313,
     699,     0,   298,   323,   361,   363,     0,   351,   348,   301,
     349,     0,   338,   193,   205,     0,   310,     0,   788,   337,
       0,     0,     7,   185,   238,   239,   107,    91,    82,    13,
      78,     0,     0,    84,     0,     0,     0,     0,    56,     0,
      53,     0,   255,     0,     0,     0,   255,   464,     0,   255,
     424,     7,     0,   255,   794,   795,   796,   797,   798,   799,
     800,   801,   803,   804,   805,   806,   807,   808,   809,   810,
     811,   812,   813,   814,   815,   816,   802,   817,   818,   819,
     820,   821,   822,   823,   824,   825,   826,   827,   828,   829,
     830,   831,   832,   833,   834,   835,   836,   837,   839,   840,
     841,   842,   843,   844,   845,   846,   847,   848,   849,   850,
     851,   852,   853,   854,   855,   856,   857,   858,   859,   860,
     861,   862,   863,   865,   864,   866,   870,   872,   867,   868,
     869,   871,   874,   876,   838,   873,   875,   877,   878,   879,
     880,   881,   882,   883,   884,   885,   886,   893,   896,   895,
     898,   899,   902,   906,   901,   903,   900,   904,   905,   907,
     897,   889,   891,   888,   887,   916,   890,   892,   894,   911,
     909,   910,   912,   913,   914,   917,   915,   296,   295,     0,
     420,   418,   291,   438,     0,   439,     0,   698,     0,   384,
       0,     0,   394,   195,   230,   396,     0,   455,   399,   254,
     443,   699,   775,   775,   609,   575,   406,   775,   669,   620,
     633,     0,     0,   631,   635,   629,   775,     0,   406,   775,
     775,   775,   775,   775,   675,   619,   621,     0,   676,   673,
     682,   674,   618,     0,   690,   573,   736,   577,   571,   613,
     699,   240,     0,   210,    74,   775,   684,   686,   685,   552,
     502,   516,   151,   514,   150,   518,   512,   775,   775,   775,
     482,     0,    12,   474,     0,   775,   471,   501,   503,   469,
     557,   556,   738,   478,   739,   500,   714,   483,   488,     0,
       0,   483,   774,   788,   719,   775,   775,   775,   775,   775,
     775,   775,   775,   775,   775,   775,   775,   775,   775,   775,
     775,   775,   775,   607,     0,     0,   563,   775,   775,   775,
     775,   775,   775,   775,   775,   775,   775,   775,   775,   775,
       0,   388,     0,     0,   775,     0,   240,   225,   226,   383,
     775,     0,   370,   416,   278,   375,    38,   240,   415,     0,
     248,   246,     0,   456,     0,   436,     0,   453,   452,     0,
     260,   430,     0,   102,    13,    92,     0,     0,     0,     0,
     299,     0,   303,   353,   352,     0,   302,   340,   339,   208,
     209,     0,   305,   321,     0,   336,     0,     0,     0,   331,
     333,     0,   324,   307,     0,   311,   775,     6,    83,    89,
       0,    13,    80,    13,   103,   133,   255,   187,    55,   186,
       0,    40,   134,   135,     0,     0,   255,     0,    13,     0,
       0,     0,   411,     0,     0,    39,     0,   445,   376,     0,
       0,   402,   404,   401,   403,   395,   240,     0,   688,   700,
     775,   707,   668,   634,     0,   683,   632,   636,   630,   672,
     240,     0,   775,   677,   679,   678,     0,     0,   775,   775,
     775,   775,   775,   775,   775,   775,   775,   775,   775,   775,
     775,   775,   775,   775,   775,   775,   666,     0,     0,   622,
     775,   775,   775,   775,   775,   775,   775,   775,   775,   775,
     775,   775,   775,     0,   763,    13,     0,   775,     0,   240,
       0,     0,   212,   687,   551,   517,     0,   153,   475,   515,
     519,   513,   555,     0,     0,     0,   479,   473,   151,   476,
     501,     0,   466,   472,   468,   719,   775,   775,   775,   775,
     775,   775,   775,   775,   775,   775,   775,   775,   775,   775,
     775,   775,   775,   775,   549,     0,     0,   505,   775,   775,
     775,   775,   775,   775,   775,   775,   775,   775,   775,   775,
     775,     0,   480,     0,     0,   775,   775,   569,   568,     0,
       0,     0,     0,   716,   723,   725,   600,   601,   595,   591,
     592,   596,   590,   589,   579,   580,   581,   582,   583,   584,
     585,   586,   587,   588,   608,   611,   612,   578,   593,   594,
     597,   598,   599,   602,   603,   604,   605,   606,     0,   486,
       0,   565,   564,     0,   772,     0,     0,     0,   398,     0,
     387,     0,   276,   451,   249,   247,     0,   456,   432,   441,
     263,     0,   262,   261,   775,    13,   416,    97,    93,    95,
       0,     0,   255,   240,     0,     0,   359,     0,   354,   364,
     346,     0,   341,   350,   312,   309,     0,     0,   326,   325,
     334,   306,     0,   308,     0,     0,    81,    85,   104,     0,
     188,    13,     0,     0,     7,   182,   177,     0,   173,    13,
     179,   180,   171,     7,     0,   699,   228,   230,   194,   397,
       0,   681,   775,   775,     0,   775,   680,   627,   628,   659,
     660,   654,   650,   651,   655,   649,   648,   638,   639,   640,
     641,   642,   643,   644,   645,   646,   647,   667,   670,   671,
     637,   652,   653,   656,   657,   658,   661,   662,   663,   664,
     665,     0,     0,   624,   623,     0,   748,     0,    13,   752,
       0,   755,     0,   701,   240,     0,   703,   775,   211,    73,
     510,   511,    74,   477,   788,   481,     0,   542,   543,   537,
     533,   534,   538,   532,   531,   521,   522,   523,   524,   525,
     526,   527,   528,   529,   530,   550,   553,   554,   520,   535,
     536,   539,   540,   541,   544,   545,   546,   547,   548,     0,
       0,   507,   506,   746,   740,   747,   741,   489,   715,   498,
     737,   499,   775,   562,   775,   718,   717,   775,   566,   567,
     775,     0,   770,   367,   386,   240,     0,     0,   101,     0,
       0,     0,   122,     0,     0,   121,     0,     0,   360,   355,
       0,   347,   342,     0,   330,   327,     0,   335,   332,   328,
     465,    87,   189,   151,     0,    13,   140,   142,   143,   144,
     156,   157,   172,     7,    13,   181,   178,   151,   167,   176,
      13,   412,     0,   229,     0,     0,   708,   240,     0,   710,
     709,   625,   626,   767,   768,   769,   764,   316,   762,   749,
     753,     0,   754,   751,   756,     0,   775,     0,   240,   702,
      76,     0,   504,   508,   509,   775,   775,   775,   775,   727,
     728,   724,   726,     0,   775,   279,   433,     0,    13,    98,
     100,   119,   123,     0,   120,   699,   314,   356,   357,   343,
     344,   329,     0,   150,   151,   145,     0,   137,   141,    13,
       0,     0,   151,   183,     0,     0,   430,     0,   699,   765,
     775,     0,   240,   775,     0,     0,   750,   697,   704,   706,
     775,   775,     0,     0,     0,     0,     0,   773,     0,   435,
      99,     0,   124,     0,   358,   345,     0,     0,    74,   111,
       0,     0,   168,   254,     0,   111,     0,   169,   413,     0,
     699,     0,     0,   711,   713,   775,   761,   317,   775,     0,
     691,   705,    75,   776,   742,   744,   743,   745,   771,   125,
       0,   149,     0,   148,     0,   254,   170,     0,     0,     0,
     254,   368,     0,     0,   766,   712,   759,   757,   760,   758,
     696,     0,   254,   775,     0,     0,     0,     0,   254,     0,
       0,     0,     0,   315,     0,   147,   146,   254,   223,   255,
     240,     0,     0,   254,   206,   255,   240,     0,   692,   255,
       0,   405,   406,     0,   221,     0,     0,   192,   184,   255,
       0,   320,   406,     0,     0,     0,   191,   693,   694,     0,
     255,   405,     0,   406,   203,   222,    74,   158,   161,     0,
     255,   405,     0,   406,   199,    74,   164,   695,   159,   162,
       0,    74,     0,   220,   165,     0,    74,     0,   216,   160,
     163,   220,    74,   217,     0,   166,   216,    74,   213,     0,
       0,   220,   218,   200,     0,   216,   214,   196,   201,     0,
     219,   197,     0,   215,   202,   198
  };

  const short int
  parser::yypgoto_[] =
  {
   -1336, -1336, -1336,  -551,   106,    12,  -427,  -404,    20,    63,
   -1336,    78, -1336,    13,  1448, -1336,     7, -1336,    42, -1104,
   -1336, -1336, -1336, -1336, -1336, -1207, -1336, -1336, -1336, -1336,
    -335,  -869, -1336,   926, -1336, -1336,   607, -1336, -1336, -1336,
   -1336,   -84,  1285, -1336,  1291, -1336,  1166, -1336, -1336, -1336,
      43,  -158, -1336, -1336, -1336, -1336, -1336,   132, -1336, -1336,
   -1336, -1000, -1336, -1336, -1336, -1336, -1336, -1336,  -992,   299,
   -1336, -1336, -1336, -1336,  1417, -1336,  -148, -1086, -1254, -1336,
   -1336, -1336, -1336,   -63, -1108,  -529,  -429,   486, -1068, -1335,
   -1336,  -280,  -412,   297, -1336,  -261,   -37,   -27, -1336, -1336,
     994, -1336,  1197, -1336,  -126,  -103, -1336, -1336,   795,  -101,
   -1336,  -514,   963,   333, -1336, -1336,   964,  -834, -1336,  -836,
    -105,   -61,   790,  -223,   791, -1336, -1336,  -290, -1336, -1336,
   -1336,  -786,   -69,  -131,  1121, -1336,  1142,  -301,  -839, -1336,
     654,   -71,  -362,  -303,  -222,   652,  -259, -1336, -1336,   663,
    -797,   -51,  -289,  2885,  -108,  -547,  1086,   627, -1336,   741,
    -420,  -949,  -387,   -16,     1,  2945,  -749,  3027,  1143, -1336,
    -439,  3253, -1336,  -277,  1105,   144,   811,    95,  -647,   513,
   -1336, -1336,   233, -1336,  3527, -1336,    53, -1336, -1336,   302,
     304, -1336, -1336, -1172, -1170,    62, -1336, -1336, -1336, -1336,
   -1336, -1336,  2157,   -18,  -273,  1441, -1336,   695, -1336,  -432,
    -144,  -106,  -142
  };

  const short int
  parser::yydefgoto_[] =
  {
      -1,     2,     3,   916,   917,    10,    11,    12,    13,    69,
     162,    14,    15,   561,    17,    43,   793,    45,    46,    47,
      48,    49,    50,    97,    51,  1022,  1472,    52,   363,   213,
     562,   563,   358,   359,    53,   518,   519,  1157,    54,    55,
      56,    82,   204,   205,   206,   522,   329,  1162,  1163,    23,
     135,   136,    57,   379,    58,  1364,  1365,  1366,  1367,  1544,
    1368,    59,    60,  1369,  1370,  1371,  1196,    61,  1197,  1198,
    1199,  1377,  1200,  1201,    62,    63,   208,  1565,  1559,   209,
     425,  1566,  1560,   343,   344,   345,  1265,   773,  1629,  1624,
    1573,   289,   290,   950,   291,   354,   275,   198,   199,   320,
     321,   322,   192,    98,   182,   307,   256,   257,   398,   346,
    1398,   347,   348,   909,   910,   911,   541,   542,   536,   537,
     250,   184,   185,   276,   186,   187,   953,   415,   299,   251,
     252,   493,   494,   259,   254,   240,   241,   318,   506,   507,
     508,   708,   261,   193,   271,   416,   509,   309,   495,   512,
     426,   427,   195,   455,   419,   553,   456,   794,   795,   796,
     806,  1128,   811,   457,    65,   458,   799,   808,   757,   758,
     460,   461,  1510,   412,   462,  1266,   760,  1389,   809,  1091,
    1092,  1093,  1094,  1325,   463,   803,   804,   464,  1258,  1259,
    1260,  1261,  1465,   465,   466,  1396,   467,   468,   469,   470,
     471,  1037,   472,   160,   146,    66,   699,   388,   700,   701,
     702,   703,   704
  };

  const short int
  parser::yytable_[] =
  {
      64,   431,   247,   151,   249,   126,   483,   473,   183,   499,
     420,   417,   724,   800,    83,   899,    16,   253,   772,   238,
     927,    20,    16,    44,   929,   791,   503,    72,    16,   564,
     938,   491,    24,   266,   810,   908,   523,   243,   230,   115,
     248,   272,  1152,  1041,   713,   284,  1044,   120,   792,   123,
     124,   125,  1187,  1343,  1187,  1166,   428,   430,   265,   865,
     268,  1170,  1251,   194,   372,  1410,   807,   145,   857,   282,
     367,    83,   292,   295,   111,   300,  1146,  1141,   576,   260,
     375,   258,    71,  1394,   239,  1395,   274,    21,   310,   434,
     218,  1376,   -25,   313,   399,   893,   214,   215,   216,  1035,
     411,   200,   870,   737,   228,   976,   380,   231,   148,     9,
     382,   -26,   302,   297,   197,   197,     1,   139,   897,     6,
     189,  1463,     5,     6,     7,     5,     6,     7,   383,   148,
    1309,   203,   384,   385,    16,   278,   140,   148,   298,   148,
     212,  1033,  -720,   732,   382,   148,     8,   422,   721,   108,
       8,   148,    83,     8,  -113,   452,   852,   -26,   775,  1018,
     400,   853,   413,   109,   725,  1214,   400,   728,  1407,   148,
    1461,  1464,   161,   733,   220,   279,   292,     4,   280,   190,
     268,   352,   201,   295,   447,     6,   360,   854,   311,   482,
     382,   349,   376,   314,   484,   429,   894,   303,   747,   211,
     722,   526,   871,     5,     6,     7,   312,   530,   150,   877,
     372,   315,     8,   386,    20,   203,   -25,   515,   203,   898,
     872,   306,    20,    16,   202,   357,   557,   210,   572,   150,
     281,     8,   527,  -231,   565,  -235,   529,   150,   532,   150,
     535,   540,   863,  1326,   407,   150,   875,  -233,   555,   888,
     306,   150,   306,   889,  1019,   577,   583,   856,   306,  1445,
    1215,    83,   393,  1408,   306,  1462,   394,   148,   374,   150,
     395,  1020,   148,   382,   711,   904,   303,  1020,     6,   221,
    1020,  1523,  1020,  -792,    33,   382,  1630,   304,   355,   948,
     401,   709,   233,   438,   402,   273,  1639,    18,   403,    27,
    1569,   731,   715,   717,   303,     8,   918,   106,    19,    30,
     719,   411,   303,   497,   106,   770,  1590,   349,   971,   292,
     117,   349,    22,   349,   239,   349,   349,   409,   707,   566,
    1394,  1349,  1395,   977,   712,  -463,   517,  1352,   149,    67,
     567,   263,   264,  -464,    27,   118,   774,    68,   571,    73,
    1337,   235,   452,   800,    30,  1144,   800,   479,   711,   222,
     221,   223,   396,  1446,   500,   807,   421,   150,   791,  1034,
    1340,  1021,   150,  -792,   480,   292,   858,  1454,   859,   382,
     878,     6,  1450,  1190,    67,   197,    27,   864,  1455,  1613,
     404,   792,    68,  1177,   304,    27,    30,   189,  1618,  1415,
    1416,   807,   421,   400,  1621,    30,  1417,  1418,     8,  1626,
     221,   372,  -112,   498,   900,  1631,   730,  -153,   926,   121,
    1635,  1150,   755,   755,   488,   489,    67,    27,   122,  1130,
     890,   349,   103,   480,    68,    67,   797,    30,   503,  1151,
     368,    91,   369,    68,  1487,   279,   960,   936,   280,   540,
     915,     6,  1494,   164,   957,  1145,   190,  1491,   972,   165,
     166,   947,   879,   319,   501,   105,  1473,    67,  1475,   104,
     815,   816,  1175,   167,     6,    68,  1579,   800,     8,   106,
     -96,    27,   -96,   326,   133,    27,   168,   887,   169,   791,
     569,    30,   570,  1016,  1600,    30,   316,   327,   317,   107,
     170,     8,   400,   171,   172,  1158,   955,   400,   956,   834,
    1017,  -258,   792,   408,  1438,   349,     6,   925,   113,  1440,
    1636,    67,  1578,  -258,   328,    67,  1451,  1443,   349,    68,
     173,   903,  -258,    68,   349,   349,  1452,  1207,   174,  1489,
    1089,   175,  1490,     8,   884,   930,   885,  1186,    89,   934,
     110,    90,   937,   176,  -154,   177,   940,   178,  1634,  1187,
     179,  1495,     6,   554,  1496,   180,   181,  1642,   114,   499,
     134,   361,   357,   841,   842,   843,   844,   845,   846,   847,
     362,   713,   848,   849,   850,   978,   979,     6,  1257,     8,
     142,   -86,  1252,   -86,   382,  1138,   552,   381,   552,   381,
     382,   552,   332,   813,   814,  1597,  1142,  1608,   333,    27,
    1619,    27,   452,   233,     8,   -90,   -90,  1498,   954,    30,
     538,    30,    26,   417,   997,   138,   153,   154,   155,   156,
      77,    78,    79,    80,    27,   334,   197,    28,    27,   143,
     964,   133,  1362,  1374,    30,     5,   774,     7,    30,    67,
     157,    67,  1380,    92,   100,   548,   549,    68,   550,    68,
    1310,   815,   816,   335,   163,    27,   908,   207,    93,   336,
      27,    27,   235,    36,    67,    30,   158,   921,    67,   922,
      30,    30,    68,   244,   245,   246,    68,   337,  1004,  1005,
    1006,  1007,  1008,  1009,  1010,  1209,   217,  1011,  1012,  1013,
     834,   923,   338,   924,    94,    67,   339,   270,   340,  1213,
      67,    67,     6,    68,   341,   342,  1443,   273,    68,    68,
     539,   755,   755,   277,  1402,   349,  1403,   219,   283,  1115,
    1116,   349,    27,   755,   755,   755,   305,   711,    27,     8,
      95,   133,    30,  1188,    96,   224,   558,   559,    30,   225,
     382,  1133,   815,   816,   817,   382,   382,   820,  1267,  1537,
     848,   849,   850,  -722,  1363,   842,   843,   844,   845,   846,
     847,  1090,    67,   848,   849,   850,  1040,    27,    67,   797,
      68,  1428,     6,  1429,   308,  1388,    68,    30,   319,  1028,
     535,   834,    27,  1169,   540,   133,   323,  1173,   574,  1039,
    1174,    27,    30,     6,    77,    78,    79,    80,     6,     8,
     711,    30,    24,  -174,   330,    89,     6,    67,    90,   100,
    1339,  -154,  1449,    27,   353,    68,   133,   371,  1085,  1086,
       8,  1257,    67,    30,  1256,     8,   349,   356,   755,  -139,
      68,    67,   364,     8,   292,   365,   101,  -175,  1402,    68,
    1466,  1132,   838,   839,   840,   841,   842,   843,   844,   845,
     846,   847,   503,    67,   848,   849,   850,     6,   813,  1045,
      27,    68,   377,   133,    27,   349,    27,   128,   349,   349,
      30,   373,   349,   378,    30,   349,    30,  1011,  1012,  1013,
    1269,    27,  1238,  1239,     8,   400,     6,   517,  1399,  1189,
     797,    30,  1345,   387,   899,   335,     6,  1274,  1045,  1193,
      67,   336,   568,   129,    67,   391,    67,   774,    68,   774,
    1432,  1433,    68,     8,    68,   245,   246,  -138,  1382,    20,
      36,    67,   392,     8,  1272,   410,  1154,  1480,  1363,    68,
     389,   390,   834,   397,   755,  1319,   405,  1321,  1155,   130,
    1156,  1195,   406,   131,   414,   423,   755,  -232,   424,   928,
    1296,  1297,   755,   755,   755,   755,   755,   755,   755,   755,
     755,   755,   755,   755,   755,   755,   755,   755,   755,   755,
     755,   815,   816,  1320,   755,   755,   755,   755,   755,   755,
     755,   755,   755,   755,   755,    27,  1474,   189,  1476,   511,
     481,  1411,   349,  1406,   349,    30,   513,   514,   843,   844,
     845,   846,   847,   521,  1254,   848,   849,   850,   520,   349,
     834,    77,    78,    79,    80,   327,   528,   543,   545,  1598,
     997,   544,  1388,   547,  1331,    67,    81,   485,  1606,   551,
     486,   487,  1609,    68,   552,  1273,   556,   834,   560,   316,
      26,   303,  1614,   578,  1361,  1341,   190,   579,   581,   718,
     720,   726,    27,  1620,   191,    28,   535,   727,  1625,   800,
     540,   729,    30,   452,  1425,  1354,   812,  1426,   855,   978,
     979,  1359,  1312,  -236,  1314,  1316,   843,   844,   845,   846,
     847,  -234,   860,   848,   849,   850,  1006,  1007,  1008,  1009,
    1010,    36,    67,  1011,  1012,  1013,   867,   873,   868,   869,
    1375,   876,   881,   774,   899,   845,   846,   847,   997,   349,
     848,   849,   850,   882,    27,   901,  1460,   892,   891,   895,
     381,   978,   979,   980,    30,  1344,   983,   896,   933,  1486,
     939,   941,    27,   943,   233,   349,   944,  1470,   407,   946,
    1021,   349,    30,    74,   949,   349,  1397,   813,  1483,  1131,
     349,   504,  1042,  1085,    67,    27,   349,  1083,  1479,  1084,
     997,  1137,    68,  1087,  1088,    30,  1147,  1160,  1161,    64,
    1167,  1501,    67,   857,  1006,  1007,  1008,  1009,  1010,  1164,
      68,  1011,  1012,  1013,  1165,  1176,   755,   755,   349,   755,
    1182,  1505,   997,   235,    36,    67,  1171,  1181,   505,  1180,
     382,  1185,  1195,    68,  1183,   815,   816,   817,   818,   819,
     820,   821,  1191,  1532,  1192,  1194,  1202,  1203,   857,  1204,
    1208,  1001,  1002,  1003,  1004,  1005,  1006,  1007,  1008,  1009,
    1010,   349,  1253,  1011,  1012,  1013,    27,  1169,    27,   535,
    1173,  1210,   540,  1212,   834,  1441,    30,  1217,    30,  1262,
    1218,    91,  1255,   815,   816,  1270,  1271,  1319,  1575,  1321,
    1008,  1009,  1010,  1322,  1584,  1011,  1012,  1013,  1574,  1324,
    1323,  1327,  1592,  1328,  1329,  1330,    67,    36,    67,  1335,
    1336,  1346,  1602,  1347,    68,  1348,    68,  1350,  1351,  1577,
    1353,   382,   834,  1612,  1356,  1586,   774,  1360,  1372,  1373,
    1384,  1378,  1391,  1617,   755,   838,   839,   840,   841,   842,
     843,   844,   845,   846,   847,   978,   979,   848,   849,   850,
     382,   411,   349,  1405,   349,   349,  1169,   349,  1173,  1392,
     349,  1404,  1413,  1632,  1412,  1414,  1424,    64,  1430,  1427,
    1447,  1456,  1431,  1434,  1437,  1500,  1439,  1457,  1471,  1484,
     774,  1485,  1492,  1507,   997,   840,   841,   842,   843,   844,
     845,   846,   847,  1497,  1499,   848,   849,   850,  1502,  1508,
    1509,  1514,  1513,  1169,  1173,  1515,  1516,  1195,  1520,  1521,
    1517,   349,  1519,  1195,  1522,  1528,  1526,  1533,    26,  1541,
    1543,  1545,  1547,  1549,  1552,  1524,  1540,  1551,   755,  1556,
      27,  1529,  1558,    28,  1567,  1564,  1561,   349,  1594,  1453,
      30,   349,  1596,   349,  1595,  -405,  1604,  1003,  1004,  1005,
    1006,  1007,  1008,  1009,  1010,  1605,  1611,  1011,  1012,  1013,
    1616,  1623,  1627,  1628,   755,   349,  1633,   755,   349,    36,
      67,  1640,  1637,   292,  1442,  1443,  1527,  1638,    68,    70,
      70,  1641,  1195,    25,  1444,  1588,  1643,   295,   349,   349,
      75,    83,    76,  1644,    84,    85,    86,    83,    87,    88,
    1645,   532,    99,   102,  1607,   919,   482,   325,  1546,   755,
     292,  1159,   797,  1550,   525,  -721,   331,  1448,  1379,   119,
     890,  1583,   942,  1090,  1383,  1554,  1268,   859,   880,    27,
     902,  1562,   502,  1358,   914,   112,   951,   952,   292,    30,
    1570,   710,   116,  1311,   754,   754,  1580,   580,   766,    27,
    1148,   127,  1576,   132,  1153,   292,  1043,   137,  1585,    30,
    1149,   141,  1589,  1184,   144,   764,   147,   152,   349,    67,
      70,  1469,  1599,   965,   188,   196,  1504,    68,  1276,  1421,
    1400,  1539,  1401,  1610,  1534,     0,   349,     0,    36,    67,
       0,     0,   137,  1615,     0,   978,   979,    68,     0,     0,
     226,   227,   763,   229,   188,   349,   188,     0,     0,   188,
       0,   262,     0,     0,     0,     0,     0,  1313,     0,     0,
       0,     0,     0,    27,   188,   188,   188,     0,   188,     0,
       0,     0,   349,    30,   997,   188,     0,   188,   293,   188,
       0,   188,   188,     0,     0,   166,     0,   349,     0,     0,
       0,     0,     0,   381,     0,     0,     0,     0,   381,     0,
       0,   324,    36,    67,     0,    27,     0,   233,   351,     0,
      27,    68,     0,     0,     0,    30,     0,     0,   366,     0,
      30,     0,   370,   137,   874,   170,   137,     0,   171,     0,
       0,     0,     0,   188,     0,     0,     0,   188,   188,  1005,
    1006,  1007,  1008,  1009,  1010,    67,     0,  1011,  1012,  1013,
      67,     0,     0,    68,     0,   173,  1315,     0,    68,     0,
       0,     0,    27,     0,     0,    70,   235,     0,   188,     0,
     165,   188,    30,     0,   188,     0,     0,     0,     0,     0,
     177,     0,   293,     0,   232,     0,   475,     0,   476,   478,
       0,     0,     0,     0,     0,   188,    27,   168,   233,   169,
     188,    36,    67,     0,   490,   147,    30,   188,     0,     0,
      68,   170,     0,   196,   171,   172,   196,   188,   510,     0,
       0,     0,     0,   188,     0,     0,     0,     0,   524,     0,
       0,     0,     0,   188,   351,     0,    67,     0,   351,   188,
     351,   234,   351,   351,    68,     0,     0,     0,   546,   174,
       0,     0,   175,     0,   188,     0,     0,   235,   188,     0,
       0,     0,     0,     0,     0,     0,   188,     0,   178,   137,
       0,   179,     0,     0,   236,     0,   180,   237,     0,   575,
       0,     0,   188,   754,   754,   164,   188,     0,   188,     0,
       0,   165,   166,     0,     0,   754,   754,   754,   262,     0,
     188,   188,     0,  1014,     0,   167,   262,   714,   188,     0,
       0,     0,   188,     0,     0,     0,   188,    27,   168,   233,
     169,     0,   147,     0,     0,   293,     0,    30,     0,     0,
     766,     0,   170,   188,     0,   171,   172,  1036,   735,     0,
     961,   962,     0,     0,     0,   768,     0,   188,   351,     0,
       0,     0,   973,   974,   975,     0,     0,    67,     0,     0,
       0,     0,   173,     0,     0,    68,     0,     0,     0,     0,
     174,     0,   905,   175,     0,     0,     0,     0,   235,     0,
     906,   293,   188,     0,   188,   176,    27,   177,     0,   178,
     188,     0,   179,  1357,     0,     0,    30,   180,   181,  1134,
     754,   906,     0,   301,     0,     0,   188,    27,     0,     0,
       0,     0,   188,     0,     0,   335,     0,    30,   883,     0,
       0,   336,     0,     0,   886,     0,    67,     0,     0,     0,
       0,     0,   351,     0,    68,     0,   335,     0,     0,   907,
       0,     0,   336,     0,     0,   351,   188,    67,     0,   147,
     912,   351,   351,     0,     0,    68,     0,  1136,     0,     0,
     907,     0,   920,     0,   815,   816,   817,   818,   819,   820,
     137,    29,     0,     0,     0,   932,     0,     0,     0,   188,
       0,    31,    32,     0,     0,    33,     0,     0,  -155,     0,
       0,     0,     0,     0,    34,   153,   154,   155,   156,    77,
      78,    79,    80,   834,     0,    35,   754,  1026,     0,     0,
       0,    38,    39,     0,     0,  1027,    41,     0,   754,   157,
       0,    42,     0,     0,   754,   754,   754,   754,   754,   754,
     754,   754,   754,   754,   754,   754,   754,   754,   754,   754,
     754,   754,   754,     0,     0,   158,   754,   754,   754,   754,
     754,   754,   754,   754,   754,   754,   754,     0,     0,     0,
       0,     0,     0,  1211,   838,   839,   840,   841,   842,   843,
     844,   845,   846,   847,     0,  1216,   848,   849,   850,     0,
       0,  1219,  1220,  1221,  1222,  1223,  1224,  1225,  1226,  1227,
    1228,  1229,  1230,  1231,  1232,  1233,  1234,  1235,  1236,  1237,
       0,     0,     0,  1240,  1241,  1242,  1243,  1244,  1245,  1246,
    1247,  1248,  1249,  1250,     0,     0,     0,     0,     0,     0,
       0,   188,     0,     0,     0,     0,     0,   188,     0,     0,
       0,     0,     0,   332,     0,     0,     0,     0,     0,   333,
       0,   963,   351,     0,     0,     0,   968,     0,   351,     0,
       0,   538,     0,    26,     0,     0,     0,   153,   154,   155,
     156,    77,    78,    79,    80,    27,   334,     0,    28,     0,
       0,     0,     0,     0,     0,    30,     0,     0,     0,     0,
       0,   157,  1332,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   335,     0,     0,     0,     0,     0,
     336,     0,     0,     0,    36,    67,     0,   158,     0,     0,
       0,     0,     0,    68,   147,  1095,     0,     0,   337,     0,
       0,     0,     0,     0,     0,   159,   978,   979,   980,   981,
     982,   983,   984,   338,     0,   188,   188,   339,     0,   340,
       0,     0,     0,     0,     0,   341,   342,     0,     0,     0,
       0,   913,     0,   351,     0,     0,     0,     0,   754,   754,
       0,   754,     0,     0,   188,   997,     0,     0,     0,   188,
       0,     0,     0,   196,     0,     0,     0,   188,     0,     0,
       0,     0,   188,   188,     0,     0,     0,     0,     0,     0,
       0,     0,   351,     0,     0,   351,   351,     0,     0,   351,
       0,  1393,   351,     0,     0,     0,     0,     0,  1178,  1179,
       0,     0,     0,     0,     0,  1385,  1386,     0,  1390,     0,
       0,     0,     0,     0,   350,     0,  1001,  1002,  1003,  1004,
    1005,  1006,  1007,  1008,  1009,  1010,     0,     0,  1011,  1012,
    1013,     0,   262,     0,   714,     0,     0,  1205,     0,     0,
     293,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   754,   815,   816,   817,
     818,   819,   820,   821,   822,   823,   824,   825,   826,   827,
     828,   829,   830,   831,   832,   833,     0,     0,   188,   188,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   834,     0,     0,   351,
       0,   351,   188,     0,     0,     0,     0,    87,     0,     0,
       0,  1459,     0,  1423,     0,     0,   351,     0,     0,     0,
       0,     0,     0,     0,     0,     0,  1095,     0,     0,     0,
     350,     0,     0,     0,   350,     0,   350,   533,   350,   350,
       0,     0,     0,     0,     0,     0,   188,   188,     0,  1477,
     754,     0,     0,   835,     0,   836,   837,   838,   839,   840,
     841,   842,   843,   844,   845,   846,   847,     0,     0,   848,
     849,   850,     0,     0,     0,     0,     0,   866,     0,     0,
       0,     0,     0,     0,     0,   165,   754,     0,     0,   754,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   167,
       0,     0,     0,     0,  1518,     0,     0,  1478,     0,     0,
       0,    27,   168,   233,   169,     0,   351,     0,  1333,     0,
       0,    30,     0,     0,     0,     0,   170,     0,  1393,   171,
     172,   754,     0,     0,  1536,   762,   762,   188,     0,     0,
       0,   188,   351,  1503,   350,     0,  1506,     0,   351,   805,
       0,    67,   351,     0,     0,     0,   234,   351,  1355,    68,
       0,   912,     0,   351,   174,     0,   188,   175,     0,     0,
       0,     0,   235,     0,     0,     0,   978,   979,   980,   981,
     982,   983,     0,   178,     0,  1381,   179,     0,  1535,     0,
       0,   180,     0,     0,     0,   351,   815,   816,   817,   818,
     819,   820,   821,   822,   823,   824,   825,   826,   827,   828,
     829,   830,   831,   832,   833,   997,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   350,     0,
       0,     0,     0,     0,     0,   834,     0,     0,   351,     0,
       0,   350,     0,     0,   350,     0,     0,   350,   350,     0,
       0,     0,     0,     0,     0,   147,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,  1001,  1002,  1003,  1004,
    1005,  1006,  1007,  1008,  1009,  1010,     0,     0,  1011,  1012,
    1013,     0,   835,     0,   836,   837,   838,   839,   840,   841,
     842,   843,   844,   845,   846,   847,     0,  1095,   848,   849,
     850,     0,     0,     0,     0,     0,   931,   188,     0,     0,
       0,     0,     0,     0,     0,     0,     0,  1435,  1436,   351,
       0,   351,   351,     0,   351,     0,     0,   351,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,  1458,     0,     0,     0,   332,
       0,     0,     0,     0,     0,   333,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,  1467,     0,   351,    26,
       0,     0,     0,   153,   154,   155,   156,    77,    78,    79,
      80,    27,   334,     0,    28,     0,     0,     0,   188,     0,
       0,    30,     0,     0,   351,     0,     0,   157,   351,     0,
     351,     0,     0,   196,     0,  1488,     0,     0,     0,     0,
     335,     0,  1493,   102,   762,   762,   336,   188,   350,     0,
      36,    67,   351,   158,   350,   351,   762,   762,   762,    68,
       0,     0,     0,     0,   337,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   351,   351,     0,     0,   338,
       0,  1525,     0,   339,     0,   340,     0,  1530,     0,     0,
    1531,   341,   342,     0,     0,     0,     0,  1342,     0,   805,
       0,     0,   805,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,  1542,     0,    26,     0,     0,     0,  1548,
       0,     0,     0,     0,     0,     0,     0,    27,     0,     0,
      28,    29,  1553,     0,     0,     0,  1557,    30,   188,     0,
    1563,    31,    32,  1568,     0,    33,     0,     0,  -155,   293,
       0,     0,     0,     0,    34,   351,     0,     0,  1587,   350,
       0,   762,   476,   478,     0,    35,    36,    37,     0,     0,
       0,    38,    39,   351,     0,    40,    41,     0,     0,     0,
     242,    42,   188,   255,     0,   255,   293,     0,     0,     0,
       0,     0,   351,     0,     0,     0,     0,     0,   350,   255,
     269,   350,   350,   188,     0,   350,     0,     0,   350,     0,
       0,   255,     0,   296,   293,     0,   255,     0,     0,   351,
       0,     0,     0,   805,     0,     0,     0,     0,     0,     0,
       0,   293,     0,     0,   351,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   762,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   762,
       0,     0,     0,     0,     0,   762,   762,   762,   762,   762,
     762,   762,   762,   762,   762,   762,   762,   762,   762,   762,
     762,   762,   762,   762,     0,   418,     0,   762,   762,   762,
     762,   762,   762,   762,   762,   762,   762,   762,     0,     0,
     269,     0,     0,   296,     0,   350,     0,   350,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   255,   350,   496,     0,     0,   164,     0,     0,     0,
       0,   418,   165,   166,     0,     0,     0,   516,     0,     0,
       0,     0,     0,     0,     0,     0,   167,   255,     0,     0,
       0,     0,     0,   255,     0,     0,     0,     0,    27,   168,
     233,   169,     0,     0,     0,     0,     0,     0,    30,     0,
       0,     0,   255,   170,     0,     0,   171,   172,     0,     0,
     255,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    67,     0,
     242,     0,   255,   173,     0,     0,    68,     0,     0,     0,
       0,   174,   255,     0,   175,   255,     0,     0,     0,   235,
     255,     0,   350,     0,     0,     0,   176,     0,   177,     0,
     178,     0,     0,   179,     0,     0,     0,     0,   180,   181,
     459,     0,     0,     0,   862,     0,     0,   255,   350,     0,
       0,     0,     0,     0,   350,     0,     0,     0,   350,     0,
       0,   255,   492,   350,     0,     0,     0,     0,     0,   350,
     815,   816,   817,   818,   819,   820,   821,   822,   823,   824,
     825,   826,   827,   828,   829,   830,   831,   832,   833,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   762,
     762,   350,   762,     0,   255,     0,     0,     0,     0,   834,
       0,     0,     0,   756,   756,     0,     0,     0,     0,     0,
     418,     0,     0,     0,     0,     0,   255,   798,     0,     0,
       0,     0,     0,     0,   573,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   350,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     255,     0,     0,     0,     0,     0,   835,   716,   836,   837,
     838,   839,   840,   841,   842,   843,   844,   845,   846,   847,
       0,     0,   848,   849,   850,     0,     0,     0,     0,     0,
     945,     0,   734,   255,     0,     0,     0,     0,   765,     0,
     767,     0,   769,     0,     0,     0,   776,   777,   778,     0,
       0,     0,     0,     0,     0,     0,     0,   762,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   350,     0,   350,   350,     0,
     350,     0,     0,   350,   861,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,  1046,  1047,  1048,  1049,
    1050,  1051,  1052,  1053,  1054,  1055,  1056,  1057,  1058,  1059,
    1060,  1061,  1062,  1063,  1064,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   350,  1065,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   762,     0,     0,     0,     0,     0,     0,     0,     0,
     350,     0,     0,     0,   350,     0,   350,     0,     0,     0,
       0,     0,     0,     0,     0,   418,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   762,   350,     0,
     762,   350,  1066,     0,  1067,  1068,  1069,  1070,  1071,  1072,
    1073,  1074,  1075,  1076,  1077,  1078,     0,     0,  1079,  1080,
    1081,   350,   350,     0,     0,     0,  1082,     0,    26,     0,
       0,     0,   153,   154,   155,   156,    77,    78,    79,    80,
      27,     0,   762,    28,     0,   805,  1025,     0,     0,     0,
      30,  1031,     0,     0,     0,     0,   157,     0,     0,     0,
       0,     0,   756,   756,     0,     0,     0,     0,     0,     0,
       0,   759,   759,     0,   756,   756,   756,     0,     0,    36,
      67,     0,   158,     0,     0,   801,     0,     0,    68,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   350,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   340,     0,     0,   798,     0,   350,
     798,     0,     0,     0,     0,     0,     0,     0,   255,     0,
       0,     0,     0,   255,  1143,     0,     0,     0,   350,   958,
     959,   418,     0,     0,     0,     0,   418,   418,     0,     0,
     966,   967,     0,   969,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   350,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   756,
     350,     0,  1023,     0,     0,     0,  1024,     0,     0,     0,
    1029,     0,  1030,     0,  1032,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   255,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,  1096,  1097,  1098,  1099,  1100,  1101,  1102,  1103,
    1104,  1105,  1106,  1107,  1108,  1109,  1110,  1111,  1112,  1113,
    1114,   798,     0,     0,  1117,  1118,  1119,  1120,  1121,  1122,
    1123,  1124,  1125,  1126,  1127,  1129,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,  1139,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   756,   255,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   756,     0,     0,
       0,     0,     0,   756,   756,   756,   756,   756,   756,   756,
     756,   756,   756,   756,   756,   756,   756,   756,   756,   756,
     756,   756,     0,     0,     0,   756,   756,   756,   756,   756,
     756,   756,   756,   756,   756,   756,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   761,   761,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   802,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     759,   759,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   759,   759,   759,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,  1046,  1047,
    1048,  1049,  1050,  1051,  1052,  1053,  1054,  1055,  1056,  1057,
    1058,  1059,  1060,  1061,  1062,  1063,  1064,     0,  1129,     0,
       0,   255,     0,     0,  1263,   801,     0,     0,   801,     0,
       0,     0,     0,     0,     0,     0,     0,  1065,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     255,     0,     0,  1277,  1278,  1279,  1280,  1281,  1282,  1283,
    1284,  1285,  1286,  1287,  1288,  1289,  1290,  1291,  1292,  1293,
    1294,  1295,     0,     0,     0,  1298,  1299,  1300,  1301,  1302,
    1303,  1304,  1305,  1306,  1307,  1308,  1129,   759,     0,     0,
       0,     0,  1317,  1318,  1066,     0,  1067,  1068,  1069,  1070,
    1071,  1072,  1073,  1074,  1075,  1076,  1077,  1078,     0,     0,
    1079,  1080,  1081,     0,     0,     0,     0,     0,  1275,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   756,   756,     0,
     756,     0,     0,     0,     0,     0,     0,     0,     0,   801,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,  1338,     0,   815,   816,   817,   818,   819,   820,   821,
     822,   823,   824,   825,   826,   827,   828,   829,   830,   831,
     832,   833,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   759,     0,     0,     0,     0,     0,     0,
       0,   418,   834,     0,     0,   759,     0,     0,     0,     0,
       0,   759,   759,   759,   759,   759,   759,   759,   759,   759,
     759,   759,   759,   759,   759,   759,   759,   759,   759,   759,
       0,     0,     0,   759,   759,   759,   759,   759,   759,   759,
     759,   759,   759,   759,   761,   761,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   756,   761,   761,   761,   835,
       0,   836,   837,   838,   839,   840,   841,   842,   843,   844,
     845,   846,   847,     0,  1409,   848,   849,   850,     0,     0,
     332,   851,     0,     0,     0,     0,   333,     0,     0,     0,
       0,     0,   418,     0,     0,     0,     0,     0,     0,  1481,
      26,     0,   802,     0,   153,   154,   155,   156,    77,    78,
      79,    80,    27,   334,     0,    28,     0,     0,     0,     0,
       0,   418,    30,     0,     0,     0,     0,     0,   157,  1419,
       0,  1420,     0,     0,  1422,     0,     0,     0,     0,     0,
       0,   335,     0,     0,     0,     0,     0,   336,     0,   756,
       0,    36,    67,     0,   158,     0,     0,     0,     0,     0,
      68,   761,     0,     0,     0,   337,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     338,     0,     0,     0,   339,   756,   340,     0,   756,     0,
       0,     0,   341,   342,     0,     0,     0,     0,  1482,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   255,  1468,     0,     0,     0,     0,     0,     0,
       0,     0,  1129,   802,  1129,     0,     0,     0,     0,     0,
     756,     0,     0,   798,     0,     0,     0,  1593,     0,     0,
       0,     0,     0,     0,     0,   759,   759,  1603,   759,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   761,     0,     0,
       0,     0,     0,     0,     0,     0,     0,  1511,  1512,   761,
       0,     0,     0,     0,     0,   761,   761,   761,   761,   761,
     761,   761,   761,   761,   761,   761,   761,   761,   761,   761,
     761,   761,   761,   761,     0,     0,     0,   761,   761,   761,
     761,   761,   761,   761,   761,   761,   761,   761,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    1555,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   759,     0,   584,   585,   586,   587,   588,
     589,   590,   591,   592,   593,   594,   595,   596,   597,   598,
     599,   600,   601,   602,   603,   604,   605,   606,   607,   608,
     609,   610,   611,   612,   613,   614,   615,   616,   617,   618,
     619,   620,   621,   622,   623,   624,   625,   626,   627,   628,
     629,   630,   631,   632,   633,   634,   635,   636,   637,   638,
     639,   640,   641,   642,   643,   644,   645,   646,   647,   648,
     649,   650,   651,   652,   653,   654,   655,   656,   657,   658,
     659,   660,   661,   662,   663,   664,   665,   666,   667,   668,
     669,   670,   671,   672,   673,   674,   675,   759,   676,     0,
       0,   677,     0,   678,   679,   680,   681,   682,   683,   684,
     685,   686,   687,   688,   689,   690,   244,   245,   246,   691,
       0,     0,   692,     0,   693,   694,   695,   696,   697,   698,
       0,     0,     0,   759,     0,     0,   759,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   761,
     761,     0,   761,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   759,     0,
       0,   801,   584,   585,   586,   587,   588,   589,   590,   591,
     592,   593,   594,   595,   596,   597,   598,   599,   600,   601,
     602,   603,   604,   605,   606,   607,   608,   609,   610,   611,
     612,   613,   614,   615,   616,   617,   618,   619,   620,   621,
     622,   623,   624,   625,   626,   627,   628,   629,   630,   631,
     632,   633,   634,   635,   636,   637,   638,   639,   640,   641,
     642,   643,   644,   645,   646,   647,   648,   649,   650,   651,
     652,   653,   654,   655,   656,   657,   658,   659,   660,   661,
     662,   663,   664,   665,   666,   667,   668,   669,   670,   671,
     672,   673,   674,   675,     0,   676,     0,   761,   677,     0,
     678,   679,   680,   681,   682,   683,   684,   685,   686,   687,
     688,   689,   690,   244,   245,   246,   691,     0,   705,   692,
       0,   693,   694,     0,   696,   697,   698,     0,     0,   432,
       0,     0,     0,     0,     0,   433,   434,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   779,     0,    26,
       0,     0,     0,   153,   154,   155,   156,    77,    78,    79,
      80,    27,     0,   233,   780,    29,     0,     0,     0,     0,
     781,    30,     0,     0,     0,    31,    32,   157,     0,    33,
       0,   438,  -155,     0,   -13,     0,     0,   439,    34,   440,
       0,   761,     0,     0,   782,     0,     0,   783,     0,    35,
      36,    37,     0,   158,  -151,    38,   784,     0,   785,    40,
      41,     0,   786,     0,   787,    42,     0,     0,     0,     6,
       0,     0,   235,     0,     0,     0,     0,   761,     0,   446,
     761,   447,     0,   448,     0,   449,   450,     0,     0,   451,
     -13,   788,   789,     0,     0,     0,     8,     0,     0,   790,
    -470,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   761,     0,     0,  1538,   584,   585,   586,   587,
     588,   589,   590,   591,   592,   593,   594,   595,   596,   597,
     598,   599,   600,   601,   602,   603,   604,   605,   606,   607,
     608,   609,   610,   611,   612,   613,   614,   615,   616,   617,
     618,   619,   620,   621,   622,   623,   624,   625,   626,   627,
     628,   629,   630,   631,   632,   633,   634,   635,   636,   637,
     638,   639,   640,   641,   642,   643,   644,   645,   646,   647,
     648,   649,   650,   651,   652,   653,   654,   655,   656,   657,
     658,   659,   660,   661,   662,   663,   664,   665,   666,   667,
     668,   669,   670,   671,   672,   673,   674,   675,     0,   676,
       0,     0,   677,     0,   678,   679,   680,   681,   682,   683,
     684,   685,   686,   687,   688,   689,   690,   244,   245,   246,
     691,     0,     0,   692,   706,   693,   694,   432,   696,   697,
     698,     0,     0,   433,   434,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   779,     0,    26,     0,     0,
       0,   153,   154,   155,   156,    77,    78,    79,    80,    27,
       0,   233,   780,    29,     0,     0,     0,     0,   781,    30,
       0,     0,     0,    31,    32,   157,     0,    33,     0,   438,
    -155,     0,   -13,     0,     0,   439,    34,   440,     0,     0,
       0,     0,   782,     0,     0,   783,     0,    35,    36,    37,
       0,   158,  -151,    38,   784,     0,   785,    40,    41,     0,
     786,     0,   787,    42,     0,     0,     0,     6,     0,     0,
     235,     0,     0,     0,     0,     0,     0,   446,     0,   447,
       0,   448,     0,   449,   450,   432,     0,   451,   -13,   788,
     789,   433,   434,     0,     8,     0,     0,   790,  -467,     0,
       0,     0,     0,   435,     0,    26,     0,     0,     0,   153,
     154,   155,   156,    77,    78,    79,    80,    27,     0,   233,
     436,     0,     0,     0,     0,     0,   437,    30,     0,     0,
       0,     0,     0,   157,     0,     0,     0,   438,     0,     0,
       0,     0,     0,   439,     0,   440,     0,     0,     0,     0,
       0,     0,     0,   441,     0,     0,    36,    67,     0,   158,
       0,     0,   442,     0,   443,    68,     0,     0,   444,     0,
     445,     0,     0,     0,     0,     0,     0,     0,   235,     0,
       0,     0,     0,     0,     0,   446,     0,   447,     0,   448,
       0,   449,   450,   432,     0,   451,   452,   453,   454,   433,
     434,  -484,     0,  -484,     0,     0,     0,     0,     0,     0,
       0,   435,     0,    26,     0,     0,     0,   153,   154,   155,
     156,    77,    78,    79,    80,    27,     0,   233,   436,     0,
       0,     0,     0,     0,   437,    30,     0,     0,     0,     0,
       0,   157,     0,     0,     0,   438,     0,     0,     0,     0,
       0,   439,     0,   440,     0,     0,     0,     0,     0,     0,
       0,   441,     0,     0,    36,    67,     0,   158,     0,     0,
     442,     0,   443,    68,     0,     0,   444,     0,   445,     0,
       0,   164,     0,     0,     0,     0,   235,   285,   166,     0,
       0,     0,     0,   446,     0,   447,     0,   448,     0,   449,
     450,   167,     0,   451,   452,   453,   454,     0,     0,     0,
       0,  -485,     0,    27,   286,     0,   169,     0,     0,     0,
       0,     0,     0,    30,     0,     0,     0,     0,   170,     0,
       0,   171,   172,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   287,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    67,     0,     0,     0,     0,   173,     0,
       0,    68,     0,     0,     0,     0,   174,     0,     0,   175,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     432,   176,     0,   177,     0,   288,   433,   434,   179,     0,
       0,     0,     0,   180,   181,     0,     0,     0,   779,   723,
      26,     0,     0,     0,   153,   154,   155,   156,    77,    78,
      79,    80,    27,     0,     0,   780,    29,     0,     0,     0,
       0,   781,    30,     0,     0,     0,    31,    32,   157,     0,
      33,     0,     0,  -155,     0,     0,     0,     0,     0,    34,
     440,     0,     0,     0,     0,  1038,     0,     0,   783,     0,
      35,    36,    37,     0,   158,  -151,    38,    39,     0,   785,
      40,    41,     0,   786,     0,   787,    42,     0,     0,     0,
       6,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     446,     0,   447,     0,   448,     0,   449,   450,   432,     0,
     451,     0,   788,   789,   433,   434,     0,     8,     0,     0,
       0,     0,     0,     0,     0,     0,   435,     0,    26,     0,
       0,     0,   153,   154,   155,   156,    77,    78,    79,    80,
      27,     0,   233,   436,     0,     0,     0,     0,     0,   437,
      30,     0,     0,     0,     0,     0,   157,     0,     0,     0,
     438,     0,     0,     0,     0,     0,   439,     0,   440,     0,
       0,     0,     0,     0,     0,     0,   441,     0,     0,    36,
      67,     0,   158,     0,     0,   442,     0,   443,    68,     0,
       0,   444,     0,   445,     0,     0,     0,     0,     0,     0,
       0,   235,     0,     0,     0,     0,     0,     0,   446,     0,
     447,     0,   448,     0,   449,   450,   432,     0,   451,   452,
     453,   454,   433,   434,  -485,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   435,     0,    26,     0,     0,     0,
     153,   154,   155,   156,    77,    78,    79,    80,    27,     0,
     233,   436,     0,     0,     0,     0,     0,   437,    30,     0,
       0,     0,     0,     0,   157,     0,     0,     0,   438,     0,
       0,     0,     0,     0,   439,     0,   440,     0,     0,     0,
       0,     0,     0,     0,   441,     0,     0,    36,    67,     0,
     158,     0,     0,   442,     0,   443,    68,     0,     0,   444,
       0,   445,     0,     0,     0,     0,     0,     0,     0,   235,
       0,     0,     0,     0,     0,     0,   446,     0,   447,     0,
     448,     0,   449,   450,     0,     0,   451,   452,   453,   454,
       0,     0,  -487,   815,   816,   817,   818,   819,   820,   821,
     822,   823,   824,   825,   826,   827,   828,   829,   830,   831,
     832,   833,   815,   816,   817,   818,   819,   820,   821,   822,
     823,   824,   825,   826,   827,   828,   829,   830,   831,   832,
     833,     0,   834,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   834,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   978,   979,   980,   981,   982,   983,
     984,   985,   986,   987,   988,   989,   990,   991,   992,   993,
     994,   995,   996,     0,     0,     0,     0,     0,     0,   835,
       0,   836,   837,   838,   839,   840,   841,   842,   843,   844,
     845,   846,   847,   997,     0,   848,   849,   850,   835,  1140,
     836,   837,   838,   839,   840,   841,   842,   843,   844,   845,
     846,   847,     0,     0,   848,   849,   850,     0,  1334,     0,
       0,     0,     0,     0,     0,   978,   979,   980,   981,   982,
     983,   984,   985,   986,   987,   988,   989,   990,   991,   992,
     993,   994,   995,   996,     0,     0,     0,     0,     0,     0,
     998,     0,   999,  1000,  1001,  1002,  1003,  1004,  1005,  1006,
    1007,  1008,  1009,  1010,   997,   452,  1011,  1012,  1013,   815,
     816,   817,   818,   819,   820,   821,   822,   823,   824,   825,
     826,   827,   828,   829,   830,   831,   832,   833,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   834,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   998,     0,   999,  1000,  1001,  1002,  1003,  1004,  1005,
    1006,  1007,  1008,  1009,  1010,     0,  1015,  1011,  1012,  1013,
     978,   979,   980,   981,   982,   983,   984,   985,   986,   987,
     988,   989,   990,   991,   992,   993,   994,   995,   996,     0,
       0,     0,     0,     0,     0,   835,     0,   836,   837,   838,
     839,   840,   841,   842,   843,   844,   845,   846,   847,   997,
       0,   848,   849,   850,  1046,  1047,  1048,  1049,  1050,  1051,
    1052,  1053,  1054,  1055,  1056,  1057,  1058,  1059,  1060,  1061,
    1062,  1063,  1064,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,  1065,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   998,     0,   999,  1000,
    1001,  1002,  1003,  1004,  1005,  1006,  1007,  1008,  1009,  1010,
       0,     0,  1011,  1012,  1013,   815,   816,   817,   818,   819,
     820,   821,   822,   823,   824,   825,   826,   827,   828,   829,
     830,   831,   832,  -793,     0,     0,     0,     0,     0,     0,
    1066,     0,  1067,  1068,  1069,  1070,  1071,  1072,  1073,  1074,
    1075,  1076,  1077,  1078,   834,     0,  1079,  1080,  1081,   815,
     816,   817,   818,   819,   820,   821,   822,   823,   824,   825,
     826,   827,   828,   829,   830,   831,   832,   833,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   834,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   837,   838,   839,   840,   841,   842,
     843,   844,   845,   846,   847,     0,     0,   848,   849,   850,
     978,   979,   980,   981,   982,   983,   984,   985,   986,   987,
     988,   989,   990,   991,   992,   993,   994,   995,  -793,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   837,   838,
     839,   840,   841,   842,   843,   844,   845,   846,   847,   997,
       0,   848,   849,   850,   815,   816,   817,   818,   819,   820,
     821,   822,   823,   824,   825,   826,   827,   828,   829,   830,
     831,   832,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   834,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,  1000,
    1001,  1002,  1003,  1004,  1005,  1006,  1007,  1008,  1009,  1010,
       0,     0,  1011,  1012,  1013,   978,   979,   980,   981,   982,
     983,   984,   985,   986,   987,   988,   989,   990,   991,   992,
     993,   994,   995,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   837,   838,   839,   840,   841,   842,   843,
     844,   845,   846,   847,   997,     0,   848,   849,   850,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,  1000,  1001,  1002,  1003,  1004,  1005,
    1006,  1007,  1008,  1009,  1010,   432,     0,  1011,  1012,  1013,
       0,   433,   434,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,  -793,     0,    26,     0,     0,     0,   153,
     154,   155,   156,    77,    78,    79,    80,    27,     0,   233,
     436,     0,     0,     0,     0,     0,   437,    30,     0,     0,
       0,     0,     0,   157,     0,     0,  -775,   438,     0,     0,
       0,  -775,     0,   439,     0,   440,     0,     0,     0,     0,
       0,     0,     0,   441,     0,     0,    36,    67,     0,   158,
       0,     0,   442,     0,   443,    68,     0,  -775,   444,     0,
     445,     0,     0,     0,     0,     0,     0,     0,   235,     0,
       0,     0,     0,     0,     0,   446,     0,   447,     0,   448,
       0,   449,   450,   432,     0,   451,   452,   453,   454,   736,
     737,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,  -793,     0,    26,     0,     0,     0,   153,   154,   155,
     156,    77,    78,    79,    80,    27,     0,   233,   739,     0,
       0,     0,     0,     0,   740,    30,     0,     0,     0,     0,
       0,   157,     0,     0,  -775,   438,     0,     0,     0,  -775,
       0,   439,     0,   742,     0,     0,     0,     0,     0,     0,
       0,   743,     0,     0,    36,    67,     0,   158,     0,     0,
     442,     0,   744,    68,     0,  -775,   745,     0,   746,     0,
       0,     0,     0,     0,     0,     0,   235,     0,     0,     0,
       0,     0,     0,   446,     0,   747,     0,   748,     0,   749,
     750,   432,     0,   751,   452,   752,   753,   433,   434,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    26,     0,     0,     0,   153,   154,   155,   156,    77,
      78,    79,    80,    27,     0,   233,   436,     0,     0,     0,
       0,     0,   437,    30,     0,     0,     0,     0,     0,   157,
       0,     0,  -775,   438,     0,     0,     0,  -775,     0,   439,
       0,   440,     0,     0,     0,     0,     0,     0,     0,   441,
       0,     0,    36,    67,     0,   158,     0,     0,   442,     0,
     443,    68,     0,  -775,   444,     0,   445,     0,     0,     0,
       0,     0,     0,     0,   235,     0,     0,     0,     0,     0,
       0,   446,     0,   447,     0,   448,     0,   449,   450,   432,
       0,   451,   452,   453,   454,   736,   737,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   738,     0,    26,
       0,     0,     0,   153,   154,   155,   156,    77,    78,    79,
      80,    27,     0,   233,   739,     0,     0,     0,     0,     0,
     740,    30,     0,     0,     0,     0,     0,   157,     0,     0,
       0,   438,     0,     0,   741,     0,     0,   439,     0,   742,
       0,     0,     0,     0,     0,     0,     0,   743,     0,     0,
      36,    67,     0,   158,     0,     0,   442,     0,   744,    68,
       0,     0,   745,     0,   746,     0,     0,     0,     0,     0,
       0,     0,   235,     0,     0,     0,     0,     0,     0,   446,
       0,   747,     0,   748,     0,   749,   750,   432,     0,   751,
     452,   752,   753,   736,   737,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   738,     0,    26,     0,     0,
       0,   153,   154,   155,   156,    77,    78,    79,    80,    27,
       0,   233,   739,     0,     0,     0,     0,     0,   740,    30,
       0,     0,     0,     0,     0,   157,     0,     0,     0,   438,
       0,     0,  1135,     0,     0,   439,     0,   742,     0,     0,
       0,     0,     0,     0,     0,   743,     0,     0,    36,    67,
       0,   158,     0,     0,   442,     0,   744,    68,     0,     0,
     745,     0,   746,     0,     0,     0,     0,     0,     0,     0,
     235,     0,     0,     0,     0,     0,     0,   446,     0,   747,
       0,   748,     0,   749,   750,   432,     0,   751,   452,   752,
     753,   433,   434,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   435,     0,    26,     0,     0,     0,   153,
     154,   155,   156,    77,    78,    79,    80,    27,     0,   233,
     436,     0,     0,     0,     0,     0,   437,    30,     0,     0,
       0,     0,     0,   157,     0,     0,     0,   438,     0,     0,
       0,     0,     0,   439,     0,   440,     0,     0,     0,     0,
       0,     0,     0,   441,     0,     0,    36,    67,     0,   158,
       0,     0,   442,     0,   443,    68,     0,     0,   444,     0,
     445,     0,     0,     0,     0,     0,     0,     0,   235,     0,
       0,     0,     0,     0,     0,   446,     0,   447,     0,   448,
       0,   449,   450,   432,     0,   451,   452,   453,   454,   736,
     737,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   738,     0,    26,     0,     0,     0,   153,   154,   155,
     156,    77,    78,    79,    80,    27,     0,   233,   739,     0,
       0,     0,     0,     0,   740,    30,     0,     0,     0,     0,
       0,   157,     0,     0,     0,   438,     0,     0,     0,     0,
       0,   439,     0,   742,     0,     0,     0,     0,     0,     0,
       0,   743,     0,     0,    36,    67,     0,   158,     0,     0,
     442,     0,   744,    68,     0,     0,   745,     0,   746,     0,
       0,     0,     0,     0,     0,     0,   235,     0,     0,     0,
       0,     0,     0,   446,     0,   747,     0,   748,     0,   749,
     750,   432,     0,   751,   452,   752,   753,   433,   434,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   779,
       0,    26,     0,     0,     0,   153,   154,   155,   156,    77,
      78,    79,    80,    27,     0,   233,   780,     0,     0,     0,
       0,     0,   781,    30,     0,     0,     0,     0,     0,   157,
       0,     0,     0,   438,     0,     0,     0,     0,     0,   439,
       0,   440,     0,     0,     0,     0,     0,     0,     0,   783,
       0,     0,    36,    67,     0,   158,     0,     0,   442,     0,
     785,    68,     0,     0,   786,     0,   787,     0,     0,     0,
       0,     0,     0,     0,   235,     0,     0,     0,   332,     0,
       0,   446,     0,   447,   333,   448,     0,   449,   450,     0,
       0,   451,   452,   788,   789,     0,   534,     0,    26,     0,
       0,     0,   153,   154,   155,   156,    77,    78,    79,    80,
      27,   334,     0,    28,     0,     0,     0,     0,     0,     0,
      30,     0,     0,     0,     0,     0,   157,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   335,
       0,   332,     0,     0,     0,   336,     0,   333,     0,    36,
      67,     0,   158,     0,     0,     0,     0,     0,    68,     0,
       0,    26,     0,   337,     0,   153,   154,   155,   156,    77,
      78,    79,    80,    27,   334,     0,    28,     0,   338,     0,
       0,     0,   339,    30,   340,     0,     0,     0,     0,   157,
     341,   342,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   335,     0,   332,     0,     0,     0,   336,     0,
     333,     0,    36,    67,     0,   158,     0,     0,     0,     0,
       0,    68,     0,     0,    26,     0,   337,     0,   153,   154,
     155,   156,    77,    78,    79,    80,    27,   334,     0,    28,
       0,   338,     0,   771,     0,   339,    30,   340,     0,     0,
       0,     0,   157,   341,   342,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   335,     0,   332,     0,     0,
       0,   336,     0,   333,     0,    36,    67,     0,   158,     0,
       0,     0,     0,     0,    68,  1168,     0,    26,     0,   337,
       0,   153,   154,   155,   156,    77,    78,    79,    80,    27,
     334,     0,    28,     0,   338,     0,   970,     0,   339,    30,
     340,     0,     0,     0,     0,   157,   341,   342,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   335,     0,
     332,     0,     0,     0,   336,     0,   333,     0,    36,    67,
       0,   158,     0,     0,     0,     0,     0,    68,  1172,     0,
      26,     0,   337,     0,   153,   154,   155,   156,    77,    78,
      79,    80,    27,   334,     0,    28,     0,   338,     0,     0,
       0,   339,    30,   340,     0,     0,     0,     0,   157,   341,
     342,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   335,     0,   332,     0,     0,     0,   336,     0,   333,
       0,    36,    67,     0,   158,     0,     0,     0,     0,     0,
      68,     0,     0,    26,     0,   337,     0,   153,   154,   155,
     156,    77,    78,    79,    80,    27,   334,     0,    28,     0,
     338,     0,     0,     0,   339,    30,   340,     0,     0,     0,
       0,   157,   341,   342,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   335,     0,   332,     0,     0,     0,
     336,     0,   333,     0,    36,    67,     0,   158,     0,     0,
       0,     0,     0,    68,     0,     0,    26,     0,   337,     0,
     153,   154,   155,   156,    77,    78,    79,    80,    27,   334,
       0,    28,     0,   338,     0,  1264,     0,   339,    30,   340,
       0,     0,     0,     0,   157,   341,   342,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   335,     0,   332,
       0,     0,     0,   336,     0,   333,     0,    36,    67,     0,
     158,     0,     0,     0,     0,     0,    68,     0,     0,    26,
       0,   337,     0,   153,   154,   155,   156,    77,    78,    79,
      80,    27,   334,   233,     0,     0,   338,     0,  1387,     0,
     339,    30,   340,     0,     0,     0,     0,   157,   341,   342,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
    1601,     0,     0,     0,     0,     0,   336,     0,     0,     0,
      36,    67,     0,   158,     0,     0,     0,     0,     0,    68,
       0,     0,     0,     0,   337,     0,     0,     0,     0,   332,
       0,     0,   235,     0,     0,   333,     0,     0,     0,   338,
       0,     0,     0,   339,     0,   340,     0,     0,     0,    26,
       0,   341,   342,   153,   154,   155,   156,    77,    78,    79,
      80,    27,   334,     0,    28,     0,     0,     0,     0,     0,
       0,    30,     0,     0,     0,     0,     0,   157,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     335,     0,   332,     0,     0,     0,   336,     0,   333,     0,
      36,    67,     0,   158,     0,     0,     0,     0,     0,    68,
       0,     0,    26,     0,   337,     0,   153,   154,   155,   156,
      77,    78,    79,    80,    27,   334,     0,    28,     0,   338,
       0,     0,     0,   339,    30,   340,     0,     0,     0,     0,
     157,   341,   342,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   531,     0,   332,     0,     0,     0,   336,
       0,   333,     0,    36,    67,     0,   158,     0,     0,     0,
       0,     0,    68,     0,     0,    26,     0,   337,     0,   153,
     154,   155,   156,    77,    78,    79,    80,    27,   334,     0,
    -406,     0,   338,     0,     0,     0,   339,    30,   340,     0,
       0,     0,     0,   157,   341,   342,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,  1581,     0,   332,     0,
       0,     0,   336,     0,   333,     0,    36,    67,     0,   158,
       0,     0,     0,     0,     0,    68,     0,     0,    26,     0,
     337,     0,   153,   154,   155,   156,    77,    78,    79,    80,
      27,   334,     0,     0,     0,   338,     0,     0,     0,  1582,
      30,   340,     0,     0,     0,     0,   157,   341,   342,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   335,
       0,     0,     0,     0,     0,   336,     0,     0,     0,    36,
      67,     0,   158,   164,     0,     0,     0,     0,    68,   165,
     166,     0,     0,   337,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   167,     0,     0,     0,     0,   338,     0,
       0,     0,   339,     0,   340,    27,   168,   233,   169,     0,
     341,   342,     0,     0,     0,    30,     0,     0,     0,     0,
     170,     0,     0,   171,   172,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   267,     0,     0,   164,     0,     0,
       0,     0,     0,   165,   166,    67,     0,     0,     0,     0,
     173,     0,     0,    68,     0,     0,     0,   167,   174,     0,
       0,   175,     0,     0,     0,     0,   235,     0,     0,    27,
     168,   233,   169,   176,     0,   177,     0,   178,     0,    30,
     179,     0,     0,     0,   170,   180,   181,   171,   172,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   294,     0,
       0,   164,     0,     0,     0,     0,     0,   165,   166,    67,
       0,     0,     0,     0,   173,     0,     0,    68,     0,     0,
       0,   167,   174,     0,     0,   175,     0,     0,     0,     0,
     235,     0,     0,    27,   474,   233,   169,   176,     0,   177,
       0,   178,     0,    30,   179,     0,     0,     0,   170,   180,
     181,   171,   172,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   267,     0,     0,   164,     0,     0,     0,     0,
       0,   165,   166,    67,     0,     0,     0,     0,   173,     0,
       0,    68,     0,     0,     0,   167,   174,     0,     0,   175,
       0,     0,     0,     0,   235,     0,     0,    27,   477,   233,
     169,   176,     0,   177,     0,   178,     0,    30,   179,     0,
       0,     0,   170,   180,   181,   171,   172,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   294,     0,     0,   164,
       0,     0,     0,     0,     0,   165,   166,    67,     0,     0,
       0,     0,   173,     0,     0,    68,     0,   582,     0,   167,
     174,     0,     0,   175,     0,     0,     0,     0,   235,     0,
       0,    27,   168,   233,   169,   176,     0,   177,     0,   178,
       0,    30,   179,     0,     0,     0,   170,   180,   181,   171,
     172,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   164,     0,     0,     0,     0,     0,   165,
     166,    67,     0,     0,     0,     0,   173,     0,     0,    68,
       0,   935,     0,   167,   174,     0,     0,   175,     0,     0,
       0,     0,   235,     0,     0,    27,   168,   233,   169,   176,
       0,   177,     0,   178,     0,    30,   179,     0,     0,     0,
     170,   180,   181,   171,   172,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   164,     0,     0,
       0,     0,     0,   165,   166,    67,     0,     0,     0,     0,
     173,     0,     0,    68,     0,     0,     0,   167,   174,     0,
       0,   175,     0,     0,     0,     0,   235,     0,     0,    27,
     168,   233,   169,   176,     0,   177,     0,   178,     0,    30,
     179,     0,     0,     0,   170,   180,   181,   171,   172,     0,
       0,     0,     0,     0,     0,     0,   164,     0,     0,     0,
       0,     0,   285,   166,     0,     0,     0,     0,     0,    67,
       0,     0,     0,     0,   173,  1206,   167,    68,     0,     0,
       0,     0,   174,     0,     0,   175,     0,     0,    27,   286,
     235,   169,     0,     0,     0,     0,     0,   176,    30,   177,
       0,   178,     0,   170,   179,     0,   171,   172,     0,   180,
     181,     0,     0,     0,     0,   164,     0,   287,     0,     0,
       0,   165,   166,     0,     0,     0,     0,     0,    67,     0,
       0,     0,     0,   173,     0,   167,    68,     0,     0,     0,
       0,   174,     0,     0,   175,     0,     0,    27,   477,   233,
       0,     0,     0,     0,     0,     0,   176,    30,   177,     0,
     288,     0,   170,   179,     0,   171,   172,     0,   180,   181,
       0,     0,     0,     0,   164,     0,  1591,     0,     0,     0,
     285,   166,     0,     0,     0,     0,     0,    67,     0,     0,
       0,     0,   173,     0,   167,    68,     0,     0,     0,     0,
     174,     0,     0,   175,     0,     0,    27,   286,   235,   169,
       0,     0,     0,     0,     0,   176,    30,   177,     0,   178,
       0,   170,   179,     0,   171,   172,     0,   180,   181,     0,
       0,     0,     0,   164,     0,   287,     0,     0,     0,   285,
     166,     0,     0,     0,     0,     0,    67,     0,     0,     0,
       0,   173,     0,   167,    68,     0,     0,     0,     0,   174,
       0,     0,   175,     0,     0,    27,   286,     0,  -406,     0,
       0,     0,     0,     0,   176,    30,   177,     0,   288,     0,
     170,   179,     0,   171,   172,     0,   180,   181,     0,     0,
       0,     0,   164,     0,  1571,     0,     0,     0,   165,   166,
       0,     0,     0,     0,     0,    67,     0,     0,     0,     0,
     173,     0,   167,    68,     0,     0,     0,     0,   174,     0,
       0,   175,     0,     0,    27,   168,     0,   169,     0,     0,
       0,     0,     0,   176,    30,   177,     0,  1572,     0,   170,
     179,     0,   171,   172,     0,   180,   181,     0,     0,     0,
       0,   164,     0,     0,     0,     0,     0,   165,   166,     0,
       0,     0,     0,     0,    67,     0,     0,     0,     0,   173,
       0,   167,    68,     0,     0,     0,     0,   174,     0,     0,
     175,     0,     0,    27,   168,     0,  1622,     0,     0,     0,
       0,     0,   176,    30,   177,     0,   178,     0,   170,   179,
       0,   171,   172,     0,   180,   181,     0,     0,     0,     0,
     164,     0,     0,     0,     0,     0,   165,   166,     0,     0,
       0,     0,     0,    67,     0,     0,     0,     0,   173,     0,
     167,    68,     0,     0,     0,     0,   174,     0,     0,   175,
       0,     0,    27,   168,     0,     0,     0,     0,     0,     0,
       0,   176,    30,   177,     0,   178,     0,   170,   179,     0,
     171,   172,     0,   180,   181,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    67,     0,     0,     0,     0,   173,     0,     0,
      68,     0,     0,     0,     0,   174,     0,     0,   175,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     176,     0,   177,     0,   178,     0,     0,   179,     0,     0,
      26,     0,   180,   181,   153,   154,   155,   156,    77,    78,
      79,    80,    27,     0,   233,   739,     0,     0,     0,     0,
       0,   740,    30,     0,     0,     0,     0,     0,   157,     0,
       0,  -775,   438,     0,     0,     0,  -775,     0,   439,     0,
     742,     0,     0,     0,     0,     0,     0,     0,   743,     0,
       0,    36,    67,     0,   158,     0,     0,   442,     0,   744,
      68,     0,  -775,   745,     0,   746,     0,     0,     0,     0,
       0,     0,     0,   235,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     751
  };

  const short int
  parser::yycheck_[] =
  {
      16,   281,   146,   106,   146,    89,   296,   284,   113,   312,
     271,   270,   424,   452,    32,   544,     3,   148,   447,   145,
     567,     9,     9,    16,   571,   452,   315,    26,    15,   364,
     581,   304,    12,   164,   454,   549,   326,   145,   143,    76,
     146,   167,   881,   792,   406,   176,   795,    84,   452,    86,
      87,    88,   921,  1161,   923,   891,   278,   280,   163,   491,
     165,   895,  1011,   114,   222,  1272,   453,   104,   480,   174,
     218,    89,   177,   178,    73,   180,   873,   863,   379,   150,
     228,   150,    19,  1255,   145,  1255,   170,     9,     4,    10,
     127,  1195,     0,     4,    42,    21,   123,   124,   125,    58,
      42,    42,     4,    10,   141,   752,   232,   144,     3,     3,
     236,     0,   181,    64,    90,    90,    95,    23,    21,    93,
      37,    55,    92,    93,    94,    92,    93,    94,   236,     3,
    1079,   118,   237,   108,   121,    10,    42,     3,    89,     3,
     116,   788,    21,   433,   270,     3,   120,   273,   421,   102,
     120,     3,   170,   120,   124,   114,    54,   124,   448,    10,
     108,    59,   267,   116,   425,    10,   108,   428,    10,     3,
      10,   105,   109,   434,   131,    50,   281,     0,    53,    96,
     285,   208,   123,   288,   105,    93,   213,    85,   104,   294,
     316,   207,   229,   104,   299,   279,   122,    23,   105,   121,
     423,   332,   104,    92,    93,    94,   122,   338,   103,   510,
     368,   122,   120,   240,   202,   202,   124,   322,   205,   122,
     122,   116,   210,   210,   118,   212,   357,   121,   376,   103,
     105,   120,   333,    99,   365,    99,   337,   103,   339,   103,
     341,   342,   116,   122,   102,   103,   505,    99,   353,   526,
     116,   103,   116,   530,   105,   381,   387,   479,   116,  1363,
     105,   279,     4,   105,   116,   105,     8,     3,   225,   103,
      12,   122,     3,   399,   405,   548,    23,   122,    93,    42,
     122,  1488,   122,    23,    53,   411,  1621,   113,   210,   721,
       4,   399,    37,    55,     8,    23,  1631,   115,    12,    35,
    1554,   432,   407,   411,    23,   120,   121,    23,   115,    45,
     415,    42,    23,     4,    23,   446,  1570,   333,   747,   424,
      89,   337,    68,   339,   385,   341,   342,   264,   397,   366,
    1502,  1167,  1502,   753,   405,    54,   323,  1171,    74,    75,
     367,   121,   122,    54,    35,   114,   447,    83,   375,    23,
    1147,    96,   114,   792,    45,     4,   795,   105,   489,   122,
      42,   124,   104,  1363,     4,   752,   113,   103,   795,   789,
    1156,    99,   103,   113,   122,   480,   481,  1377,   483,   505,
     511,    93,  1374,   930,    75,    90,    35,   490,  1380,  1596,
     104,   795,    83,   907,   113,    35,    45,    37,  1605,   115,
     116,   788,   113,   108,  1611,    45,   115,   116,   120,  1616,
      42,   569,   124,   104,   545,  1622,   105,    56,   566,   114,
    1627,   104,   438,   439,   121,   122,    75,    35,   123,   849,
     531,   447,    78,   122,    83,    75,   452,    45,   727,   122,
     122,    80,   124,    83,  1444,    50,   736,   578,    53,   550,
     551,    93,  1452,     3,   731,   104,    96,  1449,   748,     9,
      10,   720,   513,    54,   104,   113,  1415,    75,  1417,    56,
       3,     4,   904,    23,    93,    83,  1562,   916,   120,    23,
     122,    35,   124,    39,    38,    35,    36,   524,    38,   916,
     122,    45,   124,   770,  1580,    45,    99,    53,   101,   119,
      50,   120,   108,    53,    54,   124,   729,   108,   730,    42,
     771,   102,   916,   121,  1350,   531,    93,   123,    99,  1353,
    1628,    75,   123,   114,    80,    75,    79,    80,   544,    83,
      80,   547,   123,    83,   550,   551,    89,   949,    88,    50,
     813,    91,    53,   120,   122,   572,   124,   124,    50,   576,
     119,    53,   579,   103,    56,   105,   583,   107,  1626,  1428,
     110,    50,    93,   113,    53,   115,   116,  1635,   103,   872,
     124,   114,   559,   106,   107,   108,   109,   110,   111,   112,
     123,   943,   115,   116,   117,     3,     4,    93,  1015,   120,
      53,   122,  1012,   124,   720,   856,   114,    23,   114,    23,
     726,   114,     3,   113,   114,   123,   867,   123,     9,    35,
     123,    35,   114,    37,   120,   121,   122,  1456,   726,    45,
      21,    45,    23,   882,    42,   123,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    90,    38,    35,    99,
     741,    38,  1189,  1194,    45,    92,   747,    94,    45,    75,
      51,    75,  1203,    23,    53,   113,   114,    83,   116,    83,
    1080,     3,     4,    64,    99,    35,  1180,   116,    38,    70,
      35,    35,    96,    74,    75,    45,    77,   122,    75,   124,
      45,    45,    83,   114,   115,   116,    83,    88,   106,   107,
     108,   109,   110,   111,   112,   956,    53,   115,   116,   117,
      42,   122,   103,   124,    74,    75,   107,    99,   109,   970,
      75,    75,    93,    83,   115,   116,    80,    23,    83,    83,
     121,   737,   738,   103,   122,   741,   124,   124,   116,   834,
     835,   747,    35,   749,   750,   751,   102,   868,    35,   120,
     110,    38,    45,   124,   114,   110,   121,   122,    45,   114,
     876,   852,     3,     4,     5,   881,   882,     8,  1019,  1508,
     115,   116,   117,    21,  1191,   107,   108,   109,   110,   111,
     112,    29,    75,   115,   116,   117,   792,    35,    75,   795,
      83,   122,    93,   124,    99,  1214,    83,    45,    54,   782,
     891,    42,    35,   894,   895,    38,   114,   898,   101,   792,
     901,    35,    45,    93,    31,    32,    33,    34,    93,   120,
     941,    45,   792,   124,   124,    50,    93,    75,    53,    53,
    1155,    56,  1373,    35,    24,    83,    38,   124,   122,   123,
     120,  1258,    75,    45,   124,   120,   852,   124,   854,   124,
      83,    75,   114,   120,   949,   102,    80,   124,   122,    83,
     124,   850,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,  1151,    75,   115,   116,   117,    93,   113,   114,
      35,    83,   102,    38,    35,   891,    35,    38,   894,   895,
      45,   124,   898,    54,    45,   901,    45,   115,   116,   117,
    1021,    35,   997,   998,   120,   108,    93,   884,   124,   926,
     916,    45,  1163,    54,  1433,    64,    93,   113,   114,   936,
      75,    70,   124,    74,    75,   123,    75,  1018,    83,  1020,
     121,   122,    83,   120,    83,   115,   116,   124,  1205,   917,
      74,    75,   123,   120,  1035,   102,   102,   124,  1365,    83,
     245,   246,    42,   122,   960,  1089,   122,  1089,   114,   110,
     116,   938,   122,   114,    64,    53,   972,    99,   116,   124,
    1065,  1066,   978,   979,   980,   981,   982,   983,   984,   985,
     986,   987,   988,   989,   990,   991,   992,   993,   994,   995,
     996,     3,     4,  1089,  1000,  1001,  1002,  1003,  1004,  1005,
    1006,  1007,  1008,  1009,  1010,    35,  1416,    37,  1418,   102,
      99,  1274,  1018,  1264,  1020,    45,   103,   122,   108,   109,
     110,   111,   112,   124,  1013,   115,   116,   117,   123,  1035,
      42,    31,    32,    33,    34,    53,    64,   121,    99,  1576,
      42,   122,  1461,    22,  1135,    75,    46,   119,  1585,   125,
     122,   123,  1589,    83,   114,  1038,   124,    42,   123,    99,
      23,    23,  1599,    54,  1185,  1160,    96,   121,   114,   104,
     108,   104,    35,  1610,   104,    38,  1167,   122,  1615,  1508,
    1171,    53,    45,   114,  1335,  1176,    99,  1336,   104,     3,
       4,  1182,  1081,    99,  1083,  1084,   108,   109,   110,   111,
     112,    99,    21,   115,   116,   117,   108,   109,   110,   111,
     112,    74,    75,   115,   116,   117,   121,   103,   122,   108,
      83,   108,    99,  1214,  1643,   110,   111,   112,    42,  1135,
     115,   116,   117,    99,    35,   125,  1387,   119,   122,   122,
      23,     3,     4,     5,    45,  1162,     8,   121,   101,  1442,
     114,   122,    35,   122,    37,  1161,   104,  1408,   102,    23,
      99,  1167,    45,    64,   122,  1171,  1257,   113,  1435,    29,
    1176,    54,   124,   122,    75,    35,  1182,   117,  1427,   117,
      42,    23,    83,   119,   121,    45,   103,    99,   116,  1195,
     122,  1458,    75,  1595,   108,   109,   110,   111,   112,   104,
      83,   115,   116,   117,   104,    99,  1212,  1213,  1214,  1215,
      99,  1462,    42,    96,    74,    75,   122,   124,   101,   122,
    1336,    99,  1199,    83,   121,     3,     4,     5,     6,     7,
       8,     9,   114,  1500,   114,   114,   124,   114,  1640,    23,
     121,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,  1257,    29,   115,   116,   117,    35,  1348,    35,  1350,
    1351,   104,  1353,   102,    42,  1356,    45,   119,    45,   104,
     121,    80,    48,     3,     4,   119,   121,  1411,  1558,  1411,
     110,   111,   112,    99,  1564,   115,   116,   117,  1558,    21,
     124,    99,  1572,   119,   121,    57,    75,    74,    75,   121,
     104,    23,  1582,    23,    83,   122,    83,   122,   122,  1560,
     122,  1427,    42,  1593,    99,  1566,  1407,   124,   124,   114,
      23,   124,   119,  1603,  1330,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,     3,     4,   115,   116,   117,
    1456,    42,  1348,    23,  1350,  1351,  1437,  1353,  1439,   121,
    1356,   122,   119,  1623,   124,   121,   102,  1363,   121,   104,
     124,   108,   123,   123,   122,  1458,   122,   104,   102,   122,
    1461,   122,   124,  1464,    42,   105,   106,   107,   108,   109,
     110,   111,   112,   124,    23,   115,   116,   117,    48,    26,
      23,   119,   123,  1484,  1485,   121,   119,  1374,   104,   123,
     121,  1407,   121,  1380,    53,    53,   124,   104,    23,    23,
     102,    53,   102,    53,    23,  1489,  1509,   104,  1424,   123,
      35,  1495,   116,    38,    23,   116,  1547,  1433,   121,  1377,
      45,  1437,    38,  1439,   122,    38,   121,   105,   106,   107,
     108,   109,   110,   111,   112,    38,    38,   115,   116,   117,
      38,   122,    38,   122,  1460,  1461,   121,  1463,  1464,    74,
      75,   122,   121,  1558,    79,    80,  1493,   121,    83,    18,
      19,   121,  1449,    15,    89,  1568,   122,  1572,  1484,  1485,
      29,  1489,    31,   121,    33,    34,    35,  1495,    37,    38,
     121,  1582,    41,    42,  1587,   559,  1591,   202,  1525,  1505,
    1595,   884,  1508,  1530,   328,    21,   205,  1365,  1199,    82,
    1601,  1564,   707,    29,  1207,  1542,  1020,  1612,   514,    35,
     547,  1548,   315,  1180,   550,    74,   726,   726,  1623,    45,
    1557,   400,    81,    29,   438,   439,  1563,   385,   442,    35,
     876,    90,  1559,    92,   882,  1640,   795,    96,  1565,    45,
     877,   100,  1569,   916,   103,   440,   105,   106,  1564,    75,
     109,  1407,  1579,   742,   113,   114,  1461,    83,  1045,  1326,
    1258,  1508,  1258,  1590,  1502,    -1,  1582,    -1,    74,    75,
      -1,    -1,   131,  1600,    -1,     3,     4,    83,    -1,    -1,
     139,   140,   439,   142,   143,  1601,   145,    -1,    -1,   148,
      -1,   150,    -1,    -1,    -1,    -1,    -1,    29,    -1,    -1,
      -1,    -1,    -1,    35,   163,   164,   165,    -1,   167,    -1,
      -1,    -1,  1628,    45,    42,   174,    -1,   176,   177,   178,
      -1,   180,   181,    -1,    -1,    10,    -1,  1643,    -1,    -1,
      -1,    -1,    -1,    23,    -1,    -1,    -1,    -1,    23,    -1,
      -1,   200,    74,    75,    -1,    35,    -1,    37,   207,    -1,
      35,    83,    -1,    -1,    -1,    45,    -1,    -1,   217,    -1,
      45,    -1,   221,   222,    54,    50,   225,    -1,    53,    -1,
      -1,    -1,    -1,   232,    -1,    -1,    -1,   236,   237,   107,
     108,   109,   110,   111,   112,    75,    -1,   115,   116,   117,
      75,    -1,    -1,    83,    -1,    80,    29,    -1,    83,    -1,
      -1,    -1,    35,    -1,    -1,   264,    96,    -1,   267,    -1,
       9,   270,    45,    -1,   273,    -1,    -1,    -1,    -1,    -1,
     105,    -1,   281,    -1,    23,    -1,   285,    -1,   287,   288,
      -1,    -1,    -1,    -1,    -1,   294,    35,    36,    37,    38,
     299,    74,    75,    -1,   303,   304,    45,   306,    -1,    -1,
      83,    50,    -1,   312,    53,    54,   315,   316,   317,    -1,
      -1,    -1,    -1,   322,    -1,    -1,    -1,    -1,   327,    -1,
      -1,    -1,    -1,   332,   333,    -1,    75,    -1,   337,   338,
     339,    80,   341,   342,    83,    -1,    -1,    -1,   347,    88,
      -1,    -1,    91,    -1,   353,    -1,    -1,    96,   357,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   365,    -1,   107,   368,
      -1,   110,    -1,    -1,   113,    -1,   115,   116,    -1,   378,
      -1,    -1,   381,   737,   738,     3,   385,    -1,   387,    -1,
      -1,     9,    10,    -1,    -1,   749,   750,   751,   397,    -1,
     399,   400,    -1,   757,    -1,    23,   405,   406,   407,    -1,
      -1,    -1,   411,    -1,    -1,    -1,   415,    35,    36,    37,
      38,    -1,   421,    -1,    -1,   424,    -1,    45,    -1,    -1,
     784,    -1,    50,   432,    -1,    53,    54,   791,   437,    -1,
     737,   738,    -1,    -1,    -1,   444,    -1,   446,   447,    -1,
      -1,    -1,   749,   750,   751,    -1,    -1,    75,    -1,    -1,
      -1,    -1,    80,    -1,    -1,    83,    -1,    -1,    -1,    -1,
      88,    -1,    21,    91,    -1,    -1,    -1,    -1,    96,    -1,
      29,   480,   481,    -1,   483,   103,    35,   105,    -1,   107,
     489,    -1,   110,    21,    -1,    -1,    45,   115,   116,   853,
     854,    29,    -1,   121,    -1,    -1,   505,    35,    -1,    -1,
      -1,    -1,   511,    -1,    -1,    64,    -1,    45,   517,    -1,
      -1,    70,    -1,    -1,   523,    -1,    75,    -1,    -1,    -1,
      -1,    -1,   531,    -1,    83,    -1,    64,    -1,    -1,    88,
      -1,    -1,    70,    -1,    -1,   544,   545,    75,    -1,   548,
     549,   550,   551,    -1,    -1,    83,    -1,   854,    -1,    -1,
      88,    -1,   561,    -1,     3,     4,     5,     6,     7,     8,
     569,    39,    -1,    -1,    -1,   574,    -1,    -1,    -1,   578,
      -1,    49,    50,    -1,    -1,    53,    -1,    -1,    56,    -1,
      -1,    -1,    -1,    -1,    62,    27,    28,    29,    30,    31,
      32,    33,    34,    42,    -1,    73,   960,    75,    -1,    -1,
      -1,    79,    80,    -1,    -1,    83,    84,    -1,   972,    51,
      -1,    89,    -1,    -1,   978,   979,   980,   981,   982,   983,
     984,   985,   986,   987,   988,   989,   990,   991,   992,   993,
     994,   995,   996,    -1,    -1,    77,  1000,  1001,  1002,  1003,
    1004,  1005,  1006,  1007,  1008,  1009,  1010,    -1,    -1,    -1,
      -1,    -1,    -1,   960,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,    -1,   972,   115,   116,   117,    -1,
      -1,   978,   979,   980,   981,   982,   983,   984,   985,   986,
     987,   988,   989,   990,   991,   992,   993,   994,   995,   996,
      -1,    -1,    -1,  1000,  1001,  1002,  1003,  1004,  1005,  1006,
    1007,  1008,  1009,  1010,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   720,    -1,    -1,    -1,    -1,    -1,   726,    -1,    -1,
      -1,    -1,    -1,     3,    -1,    -1,    -1,    -1,    -1,     9,
      -1,   740,   741,    -1,    -1,    -1,   745,    -1,   747,    -1,
      -1,    21,    -1,    23,    -1,    -1,    -1,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    -1,    38,    -1,
      -1,    -1,    -1,    -1,    -1,    45,    -1,    -1,    -1,    -1,
      -1,    51,  1136,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    64,    -1,    -1,    -1,    -1,    -1,
      70,    -1,    -1,    -1,    74,    75,    -1,    77,    -1,    -1,
      -1,    -1,    -1,    83,   813,   814,    -1,    -1,    88,    -1,
      -1,    -1,    -1,    -1,    -1,   108,     3,     4,     5,     6,
       7,     8,     9,   103,    -1,   834,   835,   107,    -1,   109,
      -1,    -1,    -1,    -1,    -1,   115,   116,    -1,    -1,    -1,
      -1,   121,    -1,   852,    -1,    -1,    -1,    -1,  1212,  1213,
      -1,  1215,    -1,    -1,   863,    42,    -1,    -1,    -1,   868,
      -1,    -1,    -1,   872,    -1,    -1,    -1,   876,    -1,    -1,
      -1,    -1,   881,   882,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   891,    -1,    -1,   894,   895,    -1,    -1,   898,
      -1,  1255,   901,    -1,    -1,    -1,    -1,    -1,   907,   908,
      -1,    -1,    -1,    -1,    -1,  1212,  1213,    -1,  1215,    -1,
      -1,    -1,    -1,    -1,   207,    -1,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,    -1,    -1,   115,   116,
     117,    -1,   941,    -1,   943,    -1,    -1,   946,    -1,    -1,
     949,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,  1330,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    -1,    -1,   997,   998,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    42,    -1,    -1,  1018,
      -1,  1020,  1021,    -1,    -1,    -1,    -1,  1026,    -1,    -1,
      -1,  1385,    -1,  1330,    -1,    -1,  1035,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,  1045,    -1,    -1,    -1,
     333,    -1,    -1,    -1,   337,    -1,   339,   340,   341,   342,
      -1,    -1,    -1,    -1,    -1,    -1,  1065,  1066,    -1,  1423,
    1424,    -1,    -1,    99,    -1,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,    -1,    -1,   115,
     116,   117,    -1,    -1,    -1,    -1,    -1,   123,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,     9,  1460,    -1,    -1,  1463,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      -1,    -1,    -1,    -1,  1478,    -1,    -1,  1424,    -1,    -1,
      -1,    35,    36,    37,    38,    -1,  1135,    -1,  1137,    -1,
      -1,    45,    -1,    -1,    -1,    -1,    50,    -1,  1502,    53,
      54,  1505,    -1,    -1,  1508,   438,   439,  1156,    -1,    -1,
      -1,  1160,  1161,  1460,   447,    -1,  1463,    -1,  1167,   452,
      -1,    75,  1171,    -1,    -1,    -1,    80,  1176,  1177,    83,
      -1,  1180,    -1,  1182,    88,    -1,  1185,    91,    -1,    -1,
      -1,    -1,    96,    -1,    -1,    -1,     3,     4,     5,     6,
       7,     8,    -1,   107,    -1,  1204,   110,    -1,  1505,    -1,
      -1,   115,    -1,    -1,    -1,  1214,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    42,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   531,    -1,
      -1,    -1,    -1,    -1,    -1,    42,    -1,    -1,  1257,    -1,
      -1,   544,    -1,    -1,   547,    -1,    -1,   550,   551,    -1,
      -1,    -1,    -1,    -1,    -1,  1274,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,    -1,    -1,   115,   116,
     117,    -1,    99,    -1,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,    -1,  1326,   115,   116,
     117,    -1,    -1,    -1,    -1,    -1,   123,  1336,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,  1346,  1347,  1348,
      -1,  1350,  1351,    -1,  1353,    -1,    -1,  1356,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,  1384,    -1,    -1,    -1,     3,
      -1,    -1,    -1,    -1,    -1,     9,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,  1405,    -1,  1407,    23,
      -1,    -1,    -1,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    -1,    38,    -1,    -1,    -1,  1427,    -1,
      -1,    45,    -1,    -1,  1433,    -1,    -1,    51,  1437,    -1,
    1439,    -1,    -1,  1442,    -1,  1444,    -1,    -1,    -1,    -1,
      64,    -1,  1451,  1452,   737,   738,    70,  1456,   741,    -1,
      74,    75,  1461,    77,   747,  1464,   749,   750,   751,    83,
      -1,    -1,    -1,    -1,    88,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,  1484,  1485,    -1,    -1,   103,
      -1,  1490,    -1,   107,    -1,   109,    -1,  1496,    -1,    -1,
    1499,   115,   116,    -1,    -1,    -1,    -1,   121,    -1,   792,
      -1,    -1,   795,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,  1522,    -1,    23,    -1,    -1,    -1,  1528,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,    -1,    -1,
      38,    39,  1541,    -1,    -1,    -1,  1545,    45,  1547,    -1,
    1549,    49,    50,  1552,    -1,    53,    -1,    -1,    56,  1558,
      -1,    -1,    -1,    -1,    62,  1564,    -1,    -1,  1567,   852,
      -1,   854,  1571,  1572,    -1,    73,    74,    75,    -1,    -1,
      -1,    79,    80,  1582,    -1,    83,    84,    -1,    -1,    -1,
     145,    89,  1591,   148,    -1,   150,  1595,    -1,    -1,    -1,
      -1,    -1,  1601,    -1,    -1,    -1,    -1,    -1,   891,   164,
     165,   894,   895,  1612,    -1,   898,    -1,    -1,   901,    -1,
      -1,   176,    -1,   178,  1623,    -1,   181,    -1,    -1,  1628,
      -1,    -1,    -1,   916,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,  1640,    -1,    -1,  1643,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   960,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   972,
      -1,    -1,    -1,    -1,    -1,   978,   979,   980,   981,   982,
     983,   984,   985,   986,   987,   988,   989,   990,   991,   992,
     993,   994,   995,   996,    -1,   270,    -1,  1000,  1001,  1002,
    1003,  1004,  1005,  1006,  1007,  1008,  1009,  1010,    -1,    -1,
     285,    -1,    -1,   288,    -1,  1018,    -1,  1020,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   306,  1035,   308,    -1,    -1,     3,    -1,    -1,    -1,
      -1,   316,     9,    10,    -1,    -1,    -1,   322,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    23,   332,    -1,    -1,
      -1,    -1,    -1,   338,    -1,    -1,    -1,    -1,    35,    36,
      37,    38,    -1,    -1,    -1,    -1,    -1,    -1,    45,    -1,
      -1,    -1,   357,    50,    -1,    -1,    53,    54,    -1,    -1,
     365,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    75,    -1,
     385,    -1,   387,    80,    -1,    -1,    83,    -1,    -1,    -1,
      -1,    88,   397,    -1,    91,   400,    -1,    -1,    -1,    96,
     405,    -1,  1135,    -1,    -1,    -1,   103,    -1,   105,    -1,
     107,    -1,    -1,   110,    -1,    -1,    -1,    -1,   115,   116,
     283,    -1,    -1,    -1,   121,    -1,    -1,   432,  1161,    -1,
      -1,    -1,    -1,    -1,  1167,    -1,    -1,    -1,  1171,    -1,
      -1,   446,   305,  1176,    -1,    -1,    -1,    -1,    -1,  1182,
       3,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,  1212,
    1213,  1214,  1215,    -1,   489,    -1,    -1,    -1,    -1,    42,
      -1,    -1,    -1,   438,   439,    -1,    -1,    -1,    -1,    -1,
     505,    -1,    -1,    -1,    -1,    -1,   511,   452,    -1,    -1,
      -1,    -1,    -1,    -1,   377,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,  1257,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     545,    -1,    -1,    -1,    -1,    -1,    99,   410,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
      -1,    -1,   115,   116,   117,    -1,    -1,    -1,    -1,    -1,
     123,    -1,   435,   578,    -1,    -1,    -1,    -1,   441,    -1,
     443,    -1,   445,    -1,    -1,    -1,   449,   450,   451,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,  1330,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,  1348,    -1,  1350,  1351,    -1,
    1353,    -1,    -1,  1356,   487,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,  1407,    42,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,  1424,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    1433,    -1,    -1,    -1,  1437,    -1,  1439,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   720,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,  1460,  1461,    -1,
    1463,  1464,    99,    -1,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,    -1,    -1,   115,   116,
     117,  1484,  1485,    -1,    -1,    -1,   123,    -1,    23,    -1,
      -1,    -1,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    -1,  1505,    38,    -1,  1508,   781,    -1,    -1,    -1,
      45,   786,    -1,    -1,    -1,    -1,    51,    -1,    -1,    -1,
      -1,    -1,   737,   738,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   438,   439,    -1,   749,   750,   751,    -1,    -1,    74,
      75,    -1,    77,    -1,    -1,   452,    -1,    -1,    83,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,  1564,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   109,    -1,    -1,   792,    -1,  1582,
     795,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   863,    -1,
      -1,    -1,    -1,   868,   869,    -1,    -1,    -1,  1601,   732,
     733,   876,    -1,    -1,    -1,    -1,   881,   882,    -1,    -1,
     743,   744,    -1,   746,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,  1628,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   854,
    1643,    -1,   775,    -1,    -1,    -1,   779,    -1,    -1,    -1,
     783,    -1,   785,    -1,   787,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   941,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   815,   816,   817,   818,   819,   820,   821,   822,
     823,   824,   825,   826,   827,   828,   829,   830,   831,   832,
     833,   916,    -1,    -1,   837,   838,   839,   840,   841,   842,
     843,   844,   845,   846,   847,   848,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   860,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   960,  1021,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   972,    -1,    -1,
      -1,    -1,    -1,   978,   979,   980,   981,   982,   983,   984,
     985,   986,   987,   988,   989,   990,   991,   992,   993,   994,
     995,   996,    -1,    -1,    -1,  1000,  1001,  1002,  1003,  1004,
    1005,  1006,  1007,  1008,  1009,  1010,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   438,   439,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   452,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     737,   738,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   749,   750,   751,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    -1,  1011,    -1,
      -1,  1156,    -1,    -1,  1017,   792,    -1,    -1,   795,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    42,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    1185,    -1,    -1,  1046,  1047,  1048,  1049,  1050,  1051,  1052,
    1053,  1054,  1055,  1056,  1057,  1058,  1059,  1060,  1061,  1062,
    1063,  1064,    -1,    -1,    -1,  1068,  1069,  1070,  1071,  1072,
    1073,  1074,  1075,  1076,  1077,  1078,  1079,   854,    -1,    -1,
      -1,    -1,  1085,  1086,    99,    -1,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,    -1,    -1,
     115,   116,   117,    -1,    -1,    -1,    -1,    -1,   123,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,  1212,  1213,    -1,
    1215,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   916,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,  1154,    -1,     3,     4,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   960,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,  1336,    42,    -1,    -1,   972,    -1,    -1,    -1,    -1,
      -1,   978,   979,   980,   981,   982,   983,   984,   985,   986,
     987,   988,   989,   990,   991,   992,   993,   994,   995,   996,
      -1,    -1,    -1,  1000,  1001,  1002,  1003,  1004,  1005,  1006,
    1007,  1008,  1009,  1010,   737,   738,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,  1330,   749,   750,   751,    99,
      -1,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,    -1,  1267,   115,   116,   117,    -1,    -1,
       3,   121,    -1,    -1,    -1,    -1,     9,    -1,    -1,    -1,
      -1,    -1,  1427,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      23,    -1,   795,    -1,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    -1,    38,    -1,    -1,    -1,    -1,
      -1,  1456,    45,    -1,    -1,    -1,    -1,    -1,    51,  1322,
      -1,  1324,    -1,    -1,  1327,    -1,    -1,    -1,    -1,    -1,
      -1,    64,    -1,    -1,    -1,    -1,    -1,    70,    -1,  1424,
      -1,    74,    75,    -1,    77,    -1,    -1,    -1,    -1,    -1,
      83,   854,    -1,    -1,    -1,    88,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     103,    -1,    -1,    -1,   107,  1460,   109,    -1,  1463,    -1,
      -1,    -1,   115,   116,    -1,    -1,    -1,    -1,   121,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,  1547,  1406,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,  1415,   916,  1417,    -1,    -1,    -1,    -1,    -1,
    1505,    -1,    -1,  1508,    -1,    -1,    -1,  1572,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,  1212,  1213,  1582,  1215,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   960,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,  1470,  1471,   972,
      -1,    -1,    -1,    -1,    -1,   978,   979,   980,   981,   982,
     983,   984,   985,   986,   987,   988,   989,   990,   991,   992,
     993,   994,   995,   996,    -1,    -1,    -1,  1000,  1001,  1002,
    1003,  1004,  1005,  1006,  1007,  1008,  1009,  1010,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    1543,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,  1330,    -1,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,  1424,    96,    -1,
      -1,    99,    -1,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
      -1,    -1,   120,    -1,   122,   123,   124,   125,   126,   127,
      -1,    -1,    -1,  1460,    -1,    -1,  1463,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,  1212,
    1213,    -1,  1215,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,  1505,    -1,
      -1,  1508,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    -1,    96,    -1,  1330,    99,    -1,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,    -1,   119,   120,
      -1,   122,   123,    -1,   125,   126,   127,    -1,    -1,     3,
      -1,    -1,    -1,    -1,    -1,     9,    10,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    21,    -1,    23,
      -1,    -1,    -1,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    -1,    37,    38,    39,    -1,    -1,    -1,    -1,
      44,    45,    -1,    -1,    -1,    49,    50,    51,    -1,    53,
      -1,    55,    56,    -1,    58,    -1,    -1,    61,    62,    63,
      -1,  1424,    -1,    -1,    68,    -1,    -1,    71,    -1,    73,
      74,    75,    -1,    77,    78,    79,    80,    -1,    82,    83,
      84,    -1,    86,    -1,    88,    89,    -1,    -1,    -1,    93,
      -1,    -1,    96,    -1,    -1,    -1,    -1,  1460,    -1,   103,
    1463,   105,    -1,   107,    -1,   109,   110,    -1,    -1,   113,
     114,   115,   116,    -1,    -1,    -1,   120,    -1,    -1,   123,
     124,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,  1505,    -1,    -1,  1508,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    -1,    96,
      -1,    -1,    99,    -1,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,    -1,    -1,   120,   121,   122,   123,     3,   125,   126,
     127,    -1,    -1,     9,    10,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    21,    -1,    23,    -1,    -1,
      -1,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      -1,    37,    38,    39,    -1,    -1,    -1,    -1,    44,    45,
      -1,    -1,    -1,    49,    50,    51,    -1,    53,    -1,    55,
      56,    -1,    58,    -1,    -1,    61,    62,    63,    -1,    -1,
      -1,    -1,    68,    -1,    -1,    71,    -1,    73,    74,    75,
      -1,    77,    78,    79,    80,    -1,    82,    83,    84,    -1,
      86,    -1,    88,    89,    -1,    -1,    -1,    93,    -1,    -1,
      96,    -1,    -1,    -1,    -1,    -1,    -1,   103,    -1,   105,
      -1,   107,    -1,   109,   110,     3,    -1,   113,   114,   115,
     116,     9,    10,    -1,   120,    -1,    -1,   123,   124,    -1,
      -1,    -1,    -1,    21,    -1,    23,    -1,    -1,    -1,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    -1,    37,
      38,    -1,    -1,    -1,    -1,    -1,    44,    45,    -1,    -1,
      -1,    -1,    -1,    51,    -1,    -1,    -1,    55,    -1,    -1,
      -1,    -1,    -1,    61,    -1,    63,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    71,    -1,    -1,    74,    75,    -1,    77,
      -1,    -1,    80,    -1,    82,    83,    -1,    -1,    86,    -1,
      88,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    96,    -1,
      -1,    -1,    -1,    -1,    -1,   103,    -1,   105,    -1,   107,
      -1,   109,   110,     3,    -1,   113,   114,   115,   116,     9,
      10,   119,    -1,   121,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    21,    -1,    23,    -1,    -1,    -1,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    -1,    37,    38,    -1,
      -1,    -1,    -1,    -1,    44,    45,    -1,    -1,    -1,    -1,
      -1,    51,    -1,    -1,    -1,    55,    -1,    -1,    -1,    -1,
      -1,    61,    -1,    63,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    71,    -1,    -1,    74,    75,    -1,    77,    -1,    -1,
      80,    -1,    82,    83,    -1,    -1,    86,    -1,    88,    -1,
      -1,     3,    -1,    -1,    -1,    -1,    96,     9,    10,    -1,
      -1,    -1,    -1,   103,    -1,   105,    -1,   107,    -1,   109,
     110,    23,    -1,   113,   114,   115,   116,    -1,    -1,    -1,
      -1,   121,    -1,    35,    36,    -1,    38,    -1,    -1,    -1,
      -1,    -1,    -1,    45,    -1,    -1,    -1,    -1,    50,    -1,
      -1,    53,    54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    64,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    75,    -1,    -1,    -1,    -1,    80,    -1,
      -1,    83,    -1,    -1,    -1,    -1,    88,    -1,    -1,    91,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       3,   103,    -1,   105,    -1,   107,     9,    10,   110,    -1,
      -1,    -1,    -1,   115,   116,    -1,    -1,    -1,    21,   121,
      23,    -1,    -1,    -1,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    -1,    -1,    38,    39,    -1,    -1,    -1,
      -1,    44,    45,    -1,    -1,    -1,    49,    50,    51,    -1,
      53,    -1,    -1,    56,    -1,    -1,    -1,    -1,    -1,    62,
      63,    -1,    -1,    -1,    -1,    68,    -1,    -1,    71,    -1,
      73,    74,    75,    -1,    77,    78,    79,    80,    -1,    82,
      83,    84,    -1,    86,    -1,    88,    89,    -1,    -1,    -1,
      93,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     103,    -1,   105,    -1,   107,    -1,   109,   110,     3,    -1,
     113,    -1,   115,   116,     9,    10,    -1,   120,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    21,    -1,    23,    -1,
      -1,    -1,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    -1,    37,    38,    -1,    -1,    -1,    -1,    -1,    44,
      45,    -1,    -1,    -1,    -1,    -1,    51,    -1,    -1,    -1,
      55,    -1,    -1,    -1,    -1,    -1,    61,    -1,    63,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    71,    -1,    -1,    74,
      75,    -1,    77,    -1,    -1,    80,    -1,    82,    83,    -1,
      -1,    86,    -1,    88,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    96,    -1,    -1,    -1,    -1,    -1,    -1,   103,    -1,
     105,    -1,   107,    -1,   109,   110,     3,    -1,   113,   114,
     115,   116,     9,    10,   119,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    21,    -1,    23,    -1,    -1,    -1,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    -1,
      37,    38,    -1,    -1,    -1,    -1,    -1,    44,    45,    -1,
      -1,    -1,    -1,    -1,    51,    -1,    -1,    -1,    55,    -1,
      -1,    -1,    -1,    -1,    61,    -1,    63,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    71,    -1,    -1,    74,    75,    -1,
      77,    -1,    -1,    80,    -1,    82,    83,    -1,    -1,    86,
      -1,    88,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    96,
      -1,    -1,    -1,    -1,    -1,    -1,   103,    -1,   105,    -1,
     107,    -1,   109,   110,    -1,    -1,   113,   114,   115,   116,
      -1,    -1,   119,     3,     4,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    -1,    -1,    -1,    -1,    -1,    -1,    99,
      -1,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,    42,    -1,   115,   116,   117,    99,   119,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,    -1,    -1,   115,   116,   117,    -1,   119,    -1,
      -1,    -1,    -1,    -1,    -1,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    -1,    -1,    -1,    -1,    -1,    -1,
      99,    -1,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,    42,   114,   115,   116,   117,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    42,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    99,    -1,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,    -1,   114,   115,   116,   117,
       3,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    -1,
      -1,    -1,    -1,    -1,    -1,    99,    -1,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,    42,
      -1,   115,   116,   117,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    99,    -1,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
      -1,    -1,   115,   116,   117,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    -1,    -1,    -1,    -1,    -1,    -1,
      99,    -1,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,    42,    -1,   115,   116,   117,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    42,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,    -1,    -1,   115,   116,   117,
       3,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,    42,
      -1,   115,   116,   117,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
      -1,    -1,   115,   116,   117,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,    42,    -1,   115,   116,   117,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,     3,    -1,   115,   116,   117,
      -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    21,    -1,    23,    -1,    -1,    -1,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    -1,    37,
      38,    -1,    -1,    -1,    -1,    -1,    44,    45,    -1,    -1,
      -1,    -1,    -1,    51,    -1,    -1,    54,    55,    -1,    -1,
      -1,    59,    -1,    61,    -1,    63,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    71,    -1,    -1,    74,    75,    -1,    77,
      -1,    -1,    80,    -1,    82,    83,    -1,    85,    86,    -1,
      88,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    96,    -1,
      -1,    -1,    -1,    -1,    -1,   103,    -1,   105,    -1,   107,
      -1,   109,   110,     3,    -1,   113,   114,   115,   116,     9,
      10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    21,    -1,    23,    -1,    -1,    -1,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    -1,    37,    38,    -1,
      -1,    -1,    -1,    -1,    44,    45,    -1,    -1,    -1,    -1,
      -1,    51,    -1,    -1,    54,    55,    -1,    -1,    -1,    59,
      -1,    61,    -1,    63,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    71,    -1,    -1,    74,    75,    -1,    77,    -1,    -1,
      80,    -1,    82,    83,    -1,    85,    86,    -1,    88,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    96,    -1,    -1,    -1,
      -1,    -1,    -1,   103,    -1,   105,    -1,   107,    -1,   109,
     110,     3,    -1,   113,   114,   115,   116,     9,    10,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    -1,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    -1,    37,    38,    -1,    -1,    -1,
      -1,    -1,    44,    45,    -1,    -1,    -1,    -1,    -1,    51,
      -1,    -1,    54,    55,    -1,    -1,    -1,    59,    -1,    61,
      -1,    63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    71,
      -1,    -1,    74,    75,    -1,    77,    -1,    -1,    80,    -1,
      82,    83,    -1,    85,    86,    -1,    88,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    96,    -1,    -1,    -1,    -1,    -1,
      -1,   103,    -1,   105,    -1,   107,    -1,   109,   110,     3,
      -1,   113,   114,   115,   116,     9,    10,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    21,    -1,    23,
      -1,    -1,    -1,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    -1,    37,    38,    -1,    -1,    -1,    -1,    -1,
      44,    45,    -1,    -1,    -1,    -1,    -1,    51,    -1,    -1,
      -1,    55,    -1,    -1,    58,    -1,    -1,    61,    -1,    63,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    71,    -1,    -1,
      74,    75,    -1,    77,    -1,    -1,    80,    -1,    82,    83,
      -1,    -1,    86,    -1,    88,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    96,    -1,    -1,    -1,    -1,    -1,    -1,   103,
      -1,   105,    -1,   107,    -1,   109,   110,     3,    -1,   113,
     114,   115,   116,     9,    10,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    21,    -1,    23,    -1,    -1,
      -1,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      -1,    37,    38,    -1,    -1,    -1,    -1,    -1,    44,    45,
      -1,    -1,    -1,    -1,    -1,    51,    -1,    -1,    -1,    55,
      -1,    -1,    58,    -1,    -1,    61,    -1,    63,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    71,    -1,    -1,    74,    75,
      -1,    77,    -1,    -1,    80,    -1,    82,    83,    -1,    -1,
      86,    -1,    88,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      96,    -1,    -1,    -1,    -1,    -1,    -1,   103,    -1,   105,
      -1,   107,    -1,   109,   110,     3,    -1,   113,   114,   115,
     116,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    21,    -1,    23,    -1,    -1,    -1,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    -1,    37,
      38,    -1,    -1,    -1,    -1,    -1,    44,    45,    -1,    -1,
      -1,    -1,    -1,    51,    -1,    -1,    -1,    55,    -1,    -1,
      -1,    -1,    -1,    61,    -1,    63,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    71,    -1,    -1,    74,    75,    -1,    77,
      -1,    -1,    80,    -1,    82,    83,    -1,    -1,    86,    -1,
      88,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    96,    -1,
      -1,    -1,    -1,    -1,    -1,   103,    -1,   105,    -1,   107,
      -1,   109,   110,     3,    -1,   113,   114,   115,   116,     9,
      10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    21,    -1,    23,    -1,    -1,    -1,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    -1,    37,    38,    -1,
      -1,    -1,    -1,    -1,    44,    45,    -1,    -1,    -1,    -1,
      -1,    51,    -1,    -1,    -1,    55,    -1,    -1,    -1,    -1,
      -1,    61,    -1,    63,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    71,    -1,    -1,    74,    75,    -1,    77,    -1,    -1,
      80,    -1,    82,    83,    -1,    -1,    86,    -1,    88,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    96,    -1,    -1,    -1,
      -1,    -1,    -1,   103,    -1,   105,    -1,   107,    -1,   109,
     110,     3,    -1,   113,   114,   115,   116,     9,    10,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    21,
      -1,    23,    -1,    -1,    -1,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    -1,    37,    38,    -1,    -1,    -1,
      -1,    -1,    44,    45,    -1,    -1,    -1,    -1,    -1,    51,
      -1,    -1,    -1,    55,    -1,    -1,    -1,    -1,    -1,    61,
      -1,    63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    71,
      -1,    -1,    74,    75,    -1,    77,    -1,    -1,    80,    -1,
      82,    83,    -1,    -1,    86,    -1,    88,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    96,    -1,    -1,    -1,     3,    -1,
      -1,   103,    -1,   105,     9,   107,    -1,   109,   110,    -1,
      -1,   113,   114,   115,   116,    -1,    21,    -1,    23,    -1,
      -1,    -1,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    -1,    38,    -1,    -1,    -1,    -1,    -1,    -1,
      45,    -1,    -1,    -1,    -1,    -1,    51,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    64,
      -1,     3,    -1,    -1,    -1,    70,    -1,     9,    -1,    74,
      75,    -1,    77,    -1,    -1,    -1,    -1,    -1,    83,    -1,
      -1,    23,    -1,    88,    -1,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    -1,    38,    -1,   103,    -1,
      -1,    -1,   107,    45,   109,    -1,    -1,    -1,    -1,    51,
     115,   116,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    64,    -1,     3,    -1,    -1,    -1,    70,    -1,
       9,    -1,    74,    75,    -1,    77,    -1,    -1,    -1,    -1,
      -1,    83,    -1,    -1,    23,    -1,    88,    -1,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    -1,    38,
      -1,   103,    -1,   105,    -1,   107,    45,   109,    -1,    -1,
      -1,    -1,    51,   115,   116,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    64,    -1,     3,    -1,    -1,
      -1,    70,    -1,     9,    -1,    74,    75,    -1,    77,    -1,
      -1,    -1,    -1,    -1,    83,    21,    -1,    23,    -1,    88,
      -1,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    -1,    38,    -1,   103,    -1,   105,    -1,   107,    45,
     109,    -1,    -1,    -1,    -1,    51,   115,   116,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    64,    -1,
       3,    -1,    -1,    -1,    70,    -1,     9,    -1,    74,    75,
      -1,    77,    -1,    -1,    -1,    -1,    -1,    83,    21,    -1,
      23,    -1,    88,    -1,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    -1,    38,    -1,   103,    -1,    -1,
      -1,   107,    45,   109,    -1,    -1,    -1,    -1,    51,   115,
     116,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    64,    -1,     3,    -1,    -1,    -1,    70,    -1,     9,
      -1,    74,    75,    -1,    77,    -1,    -1,    -1,    -1,    -1,
      83,    -1,    -1,    23,    -1,    88,    -1,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    -1,    38,    -1,
     103,    -1,    -1,    -1,   107,    45,   109,    -1,    -1,    -1,
      -1,    51,   115,   116,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    64,    -1,     3,    -1,    -1,    -1,
      70,    -1,     9,    -1,    74,    75,    -1,    77,    -1,    -1,
      -1,    -1,    -1,    83,    -1,    -1,    23,    -1,    88,    -1,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      -1,    38,    -1,   103,    -1,   105,    -1,   107,    45,   109,
      -1,    -1,    -1,    -1,    51,   115,   116,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    64,    -1,     3,
      -1,    -1,    -1,    70,    -1,     9,    -1,    74,    75,    -1,
      77,    -1,    -1,    -1,    -1,    -1,    83,    -1,    -1,    23,
      -1,    88,    -1,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    -1,    -1,   103,    -1,   105,    -1,
     107,    45,   109,    -1,    -1,    -1,    -1,    51,   115,   116,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      64,    -1,    -1,    -1,    -1,    -1,    70,    -1,    -1,    -1,
      74,    75,    -1,    77,    -1,    -1,    -1,    -1,    -1,    83,
      -1,    -1,    -1,    -1,    88,    -1,    -1,    -1,    -1,     3,
      -1,    -1,    96,    -1,    -1,     9,    -1,    -1,    -1,   103,
      -1,    -1,    -1,   107,    -1,   109,    -1,    -1,    -1,    23,
      -1,   115,   116,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    -1,    38,    -1,    -1,    -1,    -1,    -1,
      -1,    45,    -1,    -1,    -1,    -1,    -1,    51,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      64,    -1,     3,    -1,    -1,    -1,    70,    -1,     9,    -1,
      74,    75,    -1,    77,    -1,    -1,    -1,    -1,    -1,    83,
      -1,    -1,    23,    -1,    88,    -1,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    -1,    38,    -1,   103,
      -1,    -1,    -1,   107,    45,   109,    -1,    -1,    -1,    -1,
      51,   115,   116,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    64,    -1,     3,    -1,    -1,    -1,    70,
      -1,     9,    -1,    74,    75,    -1,    77,    -1,    -1,    -1,
      -1,    -1,    83,    -1,    -1,    23,    -1,    88,    -1,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    -1,
      38,    -1,   103,    -1,    -1,    -1,   107,    45,   109,    -1,
      -1,    -1,    -1,    51,   115,   116,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    64,    -1,     3,    -1,
      -1,    -1,    70,    -1,     9,    -1,    74,    75,    -1,    77,
      -1,    -1,    -1,    -1,    -1,    83,    -1,    -1,    23,    -1,
      88,    -1,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    -1,    -1,    -1,   103,    -1,    -1,    -1,   107,
      45,   109,    -1,    -1,    -1,    -1,    51,   115,   116,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    64,
      -1,    -1,    -1,    -1,    -1,    70,    -1,    -1,    -1,    74,
      75,    -1,    77,     3,    -1,    -1,    -1,    -1,    83,     9,
      10,    -1,    -1,    88,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    -1,    -1,    -1,    -1,   103,    -1,
      -1,    -1,   107,    -1,   109,    35,    36,    37,    38,    -1,
     115,   116,    -1,    -1,    -1,    45,    -1,    -1,    -1,    -1,
      50,    -1,    -1,    53,    54,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    64,    -1,    -1,     3,    -1,    -1,
      -1,    -1,    -1,     9,    10,    75,    -1,    -1,    -1,    -1,
      80,    -1,    -1,    83,    -1,    -1,    -1,    23,    88,    -1,
      -1,    91,    -1,    -1,    -1,    -1,    96,    -1,    -1,    35,
      36,    37,    38,   103,    -1,   105,    -1,   107,    -1,    45,
     110,    -1,    -1,    -1,    50,   115,   116,    53,    54,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    64,    -1,
      -1,     3,    -1,    -1,    -1,    -1,    -1,     9,    10,    75,
      -1,    -1,    -1,    -1,    80,    -1,    -1,    83,    -1,    -1,
      -1,    23,    88,    -1,    -1,    91,    -1,    -1,    -1,    -1,
      96,    -1,    -1,    35,    36,    37,    38,   103,    -1,   105,
      -1,   107,    -1,    45,   110,    -1,    -1,    -1,    50,   115,
     116,    53,    54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    64,    -1,    -1,     3,    -1,    -1,    -1,    -1,
      -1,     9,    10,    75,    -1,    -1,    -1,    -1,    80,    -1,
      -1,    83,    -1,    -1,    -1,    23,    88,    -1,    -1,    91,
      -1,    -1,    -1,    -1,    96,    -1,    -1,    35,    36,    37,
      38,   103,    -1,   105,    -1,   107,    -1,    45,   110,    -1,
      -1,    -1,    50,   115,   116,    53,    54,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    64,    -1,    -1,     3,
      -1,    -1,    -1,    -1,    -1,     9,    10,    75,    -1,    -1,
      -1,    -1,    80,    -1,    -1,    83,    -1,    21,    -1,    23,
      88,    -1,    -1,    91,    -1,    -1,    -1,    -1,    96,    -1,
      -1,    35,    36,    37,    38,   103,    -1,   105,    -1,   107,
      -1,    45,   110,    -1,    -1,    -1,    50,   115,   116,    53,
      54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     3,    -1,    -1,    -1,    -1,    -1,     9,
      10,    75,    -1,    -1,    -1,    -1,    80,    -1,    -1,    83,
      -1,    21,    -1,    23,    88,    -1,    -1,    91,    -1,    -1,
      -1,    -1,    96,    -1,    -1,    35,    36,    37,    38,   103,
      -1,   105,    -1,   107,    -1,    45,   110,    -1,    -1,    -1,
      50,   115,   116,    53,    54,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,    -1,    -1,
      -1,    -1,    -1,     9,    10,    75,    -1,    -1,    -1,    -1,
      80,    -1,    -1,    83,    -1,    -1,    -1,    23,    88,    -1,
      -1,    91,    -1,    -1,    -1,    -1,    96,    -1,    -1,    35,
      36,    37,    38,   103,    -1,   105,    -1,   107,    -1,    45,
     110,    -1,    -1,    -1,    50,   115,   116,    53,    54,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     3,    -1,    -1,    -1,
      -1,    -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    75,
      -1,    -1,    -1,    -1,    80,    22,    23,    83,    -1,    -1,
      -1,    -1,    88,    -1,    -1,    91,    -1,    -1,    35,    36,
      96,    38,    -1,    -1,    -1,    -1,    -1,   103,    45,   105,
      -1,   107,    -1,    50,   110,    -1,    53,    54,    -1,   115,
     116,    -1,    -1,    -1,    -1,     3,    -1,    64,    -1,    -1,
      -1,     9,    10,    -1,    -1,    -1,    -1,    -1,    75,    -1,
      -1,    -1,    -1,    80,    -1,    23,    83,    -1,    -1,    -1,
      -1,    88,    -1,    -1,    91,    -1,    -1,    35,    36,    37,
      -1,    -1,    -1,    -1,    -1,    -1,   103,    45,   105,    -1,
     107,    -1,    50,   110,    -1,    53,    54,    -1,   115,   116,
      -1,    -1,    -1,    -1,     3,    -1,    64,    -1,    -1,    -1,
       9,    10,    -1,    -1,    -1,    -1,    -1,    75,    -1,    -1,
      -1,    -1,    80,    -1,    23,    83,    -1,    -1,    -1,    -1,
      88,    -1,    -1,    91,    -1,    -1,    35,    36,    96,    38,
      -1,    -1,    -1,    -1,    -1,   103,    45,   105,    -1,   107,
      -1,    50,   110,    -1,    53,    54,    -1,   115,   116,    -1,
      -1,    -1,    -1,     3,    -1,    64,    -1,    -1,    -1,     9,
      10,    -1,    -1,    -1,    -1,    -1,    75,    -1,    -1,    -1,
      -1,    80,    -1,    23,    83,    -1,    -1,    -1,    -1,    88,
      -1,    -1,    91,    -1,    -1,    35,    36,    -1,    38,    -1,
      -1,    -1,    -1,    -1,   103,    45,   105,    -1,   107,    -1,
      50,   110,    -1,    53,    54,    -1,   115,   116,    -1,    -1,
      -1,    -1,     3,    -1,    64,    -1,    -1,    -1,     9,    10,
      -1,    -1,    -1,    -1,    -1,    75,    -1,    -1,    -1,    -1,
      80,    -1,    23,    83,    -1,    -1,    -1,    -1,    88,    -1,
      -1,    91,    -1,    -1,    35,    36,    -1,    38,    -1,    -1,
      -1,    -1,    -1,   103,    45,   105,    -1,   107,    -1,    50,
     110,    -1,    53,    54,    -1,   115,   116,    -1,    -1,    -1,
      -1,     3,    -1,    -1,    -1,    -1,    -1,     9,    10,    -1,
      -1,    -1,    -1,    -1,    75,    -1,    -1,    -1,    -1,    80,
      -1,    23,    83,    -1,    -1,    -1,    -1,    88,    -1,    -1,
      91,    -1,    -1,    35,    36,    -1,    38,    -1,    -1,    -1,
      -1,    -1,   103,    45,   105,    -1,   107,    -1,    50,   110,
      -1,    53,    54,    -1,   115,   116,    -1,    -1,    -1,    -1,
       3,    -1,    -1,    -1,    -1,    -1,     9,    10,    -1,    -1,
      -1,    -1,    -1,    75,    -1,    -1,    -1,    -1,    80,    -1,
      23,    83,    -1,    -1,    -1,    -1,    88,    -1,    -1,    91,
      -1,    -1,    35,    36,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   103,    45,   105,    -1,   107,    -1,    50,   110,    -1,
      53,    54,    -1,   115,   116,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    75,    -1,    -1,    -1,    -1,    80,    -1,    -1,
      83,    -1,    -1,    -1,    -1,    88,    -1,    -1,    91,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     103,    -1,   105,    -1,   107,    -1,    -1,   110,    -1,    -1,
      23,    -1,   115,   116,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    -1,    37,    38,    -1,    -1,    -1,    -1,
      -1,    44,    45,    -1,    -1,    -1,    -1,    -1,    51,    -1,
      -1,    54,    55,    -1,    -1,    -1,    59,    -1,    61,    -1,
      63,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    71,    -1,
      -1,    74,    75,    -1,    77,    -1,    -1,    80,    -1,    82,
      83,    -1,    85,    86,    -1,    88,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    96,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     113
  };

  const unsigned short int
  parser::yystos_[] =
  {
       0,    95,   129,   130,     0,    92,    93,    94,   120,   132,
     133,   134,   135,   136,   139,   140,   141,   142,   115,   115,
     133,   139,    68,   177,   136,   142,    23,    35,    38,    39,
      45,    49,    50,    53,    62,    73,    74,    75,    79,    80,
      83,    84,    89,   143,   144,   145,   146,   147,   148,   149,
     150,   152,   155,   162,   166,   167,   168,   180,   182,   189,
     190,   195,   202,   203,   291,   292,   333,    75,    83,   137,
     333,   137,   292,    23,    64,   333,   333,    31,    32,    33,
      34,    46,   169,   331,   333,   333,   333,   333,   333,    50,
      53,    80,    23,    38,    74,   110,   114,   151,   231,   333,
      53,    80,   333,    78,    56,   113,    23,   119,   102,   116,
     119,   292,   333,    99,   103,   224,   333,    89,   114,   202,
     224,   114,   123,   224,   224,   224,   169,   333,    38,    74,
     110,   114,   333,    38,   124,   178,   179,   333,   123,    23,
      42,   333,    53,    99,   333,   224,   332,   333,     3,    74,
     103,   233,   333,    27,    28,    29,    30,    51,    77,   330,
     331,   137,   138,    99,     3,     9,    10,    23,    36,    38,
      50,    53,    54,    80,    88,    91,   103,   105,   107,   110,
     115,   116,   232,   248,   249,   250,   252,   253,   333,    37,
      96,   104,   230,   271,   279,   280,   333,    90,   225,   226,
      42,   123,   132,   141,   170,   171,   172,   116,   204,   207,
     132,   139,   116,   157,   225,   225,   225,    53,   224,   124,
     178,    42,   122,   124,   110,   114,   333,   333,   224,   333,
     248,   224,    23,    37,    80,    96,   113,   116,   232,   249,
     263,   264,   281,   282,   114,   115,   116,   338,   339,   340,
     248,   257,   258,   261,   262,   281,   234,   235,   260,   261,
     269,   270,   333,   121,   122,   248,   261,    64,   248,   281,
      99,   272,   232,    23,   169,   224,   251,   103,    10,    50,
      53,   105,   248,   116,   261,     9,    36,    64,   107,   219,
     220,   222,   248,   333,    64,   248,   281,    64,    89,   256,
     248,   121,   260,    23,   113,   102,   116,   233,    99,   275,
       4,   104,   122,     4,   104,   122,    99,   101,   265,    54,
     227,   228,   229,   114,   333,   170,    39,    53,    80,   174,
     124,   172,     3,     9,    36,    64,    70,    88,   103,   107,
     109,   115,   116,   211,   212,   213,   237,   239,   240,   291,
     330,   333,   225,    24,   223,   139,   124,   141,   160,   161,
     225,   114,   123,   156,   114,   102,   333,   204,   122,   124,
     333,   124,   179,   124,   178,   204,   224,   102,    54,   181,
     232,    23,   232,   282,   248,   108,   225,    54,   335,   335,
     335,   123,   123,     4,     8,    12,   104,   122,   236,    42,
     108,     4,     8,    12,   104,   122,   122,   102,   121,   137,
     102,    42,   301,   248,    64,   255,   273,   274,   281,   282,
     223,   113,   232,    53,   116,   208,   278,   279,   272,   169,
     251,   219,     3,     9,    10,    21,    38,    44,    55,    61,
      63,    71,    80,    82,    86,    88,   103,   105,   107,   109,
     110,   113,   114,   115,   116,   281,   284,   291,   293,   295,
     298,   299,   302,   312,   315,   321,   322,   324,   325,   326,
     327,   328,   330,   301,    36,   333,   333,    36,   333,   105,
     122,    99,   248,   255,   248,   119,   122,   123,   121,   122,
     333,   332,   295,   259,   260,   276,   281,     4,   104,   271,
       4,   104,   230,   280,    54,   101,   266,   267,   268,   274,
     333,   102,   277,   103,   122,   248,   281,   141,   163,   164,
     123,   124,   173,   255,   333,   174,   261,   237,    64,   237,
     261,    64,   237,   330,    21,   237,   246,   247,    21,   121,
     237,   244,   245,   121,   122,    99,   333,    22,   113,   114,
     116,   125,   114,   283,   113,   248,   124,   261,   121,   122,
     123,   141,   158,   159,   158,   261,   224,   225,   124,   122,
     124,   225,   204,   295,   101,   333,   265,   232,    54,   121,
     264,   114,    21,   261,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    96,    99,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   117,   120,   122,   123,   124,   125,   126,   127,   334,
     336,   337,   338,   339,   340,   119,   121,   260,   269,   282,
     262,   261,   269,   270,   333,   248,   295,   282,   104,   248,
     108,   332,   251,   121,   220,   223,   104,   122,   223,    53,
     105,   261,   255,   223,   295,   333,     9,    10,    21,    38,
      44,    58,    63,    71,    82,    86,    88,   105,   107,   109,
     110,   113,   115,   116,   284,   291,   293,   296,   297,   299,
     304,   312,   330,   296,   302,   295,   284,   295,   333,   295,
     261,   105,   214,   215,   237,   255,   295,   295,   295,    21,
      38,    44,    68,    71,    80,    82,    86,    88,   115,   116,
     123,   134,   135,   144,   285,   286,   287,   291,   293,   294,
     298,   299,   312,   313,   314,   330,   288,   290,   295,   306,
     288,   290,    99,   113,   114,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    42,    99,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   115,   116,
     117,   121,    54,    59,    85,   104,   272,   220,   248,   248,
      21,   295,   121,   116,   233,   337,   123,   121,   122,   108,
       4,   104,   122,   103,    54,   274,   108,   265,   261,   279,
     228,    99,    99,   333,   122,   124,   333,   224,   301,   301,
     237,   122,   119,    21,   122,   122,   121,    21,   122,   213,
     261,   125,   240,   291,   332,    21,    29,    88,   239,   241,
     242,   243,   333,   121,   244,   237,   131,   132,   121,   161,
     333,   122,   124,   122,   124,   123,   204,   283,   124,   283,
     225,   123,   333,   101,   225,    21,   261,   225,   131,   114,
     225,   122,   236,   122,   104,   123,    23,   274,   337,   122,
     221,   250,   252,   254,   282,   251,   272,   301,   295,   295,
     255,   296,   296,   333,   237,   304,   295,   295,   333,   295,
     105,   214,   255,   296,   296,   296,   306,   288,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    42,    99,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   115,   116,   117,   284,   114,   301,   223,    10,   105,
     122,    99,   153,   295,   295,   281,    75,    83,   144,   295,
     295,   281,   295,   306,   288,    58,   284,   329,    68,   144,
     291,   294,   124,   287,   294,   114,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    42,    99,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   115,
     116,   117,   123,   117,   117,   122,   123,   119,   121,   332,
      29,   307,   308,   309,   310,   333,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   248,   248,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   289,   295,
     288,    29,   292,   237,   284,    58,   296,    23,   223,   295,
     119,   259,   223,   281,     4,   104,   278,   103,   268,   277,
     104,   122,   266,   273,   102,   114,   116,   165,   124,   164,
      99,   116,   175,   176,   104,   104,   247,   122,    21,   237,
     245,   122,    21,   237,   237,   337,    99,   239,   333,   333,
     122,   124,    99,   121,   285,    99,   124,   159,   124,   225,
     283,   114,   114,   225,   114,   141,   194,   196,   197,   198,
     200,   201,   124,   114,    23,   333,    22,   220,   121,   223,
     104,   296,   102,   223,    10,   105,   296,   119,   121,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   248,   248,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   289,   288,    29,   292,    48,   124,   134,   316,   317,
     318,   319,   104,   295,   105,   214,   303,   223,   215,   261,
     119,   121,   237,   144,   113,   123,   307,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   248,   248,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   289,
     288,    29,   292,    29,   292,    29,   292,   295,   295,   338,
     339,   340,    99,   124,    21,   311,   122,    99,   119,   121,
      57,   237,   284,   333,   119,   121,   104,   278,   295,   158,
     259,   248,   121,   212,   225,   223,    23,    23,   122,   247,
     122,   122,   245,   122,   237,   333,    99,    21,   241,   237,
     124,   261,   283,   134,   183,   184,   185,   186,   188,   191,
     192,   193,   124,   114,   131,    83,   147,   199,   124,   197,
     131,   333,   301,   221,    23,   296,   296,   105,   214,   305,
     296,   119,   121,   284,   321,   322,   323,   237,   238,   124,
     317,   318,   122,   124,   122,    23,   223,    10,   105,   295,
     153,   332,   124,   119,   121,   115,   116,   115,   116,   295,
     295,   310,   295,   296,   102,   223,   274,   104,   122,   124,
     121,   123,   121,   122,   123,   333,   333,   122,   247,   122,
     245,   237,    79,    80,    89,   147,   189,   124,   185,   131,
     196,    79,    89,   146,   189,   196,   108,   104,   333,   284,
     223,    10,   105,    55,   105,   320,   124,   333,   295,   303,
     223,   102,   154,   289,   288,   289,   288,   284,   296,   274,
     124,    22,   121,   301,   122,   122,   271,   189,   333,    50,
      53,   196,   124,   333,   189,    50,    53,   124,   266,    23,
     233,   301,    48,   296,   305,   223,   296,   237,    26,    23,
     300,   295,   295,   123,   119,   121,   119,   121,   284,   121,
     104,   123,    53,   153,   169,   333,   124,   224,    53,   169,
     333,   333,   301,   104,   323,   296,   284,   294,   312,   314,
     233,    23,   333,   102,   187,    53,   224,   102,   333,    53,
     224,   104,    23,   333,   224,   295,   123,   333,   116,   206,
     210,   261,   224,   333,   116,   205,   209,    23,   333,   206,
     224,    64,   107,   218,   219,   255,   225,   223,   123,   205,
     224,    64,   107,   211,   255,   225,   223,   333,   233,   225,
     206,    64,   255,   281,   121,   122,    38,   123,   283,   225,
     205,    64,   255,   281,   121,    38,   283,   233,   123,   283,
     225,    38,   255,   153,   283,   225,    38,   255,   153,   123,
     283,   153,    38,   122,   217,   283,   153,    38,   122,   216,
     217,   153,   219,   121,   216,   153,   212,   121,   121,   217,
     122,   121,   216,   122,   121,   121
  };

  const unsigned short int
  parser::yyr1_[] =
  {
       0,   128,   129,   129,   130,   130,   131,   131,   132,   132,
     133,   133,   134,   134,   135,   135,   136,   136,   137,   137,
     137,   137,   138,   138,   138,   139,   139,   140,   140,   141,
     142,   143,   143,   144,   144,   144,   144,   144,   145,   145,
     146,   147,   147,   147,   148,   148,   148,   148,   149,   150,
     151,   151,   151,   151,   151,   151,   151,   151,   151,   151,
     151,   151,   151,   151,   152,   152,   152,   152,   152,   152,
     152,   152,   152,   153,   153,   154,   154,   155,   155,   155,
     156,   156,   157,   157,   158,   158,   158,   159,   160,   160,
     160,   161,   162,   162,   163,   163,   163,   164,   165,   165,
     165,   165,   165,   166,   166,   167,   167,   167,   168,   168,
     169,   169,   170,   170,   171,   171,   172,   172,   172,   173,
     174,   175,   176,   176,   176,   176,   177,   177,   178,   178,
     178,   179,   179,   180,   181,   181,   181,   182,   183,   183,
     184,   184,   185,   185,   185,   185,   186,   187,   187,   188,
     189,   189,   190,   190,   190,   190,   191,   191,   192,   192,
     192,   193,   193,   193,   194,   194,   194,   195,   195,   195,
     195,   195,   195,   196,   196,   197,   197,   198,   198,   198,
     198,   199,   199,   200,   201,   202,   202,   203,   203,   203,
     204,   205,   206,   207,   208,   208,   209,   209,   209,   209,
     210,   210,   210,   210,   211,   211,   211,   212,   212,   213,
     214,   214,   215,   216,   216,   216,   216,   217,   217,   217,
     217,   218,   218,   218,   219,   219,   220,   220,   221,   221,
     221,   222,   222,   222,   222,   222,   222,   222,   223,   223,
     223,   224,   224,   224,   224,   224,   224,   224,   224,   224,
     224,   224,   224,   224,   224,   225,   225,   226,   226,   227,
     227,   228,   228,   229,   229,   230,   230,   231,   231,   231,
     231,   231,   231,   231,   232,   232,   232,   232,   232,   232,
     233,   233,   233,   233,   233,   233,   233,   233,   234,   235,
     235,   235,   235,   235,   235,   236,   236,   237,   237,   237,
     237,   237,   237,   237,   237,   237,   237,   237,   237,   237,
     237,   237,   237,   237,   237,   237,   238,   238,   239,   239,
     239,   240,   240,   240,   241,   241,   241,   241,   241,   241,
     241,   242,   242,   243,   243,   243,   243,   243,   244,   244,
     244,   244,   244,   244,   244,   244,   244,   244,   244,   245,
     245,   246,   246,   246,   246,   246,   246,   246,   246,   246,
     246,   246,   246,   247,   247,   248,   248,   248,   248,   248,
     248,   248,   249,   249,   249,   249,   249,   249,   249,   249,
     249,   249,   249,   249,   249,   249,   249,   249,   249,   249,
     249,   249,   250,   250,   250,   250,   251,   252,   252,   252,
     252,   253,   254,   254,   254,   255,   255,   256,   256,   256,
     257,   257,   258,   258,   259,   259,   259,   260,   260,   261,
     261,   262,   262,   263,   263,   264,   264,   265,   265,   266,
     266,   267,   267,   268,   268,   268,   268,   269,   269,   270,
     271,   271,   272,   272,   273,   273,   274,   274,   275,   275,
     276,   276,   277,   277,   278,   278,   278,   279,   279,   280,
     280,   281,   281,   282,   282,   283,   284,   285,   285,   285,
     285,   286,   286,   287,   287,   287,   287,   287,   287,   287,
     287,   287,   287,   288,   288,   288,   289,   289,   290,   290,
     291,   291,   291,   292,   292,   292,   292,   292,   293,   293,
     294,   294,   294,   294,   294,   294,   294,   294,   294,   294,
     294,   294,   294,   294,   294,   294,   294,   294,   294,   294,
     294,   294,   294,   294,   294,   294,   294,   294,   294,   294,
     294,   294,   294,   294,   294,   294,   294,   294,   294,   294,
     294,   294,   294,   294,   294,   294,   294,   294,   294,   294,
     294,   294,   294,   294,   294,   294,   294,   294,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   295,   295,
     295,   295,   295,   295,   295,   295,   295,   295,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   297,   297,   297,
     297,   297,   297,   297,   298,   298,   298,   298,   298,   298,
     298,   299,   299,   299,   299,   299,   300,   300,   301,   301,
     302,   302,   302,   302,   303,   303,   303,   304,   304,   304,
     304,   305,   305,   305,   306,   306,   307,   307,   307,   307,
     308,   308,   308,   309,   309,   310,   310,   310,   311,   312,
     312,   312,   312,   312,   312,   312,   312,   312,   313,   313,
     314,   314,   314,   314,   314,   314,   314,   314,   315,   315,
     315,   315,   316,   316,   317,   317,   317,   318,   318,   319,
     319,   320,   320,   321,   321,   322,   322,   323,   323,   323,
     324,   325,   326,   327,   328,   328,   329,   330,   330,   330,
     330,   330,   330,   330,   331,   331,   331,   331,   332,   332,
     333,   333,   333,   333,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   335,   335,
     336,   336,   337,   337,   337,   338,   339,   340
  };

  const unsigned char
  parser::yyr2_[] =
  {
       0,     2,     3,     2,     1,     0,     1,     0,     1,     2,
       4,     1,     1,     0,     1,     2,     4,     1,     1,     3,
       4,     5,     0,     1,     3,     1,     0,     1,     2,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     7,     8,
       7,     5,     4,     5,     1,     1,     4,     6,     3,     3,
       1,     4,     3,     5,     4,     6,     5,     3,     2,     1,
       2,     3,     4,     3,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     0,     2,     0,     5,     6,     5,
       3,     4,     3,     4,     1,     3,     0,     4,     1,     3,
       0,     2,     7,     8,     1,     3,     0,     3,     3,     4,
       3,     2,     0,     7,     8,     3,     5,     6,     5,     6,
       1,     0,     1,     0,     1,     2,     3,     2,     3,     5,
       6,     2,     2,     3,     4,     5,     1,     0,     1,     3,
       3,     1,     1,     7,     3,     3,     0,    10,     1,     0,
       1,     2,     1,     1,     1,     2,     6,     2,     0,     4,
       1,     0,     2,     1,     1,     0,     1,     1,     8,     9,
      10,     8,     9,    10,     9,    10,    11,     9,    11,    11,
      12,     8,     9,     1,     0,     1,     2,     1,     2,     1,
       1,     1,     0,     3,     8,     6,     7,     7,     8,     9,
       2,     2,     2,     3,     4,     2,     6,     7,     8,     3,
       6,     7,     8,     3,     1,     2,     0,     1,     3,     3,
       1,     3,     2,     1,     2,     3,     0,     1,     2,     3,
       0,     1,     2,     0,     1,     3,     3,     1,     2,     3,
       0,     1,     1,     2,     2,     2,     2,     2,     2,     2,
       0,     2,     3,     4,     3,     4,     5,     6,     5,     6,
       3,     4,     3,     4,     0,     0,     1,     2,     3,     1,
       3,     4,     4,     4,     0,     1,     3,     1,     2,     1,
       2,     1,     2,     3,     1,     2,     5,     3,     4,     7,
       3,     3,     3,     3,     3,     3,     3,     3,     1,     1,
       2,     3,     1,     2,     0,     2,     0,     1,     2,     3,
       2,     2,     3,     3,     1,     3,     4,     3,     4,     4,
       2,     3,     4,     2,     6,    10,     1,     3,     1,     2,
       1,     1,     1,     2,     1,     2,     2,     3,     3,     4,
       3,     1,     3,     1,     2,     3,     1,     0,     1,     2,
       2,     3,     4,     5,     5,     6,     3,     4,     1,     1,
       3,     1,     2,     2,     3,     4,     5,     5,     6,     3,
       4,     1,     0,     1,     3,     1,     1,     6,    10,     3,
       4,     2,     1,     2,     3,     4,     5,     2,     3,     2,
       3,     2,     3,     4,     4,     3,     6,     5,     4,     1,
       1,     1,     2,     3,     4,     5,     3,     6,     5,     4,
       3,     5,     1,     1,     1,     1,     0,     1,     1,     0,
       2,     4,     6,     8,     1,     2,     0,     1,     3,     1,
       3,     1,     1,     1,     3,     1,     1,     2,     0,     1,
       0,     1,     3,     5,     1,     6,     2,     1,     3,     3,
       3,     5,     2,     0,     1,     3,     1,     1,     2,     0,
       1,     3,     2,     0,     1,     2,     0,     1,     3,     2,
       1,     1,     1,     1,     2,     4,     3,     1,     2,     1,
       0,     1,     2,     2,     1,     2,     2,     3,     1,     2,
       2,     3,     1,     1,     2,     0,     1,     0,     1,     3,
       1,     2,     3,     1,     1,     3,     3,     3,     4,     4,
       1,     1,     1,     1,     4,     2,     3,     3,     4,     4,
       3,     3,     1,     2,     1,     2,     1,     2,     1,     2,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     2,
       3,     2,     1,     3,     3,     2,     1,     1,     1,     1,
       1,     1,     4,     2,     3,     3,     4,     4,     3,     3,
       1,     2,     1,     2,     1,     2,     1,     2,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     2,     3,     2,
       1,     3,     3,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     3,     3,     4,     4,     3,     3,     1,
       2,     1,     2,     1,     2,     1,     2,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     2,     3,     2,     1,
       3,     3,     2,     1,     1,     1,     1,     2,     2,     2,
       3,     3,     1,     2,     2,     2,     2,     3,     3,     1,
       2,     7,    10,    11,    11,    12,     2,     0,     2,     0,
       3,     4,     5,     4,     3,     4,     3,     2,     4,     4,
       4,     3,     4,     3,     1,     3,     1,     2,     2,     0,
       1,     2,     0,     1,     3,     1,     3,     3,     2,     1,
       1,     1,     1,     1,     1,     1,     2,     4,     1,     1,
       3,     3,     6,     6,     6,     6,     3,     3,     4,     5,
       6,     5,     1,     2,     2,     1,     2,     5,     5,     5,
       5,     2,     0,     3,     5,     6,     8,     1,     1,     1,
       4,     7,     3,     6,     2,     0,     5,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     0,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     0,     2,
       1,     1,     1,     1,     1,     3,     3,     3
  };


#if YYDEBUG
  // YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
  // First, the terminals, then, starting at \a yyntokens_, nonterminals.
  const char*
  const parser::yytname_[] =
  {
  "$end", "error", "$undefined", "SHL", "SHR", "LE", "EQEQ", "NE", "GE",
  "ANDAND", "OROR", "SHLEQ", "SHREQ", "MINUSEQ", "ANDEQ", "OREQ", "PLUSEQ",
  "STAREQ", "SLASHEQ", "CARETEQ", "PERCENTEQ", "DOTDOT", "DOTDOTDOT",
  "MOD_SEP", "RARROW", "LARROW", "FAT_ARROW", "LIT_BYTE", "LIT_CHAR",
  "LIT_INTEGER", "LIT_FLOAT", "LIT_STR", "LIT_STR_RAW", "LIT_BYTE_STR",
  "LIT_BYTE_STR_RAW", "IDENT", "UNDERSCORE", "LIFETIME", "SELF", "STATIC",
  "ABSTRACT", "ALIGNOF", "AS", "BECOME", "BREAK", "CATCH", "CRATE", "DO",
  "ELSE", "ENUM", "EXTERN", "FALSE", "FINAL", "FN", "FOR", "IF", "IMPL",
  "IN", "LET", "LOOP", "MACRO", "MATCH", "MOD", "MOVE", "MUT", "OFFSETOF",
  "OVERRIDE", "PRIV", "PUB", "PURE", "REF", "RETURN", "SIZEOF", "STRUCT",
  "SUPER", "UNION", "UNSIZED", "TRUE", "TRAIT", "TYPE", "UNSAFE",
  "VIRTUAL", "YIELD", "DEFAULT", "USE", "WHILE", "CONTINUE", "PROC", "BOX",
  "CONST", "WHERE", "TYPEOF", "INNER_DOC_COMMENT", "OUTER_DOC_COMMENT",
  "SHEBANG", "SHEBANG_LINE", "STATIC_LIFETIME", "LAMBDA", "SHIFTPLUS",
  "':'", "FORTYPE", "'?'", "'='", "'<'", "'>'", "'|'", "'^'", "'&'", "'+'",
  "'-'", "'*'", "'/'", "'%'", "'!'", "'{'", "'['", "'('", "'.'", "RANGE",
  "']'", "'#'", "')'", "','", "';'", "'}'", "'@'", "'~'", "'$'", "$accept",
  "crate", "maybe_shebang", "maybe_inner_attrs", "inner_attrs",
  "inner_attr", "maybe_outer_attrs", "outer_attrs", "outer_attr",
  "meta_item", "meta_seq", "maybe_mod_items", "mod_items", "attrs_and_vis",
  "mod_item", "item", "stmt_item", "item_static", "item_const",
  "item_macro", "view_item", "extern_fn_item", "use_item", "view_path",
  "block_item", "maybe_ty_ascription", "maybe_init_expr", "item_struct",
  "struct_decl_args", "struct_tuple_args", "struct_decl_fields",
  "struct_decl_field", "struct_tuple_fields", "struct_tuple_field",
  "item_enum", "enum_defs", "enum_def", "enum_args", "item_union",
  "item_mod", "item_foreign_mod", "maybe_abi", "maybe_foreign_items",
  "foreign_items", "foreign_item", "item_foreign_static",
  "item_foreign_fn", "fn_decl_allow_variadic", "fn_params_allow_variadic",
  "visibility", "idents_or_self", "ident_or_self", "item_type",
  "for_sized", "item_trait", "maybe_trait_items", "trait_items",
  "trait_item", "trait_const", "maybe_const_default", "trait_type",
  "maybe_unsafe", "maybe_default_maybe_unsafe", "trait_method",
  "type_method", "method", "impl_method", "item_impl", "maybe_impl_items",
  "impl_items", "impl_item", "maybe_default", "impl_const", "impl_type",
  "item_fn", "item_unsafe_fn", "fn_decl", "fn_decl_with_self",
  "fn_decl_with_self_allow_anon_params", "fn_params", "fn_anon_params",
  "fn_params_with_self", "fn_anon_params_with_self", "maybe_params",
  "params", "param", "inferrable_params", "inferrable_param",
  "maybe_comma_params", "maybe_comma_anon_params", "maybe_anon_params",
  "anon_params", "anon_param", "anon_params_allow_variadic_tail",
  "named_arg", "ret_ty", "generic_params", "maybe_where_clause",
  "where_clause", "where_predicates", "where_predicate",
  "maybe_for_lifetimes", "ty_params", "path_no_types_allowed",
  "path_generic_args_without_colons", "generic_args", "generic_values",
  "maybe_ty_sums_and_or_bindings", "maybe_bindings", "pat", "pats_or",
  "binding_mode", "lit_or_path", "pat_field", "pat_fields", "pat_struct",
  "pat_tup", "pat_tup_elts", "pat_vec", "pat_vec_elts", "ty", "ty_prim",
  "ty_bare_fn", "ty_fn_decl", "ty_closure", "for_in_type",
  "for_in_type_suffix", "maybe_mut", "maybe_mut_or_const",
  "ty_qualified_path_and_generic_values", "ty_qualified_path",
  "maybe_ty_sums", "ty_sums", "ty_sum", "ty_sum_elt", "ty_prim_sum",
  "ty_prim_sum_elt", "maybe_ty_param_bounds", "ty_param_bounds",
  "boundseq", "polybound", "bindings", "binding", "ty_param",
  "maybe_bounds", "bounds", "bound", "maybe_ltbounds", "ltbounds",
  "maybe_ty_default", "maybe_lifetimes", "lifetimes",
  "lifetime_and_bounds", "lifetime", "trait_ref", "inner_attrs_and_block",
  "block", "maybe_stmts", "stmts", "stmt", "maybe_exprs", "maybe_expr",
  "exprs", "path_expr", "path_generic_args_with_colons", "macro_expr",
  "nonblock_expr", "expr", "expr_nostruct",
  "nonblock_prefix_expr_nostruct", "nonblock_prefix_expr",
  "expr_qualified_path", "maybe_qpath_params", "maybe_as_trait_ref",
  "lambda_expr", "lambda_expr_no_first_bar", "lambda_expr_nostruct",
  "lambda_expr_nostruct_no_first_bar", "vec_expr", "struct_expr_fields",
  "maybe_field_inits", "field_inits", "field_init", "default_field_init",
  "block_expr", "full_block_expr", "block_expr_dot", "expr_match",
  "match_clauses", "match_clause", "nonblock_match_clause",
  "block_match_clause", "maybe_guard", "expr_if", "expr_if_let",
  "block_or_if", "expr_while", "expr_while_let", "expr_loop", "expr_for",
  "maybe_label", "let", "lit", "str", "maybe_ident", "ident",
  "unpaired_token", "token_trees", "token_tree", "delimited_token_trees",
  "parens_delimited_token_trees", "braces_delimited_token_trees",
  "brackets_delimited_token_trees", YY_NULLPTR
  };


  const unsigned short int
  parser::yyrline_[] =
  {
       0,   228,   228,   229,   233,   234,   238,   239,   243,   244,
     248,   249,   253,   254,   258,   259,   263,   264,   268,   269,
     270,   271,   275,   276,   277,   281,   282,   286,   287,   291,
     295,   300,   301,   306,   307,   308,   309,   310,   314,   315,
     319,   323,   324,   325,   329,   330,   331,   332,   336,   340,
     344,   345,   346,   347,   348,   349,   350,   351,   352,   353,
     354,   355,   356,   357,   361,   362,   363,   364,   365,   366,
     367,   368,   369,   373,   374,   378,   379,   384,   388,   392,
     399,   400,   404,   405,   409,   410,   411,   415,   419,   420,
     421,   425,   430,   431,   435,   436,   437,   441,   445,   446,
     447,   448,   449,   454,   455,   458,   459,   460,   464,   465,
     469,   470,   474,   475,   479,   480,   484,   485,   486,   490,
     494,   498,   502,   503,   504,   505,   509,   510,   514,   515,
     516,   520,   521,   525,   529,   530,   531,   535,   542,   543,
     547,   548,   552,   553,   554,   555,   559,   563,   564,   568,
     572,   573,   577,   578,   579,   580,   583,   584,   588,   592,
     596,   603,   607,   611,   618,   622,   626,   648,   652,   656,
     660,   664,   668,   675,   676,   680,   681,   685,   686,   687,
     688,   692,   693,   697,   701,   705,   746,   753,   757,   761,
     768,   773,   777,   781,   785,   786,   790,   791,   792,   793,
     797,   798,   799,   800,   804,   805,   806,   810,   813,   817,
     823,   824,   828,   832,   833,   834,   835,   839,   840,   841,
     842,   846,   847,   848,   852,   853,   859,   860,   864,   865,
     866,   870,   871,   872,   873,   874,   875,   876,   880,   881,
     882,   886,   887,   888,   889,   890,   891,   892,   893,   894,
     895,   896,   897,   898,   899,   903,   904,   908,   909,   913,
     914,   918,   919,   923,   924,   927,   928,   936,   937,   938,
     939,   940,   941,   942,   957,   959,   960,   961,   962,   963,
     968,   969,   970,   971,   976,   977,   978,   979,   983,   987,
     988,   989,   990,   991,   992,   996,   997,  1005,  1006,  1007,
    1008,  1009,  1010,  1011,  1012,  1013,  1014,  1015,  1016,  1017,
    1018,  1021,  1022,  1023,  1024,  1025,  1032,  1033,  1037,  1038,
    1039,  1043,  1044,  1045,  1049,  1050,  1051,  1052,  1053,  1054,
    1055,  1059,  1060,  1064,  1065,  1066,  1067,  1068,  1072,  1073,
    1074,  1075,  1076,  1077,  1078,  1079,  1080,  1081,  1082,  1086,
    1087,  1091,  1092,  1093,  1094,  1095,  1096,  1097,  1098,  1099,
    1100,  1101,  1102,  1106,  1107,  1115,  1116,  1117,  1118,  1119,
    1120,  1121,  1125,  1127,  1128,  1129,  1130,  1131,  1132,  1133,
    1134,  1135,  1136,  1137,  1138,  1139,  1140,  1141,  1142,  1143,
    1144,  1145,  1149,  1150,  1151,  1152,  1156,  1160,  1161,  1162,
    1163,  1167,  1171,  1172,  1173,  1177,  1178,  1182,  1183,  1184,
    1188,  1192,  1199,  1200,  1204,  1205,  1206,  1210,  1211,  1215,
    1217,  1221,  1222,  1226,  1227,  1231,  1232,  1236,  1237,  1241,
    1242,  1246,  1247,  1251,  1252,  1253,  1254,  1258,  1259,  1263,
    1267,  1268,  1272,  1274,  1278,  1279,  1283,  1284,  1288,  1290,
    1294,  1295,  1299,  1300,  1304,  1305,  1306,  1310,  1311,  1315,
    1316,  1320,  1321,  1325,  1326,  1334,  1343,  1351,  1352,  1360,
    1366,  1395,  1398,  1404,  1411,  1412,  1413,  1414,  1415,  1416,
    1418,  1420,  1421,  1425,  1426,  1427,  1432,  1433,  1438,  1441,
    1446,  1447,  1448,  1457,  1459,  1460,  1461,  1462,  1467,  1483,
    1487,  1488,  1489,  1490,  1491,  1492,  1493,  1494,  1495,  1496,
    1510,  1511,  1529,  1530,  1531,  1532,  1533,  1534,  1535,  1536,
    1537,  1538,  1540,  1542,  1544,  1546,  1548,  1550,  1552,  1554,
    1556,  1558,  1559,  1560,  1561,  1562,  1563,  1564,  1565,  1566,
    1567,  1568,  1569,  1570,  1571,  1572,  1573,  1574,  1575,  1576,
    1577,  1578,  1579,  1580,  1581,  1582,  1583,  1584,  1588,  1589,
    1590,  1591,  1592,  1593,  1594,  1595,  1596,  1597,  1611,  1613,
    1614,  1615,  1616,  1617,  1618,  1619,  1620,  1621,  1622,  1623,
    1626,  1629,  1632,  1635,  1638,  1641,  1644,  1647,  1650,  1653,
    1654,  1655,  1656,  1657,  1658,  1659,  1660,  1661,  1662,  1663,
    1664,  1665,  1666,  1667,  1668,  1669,  1670,  1671,  1672,  1673,
    1674,  1675,  1676,  1677,  1678,  1679,  1680,  1681,  1685,  1687,
    1689,  1690,  1691,  1692,  1693,  1694,  1695,  1696,  1697,  1700,
    1701,  1702,  1703,  1704,  1705,  1706,  1707,  1708,  1709,  1712,
    1715,  1718,  1721,  1724,  1727,  1730,  1733,  1736,  1739,  1740,
    1741,  1742,  1743,  1744,  1745,  1746,  1747,  1748,  1749,  1750,
    1751,  1752,  1753,  1754,  1755,  1756,  1757,  1758,  1759,  1760,
    1761,  1762,  1763,  1764,  1765,  1766,  1767,  1771,  1772,  1773,
    1774,  1776,  1777,  1778,  1782,  1783,  1784,  1785,  1787,  1788,
    1789,  1793,  1797,  1801,  1805,  1809,  1815,  1816,  1820,  1821,
    1825,  1827,  1829,  1831,  1836,  1838,  1840,  1845,  1847,  1849,
    1851,  1856,  1858,  1860,  1865,  1866,  1870,  1871,  1872,  1873,
    1877,  1878,  1879,  1883,  1884,  1888,  1889,  1890,  1894,  1898,
    1899,  1900,  1901,  1902,  1903,  1904,  1905,  1907,  1911,  1912,
    1916,  1917,  1918,  1919,  1920,  1921,  1922,  1923,  1927,  1928,
    1929,  1930,  1934,  1935,  1939,  1940,  1941,  1945,  1946,  1950,
    1951,  1955,  1956,  1960,  1962,  1967,  1968,  1972,  1973,  1974,
    1978,  1983,  1987,  1992,  1996,  1997,  2001,  2074,  2075,  2078,
    2079,  2087,  2089,  2091,  2095,  2096,  2097,  2098,  2102,  2103,
    2107,  2109,  2110,  2111,  2116,  2117,  2118,  2119,  2120,  2121,
    2122,  2123,  2124,  2125,  2126,  2127,  2128,  2129,  2130,  2131,
    2132,  2133,  2134,  2135,  2136,  2137,  2138,  2139,  2140,  2141,
    2142,  2143,  2144,  2145,  2146,  2147,  2148,  2149,  2150,  2151,
    2152,  2153,  2154,  2155,  2156,  2157,  2158,  2159,  2160,  2161,
    2162,  2163,  2164,  2165,  2166,  2167,  2168,  2169,  2170,  2171,
    2172,  2173,  2174,  2175,  2176,  2177,  2178,  2179,  2180,  2181,
    2182,  2183,  2184,  2185,  2186,  2187,  2188,  2189,  2190,  2191,
    2192,  2193,  2194,  2195,  2196,  2197,  2198,  2199,  2200,  2201,
    2202,  2203,  2204,  2205,  2206,  2207,  2208,  2209,  2210,  2211,
    2212,  2213,  2214,  2215,  2216,  2217,  2218,  2219,  2220,  2221,
    2222,  2223,  2224,  2225,  2226,  2227,  2228,  2229,  2233,  2236,
    2243,  2244,  2249,  2250,  2251,  2255,  2267,  2274
  };

  // Print the state stack on the debug stream.
  void
  parser::yystack_print_ ()
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << i->state;
    *yycdebug_ << std::endl;
  }

  // Report on the debug stream that the rule \a yyrule is going to be reduced.
  void
  parser::yy_reduce_print_ (int yyrule)
  {
    unsigned int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):" << std::endl;
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // YYDEBUG

  // Symbol number corresponding to token number t.
  inline
  parser::token_number_type
  parser::yytranslate_ (int t)
  {
    static
    const token_number_type
    translate_table[] =
    {
     0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   113,     2,   120,   127,   112,   107,     2,
     116,   121,   110,   108,   122,   109,   117,   111,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    99,   123,
     103,   102,   104,   101,   125,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   115,     2,   119,   106,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   114,   105,   124,   126,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,   100,   118
    };
    const unsigned int user_token_number_max_ = 355;
    const token_number_type undef_token_ = 2;

    if (static_cast<int>(t) <= yyeof_)
      return yyeof_;
    else if (static_cast<unsigned int> (t) <= user_token_number_max_)
      return translate_table[t];
    else
      return undef_token_;
  }


} // yyrust
#line 8819 "rust_y.tab.cpp" // lalr1.cc:1167
