// Spike B: native recursive-descent parser for the full Go1.27 grammar
// (Go1.27 §Notation, productions throughout the spec). Builds a generic
// tree; the point of the spike is to measure effort and correctness
// against go/parser on real code, not to produce a CBMC-ready AST.
#include "go_parser.h"

#include <cstring>

namespace
{
struct parsert
{
  std::vector<tokent> t;
  std::size_t p = 0;
  // Go1.27 §Composite_literals: "a parsing ambiguity arises when a
  // composite literal using the TypeName form of the LiteralType appears
  // as an operand between the keyword and the opening brace of the block
  // of an "if", "for", or "switch" statement". expr_lev < 0 in headers.
  int expr_lev = 0;

  const tokent &cur() const
  {
    return t[p];
  }
  const tokent &la(std::size_t k) const
  {
    return t[std::min(p + k, t.size() - 1)];
  }
  [[noreturn]] void fail(const std::string &m) const
  {
    throw syntax_errort(
      "parse: " + m + " near '" + cur().text + "'", cur().line, cur().col);
  }
  bool got_op(const char *s)
  {
    if(cur().op(s))
    {
      p++;
      return true;
    }
    return false;
  }
  void expect_op(const char *s)
  {
    if(!got_op(s))
      fail(std::string("expected '") + s + "'");
  }
  bool got_kw(const char *s)
  {
    if(cur().kw(s))
    {
      p++;
      return true;
    }
    return false;
  }
  // A ';' is optional before a closing ')' or '}' (Go1.27 §Semicolons
  // rule 2).
  void semi()
  {
    if(cur().op(")") || cur().op("}"))
      return;
    if(cur().kind == tokt::END)
      return;
    expect_op(";");
  }
  std::string ident()
  {
    if(cur().kind != tokt::IDENT)
      fail("expected identifier");
    return t[p++].text;
  }
  static nodet mk(const char *kind, const std::string &text = "")
  {
    return nodet{kind, text, {}};
  }

  // ---------------------------------------------------------------- types
  bool starts_type(const tokent &k) const
  {
    return k.kind == tokt::IDENT || k.op("*") || k.op("[") || k.op("(") ||
           k.op("<-") || k.kw("func") || k.kw("map") || k.kw("chan") ||
           k.kw("struct") || k.kw("interface");
  }

  nodet type_name()
  {
    nodet n = mk("TypeName", ident());
    if(got_op("."))
      n.text += "." + ident();
    if(cur().op("["))
      n.kids.push_back(type_args());
    return n;
  }
  nodet type_args()
  {
    nodet n = mk("TypeArgs");
    expect_op("[");
    expr_lev++;
    do
    {
      if(cur().op("]"))
        break;
      n.kids.push_back(type());
    } while(got_op(","));
    expr_lev--;
    expect_op("]");
    return n;
  }

  nodet type()
  {
    const tokent &k = cur();
    if(k.kind == tokt::IDENT)
      return type_name();
    if(got_op("("))
    {
      nodet n = type();
      expect_op(")");
      return n;
    }
    if(k.op("["))
    {
      p++;
      if(got_op("]"))
      {
        nodet n = mk("SliceType");
        n.kids.push_back(type());
        return n;
      }
      nodet n = mk("ArrayType");
      expr_lev++;
      if(got_op("..."))
        n.text = "...";
      else
        n.kids.push_back(expr());
      expr_lev--;
      expect_op("]");
      n.kids.push_back(type());
      return n;
    }
    if(got_op("*"))
    {
      nodet n = mk("PointerType");
      n.kids.push_back(type());
      return n;
    }
    if(got_kw("func"))
      return signature("FuncType");
    if(got_kw("struct"))
      return struct_type();
    if(got_kw("interface"))
      return interface_type();
    if(got_kw("map"))
    {
      nodet n = mk("MapType");
      expect_op("[");
      n.kids.push_back(type());
      expect_op("]");
      n.kids.push_back(type());
      return n;
    }
    if(got_kw("chan"))
    {
      nodet n = mk("ChanType", got_op("<-") ? "send" : "both");
      n.kids.push_back(type());
      return n;
    }
    if(k.op("<-") && la(1).kw("chan"))
    {
      p += 2;
      nodet n = mk("ChanType", "recv");
      n.kids.push_back(type());
      return n;
    }
    fail("expected type");
  }

  // Go1.27 §Struct_types.
  nodet struct_type()
  {
    nodet n = mk("StructType");
    expect_op("{");
    while(!cur().op("}"))
    {
      nodet f = mk("Field");
      if(cur().op("*")) // embedded *T
      {
        p++;
        f.text = "embedded*";
        f.kids.push_back(type_name());
      }
      else if(
        cur().kind == tokt::IDENT &&
        (la(1).op(".") || la(1).op(";") || la(1).op("}") ||
         la(1).kind == tokt::STRING))
      {
        f.text = "embedded";
        f.kids.push_back(type_name());
      }
      else if(cur().kind == tokt::IDENT && la(1).op("["))
      {
        // "a [N]T" / "a []T" (field) vs "G[T]" (embedded generic):
        // try the embedded form and backtrack if it is not followed by
        // the end of the field declaration.
        std::size_t save = p;
        bool embedded = false;
        if(!la(2).op("]"))
        {
          try
          {
            nodet e = type_name();
            if(cur().op(";") || cur().op("}") || cur().kind == tokt::STRING)
            {
              f.text = "embedded";
              f.kids.push_back(e);
              embedded = true;
            }
          }
          catch(const syntax_errort &)
          {
          }
        }
        if(!embedded)
        {
          p = save;
          f.kids.push_back(mk("Name", ident()));
          f.kids.push_back(type());
        }
      }
      else
      {
        do
          f.kids.push_back(mk("Name", ident()));
        while(got_op(","));
        f.kids.push_back(type());
      }
      if(cur().kind == tokt::STRING)
        f.kids.push_back(mk("Tag", t[p++].text));
      n.kids.push_back(f);
      semi();
    }
    expect_op("}");
    return n;
  }

  // Go1.27 §Interface_types: methods, embedded types, and type unions.
  nodet interface_type()
  {
    nodet n = mk("InterfaceType");
    expect_op("{");
    while(!cur().op("}"))
    {
      if(cur().kind == tokt::IDENT && la(1).op("("))
      {
        nodet m = mk("Method", ident());
        m.kids.push_back(signature("FuncType"));
        n.kids.push_back(m);
      }
      else
        n.kids.push_back(type_elem());
      semi();
    }
    expect_op("}");
    return n;
  }
  nodet type_elem()
  {
    nodet n = mk("Union");
    do
    {
      nodet term = mk("Term", got_op("~") ? "~" : "");
      term.kids.push_back(type());
      n.kids.push_back(term);
    } while(got_op("|"));
    return n;
  }

  // Go1.27 §Function_types: Parameters and Result.
  nodet signature(const char *kind)
  {
    nodet n = mk(kind);
    n.kids.push_back(params());
    if(cur().op("("))
      n.kids.push_back(params());
    else if(starts_type(cur()) && !cur().op("("))
      n.kids.push_back(type());
    return n;
  }
  // Parameter lists are either all-named or all-anonymous; names are
  // recognised by a following type (Go1.27 §Function_types).
  nodet params()
  {
    nodet n = mk("Params");
    expect_op("(");
    expr_lev++;
    while(!cur().op(")"))
    {
      nodet item = mk("Param");
      if(got_op("..."))
      {
        item.text = "...";
        item.kids.push_back(type());
      }
      else if(cur().kind == tokt::IDENT && la(1).op("[") && !la(2).op("]"))
      {
        // "a [4]byte" (name + array type) vs "G[T]" (generic type): try
        // the latter and backtrack unless the item ends right after it.
        std::size_t save = p;
        bool generic = false;
        try
        {
          nodet g = type();
          if((generic = cur().op(",") || cur().op(")")))
            item.kids.push_back(g);
        }
        catch(const syntax_errort &)
        {
        }
        if(!generic)
        {
          p = save;
          item.kids.push_back(mk("Name", ident()));
          if(got_op("..."))
            item.text = "...";
          item.kids.push_back(type());
        }
      }
      else
      {
        item.kids.push_back(type());
        if(got_op("..."))
        {
          item.text = "...";
          item.kids.push_back(type());
        }
        else if(starts_type(cur()))
          item.kids.push_back(type());
      }
      n.kids.push_back(item);
      if(!got_op(","))
        break;
    }
    expr_lev--;
    expect_op(")");
    return n;
  }

  // Go1.27 §Type_parameter_declarations.
  nodet type_params()
  {
    nodet n = mk("TypeParams");
    expect_op("[");
    while(!cur().op("]"))
    {
      nodet tp = mk("TypeParam");
      do
        tp.kids.push_back(mk("Name", ident()));
      while(got_op(","));
      tp.kids.push_back(type_elem());
      n.kids.push_back(tp);
      if(!got_op(","))
        break;
    }
    expect_op("]");
    return n;
  }

  // ---------------------------------------------------------- expressions
  static int precedence(const tokent &k)
  {
    if(k.kind != tokt::OP)
      return 0;
    static const char *levels[][6] = {
      {"||"},
      {"&&"},
      {"==", "!=", "<", "<=", ">", ">="},
      {"+", "-", "|", "^"},
      {"*", "/", "%", "<<", ">>", "&"}};
    for(int l = 0; l < 5; l++)
      for(const char *op : levels[l])
        if(op && k.text == op)
          return l + 1;
    if(k.text == "&^")
      return 5;
    return 0;
  }

  nodet expr(int prec = 1)
  {
    nodet x = unary();
    for(int pr; (pr = precedence(cur())) >= prec;)
    {
      nodet b = mk("BinaryExpr", t[p++].text);
      b.kids.push_back(x);
      b.kids.push_back(expr(pr + 1));
      x = b;
    }
    return x;
  }

  nodet unary()
  {
    const tokent &k = cur();
    if(k.op("<-") && la(1).kw("chan"))
      return primary(type()); // channel type as operand
    if(
      k.op("+") || k.op("-") || k.op("!") || k.op("^") || k.op("*") ||
      k.op("&") || k.op("<-") || k.op("~"))
    {
      nodet u = mk("UnaryExpr", t[p++].text);
      u.kids.push_back(unary());
      return u;
    }
    return primary(operand());
  }

  static bool is_literal_type(const nodet &x)
  {
    return x.kind == "Ident" || x.kind == "Selector" || x.kind == "Index" ||
           x.kind == "ArrayType" || x.kind == "SliceType" ||
           x.kind == "StructType" || x.kind == "MapType" ||
           x.kind == "TypeName";
  }
  static bool is_type_name(const nodet &x)
  {
    return x.kind == "Ident" || x.kind == "Selector" || x.kind == "Index";
  }

  nodet operand()
  {
    const tokent &k = cur();
    switch(k.kind)
    {
    case tokt::INT:
    case tokt::FLOAT:
    case tokt::IMAG:
    case tokt::CHAR:
    case tokt::STRING:
      p++;
      return mk("BasicLit", k.text);
    case tokt::IDENT:
      p++;
      return mk("Ident", k.text);
    default:
      break;
    }
    if(got_op("("))
    {
      expr_lev++;
      nodet n = mk("Paren");
      n.kids.push_back(expr_or_type());
      expr_lev--;
      expect_op(")");
      return n;
    }
    if(got_kw("func"))
    {
      nodet sig = signature("FuncType");
      if(!cur().op("{"))
        return sig;
      nodet lit = mk("FuncLit");
      lit.kids.push_back(sig);
      expr_lev++;
      lit.kids.push_back(block());
      expr_lev--;
      return lit;
    }
    if(
      k.op("[") || k.kw("struct") || k.kw("map") || k.kw("chan") ||
      k.kw("interface"))
      return type();
    fail("expected operand");
  }

  // Types may appear wherever an expression is allowed in conversions,
  // make/new arguments, and type arguments.
  nodet expr_or_type()
  {
    if(cur().op("[") || cur().kw("struct") || cur().kw("map") ||
       cur().kw("chan") || cur().kw("interface") || cur().kw("func"))
      return expr();
    if(cur().op("...")) // [...]T inside composite types is handled by type()
      fail("unexpected ...");
    return expr();
  }

  nodet primary(nodet x)
  {
    for(;;)
    {
      if(got_op("."))
      {
        if(got_op("("))
        {
          nodet a = mk("TypeAssert");
          a.kids.push_back(x);
          if(!got_kw("type"))
            a.kids.push_back(type());
          expect_op(")");
          x = a;
        }
        else
        {
          nodet s = mk("Selector", ident());
          s.kids.push_back(x);
          x = s;
        }
      }
      else if(got_op("["))
      {
        // Index, slice, or instantiation with type arguments.
        expr_lev++;
        nodet ix = mk("Index");
        ix.kids.push_back(x);
        if(!cur().op(":"))
          ix.kids.push_back(index_operand());
        if(cur().op(":"))
        {
          ix.kind = "Slice";
          while(got_op(":"))
            if(!cur().op(":") && !cur().op("]"))
              ix.kids.push_back(expr());
        }
        else
          while(got_op(","))
            if(!cur().op("]"))
              ix.kids.push_back(index_operand());
        expr_lev--;
        expect_op("]");
        x = ix;
      }
      else if(got_op("("))
      {
        expr_lev++;
        nodet c = mk("Call");
        c.kids.push_back(x);
        while(!cur().op(")"))
        {
          c.kids.push_back(index_operand());
          got_op("...");
          if(!got_op(","))
            break;
        }
        expr_lev--;
        expect_op(")");
        x = c;
      }
      else if(
        cur().op("{") && is_literal_type(x) &&
        (expr_lev >= 0 || !is_type_name(x)))
        x = composite_lit(x);
      else
        return x;
    }
  }
  // Index and call operands may be types: F[[]int], make(map[K]V).
  // Function literals and func types go through expr() via operand().
  nodet index_operand()
  {
    if(cur().op("*") || cur().op("<-") || cur().kw("func"))
      return expr();
    if(starts_type(cur()) && cur().kind != tokt::IDENT && !cur().op("("))
      return primary(type());
    return expr();
  }

  // Go1.27 §Composite_literals.
  nodet composite_lit(const nodet &type_node)
  {
    nodet n = literal_value();
    n.kids.insert(n.kids.begin(), type_node);
    return n;
  }
  nodet literal_value()
  {
    nodet n = mk("CompositeLit");
    expect_op("{");
    expr_lev++;
    while(!cur().op("}"))
    {
      nodet e = element();
      if(got_op(":"))
      {
        nodet kv = mk("KeyValue");
        kv.kids.push_back(e);
        kv.kids.push_back(element());
        e = kv;
      }
      n.kids.push_back(e);
      if(!got_op(","))
        break;
    }
    expr_lev--;
    expect_op("}");
    return n;
  }
  nodet element()
  {
    if(cur().op("{"))
      return literal_value(); // elided type
    return expr();
  }

  // ----------------------------------------------------------- statements
  nodet block()
  {
    nodet n = mk("Block");
    expect_op("{");
    stmt_list(n);
    expect_op("}");
    return n;
  }
  void stmt_list(nodet &n)
  {
    while(!cur().op("}") && !cur().kw("case") && !cur().kw("default") &&
          cur().kind != tokt::END)
    {
      if(got_op(";"))
        continue; // empty statement
      n.kids.push_back(stmt());
      semi();
    }
  }

  nodet expr_list()
  {
    nodet n = mk("ExprList");
    do
      n.kids.push_back(expr());
    while(got_op(","));
    return n;
  }

  // Go1.27 §Simple_statements. range_ok permits "x := range y" in for.
  nodet simple_stmt(bool range_ok = false)
  {
    if(range_ok && cur().kw("range")) // for range x
    {
      p++;
      nodet r = mk("Range");
      r.kids.push_back(expr());
      return r;
    }
    nodet lhs = expr_list();
    const tokent &k = cur();
    if(k.op(":=") || k.op("=") || (k.kind == tokt::OP && k.text.size() >= 2 &&
                                   k.text.back() == '=' && k.text != "==" &&
                                   k.text != "!=" && k.text != "<=" &&
                                   k.text != ">="))
    {
      nodet a = mk("Assign", t[p++].text);
      a.kids.push_back(lhs);
      if(range_ok && got_kw("range"))
      {
        nodet r = mk("Range");
        r.kids.push_back(expr());
        a.kids.push_back(r);
      }
      else
        a.kids.push_back(expr_list());
      return a;
    }
    if(k.op("++") || k.op("--"))
    {
      nodet s = mk("IncDec", t[p++].text);
      s.kids.push_back(lhs);
      return s;
    }
    if(got_op("<-"))
    {
      nodet s = mk("Send");
      s.kids.push_back(lhs);
      s.kids.push_back(expr());
      return s;
    }
    nodet s = mk("ExprStmt");
    s.kids.push_back(lhs);
    return s;
  }

  nodet stmt()
  {
    const tokent &k = cur();
    if(k.kw("var") || k.kw("const") || k.kw("type"))
      return decl();
    if(k.kind == tokt::IDENT && la(1).op(":"))
    {
      nodet l = mk("Labeled", ident());
      p++; // ':'
      if(!cur().op("}") && !cur().op(";")) // label on an empty statement
        l.kids.push_back(stmt());
      return l;
    }
    if(got_kw("go") || (k.kw("defer") && (p++, true)))
    {
      nodet n = mk(t[p - 1].text == "go" ? "Go" : "Defer");
      n.kids.push_back(expr());
      return n;
    }
    if(got_kw("return"))
    {
      nodet n = mk("Return");
      if(!cur().op(";") && !cur().op("}"))
        n.kids.push_back(expr_list());
      return n;
    }
    if(k.kw("break") || k.kw("continue") || k.kw("goto"))
    {
      nodet n = mk("Branch", t[p++].text);
      if(cur().kind == tokt::IDENT)
        n.kids.push_back(mk("Label", ident()));
      return n;
    }
    if(got_kw("fallthrough"))
      return mk("Fallthrough");
    if(k.op("{"))
      return block();
    if(got_kw("if"))
      return if_stmt();
    if(got_kw("switch"))
      return switch_stmt();
    if(got_kw("select"))
      return select_stmt();
    if(got_kw("for"))
      return for_stmt();
    return simple_stmt();
  }

  nodet if_stmt()
  {
    nodet n = mk("If");
    int saved = expr_lev;
    expr_lev = -1;
    nodet s = simple_stmt();
    if(got_op(";"))
    {
      n.kids.push_back(s);
      n.kids.push_back(expr());
    }
    else
      n.kids.push_back(s);
    expr_lev = saved;
    n.kids.push_back(block());
    if(got_kw("else"))
    {
      if(got_kw("if"))
        n.kids.push_back(if_stmt());
      else
        n.kids.push_back(block());
    }
    return n;
  }

  nodet switch_stmt()
  {
    nodet n = mk("Switch");
    int saved = expr_lev;
    expr_lev = -1;
    if(!cur().op("{"))
    {
      if(!cur().op(";"))
        n.kids.push_back(simple_stmt());
      if(got_op(";") && !cur().op("{"))
        n.kids.push_back(simple_stmt());
    }
    expr_lev = saved;
    expect_op("{");
    while(!cur().op("}"))
    {
      nodet c = mk("Case");
      if(got_kw("case"))
      {
        do
          c.kids.push_back(index_operand()); // types in type switches
        while(got_op(","));
      }
      else if(!got_kw("default"))
        fail("expected case or default");
      expect_op(":");
      stmt_list(c);
      n.kids.push_back(c);
    }
    expect_op("}");
    return n;
  }

  nodet select_stmt()
  {
    nodet n = mk("Select");
    expect_op("{");
    while(!cur().op("}"))
    {
      nodet c = mk("CommClause");
      if(got_kw("case"))
        c.kids.push_back(simple_stmt());
      else if(!got_kw("default"))
        fail("expected case or default");
      expect_op(":");
      stmt_list(c);
      n.kids.push_back(c);
    }
    expect_op("}");
    return n;
  }

  // Go1.27 §For_statements: for, for cond, for init;cond;post, for range.
  nodet for_stmt()
  {
    nodet n = mk("For");
    int saved = expr_lev;
    expr_lev = -1;
    if(!cur().op("{"))
    {
      if(!cur().op(";"))
        n.kids.push_back(simple_stmt(true));
      if(got_op(";"))
      {
        if(!cur().op(";"))
          n.kids.push_back(expr());
        expect_op(";");
        if(!cur().op("{"))
          n.kids.push_back(simple_stmt());
      }
    }
    expr_lev = saved;
    n.kids.push_back(block());
    return n;
  }

  // --------------------------------------------------------- declarations
  template <typename F>
  nodet group(const char *kind, F spec)
  {
    nodet n = mk(kind);
    if(got_op("("))
    {
      while(!cur().op(")"))
      {
        n.kids.push_back(spec());
        semi();
      }
      expect_op(")");
    }
    else
      n.kids.push_back(spec());
    return n;
  }

  nodet decl()
  {
    if(got_kw("var"))
      return group("VarDecl", [&] {
        nodet s = mk("Spec");
        do
          s.kids.push_back(mk("Name", ident()));
        while(got_op(","));
        if(!cur().op("="))
          s.kids.push_back(type());
        if(got_op("="))
          s.kids.push_back(expr_list());
        return s;
      });
    if(got_kw("const"))
      return group("ConstDecl", [&] {
        nodet s = mk("Spec");
        do
          s.kids.push_back(mk("Name", ident()));
        while(got_op(","));
        if(!cur().op("=") && !cur().op(";") && !cur().op(")"))
          s.kids.push_back(type());
        if(got_op("="))
          s.kids.push_back(expr_list());
        return s;
      });
    if(got_kw("type"))
      return group("TypeDecl", [&] { return type_spec(); });
    fail("expected declaration");
  }

  // Go1.27 §Type_declarations. "type T[P C] ..." declares a generic type;
  // "type T [N]E" an array type. Disambiguate by the token after "[ IDENT".
  nodet type_spec()
  {
    nodet s = mk("TypeSpec", ident());
    if(
      cur().op("[") && la(1).kind == tokt::IDENT &&
      (la(2).kind == tokt::IDENT || la(2).op(",") || la(2).op("~") ||
       la(2).op("[") || la(2).op("(") || la(2).kw("interface") ||
       la(2).kw("func") || la(2).kw("map") || la(2).kw("chan") ||
       la(2).kw("struct") ||
       (la(2).op("*") && la(3).kind == tokt::IDENT && !la(4).op("]")) ||
       (la(2).op("*") && la(3).kind == tokt::IDENT && la(4).op("]") &&
        starts_type(la(5)) == false)))
      s.kids.push_back(type_params());
    if(got_op("="))
      s.text += "=";
    s.kids.push_back(type());
    return s;
  }

  nodet func_decl()
  {
    nodet n = mk("FuncDecl");
    if(cur().op("("))
      n.kids.push_back(params()); // receiver
    n.text = ident();
    if(cur().op("["))
      n.kids.push_back(type_params());
    n.kids.push_back(signature("FuncType"));
    if(cur().op("{"))
    {
      expr_lev++;
      n.kids.push_back(block());
      expr_lev--;
    }
    return n;
  }

  nodet file()
  {
    nodet f = mk("File");
    if(!got_kw("package"))
      fail("expected package clause");
    f.text = ident();
    semi();
    while(got_kw("import"))
    {
      f.kids.push_back(group("ImportDecl", [&] {
        nodet s = mk("ImportSpec");
        if(cur().kind == tokt::IDENT || cur().op("."))
          s.text = t[p++].text;
        if(cur().kind != tokt::STRING)
          fail("expected import path");
        s.kids.push_back(mk("Path", t[p++].text));
        return s;
      }));
      semi();
    }
    while(cur().kind != tokt::END)
    {
      if(got_kw("func"))
        f.kids.push_back(func_decl());
      else
        f.kids.push_back(decl());
      semi();
    }
    return f;
  }
};
} // namespace

nodet go_parse(const std::string &src)
{
  parsert p;
  p.t = go_lex(src);
  return p.file();
}

std::size_t nodet::size() const
{
  std::size_t n = 1;
  for(const auto &k : kids)
    n += k.size();
  return n;
}
