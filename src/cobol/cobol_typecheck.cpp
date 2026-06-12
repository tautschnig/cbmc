/*******************************************************************\

Module: COBOL Parsing and Type Checking

Author: Kiro

\*******************************************************************/

/// \file
/// Recursive-descent parser + converter for the COBOL day-one subset.
/// Walks the token stream, populates the symbol table with DATA DIVISION
/// items, and lowers each PROGRAM-ID's PROCEDURE DIVISION into a single
/// goto-function whose body is a codet tree (CBMC's goto_convert lowers the
/// labels/gotos/if-then-else to GOTO instructions).

#include "cobol_typecheck.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/message.h>
#include <util/mp_arith.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

#include "cobol_language.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
/// Thrown on a parse/typecheck error.
struct cobol_parse_errort
{
  std::string message;
  source_locationt location;
};

/// Value-domain integer type for all COBOL numerics. The logical value is
/// stored in units of 10^-scale, which makes decimal arithmetic exact.
signedbv_typet cobol_value_type()
{
  return signedbv_typet{64};
}

mp_integer power10(std::size_t n)
{
  mp_integer r = 1;
  for(std::size_t i = 0; i < n; ++i)
    r *= 10;
  return r;
}

mp_integer rescale_int(const mp_integer &v, std::size_t from, std::size_t to)
{
  if(from == to)
    return v;
  if(to > from)
    return v * power10(to - from);
  return v / power10(from - to);
}

/// Description of an elementary data item.
struct item_infot
{
  irep_idt symbol_name;
  bool is_numeric = true;
  std::size_t digits = 0;
  std::size_t scale = 0;
  bool is_signed = false;
  std::size_t char_count = 0;
  bool is_table = false;  ///< has a fixed OCCURS clause
  std::size_t occurs = 0; ///< number of elements when is_table
};

/// 88-level condition name: value ranges/values over its parent item.
struct cond_infot
{
  item_infot parent;
  std::vector<std::pair<mp_integer, mp_integer>> num_ranges; // numeric parent
  std::vector<exprt> alnum_values;                           // alphanumeric
};

/// A numeric expression together with its decimal scale.
struct valuet
{
  exprt expr;
  std::size_t scale = 0;
};

// Forward declaration; defined above.
struct item_infot;

/// A reference to a data item, possibly subscripted (e.g. WS-TABLE(I)). The
/// expr is usable both as an rvalue and as an assignment target.
struct reft
{
  const item_infot *info = nullptr;
  exprt expr;
};

/// A parsed VALUE / 88-level literal, before interpretation against the
/// receiving item's picture. Captures numeric literals, quoted strings, and
/// COBOL figurative constants.
struct value_spect
{
  enum class kindt
  {
    NONE,
    NUMERIC,
    STRING,
    SPACES,
    ZEROS,
    HIGH_VALUES,
    LOW_VALUES,
    QUOTES
  } kind = kindt::NONE;
  mp_integer num = 0;
  std::size_t scale = 0;
  std::string str;
  bool all = false; ///< ALL <literal> repetition
};

bool is_figurative(const std::string &w)
{
  static const std::set<std::string> f = {
    "SPACE",
    "SPACES",
    "ZERO",
    "ZEROS",
    "ZEROES",
    "HIGH-VALUE",
    "HIGH-VALUES",
    "LOW-VALUE",
    "LOW-VALUES",
    "QUOTE",
    "QUOTES",
    "NULL",
    "NULLS"};
  return f.find(w) != f.end();
}

/// A parsed statement. Operands are pre-lowered to exprt/codet during parsing;
/// only control-flow structure is retained for code generation and PERFORM
/// inlining.
struct stmtt
{
  enum class kindt
  {
    ASSIGN,
    ASSERT,
    ASSUME,
    IFTE,
    EVALUATE,
    PERFORM,
    GOTO,
    STOP,
    SKIP
  } kind = kindt::SKIP;

  source_locationt location;

  exprt lhs;
  exprt rhs;
  exprt cond;

  std::vector<stmtt> then_stmts;
  std::vector<stmtt> else_stmts;

  std::vector<std::pair<exprt, std::vector<stmtt>>> when_clauses;
  std::vector<stmtt> other_stmts;

  std::string target;
  std::string target_end;

  enum class perform_kindt
  {
    ONCE,
    TIMES,
    UNTIL,
    VARYING
  } pkind = perform_kindt::ONCE;
  bool inline_body = false;
  std::vector<stmtt> body;
  exprt times;
  bool test_after = false;
  exprt var;
  exprt from_val;
  exprt by_val;
};

struct paragrapht
{
  std::string name;
  std::vector<stmtt> statements;
};

bool is_verb(const std::string &w)
{
  static const std::set<std::string> verbs = {
    "MOVE",
    "ADD",
    "SUBTRACT",
    "MULTIPLY",
    "DIVIDE",
    "COMPUTE",
    "IF",
    "EVALUATE",
    "PERFORM",
    "GO",
    "STOP",
    "GOBACK",
    "EXIT",
    "DISPLAY",
    "ACCEPT",
    "CONTINUE",
    "CALL",
    "NEXT",
    "SET"};
  return verbs.find(w) != verbs.end();
}

bool is_pic_stop_word(const std::string &w)
{
  static const std::set<std::string> s = {
    "VALUE",
    "VALUES",
    "USAGE",
    "OCCURS",
    "REDEFINES",
    "COMP",
    "COMP-1",
    "COMP-2",
    "COMP-3",
    "COMP-4",
    "COMP-5",
    "BINARY",
    "PACKED-DECIMAL",
    "DISPLAY",
    "SIGN",
    "SYNC",
    "SYNCHRONIZED",
    "JUSTIFIED",
    "JUST",
    "BLANK",
    "PIC",
    "PICTURE",
    "IS"};
  return s.find(w) != s.end();
}

/// Parse a PICTURE string body, e.g. "9(5)V99", "S9(4)", "X(20)".
void parse_picture(
  const std::string &pic,
  bool &is_numeric,
  std::size_t &digits,
  std::size_t &scale,
  bool &is_signed,
  std::size_t &char_count)
{
  is_numeric = true;
  digits = 0;
  scale = 0;
  is_signed = false;
  char_count = 0;

  bool after_v = false;
  std::size_t i = 0;
  const std::size_t n = pic.size();
  while(i < n)
  {
    const char sym =
      static_cast<char>(std::toupper(static_cast<unsigned char>(pic[i])));
    std::size_t rep = 1;
    ++i;
    if(i < n && pic[i] == '(')
    {
      ++i;
      std::string num;
      while(i < n && pic[i] != ')')
        num.push_back(pic[i++]);
      if(i < n)
        ++i;
      rep = num.empty() ? 1 : static_cast<std::size_t>(std::stoul(num));
    }

    switch(sym)
    {
    case 'S':
      is_signed = true;
      break;
    case 'V':
      after_v = true;
      break;
    case '9':
      digits += rep;
      if(after_v)
        scale += rep;
      break;
    case 'X':
    case 'A':
      is_numeric = false;
      char_count += rep;
      break;
    default:
      break;
    }
  }
}

/// Parse a decimal literal into (value, frac_digit_count).
std::pair<mp_integer, std::size_t> parse_decimal(const std::string &text)
{
  std::string s = text;
  bool neg = false;
  if(!s.empty() && (s[0] == '+' || s[0] == '-'))
  {
    neg = s[0] == '-';
    s = s.substr(1);
  }
  const std::size_t dot = s.find('.');
  const std::string int_part = dot == std::string::npos ? s : s.substr(0, dot);
  const std::string frac_part =
    dot == std::string::npos ? std::string{} : s.substr(dot + 1);
  std::string combined = int_part + frac_part;
  if(combined.empty())
    combined = "0";
  mp_integer v = string2integer(combined);
  if(neg)
    v = -v;
  return {v, frac_part.size()};
}

class cobol_typecheckt
{
public:
  cobol_typecheckt(
    const std::vector<cobol_tokent> &_tokens,
    symbol_table_baset &_symbol_table,
    const std::string &_module,
    message_handlert &_message_handler)
    : tokens(_tokens),
      symbol_table(_symbol_table),
      module(_module),
      log(_message_handler)
  {
  }

  bool typecheck();

protected:
  const std::vector<cobol_tokent> &tokens;
  symbol_table_baset &symbol_table;
  const std::string module;
  messaget log;

  std::size_t pos = 0;

  std::string program_id;
  std::map<std::string, item_infot> items;
  std::map<std::string, cond_infot> conds;
  std::vector<paragrapht> paragraphs;
  std::string last_elementary;
  std::size_t unique = 0;

  // ---- token cursor ----
  const cobol_tokent &cur() const
  {
    return tokens[pos];
  }
  const cobol_tokent &peek(std::size_t k) const
  {
    const std::size_t p = pos + k;
    return p < tokens.size() ? tokens[p] : tokens.back();
  }
  bool at_eof() const
  {
    return cur().kind == cobol_token_kindt::END_OF_FILE;
  }
  bool is_word(const char *w) const
  {
    return cur().kind == cobol_token_kindt::WORD && cur().text == w;
  }
  bool is_kind(cobol_token_kindt k) const
  {
    return cur().kind == k;
  }
  bool is_item_word() const
  {
    return cur().kind == cobol_token_kindt::WORD &&
           items.find(cur().text) != items.end();
  }
  void advance()
  {
    if(!at_eof())
      ++pos;
  }
  bool eat_word(const char *w)
  {
    if(is_word(w))
    {
      advance();
      return true;
    }
    return false;
  }
  [[noreturn]] void error(const std::string &msg)
  {
    throw cobol_parse_errort{msg, cur().location};
  }
  void expect_word(const char *w)
  {
    if(!eat_word(w))
      error(std::string{"expected '"} + w + "' but got '" + cur().text + "'");
  }
  void expect_period()
  {
    if(is_kind(cobol_token_kindt::PERIOD))
      advance();
  }

  // ---- helpers ----
  std::string fresh_label(const std::string &base)
  {
    return "cobol::" + program_id + "::$" + base + std::to_string(unique++);
  }
  symbol_exprt make_counter()
  {
    const std::string base = "ctr" + std::to_string(unique++);
    const irep_idt name = "cobol::" + program_id + "::$" + base;
    symbolt symbol{name, cobol_value_type(), COBOL_MODE};
    symbol.base_name = base;
    symbol.is_static_lifetime = true;
    symbol.is_lvalue = true;
    symbol.is_state_var = true;
    symbol_table.add(symbol);
    return symbol_exprt{name, cobol_value_type()};
  }
  std::string para_label(const std::string &name) const
  {
    return "cobol::" + program_id + "::para::" + name;
  }
  std::string program_end_label() const
  {
    return "cobol::" + program_id + "::$end";
  }

  exprt rescale(const exprt &e, std::size_t from, std::size_t to);
  std::size_t align(valuet &a, valuet &b);
  exprt store_to_item(const item_infot &item, valuet v);
  exprt build_relation(valuet a, const std::string &op, valuet b);

  const item_infot &lookup_item(const std::string &name);
  std::size_t paragraph_index(const std::string &name);

  // ---- parsers ----
  void parse_program();
  void parse_data_division();
  void parse_data_item();
  std::string read_picture_string();
  value_spect read_value_spec();
  bool at_value_start() const;
  std::optional<mp_integer>
  spec_to_numeric(const value_spect &spec, std::size_t target_scale);
  exprt make_alnum_constant(const value_spect &spec, std::size_t n);
  void parse_procedure_division();

  valuet parse_expr();
  valuet parse_term();
  valuet parse_factor();
  valuet parse_primary();
  valuet parse_operand();
  std::vector<valuet> parse_operand_list();
  reft parse_ref();
  typet element_type(const item_infot &item) const;
  typet symbol_type(const item_infot &item) const;
  const item_infot *item_of(const exprt &e) const;

  exprt parse_condition();
  exprt parse_and_condition();
  exprt parse_not_condition();
  exprt parse_relation();
  std::string parse_relop();
  exprt cond_predicate(const std::string &name);

  std::vector<stmtt> parse_statements();
  std::vector<stmtt> parse_statement();
  stmtt parse_if();
  stmtt parse_evaluate();
  stmtt parse_perform();
  std::vector<stmtt> parse_move();
  std::vector<stmtt> parse_add();
  std::vector<stmtt> parse_subtract();
  std::vector<stmtt> parse_multiply();
  std::vector<stmtt> parse_divide();
  std::vector<stmtt> parse_compute();
  std::vector<stmtt> parse_call();
  std::vector<stmtt> parse_set();
  void skip_to_sentence_end();

  stmtt make_assign_ref(
    const item_infot &item,
    exprt lhs,
    valuet v,
    source_locationt loc);

  // ---- code generation ----
  void build_function();
  void gen_statements(
    const std::vector<stmtt> &stmts,
    code_blockt &out,
    std::set<std::string> &inlining);
  void gen_statement(
    const stmtt &s,
    code_blockt &out,
    std::set<std::string> &inlining);
  void gen_perform_invocation(
    const stmtt &s,
    code_blockt &out,
    std::set<std::string> &inlining);
};

// ---------------------------------------------------------------------------
// scale / arithmetic helpers
// ---------------------------------------------------------------------------

exprt cobol_typecheckt::rescale(
  const exprt &e,
  std::size_t from,
  std::size_t to)
{
  if(from == to)
    return e;
  if(to > from)
    return mult_exprt{e, from_integer(power10(to - from), cobol_value_type())};
  return div_exprt{e, from_integer(power10(from - to), cobol_value_type())};
}

std::size_t cobol_typecheckt::align(valuet &a, valuet &b)
{
  const std::size_t s = std::max(a.scale, b.scale);
  a.expr = rescale(a.expr, a.scale, s);
  b.expr = rescale(b.expr, b.scale, s);
  a.scale = s;
  b.scale = s;
  return s;
}

exprt cobol_typecheckt::store_to_item(const item_infot &item, valuet v)
{
  exprt e = rescale(v.expr, v.scale, item.scale);
  if(item.digits > 0 && item.digits <= 18)
    e = mod_exprt{e, from_integer(power10(item.digits), cobol_value_type())};
  return e;
}

exprt cobol_typecheckt::build_relation(
  valuet a,
  const std::string &op,
  valuet b)
{
  align(a, b);
  if(op == "=")
    return equal_exprt{a.expr, b.expr};
  if(op == "<>")
    return notequal_exprt{a.expr, b.expr};
  if(op == "<")
    return binary_relation_exprt{a.expr, ID_lt, b.expr};
  if(op == ">")
    return binary_relation_exprt{a.expr, ID_gt, b.expr};
  if(op == "<=")
    return binary_relation_exprt{a.expr, ID_le, b.expr};
  if(op == ">=")
    return binary_relation_exprt{a.expr, ID_ge, b.expr};
  error("unsupported relational operator '" + op + "'");
}

const item_infot &cobol_typecheckt::lookup_item(const std::string &name)
{
  auto it = items.find(name);
  if(it == items.end())
    error("unknown data item '" + name + "'");
  return it->second;
}

std::size_t cobol_typecheckt::paragraph_index(const std::string &name)
{
  for(std::size_t i = 0; i < paragraphs.size(); ++i)
    if(paragraphs[i].name == name)
      return i;
  error("unknown paragraph '" + name + "'");
}

// ---------------------------------------------------------------------------
// top level
// ---------------------------------------------------------------------------

bool cobol_typecheckt::typecheck()
{
  try
  {
    while(!at_eof())
    {
      // Skip until the next IDENTIFICATION DIVISION.
      while(!at_eof() && !is_word("IDENTIFICATION"))
        advance();
      if(at_eof())
        break;
      parse_program();
    }
  }
  catch(const cobol_parse_errort &e)
  {
    log.error().source_location = e.location;
    log.error() << "COBOL: " << e.message << messaget::eom;
    return true;
  }
  return false;
}

void cobol_typecheckt::parse_program()
{
  program_id.clear();
  items.clear();
  conds.clear();
  paragraphs.clear();
  last_elementary.clear();

  expect_word("IDENTIFICATION");
  expect_word("DIVISION");
  expect_period();
  expect_word("PROGRAM-ID");
  expect_period();
  if(cur().kind != cobol_token_kindt::WORD)
    error("expected PROGRAM-ID name");
  program_id = cur().text;
  advance();
  expect_period();

  // Skip ENVIRONMENT DIVISION etc. until DATA or PROCEDURE.
  while(!at_eof() && !is_word("DATA") && !is_word("PROCEDURE") &&
        !is_word("IDENTIFICATION"))
    advance();

  if(is_word("DATA"))
    parse_data_division();

  if(is_word("PROCEDURE"))
    parse_procedure_division();

  build_function();
}

// ---------------------------------------------------------------------------
// DATA DIVISION
// ---------------------------------------------------------------------------

void cobol_typecheckt::parse_data_division()
{
  expect_word("DATA");
  expect_word("DIVISION");
  expect_period();

  while(!at_eof() && !is_word("PROCEDURE") && !is_word("IDENTIFICATION"))
  {
    if(
      is_word("WORKING-STORAGE") || is_word("LOCAL-STORAGE") ||
      is_word("LINKAGE") || is_word("FILE"))
    {
      advance();
      eat_word("SECTION");
      expect_period();
    }
    else if(cur().kind == cobol_token_kindt::NUMBER)
    {
      parse_data_item();
    }
    else
    {
      advance();
    }
  }
}

std::string cobol_typecheckt::read_picture_string()
{
  std::string s;
  while(!at_eof() && !is_kind(cobol_token_kindt::PERIOD))
  {
    if(cur().kind == cobol_token_kindt::WORD && is_pic_stop_word(cur().text))
      break;
    if(cur().kind == cobol_token_kindt::LPAREN)
      s.push_back('(');
    else if(cur().kind == cobol_token_kindt::RPAREN)
      s.push_back(')');
    else if(
      cur().kind == cobol_token_kindt::WORD ||
      cur().kind == cobol_token_kindt::NUMBER)
      s += cur().text;
    else
      return s;
    advance();
  }
  return s;
}

value_spect cobol_typecheckt::read_value_spec()
{
  value_spect spec;
  spec.all = eat_word("ALL");

  bool neg = false;
  if(
    cur().kind == cobol_token_kindt::PUNCT &&
    (cur().text == "+" || cur().text == "-"))
  {
    neg = cur().text == "-";
    advance();
  }
  if(cur().kind == cobol_token_kindt::NUMBER)
  {
    auto r = parse_decimal(cur().text);
    advance();
    spec.kind = value_spect::kindt::NUMERIC;
    spec.num = neg ? -r.first : r.first;
    spec.scale = r.second;
    return spec;
  }
  if(cur().kind == cobol_token_kindt::STRING)
  {
    spec.kind = value_spect::kindt::STRING;
    spec.str = cur().text;
    advance();
    return spec;
  }
  if(cur().kind == cobol_token_kindt::WORD)
  {
    const std::string w = cur().text;
    if(w == "SPACE" || w == "SPACES")
      spec.kind = value_spect::kindt::SPACES;
    else if(w == "ZERO" || w == "ZEROS" || w == "ZEROES")
      spec.kind = value_spect::kindt::ZEROS;
    else if(w == "HIGH-VALUE" || w == "HIGH-VALUES")
      spec.kind = value_spect::kindt::HIGH_VALUES;
    else if(
      w == "LOW-VALUE" || w == "LOW-VALUES" || w == "NULL" || w == "NULLS")
      spec.kind = value_spect::kindt::LOW_VALUES;
    else if(w == "QUOTE" || w == "QUOTES")
      spec.kind = value_spect::kindt::QUOTES;
    else
      error("unsupported literal '" + w + "'");
    advance();
    return spec;
  }
  error("unsupported literal '" + cur().text + "'");
}

bool cobol_typecheckt::at_value_start() const
{
  if(
    cur().kind == cobol_token_kindt::NUMBER ||
    cur().kind == cobol_token_kindt::STRING)
    return true;
  if(
    cur().kind == cobol_token_kindt::PUNCT &&
    (cur().text == "+" || cur().text == "-"))
    return true;
  if(cur().kind == cobol_token_kindt::WORD)
    return is_figurative(cur().text) || cur().text == "ALL";
  return false;
}

std::optional<mp_integer> cobol_typecheckt::spec_to_numeric(
  const value_spect &spec,
  std::size_t target_scale)
{
  if(spec.kind == value_spect::kindt::NUMERIC)
    return rescale_int(spec.num, spec.scale, target_scale);
  if(spec.kind == value_spect::kindt::ZEROS)
    return mp_integer{0};
  return std::nullopt;
}

exprt cobol_typecheckt::make_alnum_constant(
  const value_spect &spec,
  std::size_t n)
{
  const unsignedbv_typet byte_type{8};
  const array_typet array_type{byte_type, from_integer(n, size_type())};

  std::string pattern;
  bool repeat = spec.all;
  switch(spec.kind)
  {
  case value_spect::kindt::STRING:
    pattern = spec.str;
    break;
  case value_spect::kindt::SPACES:
    pattern = " ";
    repeat = true;
    break;
  case value_spect::kindt::ZEROS:
    pattern = "0";
    repeat = true;
    break;
  case value_spect::kindt::QUOTES:
    pattern = "\"";
    repeat = true;
    break;
  case value_spect::kindt::HIGH_VALUES:
    pattern = std::string(1, static_cast<char>(0xff));
    repeat = true;
    break;
  case value_spect::kindt::LOW_VALUES:
    pattern = std::string(1, static_cast<char>(0x00));
    repeat = true;
    break;
  case value_spect::kindt::NUMERIC:
    pattern = integer2string(spec.num);
    break;
  case value_spect::kindt::NONE:
    break;
  }
  if(pattern.empty())
    pattern = " ";

  array_exprt::operandst ops;
  ops.reserve(n);
  for(std::size_t i = 0; i < n; ++i)
  {
    unsigned char ch;
    if(repeat)
      ch = static_cast<unsigned char>(pattern[i % pattern.size()]);
    else
      ch = i < pattern.size() ? static_cast<unsigned char>(pattern[i])
                              : static_cast<unsigned char>(' ');
    ops.push_back(from_integer(ch, byte_type));
  }
  return array_exprt{std::move(ops), array_type};
}

void cobol_typecheckt::parse_data_item()
{
  const std::size_t level = static_cast<std::size_t>(std::stoul(cur().text));
  const source_locationt loc = cur().location;
  advance();

  if(cur().kind != cobol_token_kindt::WORD)
    error("expected data item name");
  const std::string name = cur().text;
  advance();

  if(level == 88)
  {
    if(last_elementary.empty())
      error("88-level without a preceding elementary item");
    const item_infot parent = items.at(last_elementary);
    cond_infot cinfo;
    cinfo.parent = parent;

    if(!eat_word("VALUE"))
      expect_word("VALUES");
    eat_word("IS");
    eat_word("ARE");
    do
    {
      const value_spect lo = read_value_spec();
      if(parent.is_numeric)
      {
        const mp_integer lo_v =
          spec_to_numeric(lo, parent.scale).value_or(mp_integer{0});
        mp_integer hi_v = lo_v;
        if(eat_word("THRU") || eat_word("THROUGH"))
        {
          const value_spect hi = read_value_spec();
          hi_v = spec_to_numeric(hi, parent.scale).value_or(lo_v);
        }
        cinfo.num_ranges.emplace_back(lo_v, hi_v);
      }
      else
      {
        cinfo.alnum_values.push_back(
          make_alnum_constant(lo, parent.char_count));
        if(eat_word("THRU") || eat_word("THROUGH"))
        {
          const value_spect hi = read_value_spec();
          cinfo.alnum_values.push_back(
            make_alnum_constant(hi, parent.char_count));
        }
      }
    } while(at_value_start());
    conds[name] = cinfo;
    expect_period();
    return;
  }

  std::string pic;
  bool has_pic = false;
  bool has_value = false;
  bool is_table = false;
  std::size_t occurs = 0;
  value_spect value_spec;

  while(!is_kind(cobol_token_kindt::PERIOD) && !at_eof())
  {
    if(eat_word("PIC") || eat_word("PICTURE"))
    {
      eat_word("IS");
      pic = read_picture_string();
      has_pic = true;
    }
    else if(eat_word("VALUE"))
    {
      eat_word("IS");
      value_spec = read_value_spec();
      has_value = true;
    }
    else if(eat_word("OCCURS"))
    {
      if(cur().kind != cobol_token_kindt::NUMBER)
        error("expected a count after OCCURS");
      occurs = static_cast<std::size_t>(std::stoul(cur().text));
      advance();
      eat_word("TIMES");
      if(is_word("DEPENDING"))
        error("OCCURS DEPENDING ON is not yet supported");
      is_table = true;
    }
    else if(eat_word("REDEFINES"))
    {
      error("REDEFINES is not yet supported");
    }
    else
    {
      // USAGE / SIGN / SYNC / ... : consume one token.
      advance();
    }
  }
  expect_period();

  if(!has_pic)
    return; // group item: flattened in the MVP

  item_infot info;
  bool is_numeric;
  std::size_t digits, scale, char_count;
  bool is_signed;
  parse_picture(pic, is_numeric, digits, scale, is_signed, char_count);
  info.is_numeric = is_numeric;
  info.digits = digits;
  info.scale = scale;
  info.is_signed = is_signed;
  info.char_count = char_count;
  info.is_table = is_table;
  info.occurs = occurs;
  info.symbol_name = "cobol::" + program_id + "::" + name;

  const typet type = symbol_type(info);

  symbolt symbol{info.symbol_name, type, COBOL_MODE};
  symbol.base_name = name;
  symbol.is_static_lifetime = true;
  symbol.is_lvalue = true;
  symbol.is_state_var = true;
  symbol.location = loc;
  if(has_value && !is_table)
  {
    if(is_numeric)
    {
      if(auto v = spec_to_numeric(value_spec, scale))
        symbol.value = from_integer(*v, element_type(info));
    }
    else
    {
      symbol.value = make_alnum_constant(value_spec, char_count);
    }
  }
  symbol_table.add(symbol);

  items[name] = info;
  last_elementary = name;
}

// ---------------------------------------------------------------------------
// expressions
// ---------------------------------------------------------------------------

typet cobol_typecheckt::element_type(const item_infot &item) const
{
  if(item.is_numeric)
    return cobol_value_type();
  return array_typet{
    unsignedbv_typet{8}, from_integer(item.char_count, size_type())};
}

typet cobol_typecheckt::symbol_type(const item_infot &item) const
{
  const typet elem = element_type(item);
  if(item.is_table)
    return array_typet{elem, from_integer(item.occurs, size_type())};
  return elem;
}

const item_infot *cobol_typecheckt::item_of(const exprt &e) const
{
  irep_idt id;
  if(e.id() == ID_symbol)
    id = to_symbol_expr(e).get_identifier();
  else if(e.id() == ID_index && to_index_expr(e).array().id() == ID_symbol)
    id = to_symbol_expr(to_index_expr(e).array()).get_identifier();
  else
    return nullptr;
  for(const auto &pair : items)
    if(pair.second.symbol_name == id)
      return &pair.second;
  return nullptr;
}

reft cobol_typecheckt::parse_ref()
{
  const item_infot &item = lookup_item(cur().text);
  advance();
  exprt base = symbol_exprt{item.symbol_name, symbol_type(item)};
  if(is_kind(cobol_token_kindt::LPAREN))
  {
    if(!item.is_table)
      error("subscript on a non-table item");
    advance();
    valuet idx = parse_expr();
    if(!is_kind(cobol_token_kindt::RPAREN))
      error("expected ')' after subscript");
    advance();
    // COBOL subscripts are 1-based; the IR array is 0-based.
    const exprt zero_based = minus_exprt{
      rescale(idx.expr, idx.scale, 0), from_integer(1, cobol_value_type())};
    base = index_exprt{base, zero_based};
  }
  return reft{&item, base};
}

valuet cobol_typecheckt::parse_primary()
{
  if(is_kind(cobol_token_kindt::LPAREN))
  {
    advance();
    valuet v = parse_expr();
    if(!is_kind(cobol_token_kindt::RPAREN))
      error("expected ')'");
    advance();
    return v;
  }
  if(cur().kind == cobol_token_kindt::NUMBER)
  {
    auto r = parse_decimal(cur().text);
    advance();
    return valuet{from_integer(r.first, cobol_value_type()), r.second};
  }
  if(cur().kind == cobol_token_kindt::WORD)
  {
    reft r = parse_ref();
    if(!r.info->is_numeric)
      error(
        "non-numeric item '" + id2string(r.info->symbol_name) +
        "' used in expression");
    return valuet{r.expr, r.info->scale};
  }
  error("expected an operand but got '" + cur().text + "'");
}

valuet cobol_typecheckt::parse_factor()
{
  bool neg = false;
  if(
    cur().kind == cobol_token_kindt::PUNCT &&
    (cur().text == "+" || cur().text == "-"))
  {
    neg = cur().text == "-";
    advance();
  }
  valuet v = parse_primary();
  if(neg)
    v.expr = unary_minus_exprt{v.expr};
  return v;
}

valuet cobol_typecheckt::parse_term()
{
  valuet a = parse_factor();
  while(cur().kind == cobol_token_kindt::PUNCT &&
        (cur().text == "*" || cur().text == "/"))
  {
    const std::string op = cur().text;
    advance();
    valuet b = parse_factor();
    if(op == "*")
    {
      a = valuet{mult_exprt{a.expr, b.expr}, a.scale + b.scale};
    }
    else
    {
      const std::size_t s = align(a, b);
      (void)s;
      a = valuet{div_exprt{a.expr, b.expr}, 0};
    }
  }
  return a;
}

valuet cobol_typecheckt::parse_expr()
{
  valuet a = parse_term();
  while(cur().kind == cobol_token_kindt::PUNCT &&
        (cur().text == "+" || cur().text == "-"))
  {
    const std::string op = cur().text;
    advance();
    valuet b = parse_term();
    const std::size_t s = align(a, b);
    if(op == "+")
      a = valuet{plus_exprt{a.expr, b.expr}, s};
    else
      a = valuet{minus_exprt{a.expr, b.expr}, s};
  }
  return a;
}

valuet cobol_typecheckt::parse_operand()
{
  return parse_factor();
}

std::vector<valuet> cobol_typecheckt::parse_operand_list()
{
  std::vector<valuet> result;
  while(cur().kind == cobol_token_kindt::NUMBER || is_item_word())
    result.push_back(parse_operand());
  return result;
}

// ---------------------------------------------------------------------------
// conditions
// ---------------------------------------------------------------------------

std::string cobol_typecheckt::parse_relop()
{
  if(cur().kind == cobol_token_kindt::PUNCT)
  {
    const std::string t = cur().text;
    if(t == "=" || t == "<" || t == ">" || t == "<=" || t == ">=" || t == "<>")
    {
      advance();
      return t;
    }
  }

  const bool neg = eat_word("NOT");
  std::string op;
  if(eat_word("GREATER"))
  {
    eat_word("THAN");
    if(eat_word("OR"))
    {
      expect_word("EQUAL");
      eat_word("TO");
      op = ">=";
    }
    else
      op = ">";
  }
  else if(eat_word("LESS"))
  {
    eat_word("THAN");
    if(eat_word("OR"))
    {
      expect_word("EQUAL");
      eat_word("TO");
      op = "<=";
    }
    else
      op = "<";
  }
  else if(eat_word("EQUAL"))
  {
    eat_word("TO");
    op = "=";
  }
  else
    error("expected a relational operator");

  if(neg)
  {
    if(op == "=")
      return "<>";
    if(op == ">")
      return "<=";
    if(op == "<")
      return ">=";
    if(op == ">=")
      return "<";
    if(op == "<=")
      return ">";
  }
  return op;
}

exprt cobol_typecheckt::cond_predicate(const std::string &name)
{
  const cond_infot &c = conds.at(name);

  if(!c.parent.is_numeric)
  {
    // Alphanumeric parent: OR of equalities against the byte-array constants.
    const array_typet parent_type{
      unsignedbv_typet{8}, from_integer(c.parent.char_count, size_type())};
    const symbol_exprt parent{c.parent.symbol_name, parent_type};
    exprt result = false_exprt{};
    bool first = true;
    for(const auto &value : c.alnum_values)
    {
      exprt clause = equal_exprt{parent, value};
      result = first ? clause : or_exprt{result, clause};
      first = false;
    }
    return result;
  }

  const symbol_exprt parent{c.parent.symbol_name, cobol_value_type()};
  exprt result = false_exprt{};
  bool first = true;
  for(const auto &range : c.num_ranges)
  {
    exprt clause;
    if(range.first == range.second)
    {
      clause =
        equal_exprt{parent, from_integer(range.first, cobol_value_type())};
    }
    else
    {
      clause = and_exprt{
        binary_relation_exprt{
          parent, ID_ge, from_integer(range.first, cobol_value_type())},
        binary_relation_exprt{
          parent, ID_le, from_integer(range.second, cobol_value_type())}};
    }
    result = first ? clause : or_exprt{result, clause};
    first = false;
  }
  return result;
}

exprt cobol_typecheckt::parse_relation()
{
  if(
    cur().kind == cobol_token_kindt::WORD &&
    conds.find(cur().text) != conds.end())
  {
    const std::string name = cur().text;
    advance();
    return cond_predicate(name);
  }
  valuet a = parse_expr();
  const std::string op = parse_relop();
  valuet b = parse_expr();
  return build_relation(a, op, b);
}

exprt cobol_typecheckt::parse_not_condition()
{
  if(eat_word("NOT"))
    return not_exprt{parse_not_condition()};
  return parse_relation();
}

exprt cobol_typecheckt::parse_and_condition()
{
  exprt l = parse_not_condition();
  while(eat_word("AND"))
    l = and_exprt{l, parse_not_condition()};
  return l;
}

exprt cobol_typecheckt::parse_condition()
{
  exprt l = parse_and_condition();
  while(eat_word("OR"))
    l = or_exprt{l, parse_and_condition()};
  return l;
}

// ---------------------------------------------------------------------------
// statements
// ---------------------------------------------------------------------------

stmtt cobol_typecheckt::make_assign_ref(
  const item_infot &item,
  exprt lhs,
  valuet v,
  source_locationt loc)
{
  stmtt s;
  s.kind = stmtt::kindt::ASSIGN;
  s.location = loc;
  s.lhs = std::move(lhs);
  s.rhs = store_to_item(item, std::move(v));
  return s;
}

void cobol_typecheckt::skip_to_sentence_end()
{
  while(!at_eof() && !is_kind(cobol_token_kindt::PERIOD))
  {
    if(
      cur().kind == cobol_token_kindt::WORD &&
      (is_verb(cur().text) || cur().text == "ELSE" || cur().text == "END-IF" ||
       cur().text == "END-EVALUATE" || cur().text == "END-PERFORM" ||
       cur().text == "WHEN"))
      break;
    advance();
  }
}

std::vector<stmtt> cobol_typecheckt::parse_statements()
{
  std::vector<stmtt> result;
  while(cur().kind == cobol_token_kindt::WORD && is_verb(cur().text))
  {
    std::vector<stmtt> s = parse_statement();
    for(auto &x : s)
      result.push_back(std::move(x));
  }
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_statement()
{
  const std::string verb = cur().text;
  if(verb == "MOVE")
    return parse_move();
  if(verb == "ADD")
    return parse_add();
  if(verb == "SUBTRACT")
    return parse_subtract();
  if(verb == "MULTIPLY")
    return parse_multiply();
  if(verb == "DIVIDE")
    return parse_divide();
  if(verb == "COMPUTE")
    return parse_compute();
  if(verb == "IF")
    return {parse_if()};
  if(verb == "EVALUATE")
    return {parse_evaluate()};
  if(verb == "PERFORM")
    return {parse_perform()};
  if(verb == "CALL")
    return parse_call();
  if(verb == "GO")
  {
    advance();
    eat_word("TO");
    if(cur().kind != cobol_token_kindt::WORD)
      error("expected paragraph name after GO TO");
    stmtt s;
    s.kind = stmtt::kindt::GOTO;
    s.location = cur().location;
    s.target = cur().text;
    advance();
    return {s};
  }
  if(verb == "STOP")
  {
    const source_locationt loc = cur().location;
    advance();
    eat_word("RUN");
    stmtt s;
    s.kind = stmtt::kindt::STOP;
    s.location = loc;
    return {s};
  }
  if(verb == "GOBACK")
  {
    const source_locationt loc = cur().location;
    advance();
    stmtt s;
    s.kind = stmtt::kindt::STOP;
    s.location = loc;
    return {s};
  }
  if(verb == "EXIT")
  {
    advance();
    eat_word("PROGRAM");
    stmtt s;
    s.kind = stmtt::kindt::STOP;
    return {s};
  }
  if(verb == "CONTINUE" || verb == "NEXT")
  {
    advance();
    eat_word("SENTENCE");
    return {};
  }
  if(verb == "DISPLAY" || verb == "ACCEPT")
  {
    advance();
    skip_to_sentence_end();
    return {};
  }
  if(verb == "SET")
    return parse_set();
  error("unsupported statement '" + verb + "'");
}

std::vector<stmtt> cobol_typecheckt::parse_move()
{
  const source_locationt loc = cur().location;
  expect_word("MOVE");
  valuet src = parse_operand();
  expect_word("TO");
  std::vector<stmtt> result;
  while(is_item_word())
  {
    reft r = parse_ref();
    result.push_back(make_assign_ref(*r.info, r.expr, src, loc));
  }
  if(result.empty())
    error("MOVE without a target");
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_add()
{
  const source_locationt loc = cur().location;
  expect_word("ADD");
  std::vector<valuet> ops = parse_operand_list();
  if(ops.empty())
    error("ADD without operands");
  valuet sum = ops[0];
  for(std::size_t i = 1; i < ops.size(); ++i)
  {
    align(sum, ops[i]);
    sum = valuet{plus_exprt{sum.expr, ops[i].expr}, sum.scale};
  }

  std::vector<stmtt> result;
  if(eat_word("TO"))
  {
    std::vector<reft> targets;
    while(is_item_word())
      targets.push_back(parse_ref());
    if(eat_word("GIVING"))
    {
      // ADD ops TO addend GIVING results: result = sum + addend
      if(targets.size() != 1)
        error("ADD ... TO ... GIVING expects a single addend");
      valuet addend{targets[0].expr, targets[0].info->scale};
      align(sum, addend);
      valuet total{plus_exprt{sum.expr, addend.expr}, sum.scale};
      while(is_item_word())
      {
        reft r = parse_ref();
        result.push_back(make_assign_ref(*r.info, r.expr, total, loc));
      }
    }
    else
    {
      // ADD ops TO targets: each target += sum
      for(const reft &t : targets)
      {
        valuet cur_val{t.expr, t.info->scale};
        valuet s = sum;
        align(cur_val, s);
        result.push_back(make_assign_ref(
          *t.info,
          t.expr,
          valuet{plus_exprt{cur_val.expr, s.expr}, s.scale},
          loc));
      }
    }
  }
  else if(eat_word("GIVING"))
  {
    while(is_item_word())
    {
      reft r = parse_ref();
      result.push_back(make_assign_ref(*r.info, r.expr, sum, loc));
    }
  }
  else
    error("ADD requires TO or GIVING");

  if(result.empty())
    error("ADD without a target");
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_subtract()
{
  const source_locationt loc = cur().location;
  expect_word("SUBTRACT");
  std::vector<valuet> ops = parse_operand_list();
  if(ops.empty())
    error("SUBTRACT without operands");
  valuet sum = ops[0];
  for(std::size_t i = 1; i < ops.size(); ++i)
  {
    align(sum, ops[i]);
    sum = valuet{plus_exprt{sum.expr, ops[i].expr}, sum.scale};
  }
  expect_word("FROM");

  std::vector<reft> minuends;
  while(is_item_word())
    minuends.push_back(parse_ref());

  std::vector<stmtt> result;
  if(eat_word("GIVING"))
  {
    if(minuends.size() != 1)
      error("SUBTRACT ... FROM ... GIVING expects a single minuend");
    valuet mv{minuends[0].expr, minuends[0].info->scale};
    align(mv, sum);
    valuet diff{minus_exprt{mv.expr, sum.expr}, mv.scale};
    while(is_item_word())
    {
      reft r = parse_ref();
      result.push_back(make_assign_ref(*r.info, r.expr, diff, loc));
    }
  }
  else
  {
    for(const reft &t : minuends)
    {
      valuet mv{t.expr, t.info->scale};
      valuet s = sum;
      align(mv, s);
      result.push_back(make_assign_ref(
        *t.info, t.expr, valuet{minus_exprt{mv.expr, s.expr}, s.scale}, loc));
    }
  }
  if(result.empty())
    error("SUBTRACT without a target");
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_multiply()
{
  const source_locationt loc = cur().location;
  expect_word("MULTIPLY");
  valuet a = parse_operand();
  expect_word("BY");
  valuet b = parse_operand();
  valuet product{mult_exprt{a.expr, b.expr}, a.scale + b.scale};
  std::vector<stmtt> result;
  if(eat_word("GIVING"))
  {
    while(is_item_word())
    {
      reft r = parse_ref();
      result.push_back(make_assign_ref(*r.info, r.expr, product, loc));
    }
  }
  else
  {
    // MULTIPLY a BY b : b = a * b (b must be a data item)
    const item_infot *t = item_of(b.expr);
    if(t == nullptr)
      error("MULTIPLY without GIVING requires an item operand");
    result.push_back(make_assign_ref(*t, b.expr, product, loc));
  }
  if(result.empty())
    error("MULTIPLY without a target");
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_divide()
{
  const source_locationt loc = cur().location;
  expect_word("DIVIDE");
  valuet first = parse_operand();
  std::vector<stmtt> result;
  if(eat_word("INTO"))
  {
    valuet dividend = parse_operand();
    valuet divisor = first;
    align(dividend, divisor);
    valuet quotient{div_exprt{dividend.expr, divisor.expr}, 0};
    if(eat_word("GIVING"))
    {
      while(is_item_word())
      {
        reft r = parse_ref();
        result.push_back(make_assign_ref(*r.info, r.expr, quotient, loc));
      }
    }
    else
    {
      const item_infot *t = item_of(dividend.expr);
      if(t == nullptr)
        error("DIVIDE without GIVING requires an item operand");
      result.push_back(make_assign_ref(*t, dividend.expr, quotient, loc));
    }
  }
  else if(eat_word("BY"))
  {
    valuet divisor = parse_operand();
    valuet dividend = first;
    align(dividend, divisor);
    valuet quotient{div_exprt{dividend.expr, divisor.expr}, 0};
    expect_word("GIVING");
    while(is_item_word())
    {
      reft r = parse_ref();
      result.push_back(make_assign_ref(*r.info, r.expr, quotient, loc));
    }
  }
  else
    error("DIVIDE requires INTO or BY");

  if(result.empty())
    error("DIVIDE without a target");
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_compute()
{
  const source_locationt loc = cur().location;
  expect_word("COMPUTE");
  std::vector<reft> targets;
  while(is_item_word())
  {
    targets.push_back(parse_ref());
    eat_word("ROUNDED");
  }
  if(!is_kind(cobol_token_kindt::PUNCT) || cur().text != "=")
    error("expected '=' in COMPUTE");
  advance();
  valuet v = parse_expr();
  std::vector<stmtt> result;
  for(const reft &t : targets)
    result.push_back(make_assign_ref(*t.info, t.expr, v, loc));
  if(result.empty())
    error("COMPUTE without a target");
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_call()
{
  const source_locationt loc = cur().location;
  expect_word("CALL");
  if(cur().kind != cobol_token_kindt::STRING)
    error("only static CALL with a literal program name is supported");
  const std::string name = cur().text;
  advance();

  if(name == "__CPROVER_assert" || name == "__CPROVER_assume")
  {
    expect_word("USING");
    exprt cond = parse_condition();
    stmtt s;
    s.kind =
      name == "__CPROVER_assert" ? stmtt::kindt::ASSERT : stmtt::kindt::ASSUME;
    s.cond = cond;
    s.location = loc;
    return {s};
  }

  error("CALL to '" + name + "' is not supported");
}

std::vector<stmtt> cobol_typecheckt::parse_set()
{
  const source_locationt loc = cur().location;
  expect_word("SET");

  // Collect target names up to TO.
  std::vector<std::string> targets;
  while(cur().kind == cobol_token_kindt::WORD && !is_word("TO"))
  {
    targets.push_back(cur().text);
    advance();
  }
  if(!eat_word("TO"))
  {
    // Unrecognised SET form (e.g. SET ADDRESS OF ...): no-op.
    skip_to_sentence_end();
    return {};
  }

  std::vector<stmtt> result;

  if(eat_word("TRUE"))
  {
    // SET cond-name TO TRUE: assign the parent its first 88-level value.
    for(const std::string &t : targets)
    {
      auto it = conds.find(t);
      if(it == conds.end())
        continue;
      const cond_infot &c = it->second;
      if(c.parent.is_numeric && !c.num_ranges.empty())
      {
        const symbol_exprt parent{c.parent.symbol_name, cobol_value_type()};
        result.push_back(make_assign_ref(
          c.parent,
          parent,
          valuet{
            from_integer(c.num_ranges.front().first, cobol_value_type()),
            c.parent.scale},
          loc));
      }
      else if(!c.parent.is_numeric && !c.alnum_values.empty())
      {
        stmtt s;
        s.kind = stmtt::kindt::ASSIGN;
        s.location = loc;
        s.lhs = symbol_exprt{c.parent.symbol_name, symbol_type(c.parent)};
        s.rhs = c.alnum_values.front();
        result.push_back(s);
      }
    }
    return result;
  }

  if(eat_word("FALSE"))
  {
    // SET cond-name TO FALSE is implementor-defined without a FALSE clause;
    // treat as a no-op.
    return {};
  }

  // SET item TO <numeric value> (e.g. an index). Other forms are no-ops.
  if(
    cur().kind == cobol_token_kindt::NUMBER ||
    (is_item_word() && lookup_item(cur().text).is_numeric))
  {
    valuet v = parse_operand();
    for(const std::string &t : targets)
    {
      auto it = items.find(t);
      if(it != items.end() && it->second.is_numeric)
      {
        const symbol_exprt sym{it->second.symbol_name, cobol_value_type()};
        result.push_back(make_assign_ref(it->second, sym, v, loc));
      }
    }
    return result;
  }

  // Unsupported value form: no-op.
  skip_to_sentence_end();
  return result;
}

stmtt cobol_typecheckt::parse_if()
{
  stmtt s;
  s.kind = stmtt::kindt::IFTE;
  s.location = cur().location;
  expect_word("IF");
  s.cond = parse_condition();
  eat_word("THEN");
  s.then_stmts = parse_statements();
  if(eat_word("ELSE"))
    s.else_stmts = parse_statements();
  eat_word("END-IF");
  return s;
}

stmtt cobol_typecheckt::parse_evaluate()
{
  stmtt s;
  s.kind = stmtt::kindt::EVALUATE;
  s.location = cur().location;
  expect_word("EVALUATE");

  const bool subject_true = eat_word("TRUE");
  valuet subject;
  if(!subject_true)
    subject = parse_expr();

  while(eat_word("WHEN"))
  {
    if(eat_word("OTHER"))
    {
      s.other_stmts = parse_statements();
      continue;
    }
    exprt cond;
    if(subject_true)
    {
      cond = parse_condition();
    }
    else
    {
      valuet w = parse_operand();
      valuet subj = subject;
      cond = build_relation(subj, "=", w);
    }
    std::vector<stmtt> body = parse_statements();
    s.when_clauses.emplace_back(cond, std::move(body));
  }
  eat_word("END-EVALUATE");
  return s;
}

stmtt cobol_typecheckt::parse_perform()
{
  stmtt s;
  s.kind = stmtt::kindt::PERFORM;
  s.location = cur().location;
  expect_word("PERFORM");

  // Out-of-line: PERFORM proc-name [THRU proc-name] [control]
  const bool out_of_line = cur().kind == cobol_token_kindt::WORD &&
                           !is_word("VARYING") && !is_word("UNTIL") &&
                           !is_word("WITH") && !is_word("TEST") &&
                           !is_word("FOREVER");
  if(out_of_line)
  {
    s.target = cur().text;
    advance();
    if(eat_word("THRU") || eat_word("THROUGH"))
    {
      if(cur().kind != cobol_token_kindt::WORD)
        error("expected paragraph name after THRU");
      s.target_end = cur().text;
      advance();
    }
  }
  else
  {
    s.inline_body = true;
  }

  // control phrase
  if(eat_word("VARYING"))
  {
    s.pkind = stmtt::perform_kindt::VARYING;
    const item_infot &var = lookup_item(cur().text);
    s.var = symbol_exprt{var.symbol_name, cobol_value_type()};
    advance();
    expect_word("FROM");
    valuet from = parse_operand();
    s.from_val = store_to_item(var, from);
    expect_word("BY");
    valuet by = parse_operand();
    valuet var_v{s.var, var.scale};
    align(var_v, by);
    s.by_val = by.expr;
    s.var = var_v.expr;
    expect_word("UNTIL");
    s.cond = parse_condition();
  }
  else if(eat_word("UNTIL"))
  {
    s.pkind = stmtt::perform_kindt::UNTIL;
    s.cond = parse_condition();
  }
  else if(cur().kind == cobol_token_kindt::NUMBER)
  {
    s.pkind = stmtt::perform_kindt::TIMES;
    auto r = parse_decimal(cur().text);
    advance();
    s.times =
      from_integer(rescale_int(r.first, r.second, 0), cobol_value_type());
    expect_word("TIMES");
  }

  if(s.inline_body)
  {
    s.body = parse_statements();
    expect_word("END-PERFORM");
  }
  return s;
}

void cobol_typecheckt::parse_procedure_division()
{
  expect_word("PROCEDURE");
  expect_word("DIVISION");
  // skip USING ... etc. until the period
  while(!at_eof() && !is_kind(cobol_token_kindt::PERIOD))
    advance();
  expect_period();

  paragraphs.push_back(paragrapht{"$entry", {}});

  while(!at_eof() && !is_word("IDENTIFICATION"))
  {
    if(is_kind(cobol_token_kindt::PERIOD))
    {
      advance();
      continue;
    }
    if(
      cur().kind == cobol_token_kindt::WORD && !is_verb(cur().text) &&
      (peek(1).kind == cobol_token_kindt::PERIOD || peek(1).text == "SECTION"))
    {
      const std::string name = cur().text;
      advance();
      eat_word("SECTION");
      expect_period();
      paragraphs.push_back(paragrapht{name, {}});
      continue;
    }
    if(cur().kind == cobol_token_kindt::WORD && is_verb(cur().text))
    {
      std::vector<stmtt> stmts = parse_statements();
      for(auto &x : stmts)
        paragraphs.back().statements.push_back(std::move(x));
      expect_period();
      continue;
    }
    // Unknown token at statement position: skip.
    advance();
  }
}

// ---------------------------------------------------------------------------
// code generation
// ---------------------------------------------------------------------------

void cobol_typecheckt::gen_statements(
  const std::vector<stmtt> &stmts,
  code_blockt &out,
  std::set<std::string> &inlining)
{
  for(const stmtt &s : stmts)
    gen_statement(s, out, inlining);
}

void cobol_typecheckt::gen_statement(
  const stmtt &s,
  code_blockt &out,
  std::set<std::string> &inlining)
{
  switch(s.kind)
  {
  case stmtt::kindt::SKIP:
    break;
  case stmtt::kindt::ASSIGN:
  {
    code_frontend_assignt a{s.lhs, s.rhs};
    a.add_source_location() = s.location;
    out.add(std::move(a));
    break;
  }
  case stmtt::kindt::ASSERT:
  {
    code_assertt a{s.cond};
    a.add_source_location() = s.location;
    a.add_source_location().set_comment("assertion");
    a.add_source_location().set_property_class("assertion");
    out.add(std::move(a));
    break;
  }
  case stmtt::kindt::ASSUME:
  {
    code_assumet a{s.cond};
    a.add_source_location() = s.location;
    out.add(std::move(a));
    break;
  }
  case stmtt::kindt::IFTE:
  {
    code_blockt then_block;
    gen_statements(s.then_stmts, then_block, inlining);
    if(s.else_stmts.empty())
    {
      out.add(code_ifthenelset{s.cond, std::move(then_block)});
    }
    else
    {
      code_blockt else_block;
      gen_statements(s.else_stmts, else_block, inlining);
      out.add(
        code_ifthenelset{s.cond, std::move(then_block), std::move(else_block)});
    }
    break;
  }
  case stmtt::kindt::EVALUATE:
  {
    code_blockt other_block;
    gen_statements(s.other_stmts, other_block, inlining);
    codet rest = std::move(other_block);
    for(auto it = s.when_clauses.rbegin(); it != s.when_clauses.rend(); ++it)
    {
      code_blockt clause_block;
      gen_statements(it->second, clause_block, inlining);
      rest = code_ifthenelset{it->first, std::move(clause_block), rest};
    }
    out.add(std::move(rest));
    break;
  }
  case stmtt::kindt::GOTO:
    out.add(code_gotot{para_label(s.target)});
    break;
  case stmtt::kindt::STOP:
    out.add(code_gotot{program_end_label()});
    break;
  case stmtt::kindt::PERFORM:
  {
    switch(s.pkind)
    {
    case stmtt::perform_kindt::ONCE:
      gen_perform_invocation(s, out, inlining);
      break;
    case stmtt::perform_kindt::TIMES:
    {
      const symbol_exprt ctr = make_counter();
      const std::string test = fresh_label("ptest");
      const std::string done = fresh_label("pdone");
      out.add(code_frontend_assignt{ctr, from_integer(0, cobol_value_type())});
      out.add(code_labelt{test, code_skipt{}});
      out.add(code_ifthenelset{
        not_exprt{binary_relation_exprt{ctr, ID_lt, s.times}},
        code_gotot{done}});
      gen_perform_invocation(s, out, inlining);
      out.add(code_frontend_assignt{
        ctr, plus_exprt{ctr, from_integer(1, cobol_value_type())}});
      out.add(code_gotot{test});
      out.add(code_labelt{done, code_skipt{}});
      break;
    }
    case stmtt::perform_kindt::UNTIL:
    {
      const std::string test = fresh_label("ptest");
      const std::string done = fresh_label("pdone");
      if(s.test_after)
      {
        out.add(code_labelt{test, code_skipt{}});
        gen_perform_invocation(s, out, inlining);
        out.add(code_ifthenelset{not_exprt{s.cond}, code_gotot{test}});
      }
      else
      {
        out.add(code_labelt{test, code_skipt{}});
        out.add(code_ifthenelset{s.cond, code_gotot{done}});
        gen_perform_invocation(s, out, inlining);
        out.add(code_gotot{test});
        out.add(code_labelt{done, code_skipt{}});
      }
      break;
    }
    case stmtt::perform_kindt::VARYING:
    {
      const std::string test = fresh_label("ptest");
      const std::string done = fresh_label("pdone");
      out.add(code_frontend_assignt{s.var, s.from_val});
      out.add(code_labelt{test, code_skipt{}});
      out.add(code_ifthenelset{s.cond, code_gotot{done}});
      gen_perform_invocation(s, out, inlining);
      out.add(code_frontend_assignt{s.var, plus_exprt{s.var, s.by_val}});
      out.add(code_gotot{test});
      out.add(code_labelt{done, code_skipt{}});
      break;
    }
    }
    break;
  }
  }
}

void cobol_typecheckt::gen_perform_invocation(
  const stmtt &s,
  code_blockt &out,
  std::set<std::string> &inlining)
{
  if(s.inline_body)
  {
    gen_statements(s.body, out, inlining);
    return;
  }

  const std::size_t start = paragraph_index(s.target);
  const std::size_t end =
    s.target_end.empty() ? start : paragraph_index(s.target_end);
  if(end < start)
    error("PERFORM THRU range ends before it starts");

  for(std::size_t i = start; i <= end; ++i)
    if(inlining.find(paragraphs[i].name) != inlining.end())
      error("recursive PERFORM is not supported");

  for(std::size_t i = start; i <= end; ++i)
    inlining.insert(paragraphs[i].name);
  for(std::size_t i = start; i <= end; ++i)
    gen_statements(paragraphs[i].statements, out, inlining);
  for(std::size_t i = start; i <= end; ++i)
    inlining.erase(paragraphs[i].name);
}

void cobol_typecheckt::build_function()
{
  code_blockt body;
  std::set<std::string> inlining;

  for(const paragrapht &p : paragraphs)
  {
    body.add(code_labelt{para_label(p.name), code_skipt{}});
    gen_statements(p.statements, body, inlining);
  }
  body.add(code_labelt{program_end_label(), code_skipt{}});

  const irep_idt fname = "cobol::" + program_id;
  symbolt function{fname, code_typet{{}, empty_typet{}}, COBOL_MODE};
  function.base_name = program_id;
  function.value = std::move(body);
  symbol_table.add(function);
}

} // namespace

bool cobol_typecheck(
  const std::vector<cobol_tokent> &tokens,
  symbol_table_baset &symbol_table,
  const std::string &module,
  message_handlert &message_handler)
{
  cobol_typecheckt cobol_typecheck{
    tokens, symbol_table, module, message_handler};
  return cobol_typecheck.typecheck();
}
