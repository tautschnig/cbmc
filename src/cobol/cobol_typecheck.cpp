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
#include <util/byte_operators.h>
#include <util/c_types.h>
#include <util/message.h>
#include <util/mp_arith.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

#include <goto-programs/goto_instruction_code.h>

#include "cobol_language.h"

#include <algorithm>
#include <cctype>
#include <deque>
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

/// Physical encoding of a numeric item (IBM LR "USAGE clause"). The byte
/// *size* per usage is given by phys_size_of; this records how the value is
/// laid out within those bytes so that byte-level observation (a REDEFINES of
/// a different category, a class condition) can be faithful.
enum class usaget
{
  DISPLAY,       ///< zoned external decimal: one byte per digit
  PACKED,        ///< COMP-3 / PACKED-DECIMAL: two digits per byte + sign nibble
  BINARY,        ///< COMP / COMP-4 / BINARY: two's complement, picture-bounded
  NATIVE_BINARY, ///< COMP-5: native binary, full bit-width (no decimal trunc)
  FLOAT_SHORT,   ///< COMP-1
  FLOAT_LONG     ///< COMP-2
};

/// Sign representation of a numeric item (IBM LR "SIGN clause"). For DISPLAY,
/// the default is a trailing overpunch on the last digit byte.
enum class signt
{
  UNSIGNED,
  OVERPUNCH_TRAILING,
  OVERPUNCH_LEADING,
  SEPARATE_TRAILING,
  SEPARATE_LEADING
};

/// Description of an elementary data item.
struct item_infot
{
  irep_idt record_symbol;    ///< enclosing 01/77 record's byte-array symbol
  std::size_t offset = 0;    ///< byte offset of this field within the record
  std::size_t byte_size = 0; ///< physical size of one occurrence, in bytes
  bool is_group = false;     ///< true for group items (no PICTURE)
  bool is_numeric = true;
  std::size_t digits = 0;
  std::size_t scale = 0;
  bool is_signed = false;
  std::size_t char_count = 0;
  bool is_table = false;          ///< has a fixed OCCURS clause
  std::size_t occurs = 0;         ///< number of elements when is_table
  usaget usage = usaget::DISPLAY; ///< physical encoding (USAGE clause)
  signt sign = signt::UNSIGNED;   ///< sign representation (SIGN clause)
  /// When set, this numeric field's bytes are kept in its USAGE-faithful
  /// encoding (zoned/packed/...) rather than the uniform binary value model,
  /// because its bytes are observed at a different category (e.g. a REDEFINES
  /// alias). Set by the alias classifier at record finalisation.
  bool faithful_bytes = false;
  /// Indices into all_items of the enclosing OCCURS groups (outermost first);
  /// each is a subscript dimension whose stride is the group's byte_size.
  std::vector<std::size_t> occurs_dims;
};

/// A data item together with its own name and the names of its containing
/// groups (innermost first), used to resolve qualified references
/// "name OF group OF ..." (IBM LR "Qualification", pp. 67-68).
struct entryt
{
  std::string name;
  item_infot info;
  std::vector<std::string> ancestors;
};

/// A field of a synthesised built-in record (EIB / DFHAID / DFHBMSCA).
struct builtin_fieldt
{
  const char *name;
  bool numeric;
  std::size_t digits;
  std::size_t chars;
  const char *usage;
};

/// Physical size of one occurrence of an elementary item, in bytes.
///
/// IBM Enterprise COBOL for z/OS 6.4 Language Reference, USAGE clause /
/// "Storage" (pp. 9048-9075):
///   * DISPLAY external decimal / alphanumeric: 1 byte per digit/character.
///   * BINARY/COMP/COMP-4 (COMP-5 modelled likewise): halfword (2 bytes) for
///     1-4 digits, fullword (4) for 5-9, doubleword (8) for 10-18.
///   * PACKED-DECIMAL/COMP-3: ceil((digits + 1) / 2) bytes.
///   * COMP-1: 4 bytes; COMP-2: 8 bytes.
inline std::size_t phys_size_of(
  const std::string &usage,
  bool is_numeric,
  std::size_t digits,
  std::size_t char_count)
{
  if(!is_numeric)
    return char_count == 0 ? 1 : char_count;

  if(usage == "COMP-3" || usage == "PACKED-DECIMAL" || usage == "COMP-6")
    return (digits + 2) / 2; // ceil((digits + 1) / 2)
  if(
    usage == "COMP" || usage == "COMP-4" || usage == "COMP-5" ||
    usage == "BINARY" || usage == "COMPUTATIONAL" ||
    usage == "COMPUTATIONAL-4" || usage == "COMPUTATIONAL-5")
  {
    if(digits <= 4)
      return 2;
    if(digits <= 9)
      return 4;
    return 8;
  }
  if(usage == "COMP-1" || usage == "COMPUTATIONAL-1")
    return 4;
  if(usage == "COMP-2" || usage == "COMPUTATIONAL-2")
    return 8;
  return digits == 0 ? 1 : digits; // DISPLAY external decimal
}

/// Map a USAGE keyword to the physical encoding kind (IBM LR "USAGE clause").
inline usaget usage_of(const std::string &usage)
{
  if(
    usage == "COMP-3" || usage == "PACKED-DECIMAL" || usage == "COMP-6" ||
    usage == "COMPUTATIONAL-3")
    return usaget::PACKED;
  if(usage == "COMP-5" || usage == "COMPUTATIONAL-5")
    return usaget::NATIVE_BINARY;
  if(usage == "COMP-1" || usage == "COMPUTATIONAL-1")
    return usaget::FLOAT_SHORT;
  if(usage == "COMP-2" || usage == "COMPUTATIONAL-2")
    return usaget::FLOAT_LONG;
  if(
    usage == "COMP" || usage == "COMP-4" || usage == "BINARY" ||
    usage == "COMPUTATIONAL" || usage == "COMPUTATIONAL-4")
    return usaget::BINARY;
  return usaget::DISPLAY;
}

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

/// A reference to a data item, possibly subscripted (e.g. WS-TABLE(I)).
/// In the byte-level model a reference is the enclosing record byte array
/// plus a byte offset within it; reads use byte_extract and writes byte_update.
struct reft
{
  const item_infot *info = nullptr;
  exprt record; ///< symbol_exprt of the record byte array
  exprt offset; ///< byte offset within the record (size_type)
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

/// An operand of a relation condition: either a numeric value or an
/// alphanumeric operand (an item's bytes, or a literal / figurative constant).
/// Drives numeric vs. alphanumeric comparison (IBM LR "Comparison of two
/// alphanumeric operands", pp. 276-277).
struct cond_operandt
{
  bool numeric = false;
  valuet num; ///< when numeric

  // alphanumeric item bytes:
  bool is_item = false;
  exprt record;
  exprt offset;
  std::size_t length = 0;
  const item_infot *item = nullptr; ///< the field, when is_item

  // alphanumeric literal / figurative:
  bool is_spec = false;
  value_spect spec;
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
    SEARCH,
    GOTO,
    STOP,
    // COBOL-2002 explicit scope-exit statements (IBM LR "EXIT statement"):
    EXIT_PERFORM,   ///< EXIT PERFORM: leave the innermost inline PERFORM
    EXIT_CYCLE,     ///< EXIT PERFORM CYCLE: next iteration of inline PERFORM
    EXIT_PARAGRAPH, ///< EXIT PARAGRAPH: to the end of the current paragraph
    EXIT_SECTION,   ///< EXIT SECTION: to the end of the current section
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
  std::vector<stmtt> var_init; ///< VARYING: initialise the loop variable
  std::vector<stmtt> var_step; ///< VARYING: step the loop variable
};

struct paragrapht
{
  std::string name;
  std::vector<stmtt> statements;
  /// true if this entry is a SECTION header (vs a paragraph). A PERFORM of a
  /// section-name runs through the last paragraph of the section, not just the
  /// section header's own statements (IBM LR "PERFORM statement").
  bool is_section = false;
};

bool is_verb(const std::string &w)
{
  static const std::set<std::string> verbs = {
    "MOVE",     "ADD",      "SUBTRACT", "MULTIPLY",   "DIVIDE",  "COMPUTE",
    "IF",       "EVALUATE", "PERFORM",  "GO",         "STOP",    "GOBACK",
    "EXIT",     "DISPLAY",  "ACCEPT",   "CONTINUE",   "CALL",    "NEXT",
    "SET",      "EXEC",     "STRING",   "INITIALIZE", "SEARCH",  "INSPECT",
    "UNSTRING", "OPEN",     "CLOSE",    "READ",       "WRITE",   "REWRITE",
    "DELETE",   "START",    "SORT",     "MERGE",      "RELEASE", "RETURN"};
  return verbs.find(w) != verbs.end();
}

bool is_pic_stop_word(const std::string &w)
{
  static const std::set<std::string> s = {
    "VALUE",          "VALUES",       "USAGE",
    "OCCURS",         "REDEFINES",    "COMP",
    "COMP-1",         "COMP-2",       "COMP-3",
    "COMP-4",         "COMP-5",       "BINARY",
    "PACKED-DECIMAL", "DISPLAY",      "SIGN",
    "SYNC",           "SYNCHRONIZED", "JUSTIFIED",
    "JUST",           "BLANK",        "PIC",
    "PICTURE",        "IS",           "INDEXED",
    "ASCENDING",      "DESCENDING",   "KEY",
    "DEPENDING"};
  return s.find(w) != s.end();
}

/// True for a word that begins (or belongs to) a data-description clause, used
/// to bound the name lists of the INDEXED BY and ASCENDING/DESCENDING KEY
/// phrases (IBM LR "OCCURS clause").
bool is_clause_keyword(const std::string &w)
{
  return is_pic_stop_word(w) || w == "INDEXED" || w == "ASCENDING" ||
         w == "DESCENDING" || w == "KEY" || w == "BY" || w == "TIMES" ||
         w == "DEPENDING";
}

/// Parse a PICTURE string body, e.g. "9(5)V99", "S9(4)", "X(20)", "ZZ,ZZ9.99".
///
/// IBM Enterprise COBOL for z/OS 6.4 Language Reference, "The PICTURE clause"
/// and "PICTURE character-strings" (pp. 44-62). A picture with editing symbols
/// (Z * . , + - $ B 0 / CR DB) denotes a numeric-edited or alphanumeric-edited
/// item; such items are display-only formatting fields, so we model them as
/// alphanumeric storage whose size is the number of character positions.
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
  bool has_alpha = false;
  bool has_edit = false;
  std::size_t i = 0;
  const std::size_t n = pic.size();
  while(i < n)
  {
    char sym =
      static_cast<char>(std::toupper(static_cast<unsigned char>(pic[i])));

    // Two-character insertion editing symbols CR and DB (LR p. 57).
    if(
      i + 1 < n &&
      ((sym == 'C' &&
        std::toupper(static_cast<unsigned char>(pic[i + 1])) == 'R') ||
       (sym == 'D' &&
        std::toupper(static_cast<unsigned char>(pic[i + 1])) == 'B')))
    {
      char_count += 2;
      has_edit = true;
      i += 2;
      continue;
    }

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
    case 'S': // sign; no character position unless SEPARATE (not modelled)
      is_signed = true;
      break;
    case 'V': // implied decimal point: no storage position
      after_v = true;
      break;
    case 'P': // assumed scaling position: no storage position
      break;
    case '9':
      digits += rep;
      char_count += rep;
      if(after_v)
        scale += rep;
      break;
    case 'Z': // zero suppression (editing)
    case '*': // asterisk (check protection) suppression (editing)
      digits += rep;
      char_count += rep;
      has_edit = true;
      if(after_v)
        scale += rep;
      break;
    case 'X':
      has_alpha = true;
      char_count += rep;
      break;
    case 'A':
      has_alpha = true;
      char_count += rep;
      break;
    case '.': // actual decimal point insertion (editing); also marks scale
      char_count += rep;
      has_edit = true;
      after_v = true;
      break;
    case ',': // insertion characters (editing)
    case '+':
    case '-':
    case '$':
    case 'B':
    case '0':
    case '/':
      char_count += rep;
      has_edit = true;
      break;
    default:
      char_count += rep;
      break;
    }
  }

  // Numeric only if it has no alphabetic and no editing symbols; otherwise it
  // is an (alphanumeric- or numeric-) edited item modelled as alphanumeric.
  is_numeric = !has_alpha && !has_edit;
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
  std::vector<entryt> all_items; ///< every field, for qualified-name resolution
  /// stable storage for synthetic field descriptors (reference modification)
  std::deque<item_infot> synth_items;
  std::map<std::string, cond_infot> conds;
  /// For each table (OCCURS) item, the index-names declared in its INDEXED BY
  /// phrase, in order; used by SEARCH to choose the index to vary.
  std::map<std::string, std::vector<std::string>> table_indexes;
  /// For each FD file-name, the base name of its 01 record area; used by READ
  /// to havoc the record. \ref pending_file holds the file-name between an FD
  /// entry and the record description that follows it.
  std::map<std::string, std::string> file_records;
  std::string pending_file;
  std::vector<paragrapht> paragraphs;
  std::size_t unique = 0;
  /// Scope context for the COBOL-2002 EXIT statements (IBM LR "EXIT
  /// statement"). Inline PERFORMs nest, so their (cycle, done) labels form a
  /// stack; a paragraph/section does not nest within another, so the current
  /// paragraph- and section-end labels are single values set while that
  /// paragraph is generated.
  std::vector<std::pair<std::string, std::string>> perform_loop_stack;
  std::string cur_para_end_label;
  std::string cur_section_end_label;

  // ---- DATA DIVISION layout state ----
  /// byte size of each record (its byte-array symbol)
  std::map<irep_idt, std::size_t> record_sizes;
  struct layout_framet
  {
    std::size_t level = 0;
    std::string name;       ///< group field name ("" if anonymous/FILLER)
    std::size_t start = 0;  ///< byte offset where this group begins
    std::size_t cursor = 0; ///< next free byte offset within this group
    bool is_redefines = false;
    bool is_occurs = false;      ///< this group has an OCCURS clause
    std::size_t entry_index = 0; ///< index of this group in all_items
  };
  std::vector<layout_framet> layout_stack;
  irep_idt cur_record;         ///< current 01/77 record byte-array symbol
  std::string cur_record_base; ///< base name of the current record
  std::size_t record_max = 0;  ///< max end offset placed in the current record
  /// pending VALUE writes for the current record: (offset, little-endian bytes)
  std::vector<std::pair<std::size_t, std::vector<unsigned char>>> record_inits;
  bool record_has_value = false;
  /// Deferred numeric VALUE initialisations for the current record:
  /// (byte offset, value at the item's scale, index into all_items). The
  /// bytes are encoded at record finalisation, after the alias classifier has
  /// decided whether the field uses its faithful (zoned/...) encoding.
  std::vector<std::tuple<std::size_t, mp_integer, std::size_t>>
    pending_num_values;
  std::string last_field; ///< most recent elementary field (for 88-levels)
  /// Implied subject/operator for abbreviated combined relation conditions
  /// (IBM LR "Abbreviated combined relation conditions", p. 287).
  cond_operandt abbr_subject;
  std::string abbr_op;
  bool have_abbr = false;

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
  // The PROCEDURE DIVISION is lowered to one re-entrant function
  // $proc(entry, exit); PERFORM is a (possibly recursive) call to it. See
  // doc/architectural/cobol-perform-control-flow-design.md.
  irep_idt proc_name() const
  {
    return "cobol::" + program_id + "::$proc";
  }
  irep_idt entry_param_name() const
  {
    return "cobol::" + program_id + "::$proc::entry";
  }
  irep_idt exit_param_name() const
  {
    return "cobol::" + program_id + "::$proc::exit";
  }
  std::string proc_ret_label() const
  {
    return "cobol::" + program_id + "::$ret";
  }
  irep_idt stopped_name() const
  {
    return "cobol::" + program_id + "::$stopped";
  }
  symbol_exprt stopped_expr() const
  {
    return symbol_exprt{stopped_name(), bool_typet{}};
  }
  code_typet proc_type() const
  {
    code_typet::parametert ep{cobol_value_type()};
    ep.set_identifier(entry_param_name());
    ep.set_base_name("entry");
    code_typet::parametert xp{cobol_value_type()};
    xp.set_identifier(exit_param_name());
    xp.set_base_name("exit");
    return code_typet{{ep, xp}, empty_typet{}};
  }
  symbol_exprt proc_symbol_expr() const
  {
    return symbol_exprt{proc_name(), proc_type()};
  }

  exprt rescale(const exprt &e, std::size_t from, std::size_t to) const;
  std::size_t align(valuet &a, valuet &b);
  exprt build_relation(valuet a, const std::string &op, valuet b);

  const item_infot &lookup_item(const std::string &name);
  const item_infot &
  resolve_item(const std::string &name, const std::vector<std::string> &quals);
  std::size_t paragraph_index(const std::string &name);
  /// Last paragraph index of the range denoted by \p name: the item's own
  /// index for a paragraph, or the section's last paragraph for a section name
  /// (IBM LR "PERFORM statement").
  std::size_t range_end(const std::string &name);
  /// Collect the paragraph indices that are PERFORM range *starts* (entry
  /// targets) and range *ends* (exit targets) anywhere in \p stmts, recursing
  /// into nested statement lists. $proc only ever needs an entry-dispatch case
  /// for a start and an end check for an end.
  void collect_perform_targets(
    const std::vector<stmtt> &stmts,
    std::set<std::size_t> &entries,
    std::set<std::size_t> &exits);

  // ---- byte-level storage helpers ----
  typet record_type(const irep_idt &record) const;
  symbol_exprt record_expr(const irep_idt &record) const;
  /// build a reference (record + offset) for a field descriptor
  reft ref_of(const item_infot &info) const;
  /// Storage (physical) integer type of a numeric field: signedbv(8*size).
  signedbv_typet phys_type(const item_infot &item) const;
  /// rvalue of a field: byte_extract + decode to the value domain.
  valuet read_field(const reft &r) const;
  /// encode a value-domain number into the field's physical storage type.
  exprt
  encode_numeric(const item_infot &item, valuet v, bool rounded = false) const;
  /// The i-th character byte of an item as a value-domain integer (0..255).
  /// The shared building block for character-content semantics (INSPECT,
  /// STRING, class conditions, ...): item bytes are the record byte array.
  exprt byte_of(const reft &r, std::size_t i) const;
  /// Decode a faithfully-encoded zoned DISPLAY field's bytes to its value
  /// (IBM LR "USAGE DISPLAY" external decimal).
  valuet decode_zoned(const reft &r) const;
  /// Encode a value (already at item.scale) into the little-endian integer
  /// whose bytes are the field's zoned DISPLAY representation.
  exprt encode_zoned(const exprt &scaled_value, const item_infot &item) const;
  /// Compile-time zoned bytes for a constant value (VALUE initialisation).
  std::vector<unsigned char>
  zoned_bytes(const mp_integer &value, const item_infot &item) const;
  /// Decode a faithfully-encoded packed-decimal (COMP-3) field to its value
  /// (IBM LR "USAGE PACKED-DECIMAL"): two digits per byte, sign in the last
  /// nibble.
  valuet decode_packed(const reft &r) const;
  /// Encode a value (at item.scale) into the little-endian integer whose bytes
  /// are the field's packed-decimal representation.
  exprt encode_packed(const exprt &scaled_value, const item_infot &item) const;
  /// Compile-time packed-decimal bytes for a constant value.
  std::vector<unsigned char>
  packed_bytes(const mp_integer &value, const item_infot &item) const;
  /// Whether item's faithful encoding is implemented (so the classifier may
  /// mark it and read/write/VALUE go through the codec).
  bool has_faithful_codec(const item_infot &item) const;
  /// Mark numeric fields whose bytes are observed at a different category
  /// (e.g. a REDEFINES alias) as needing their faithful encoding.
  void classify_record_aliases();
  /// finalise the current record: create its byte-array symbol + initialiser.
  void finalize_record();

  // ---- parsers ----
  void parse_program();
  void parse_data_division();
  void parse_data_item();
  void place_field(
    std::size_t level,
    const std::string &name,
    bool has_pic,
    const std::string &pic,
    const std::string &usage,
    bool sign_leading,
    bool sign_separate,
    bool is_table,
    std::size_t occurs,
    const std::string &redefines_target,
    bool has_value,
    const value_spect &value_spec,
    const source_locationt &loc);
  void close_groups_below(std::size_t level);
  std::string read_picture_string();
  value_spect read_value_spec();
  bool at_value_start() const;
  std::optional<mp_integer>
  spec_to_numeric(const value_spect &spec, std::size_t target_scale);
  exprt make_alnum_constant(const value_spect &spec, std::size_t n);
  void parse_procedure_division();

  valuet parse_expr();
  valuet parse_term();
  valuet parse_expr_from(valuet a);
  valuet parse_term_from(valuet a);
  valuet parse_factor();
  valuet parse_primary();
  valuet parse_operand();
  bool at_operand() const;
  std::vector<valuet> parse_operand_list();
  reft parse_ref();
  std::optional<reft> last_ref; ///< set when an operand was a bare field

  exprt parse_condition();
  exprt parse_and_condition();
  exprt parse_not_condition();
  bool paren_is_condition() const;
  bool is_relop_start() const;
  exprt parse_relation();
  cond_operandt parse_cond_operand();
  bool at_intrinsic() const;
  cond_operandt parse_intrinsic();
  valuet compute_numeric_intrinsic(
    const std::string &fname,
    std::vector<valuet> &args);
  valuet numval_of_string(const std::string &s, bool currency) const;
  cond_operandt nondet_alnum_operand(std::size_t len);
  exprt
  build_cond_relation(cond_operandt a, const std::string &op, cond_operandt b);
  exprt build_alnum_relation(
    cond_operandt &l,
    const std::string &op,
    cond_operandt &r);
  std::string parse_relop();
  exprt cond_predicate(const std::string &name, const reft &parent_ref);
  reft apply_cond_subscript(const item_infot &item, reft r);

  std::vector<stmtt> parse_statements();
  std::vector<stmtt> parse_statement();
  stmtt parse_if();
  stmtt parse_evaluate();
  stmtt parse_perform();
  std::vector<stmtt> parse_move();
  std::vector<stmtt> parse_string();
  std::vector<stmtt> parse_inspect();
  std::vector<stmtt> parse_unstring();
  std::vector<stmtt> parse_open_close();
  std::vector<stmtt> parse_read();
  std::vector<stmtt> parse_write();
  std::vector<stmtt> parse_sort();
  void parse_io_exception(
    std::vector<stmtt> &result,
    const char *end_kw,
    source_locationt loc);
  void parse_overflow_phrase(
    std::vector<stmtt> &result,
    const char *end_kw,
    source_locationt loc);
  std::vector<stmtt> parse_search();
  std::vector<stmtt> parse_initialize();
  std::vector<stmtt> parse_add();
  std::vector<stmtt> parse_subtract();
  std::vector<stmtt> parse_multiply();
  std::vector<stmtt> parse_divide();
  std::vector<stmtt> parse_compute();
  /// The size-error condition for storing \p v into receiver \p r: true when
  /// the value does not fit the receiver's digit capacity (IBM LR "SIZE ERROR
  /// phrases").
  exprt size_error_cond(const reft &r, const valuet &v);
  /// Parse a "GIVING receiver-1 [ROUNDED] ..." receiver list, appending the
  /// assignments of \p v and OR-ing each receiver's size-error condition into
  /// \p overflow.
  void assign_giving(
    const valuet &v,
    std::vector<stmtt> &result,
    exprt &overflow,
    source_locationt loc);
  /// Parse the optional [ON SIZE ERROR ...] [NOT ON SIZE ERROR ...] [END-verb]
  /// tail and combine it with the receiver assignments.
  std::vector<stmtt> finish_arith(
    std::vector<stmtt> assigns,
    exprt overflow,
    const char *end_kw,
    source_locationt loc);
  std::vector<stmtt> parse_call();
  std::vector<stmtt> parse_set();
  std::vector<stmtt> parse_exec();
  stmtt havoc_field(const reft &r, source_locationt loc);
  void inject_eib();
  void inject_builtin_record(
    const std::string &base,
    const std::vector<builtin_fieldt> &fields,
    int value_mode);
  void register_index(const std::string &name);
  void skip_to_sentence_end();

  stmtt make_assign_ref(
    const reft &target,
    valuet v,
    source_locationt loc,
    bool rounded = false);
  stmtt
  make_move_group(const reft &target, const reft &src, source_locationt loc);

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
  std::size_t to) const
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

reft cobol_typecheckt::ref_of(const item_infot &info) const
{
  return reft{
    &info,
    record_expr(info.record_symbol),
    from_integer(info.offset, size_type())};
}

typet cobol_typecheckt::record_type(const irep_idt &record) const
{
  auto it = record_sizes.find(record);
  const std::size_t size = it == record_sizes.end() ? 1 : it->second;
  return array_typet{unsignedbv_typet{8}, from_integer(size, size_type())};
}

symbol_exprt cobol_typecheckt::record_expr(const irep_idt &record) const
{
  return symbol_exprt{record, record_type(record)};
}

signedbv_typet cobol_typecheckt::phys_type(const item_infot &item) const
{
  return signedbv_typet{item.byte_size * 8};
}

std::vector<unsigned char> cobol_typecheckt::zoned_bytes(
  const mp_integer &value,
  const item_infot &item) const
{
  // Unsigned zoned DISPLAY (IBM LR "USAGE DISPLAY", external decimal): one
  // ASCII digit per byte, most significant first; the implied decimal point
  // is not stored. (Signed/EBCDIC zoned and packed are tracked follow-ups, so
  // the classifier only marks unsigned DISPLAY fields faithful for now.)
  const std::size_t n = item.byte_size;
  std::vector<unsigned char> bytes(n, '0');
  mp_integer v = value < 0 ? -value : value;
  if(n > 0)
    v = v % power10(n);
  for(std::size_t i = 0; i < n; ++i)
  {
    bytes[n - 1 - i] = static_cast<unsigned char>('0' + (v % 10).to_long());
    v /= 10;
  }
  return bytes;
}

exprt cobol_typecheckt::byte_of(const reft &r, std::size_t i) const
{
  // A single character byte at offset+i (endianness-independent), widened to
  // the value domain so it can be compared/arithmetised.
  const exprt off = plus_exprt{r.offset, from_integer(i, r.offset.type())};
  return typecast_exprt{
    make_byte_extract(r.record, off, unsignedbv_typet{8}), cobol_value_type()};
}

valuet cobol_typecheckt::decode_zoned(const reft &r) const
{
  // Sum the ASCII digit bytes with decreasing place value (byte at offset+i is
  // the digit at position i, position 0 most significant).
  const item_infot &item = *r.info;
  const std::size_t n = item.byte_size;
  exprt acc = from_integer(0, cobol_value_type());
  for(std::size_t i = 0; i < n; ++i)
  {
    const exprt digit =
      minus_exprt{byte_of(r, i), from_integer('0', cobol_value_type())};
    const exprt place = from_integer(power10(n - 1 - i), cobol_value_type());
    acc = plus_exprt{acc, mult_exprt{digit, place}};
  }
  return valuet{acc, item.scale};
}

exprt cobol_typecheckt::encode_zoned(
  const exprt &scaled_value,
  const item_infot &item) const
{
  // Build the little-endian integer whose byte i ('0'+digit) is the field's
  // zoned byte at offset+i. Unsigned: store the magnitude (IBM LR "MOVE
  // statement": moving a signed value to an unsigned item drops the sign).
  const std::size_t n = item.byte_size;
  const exprt zero = from_integer(0, cobol_value_type());
  const exprt mag = if_exprt{
    binary_relation_exprt{scaled_value, ID_ge, zero},
    scaled_value,
    unary_minus_exprt{scaled_value}};
  exprt acc = from_integer(0, phys_type(item));
  for(std::size_t i = 0; i < n; ++i)
  {
    const exprt place = from_integer(power10(n - 1 - i), cobol_value_type());
    const exprt digit =
      mod_exprt{div_exprt{mag, place}, from_integer(10, cobol_value_type())};
    const exprt byte = plus_exprt{from_integer('0', cobol_value_type()), digit};
    const exprt shifted = mult_exprt{
      typecast_exprt{byte, phys_type(item)},
      from_integer(power(mp_integer{256}, i), phys_type(item))};
    acc = plus_exprt{acc, shifted};
  }
  return acc;
}

// COMP-3 / PACKED-DECIMAL: byte_size bytes hold 2*byte_size nibbles; the last
// nibble is the sign (0xC positive, 0xD negative, 0xF unsigned; 0xB also
// negative), the preceding nibbles hold the digits, most significant first,
// right-justified (IBM LR "USAGE PACKED-DECIMAL"). For n bytes there are
// D = 2n-1 digit nibbles.

std::vector<unsigned char> cobol_typecheckt::packed_bytes(
  const mp_integer &value,
  const item_infot &item) const
{
  const std::size_t n = item.byte_size;
  const std::size_t digit_nibbles = 2 * n - 1;
  mp_integer m = value < 0 ? -value : value;
  m = m % power10(digit_nibbles);
  std::vector<unsigned char> nib(2 * n, 0);
  for(std::size_t p = 0; p < digit_nibbles; ++p)
  {
    const mp_integer place = power10(digit_nibbles - 1 - p);
    nib[p] = static_cast<unsigned char>(((m / place) % 10).to_long());
  }
  nib[digit_nibbles] = !item.is_signed ? 0xF : (value < 0 ? 0xD : 0xC);
  std::vector<unsigned char> bytes(n, 0);
  for(std::size_t k = 0; k < n; ++k)
    bytes[k] = static_cast<unsigned char>((nib[2 * k] << 4) | nib[2 * k + 1]);
  return bytes;
}

valuet cobol_typecheckt::decode_packed(const reft &r) const
{
  const item_infot &item = *r.info;
  const std::size_t n = item.byte_size;
  const std::size_t digit_nibbles = 2 * n - 1;
  const unsignedbv_typet u8{8};
  const typet vt = cobol_value_type();
  const auto nibble = [&](std::size_t p) -> exprt
  {
    const exprt off =
      plus_exprt{r.offset, from_integer(p / 2, r.offset.type())};
    const exprt byte = typecast_exprt{make_byte_extract(r.record, off, u8), vt};
    const exprt sixteen = from_integer(16, vt);
    return (p % 2 == 0) ? static_cast<exprt>(div_exprt{byte, sixteen})
                        : static_cast<exprt>(mod_exprt{byte, sixteen});
  };
  exprt acc = from_integer(0, vt);
  for(std::size_t p = 0; p < digit_nibbles; ++p)
    acc = plus_exprt{
      acc,
      mult_exprt{nibble(p), from_integer(power10(digit_nibbles - 1 - p), vt)}};
  // Sign nibble (the last nibble): 0xB and 0xD denote a negative value.
  const exprt s = nibble(digit_nibbles);
  const exprt negative = or_exprt{
    equal_exprt{s, from_integer(0xD, vt)},
    equal_exprt{s, from_integer(0xB, vt)}};
  return valuet{if_exprt{negative, unary_minus_exprt{acc}, acc}, item.scale};
}

exprt cobol_typecheckt::encode_packed(
  const exprt &scaled_value,
  const item_infot &item) const
{
  const std::size_t n = item.byte_size;
  const std::size_t digit_nibbles = 2 * n - 1;
  const typet vt = cobol_value_type();
  const exprt zero = from_integer(0, vt);
  const exprt mag = if_exprt{
    binary_relation_exprt{scaled_value, ID_ge, zero},
    scaled_value,
    unary_minus_exprt{scaled_value}};
  const exprt sign_nib = !item.is_signed
                           ? static_cast<exprt>(from_integer(0xF, vt))
                           : static_cast<exprt>(if_exprt{
                               binary_relation_exprt{scaled_value, ID_lt, zero},
                               from_integer(0xD, vt),
                               from_integer(0xC, vt)});
  const auto nibble = [&](std::size_t p) -> exprt
  {
    if(p >= digit_nibbles)
      return sign_nib;
    const exprt place = from_integer(power10(digit_nibbles - 1 - p), vt);
    return mod_exprt{div_exprt{mag, place}, from_integer(10, vt)};
  };
  exprt acc = from_integer(0, phys_type(item));
  for(std::size_t k = 0; k < n; ++k)
  {
    const exprt byte = plus_exprt{
      mult_exprt{nibble(2 * k), from_integer(16, vt)}, nibble(2 * k + 1)};
    acc = plus_exprt{
      acc,
      mult_exprt{
        typecast_exprt{byte, phys_type(item)},
        from_integer(power(mp_integer{256}, k), phys_type(item))}};
  }
  return acc;
}

bool cobol_typecheckt::has_faithful_codec(const item_infot &item) const
{
  // Encodings with an implemented codec. DISPLAY is only faithful when
  // unsigned (signed zoned overpunch is charset-dependent and deferred);
  // PACKED handles both signs via its sign nibble.
  if(item.usage == usaget::DISPLAY)
    return !item.is_signed;
  return item.usage == usaget::PACKED;
}

valuet cobol_typecheckt::read_field(const reft &r) const
{
  const item_infot &item = *r.info;
  // A field whose bytes are observed at a different category keeps its
  // USAGE-faithful encoding (IBM LR "USAGE clause"); decode it accordingly.
  if(item.faithful_bytes)
  {
    switch(item.usage)
    {
    case usaget::DISPLAY:
      return decode_zoned(r);
    case usaget::PACKED:
      return decode_packed(r);
    case usaget::BINARY:
    case usaget::NATIVE_BINARY:
    case usaget::FLOAT_SHORT:
    case usaget::FLOAT_LONG:
      break; // no faithful codec yet; fall through to the binary model
    }
  }
  // Otherwise: uniform little-endian binary storage (documented deviation from
  // real zoned/packed/EBCDIC encodings; the byte SIZES follow IBM LR so that
  // REDEFINES overlap and group MOVE line up).
  const exprt phys = make_byte_extract(r.record, r.offset, phys_type(item));
  return valuet{typecast_exprt{phys, cobol_value_type()}, item.scale};
}

exprt cobol_typecheckt::encode_numeric(
  const item_infot &item,
  valuet v,
  bool rounded) const
{
  exprt e;
  if(rounded && v.scale > item.scale)
  {
    // ROUNDED: when fractional digits are discarded, round to nearest,
    // half away from zero (IBM LR "ROUNDED phrase", default mode
    // NEAREST-AWAY-FROM-ZERO). Truncating integer division gives this for
    // (x + d/2)/d when x >= 0 and (x - d/2)/d when x < 0.
    const mp_integer d = power10(v.scale - item.scale);
    const exprt zero = from_integer(0, cobol_value_type());
    const exprt half = from_integer(d / 2, cobol_value_type());
    const exprt adj = if_exprt{
      binary_relation_exprt{v.expr, ID_ge, zero},
      plus_exprt{v.expr, half},
      minus_exprt{v.expr, half}};
    e = div_exprt{adj, from_integer(d, cobol_value_type())};
  }
  else
    e = rescale(v.expr, v.scale, item.scale);
  // An unsigned receiver stores the absolute value; the sign is dropped
  // (IBM LR "MOVE statement" and the arithmetic statements: when the receiving
  // item is unsigned, the absolute value is stored). Applied before the
  // encoding so both the faithful codecs and the binary path see a magnitude.
  if(!item.is_signed)
    e = if_exprt{
      binary_relation_exprt{e, ID_ge, from_integer(0, cobol_value_type())},
      e,
      unary_minus_exprt{e}};
  // Faithful fields store the value in their USAGE encoding; e is already at
  // item.scale (its digits, decimal point removed).
  if(item.faithful_bytes)
  {
    switch(item.usage)
    {
    case usaget::DISPLAY:
      return encode_zoned(e, item);
    case usaget::PACKED:
      return encode_packed(e, item);
    case usaget::BINARY:
    case usaget::NATIVE_BINARY:
    case usaget::FLOAT_SHORT:
    case usaget::FLOAT_LONG:
      break;
    }
  }
  if(item.digits > 0 && item.digits <= 18)
    e = mod_exprt{e, from_integer(power10(item.digits), cobol_value_type())};
  return typecast_exprt{e, phys_type(item)};
}

void cobol_typecheckt::classify_record_aliases()
{
  // A numeric field whose bytes overlap an item of a different category (a
  // REDEFINES alias, group MOVE target, ...) must hold its USAGE-faithful
  // bytes so the other view sees IBM-faithful content. We currently support
  // the unsigned DISPLAY (zoned) encoding, so only those fields are marked;
  // others keep the binary value model (imprecise but unchanged). The byte
  // ranges are the elementary items' base ranges in the current record.
  std::vector<std::size_t> elem;
  for(std::size_t i = 0; i < all_items.size(); ++i)
    if(all_items[i].info.record_symbol == cur_record)
      elem.push_back(i);

  for(std::size_t a : elem)
  {
    item_infot &ia = all_items[a].info;
    if(
      ia.is_group || !ia.is_numeric || ia.is_table || ia.faithful_bytes ||
      !has_faithful_codec(ia))
      continue;
    const std::size_t a0 = ia.offset, a1 = ia.offset + ia.byte_size;
    for(std::size_t b : elem)
    {
      if(a == b)
        continue;
      const item_infot &ib = all_items[b].info;
      // A different-category observer: an alphanumeric/group item (or a
      // numeric of a different physical encoding) overlapping this field.
      const bool different_category =
        ib.is_group || !ib.is_numeric || ib.usage != ia.usage;
      const std::size_t b0 = ib.offset, b1 = ib.offset + ib.byte_size;
      if(different_category && a0 < b1 && b0 < a1)
      {
        ia.faithful_bytes = true;
        auto it = items.find(all_items[a].name);
        if(it != items.end() && it->second.offset == ia.offset)
          it->second.faithful_bytes = true;
        break;
      }
    }
  }
}

void cobol_typecheckt::finalize_record()
{
  if(cur_record.empty())
    return;

  // Close any still-open group frames.
  while(!layout_stack.empty())
  {
    layout_framet g = layout_stack.back();
    layout_stack.pop_back();
    const std::size_t gsize = g.cursor - g.start;
    if(items.count(g.name))
      items[g.name].byte_size = gsize;
    // Refresh the group's recorded size so subscript strides are correct.
    if(g.entry_index < all_items.size())
      all_items[g.entry_index].info.byte_size = gsize;
    const std::size_t occ =
      items.count(g.name) && items[g.name].is_table ? items[g.name].occurs : 1;
    record_max = std::max(record_max, g.start + gsize * occ);
    if(!g.is_redefines && !layout_stack.empty())
      layout_stack.back().cursor += gsize * occ;
  }

  const std::size_t size = std::max<std::size_t>(record_max, 1);
  record_sizes[cur_record] = size;

  // Decide which numeric fields keep their faithful (zoned) encoding before
  // any constant VALUE bytes are laid down, so a VALUE and the runtime
  // read/write of the same field agree.
  classify_record_aliases();

  // Encode the deferred numeric VALUEs now that faithfulness is known: a
  // faithful unsigned DISPLAY field is initialised with zoned ASCII bytes, an
  // ordinary field with little-endian binary.
  for(const auto &pv : pending_num_values)
  {
    const std::size_t off = std::get<0>(pv);
    const mp_integer val = std::get<1>(pv);
    const item_infot &info = all_items[std::get<2>(pv)].info;
    std::vector<unsigned char> b;
    if(info.faithful_bytes && info.usage == usaget::DISPLAY)
      b = zoned_bytes(val, info);
    else if(info.faithful_bytes && info.usage == usaget::PACKED)
      b = packed_bytes(val, info);
    else
    {
      const mp_integer modulus = power(mp_integer{2}, info.byte_size * 8);
      mp_integer u = val % modulus;
      if(u < 0)
        u += modulus;
      b.resize(info.byte_size);
      for(std::size_t k = 0; k < info.byte_size; ++k)
      {
        b[k] = static_cast<unsigned char>((u % 256).to_long());
        u /= 256;
      }
    }
    record_inits.emplace_back(off, std::move(b));
  }

  symbolt symbol{cur_record, record_type(cur_record), COBOL_MODE};
  symbol.base_name = cur_record_base;
  symbol.is_static_lifetime = true;
  symbol.is_lvalue = true;
  symbol.is_state_var = true;

  if(record_has_value)
  {
    std::vector<unsigned char> bytes(size, 0);
    for(const auto &write : record_inits)
      for(std::size_t b = 0; b < write.second.size() && write.first + b < size;
          ++b)
        bytes[write.first + b] = write.second[b];

    const unsignedbv_typet byte_type{8};
    array_exprt::operandst ops;
    ops.reserve(size);
    for(unsigned char b : bytes)
      ops.push_back(from_integer(b, byte_type));
    symbol.value =
      array_exprt{std::move(ops), to_array_type(record_type(cur_record))};
  }

  symbol_table.add(symbol);
  cur_record.clear();
  pending_num_values.clear();
}

exprt cobol_typecheckt::build_relation(
  valuet a,
  const std::string &op,
  valuet b)
{
  // Numeric comparison (IBM LR "Relation condition" / "Comparison of numeric
  // operands", pp. 268-): the operands are compared by algebraic value
  // regardless of their pictures or usages, so align the scales and emit the
  // bitvector relation.
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

const item_infot &cobol_typecheckt::resolve_item(
  const std::string &name,
  const std::vector<std::string> &quals)
{
  // Resolve a (possibly qualified) reference "name OF q1 OF q2 ..."
  // (IBM LR "Qualification", pp. 67-68): each qualifier must be a higher-level
  // name in the same hierarchy, and enough qualifiers must be given to make
  // the reference unique.
  std::vector<const entryt *> candidates;
  for(const entryt &e : all_items)
    if(e.name == name)
      candidates.push_back(&e);

  if(candidates.empty())
    error("unknown data item '" + name + "'");
  if(candidates.size() == 1)
    return candidates.front()->info;

  // Filter by qualifiers: each qualifier must appear among the candidate's
  // ancestors (not necessarily at a consecutive level).
  std::vector<const entryt *> matching;
  for(const entryt *c : candidates)
  {
    bool ok = true;
    for(const std::string &q : quals)
      if(
        std::find(c->ancestors.begin(), c->ancestors.end(), q) ==
        c->ancestors.end())
      {
        ok = false;
        break;
      }
    if(ok)
      matching.push_back(c);
  }

  if(matching.size() == 1)
    return matching.front()->info;
  if(matching.empty())
    error("no '" + name + "' matches the given qualification");

  // Still ambiguous: COBOL would reject this, but to keep parsing we take the
  // first match and warn.
  log.warning() << "COBOL: reference to '" << name
                << "' is ambiguous; using the first definition"
                << messaget::eom;
  return matching.front()->info;
}

std::size_t cobol_typecheckt::paragraph_index(const std::string &name)
{
  for(std::size_t i = 0; i < paragraphs.size(); ++i)
    if(paragraphs[i].name == name)
      return i;
  error("unknown paragraph '" + name + "'");
}

std::size_t cobol_typecheckt::range_end(const std::string &name)
{
  const std::size_t i = paragraph_index(name);
  if(!paragraphs[i].is_section)
    return i;
  // A section runs through the paragraph just before the next section header
  // (IBM LR "PERFORM statement": performing a section-name executes all of its
  // paragraphs).
  for(std::size_t j = i + 1; j < paragraphs.size(); ++j)
    if(paragraphs[j].is_section)
      return j - 1;
  return paragraphs.size() - 1;
}

void cobol_typecheckt::collect_perform_targets(
  const std::vector<stmtt> &stmts,
  std::set<std::size_t> &entries,
  std::set<std::size_t> &exits)
{
  for(const stmtt &s : stmts)
  {
    if(s.kind == stmtt::kindt::PERFORM && !s.inline_body)
    {
      entries.insert(paragraph_index(s.target));
      exits.insert(range_end(s.target_end.empty() ? s.target : s.target_end));
    }
    // Recurse into every nested statement list so a PERFORM inside an IF,
    // EVALUATE, SEARCH or an inline PERFORM body is also accounted for.
    collect_perform_targets(s.then_stmts, entries, exits);
    collect_perform_targets(s.else_stmts, entries, exits);
    collect_perform_targets(s.other_stmts, entries, exits);
    collect_perform_targets(s.body, entries, exits);
    collect_perform_targets(s.var_init, entries, exits);
    collect_perform_targets(s.var_step, entries, exits);
    for(const auto &w : s.when_clauses)
      collect_perform_targets(w.second, entries, exits);
  }
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
  all_items.clear();
  conds.clear();
  table_indexes.clear();
  file_records.clear();
  pending_file.clear();
  paragraphs.clear();
  last_field.clear();

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

  // Provide the CICS EXEC INTERFACE BLOCK so EIB* references resolve.
  inject_eib();

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
    else if(is_word("FD") || is_word("SD"))
    {
      // A file/sort description entry: skip its clauses (RECORDING MODE,
      // RECORD [IS VARYING ... DEPENDING ON ...], BLOCK CONTAINS, LABEL
      // RECORDS, ...) up to the terminating separator period. The record
      // descriptions that follow are parsed as ordinary records (IBM LR
      // "File description entry"). This avoids interpreting numeric tokens in
      // the clauses (e.g. FROM 10 TO 80) as level numbers.
      advance();
      // Remember the file-name so the record description that follows can be
      // associated with it (used by READ to havoc the file's record area).
      pending_file = cur().kind == cobol_token_kindt::WORD ? cur().text : "";
      while(!at_eof() && !is_kind(cobol_token_kindt::PERIOD) &&
            !is_word("PROCEDURE"))
        advance();
      if(is_kind(cobol_token_kindt::PERIOD))
        advance();
    }
    else if(is_word("EXEC"))
    {
      // An EXEC SQL DECLARE ... TABLE ... END-EXEC block (as produced by
      // DCLGEN ahead of the host-variable structure) is a declaration for the
      // SQL precompiler, not COBOL data. Skip it to its END-EXEC terminator so
      // that numeric tokens inside it (e.g. CHAR(2)) are not taken as level
      // numbers; the data description that follows is parsed normally.
      while(!at_eof() && !is_word("END-EXEC") && !is_word("PROCEDURE"))
        advance();
      if(is_word("END-EXEC"))
        advance();
      if(is_kind(cobol_token_kindt::PERIOD))
        advance();
    }
    else if(cur().kind == cobol_token_kindt::NUMBER)
    {
      const std::size_t level =
        static_cast<std::size_t>(std::stoul(cur().text));
      if(level == 1 || level == 77)
      {
        // A 01/77-level REDEFINES of the record just built shares that
        // record's storage (IBM LR "REDEFINES clause"): keep the same record
        // byte-array symbol and place the redefining entry as a redefinition
        // at offset 0, instead of starting a new (independent) record. The
        // REDEFINES clause must immediately follow the data-name, so the
        // tokens are <level> <name> REDEFINES <current-record-name>.
        const bool redefines_current =
          !cur_record.empty() && peek(2).kind == cobol_token_kindt::WORD &&
          peek(2).text == "REDEFINES" &&
          peek(3).kind == cobol_token_kindt::WORD &&
          peek(3).text == cur_record_base;
        if(redefines_current)
        {
          // Flush the current record's open group frames (finalising their
          // sizes and record_max) but keep cur_record / record_max /
          // record_inits / pending_num_values, so the redefining entry aliases
          // the same storage. parse_data_item places it via its REDEFINES
          // target, which resolves to offset 0.
          close_groups_below(1);
        }
        else
        {
          // A new record begins; close the previous one and open this.
          finalize_record();
          const std::string rname =
            peek(1).kind == cobol_token_kindt::WORD ? peek(1).text : "FILLER";
          cur_record_base = rname;
          cur_record = "cobol::" + program_id + "::" + rname;
          // Associate the FD's file-name with this record (its 01 record area).
          if(!pending_file.empty())
          {
            file_records[pending_file] = rname;
            pending_file.clear();
          }
          layout_stack.clear();
          record_max = 0;
          record_inits.clear();
          record_has_value = false;
          pending_num_values.clear();
        }
      }
      parse_data_item();
    }
    else
    {
      advance();
    }
  }
  finalize_record();
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
  // A VALUE / 88-level literal: a numeric or alphanumeric literal (optionally
  // ALL), or a figurative constant (IBM LR "VALUE clause" and "Figurative
  // constants", pp. 23-26: ZERO/SPACE/HIGH-VALUE/LOW-VALUE/QUOTE/NULL).
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
  // Build the n-byte alphanumeric constant for a literal or figurative
  // constant (IBM LR "Figurative constants" / "VALUE clause"): SPACE=0x20,
  // ZERO='0', QUOTE='"', HIGH-VALUE=0xFF, LOW-VALUE=0x00 (ASCII host); a
  // shorter literal is left-justified and space-padded on the right.
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
    if(last_field.empty())
      error("88-level without a preceding elementary item");
    const item_infot parent = items.at(last_field);
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

  if(level == 66)
  {
    // 66 new-name RENAMES item-1 [{THRU|THROUGH} item-2]: an alphanumeric
    // alias over the contiguous storage from the start of item-1 to the end of
    // item-2 (IBM LR "RENAMES clause"). It overlays existing storage and does
    // not take part in the record layout.
    expect_word("RENAMES");
    if(cur().kind != cobol_token_kindt::WORD)
      error("expected a data name after RENAMES");
    const item_infot first = lookup_item(cur().text);
    advance();
    std::size_t start = first.offset;
    std::size_t end = first.offset + first.byte_size;
    if(eat_word("THRU") || eat_word("THROUGH"))
    {
      if(cur().kind != cobol_token_kindt::WORD)
        error("expected a data name after RENAMES ... THRU");
      const item_infot last = lookup_item(cur().text);
      advance();
      end = last.offset + last.byte_size;
    }
    item_infot info;
    info.record_symbol = first.record_symbol;
    info.offset = start;
    info.byte_size = end > start ? end - start : 1;
    info.is_numeric = false;
    info.char_count = info.byte_size;
    items[name] = info;
    all_items.push_back(entryt{name, info, {cur_record_base}});
    expect_period();
    return;
  }

  std::string pic;
  bool has_pic = false;
  bool has_value = false;
  bool is_table = false;
  std::size_t occurs = 0;
  std::string usage = "DISPLAY";
  bool sign_leading = false;  ///< SIGN IS LEADING (default TRAILING)
  bool sign_separate = false; ///< SIGN ... SEPARATE [CHARACTER]
  std::string redefines_target;
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
      // OCCURS [integer-1 TO] integer-2 TIMES [DEPENDING ON data-name]: a
      // variable-length table (IBM LR "OCCURS clause", format 2). We model it
      // as a fixed table of its maximum (integer-2) occurrences, a sound
      // over-approximation; the DEPENDING ON item is an ordinary numeric field
      // the program maintains.
      if(eat_word("TO"))
      {
        if(cur().kind != cobol_token_kindt::NUMBER)
          error("expected a maximum count after OCCURS ... TO");
        occurs = static_cast<std::size_t>(std::stoul(cur().text));
        advance();
      }
      eat_word("TIMES");
      if(eat_word("DEPENDING"))
      {
        eat_word("ON");
        if(cur().kind == cobol_token_kindt::WORD)
          advance(); // the DEPENDING ON data-name
      }
      is_table = true;
    }
    else if(eat_word("REDEFINES"))
    {
      if(cur().kind != cobol_token_kindt::WORD)
        error("expected a data name after REDEFINES");
      redefines_target = cur().text;
      advance();
    }
    else if(eat_word("USAGE"))
    {
      eat_word("IS");
      if(cur().kind == cobol_token_kindt::WORD)
      {
        usage = cur().text;
        advance();
      }
    }
    else if(
      is_word("COMP") || is_word("COMP-1") || is_word("COMP-2") ||
      is_word("COMP-3") || is_word("COMP-4") || is_word("COMP-5") ||
      is_word("COMP-6") || is_word("BINARY") || is_word("PACKED-DECIMAL") ||
      is_word("COMPUTATIONAL") || is_word("COMPUTATIONAL-1") ||
      is_word("COMPUTATIONAL-2") || is_word("COMPUTATIONAL-3") ||
      is_word("COMPUTATIONAL-4") || is_word("COMPUTATIONAL-5") ||
      is_word("DISPLAY"))
    {
      usage = cur().text;
      advance();
    }
    else if(eat_word("INDEXED"))
    {
      // OCCURS ... INDEXED BY index-name-1 [index-name-2] ... (IBM LR "INDEXED
      // BY phrase"). Each index-name is registered as an index data item.
      eat_word("BY");
      while(cur().kind == cobol_token_kindt::WORD &&
            !is_clause_keyword(cur().text))
      {
        register_index(cur().text);
        // Associate the index with its table so SEARCH can vary it.
        table_indexes[name].push_back(cur().text);
        advance();
      }
    }
    else if(is_word("ASCENDING") || is_word("DESCENDING"))
    {
      // ASCENDING/DESCENDING KEY IS data-name ... (search key for SEARCH ALL);
      // consumed but not otherwise modelled.
      advance();
      eat_word("KEY");
      eat_word("IS");
      while(cur().kind == cobol_token_kindt::WORD &&
            !is_clause_keyword(cur().text))
        advance();
    }
    else if(is_word("SIGN") || is_word("LEADING") || is_word("TRAILING"))
    {
      // [SIGN IS] {LEADING | TRAILING} [SEPARATE CHARACTER] (IBM LR "SIGN
      // clause"). The SIGN keyword is optional; default is TRAILING overpunch.
      eat_word("SIGN");
      eat_word("IS");
      if(eat_word("LEADING"))
        sign_leading = true;
      else if(eat_word("TRAILING"))
        sign_leading = false;
      if(eat_word("SEPARATE"))
      {
        sign_separate = true;
        eat_word("CHARACTER");
      }
    }
    else
    {
      // SYNC / JUSTIFIED / BLANK WHEN ZERO / ... : consume one token.
      advance();
    }
  }
  expect_period();

  place_field(
    level,
    name,
    has_pic,
    pic,
    usage,
    sign_leading,
    sign_separate,
    is_table,
    occurs,
    redefines_target,
    has_value,
    value_spec,
    loc);
}

// ---------------------------------------------------------------------------
// expressions
// ---------------------------------------------------------------------------

void cobol_typecheckt::close_groups_below(std::size_t level)
{
  while(!layout_stack.empty() && layout_stack.back().level >= level)
  {
    layout_framet g = layout_stack.back();
    layout_stack.pop_back();
    const std::size_t gsize = g.cursor - g.start;
    if(items.count(g.name))
      items[g.name].byte_size = gsize;
    // Refresh the group's recorded size so subscript strides are correct.
    if(g.entry_index < all_items.size())
      all_items[g.entry_index].info.byte_size = gsize;
    const std::size_t occ =
      items.count(g.name) && items[g.name].is_table ? items[g.name].occurs : 1;
    record_max = std::max(record_max, g.start + gsize * occ);
    if(!g.is_redefines && !layout_stack.empty())
      layout_stack.back().cursor += gsize * occ;
  }
}

void cobol_typecheckt::place_field(
  std::size_t level,
  const std::string &name,
  bool has_pic,
  const std::string &pic,
  const std::string &usage,
  bool sign_leading,
  bool sign_separate,
  bool is_table,
  std::size_t occurs,
  const std::string &redefines_target,
  bool has_value,
  const value_spect &value_spec,
  const source_locationt &loc)
{
  (void)loc;
  close_groups_below(level);

  // Ancestors (containing groups, innermost first) for qualified-name
  // resolution (IBM LR "Qualification", pp. 67-68).
  std::vector<std::string> ancestors;
  for(auto it = layout_stack.rbegin(); it != layout_stack.rend(); ++it)
    if(!it->name.empty())
      ancestors.push_back(it->name);

  // Subscript dimensions: the enclosing OCCURS groups, outermost first
  // (IBM LR "Subscripting": one subscript per OCCURS, written in order of
  // successively less inclusive dimensions).
  std::vector<std::size_t> occurs_dims;
  for(const layout_framet &frame : layout_stack)
    if(frame.is_occurs)
      occurs_dims.push_back(frame.entry_index);

  const bool is_redefines = !redefines_target.empty();
  std::size_t base_offset;
  if(is_redefines)
  {
    auto it = items.find(redefines_target);
    base_offset = it == items.end()
                    ? (layout_stack.empty() ? 0 : layout_stack.back().cursor)
                    : it->second.offset;
  }
  else
    base_offset = layout_stack.empty() ? 0 : layout_stack.back().cursor;

  item_infot info;
  info.record_symbol = cur_record;
  info.offset = base_offset;
  info.is_table = is_table;
  info.occurs = occurs;
  info.occurs_dims = occurs_dims;

  if(!has_pic)
  {
    // Group item: its byte_size is the span of its children, set at close.
    info.is_group = true;
    info.is_numeric = false;
    items[name] = info;
    const std::size_t entry_index = all_items.size();
    all_items.push_back(entryt{name, info, ancestors});
    layout_framet frame;
    frame.level = level;
    frame.name = name;
    frame.start = base_offset;
    frame.cursor = base_offset;
    frame.is_redefines = is_redefines;
    frame.is_occurs = is_table;
    frame.entry_index = entry_index;
    layout_stack.push_back(frame);
    return;
  }

  bool is_numeric;
  std::size_t digits, scale, char_count;
  bool is_signed;
  parse_picture(pic, is_numeric, digits, scale, is_signed, char_count);
  info.is_group = false;
  info.is_numeric = is_numeric;
  info.digits = digits;
  info.scale = scale;
  info.is_signed = is_signed;
  info.char_count = char_count;
  info.byte_size = phys_size_of(usage, is_numeric, digits, char_count);
  // Record the physical encoding and sign representation (IBM LR "USAGE
  // clause" / "SIGN clause") so byte-level observation can be faithful.
  info.usage = usage_of(usage);
  if(!is_signed)
    info.sign = signt::UNSIGNED;
  else if(sign_separate)
    info.sign =
      sign_leading ? signt::SEPARATE_LEADING : signt::SEPARATE_TRAILING;
  else
    info.sign =
      sign_leading ? signt::OVERPUNCH_LEADING : signt::OVERPUNCH_TRAILING;

  const std::size_t total = info.byte_size * (is_table ? occurs : 1);
  record_max = std::max(record_max, base_offset + total);
  if(!is_redefines && !layout_stack.empty())
    layout_stack.back().cursor += total;

  items[name] = info;
  all_items.push_back(entryt{name, info, ancestors});
  last_field = name;

  // VALUE initialisation (single occurrence only).
  if(has_value && !is_table)
  {
    record_has_value = true;
    if(is_numeric)
    {
      if(auto v = spec_to_numeric(value_spec, scale))
      {
        // Defer to finalize_record: the byte encoding (zoned vs binary)
        // depends on whether the alias classifier marks this field faithful,
        // which is only known once the whole record has been seen.
        pending_num_values.emplace_back(base_offset, *v, all_items.size() - 1);
      }
    }
    else
    {
      const exprt c = make_alnum_constant(value_spec, char_count);
      std::vector<unsigned char> bytes;
      bytes.reserve(c.operands().size());
      for(const exprt &op : c.operands())
      {
        const auto n = numeric_cast<mp_integer>(op);
        bytes.push_back(
          static_cast<unsigned char>(n.has_value() ? n->to_long() : 0));
      }
      record_inits.emplace_back(base_offset, std::move(bytes));
    }
  }
}

// ---------------------------------------------------------------------------
// expressions
// ---------------------------------------------------------------------------

reft cobol_typecheckt::parse_ref()
{
  const std::string name = cur().text;
  advance();
  // Optional qualification: name OF/IN qualifier OF/IN qualifier ...
  // (IBM LR "Qualification", pp. 67-68; IN and OF are equivalent).
  std::vector<std::string> quals;
  while(is_word("OF") || is_word("IN"))
  {
    advance();
    if(cur().kind != cobol_token_kindt::WORD)
      error("expected a qualifier name after OF/IN");
    quals.push_back(cur().text);
    advance();
  }
  const item_infot &item = resolve_item(name, quals);
  const item_infot *info = &item;
  exprt offset = from_integer(item.offset, size_type());

  const auto is_colon = [&]()
  { return cur().kind == cobol_token_kindt::PUNCT && cur().text == ":"; };

  // Reference modification data-name(start:length) selects a substring
  // (IBM LR "Reference modification"): the result is an alphanumeric item of
  // `length` characters starting at 1-based character position `start`.
  const auto apply_refmod =
    [&](const valuet &start, const std::optional<valuet> &len)
  {
    offset = plus_exprt{
      offset,
      typecast_exprt{
        minus_exprt{
          rescale(start.expr, start.scale, 0),
          from_integer(1, cobol_value_type())},
        size_type()}};
    std::size_t length;
    const auto start_const = numeric_cast<mp_integer>(start.expr);
    if(len.has_value())
    {
      const auto c = numeric_cast<mp_integer>(len->expr);
      length =
        c.has_value() ? numeric_cast_v<std::size_t>(*c) : info->byte_size;
    }
    else if(
      start_const.has_value() &&
      info->byte_size >= numeric_cast_v<std::size_t>(*start_const) - 1 + 1)
    {
      // Length omitted: from `start` to the end of the item.
      length =
        info->byte_size - (numeric_cast_v<std::size_t>(*start_const) - 1);
    }
    else
      length = info->byte_size;
    if(length == 0)
      length = 1;

    item_infot synth = *info;
    synth.is_group = false;
    synth.is_numeric = false;
    synth.is_table = false;
    synth.occurs = 0;
    synth.occurs_dims.clear();
    synth.byte_size = length;
    synth.char_count = length;
    synth.digits = 0;
    synth.scale = 0;
    synth_items.push_back(synth);
    info = &synth_items.back();
  };

  if(is_kind(cobol_token_kindt::LPAREN))
  {
    advance();
    valuet first = parse_expr();
    if(is_colon())
    {
      // Reference modification with no subscripts.
      advance();
      std::optional<valuet> len;
      if(!is_kind(cobol_token_kindt::RPAREN))
        len = parse_expr();
      if(!is_kind(cobol_token_kindt::RPAREN))
        error("expected ')' after reference modification");
      advance();
      apply_refmod(first, len);
    }
    else
    {
      // Subscript strides, outermost first: the enclosing OCCURS groups then
      // this item's own OCCURS dimension (IBM LR "Subscripting": one subscript
      // per OCCURS, in order of successively less inclusive dimensions).
      std::vector<std::size_t> strides;
      for(std::size_t dim : item.occurs_dims)
        if(dim < all_items.size())
          strides.push_back(all_items[dim].info.byte_size);
      if(item.is_table)
        strides.push_back(item.byte_size);
      if(strides.empty())
        error("subscript on a non-table item");

      std::vector<valuet> subs;
      subs.push_back(first);
      while(!is_kind(cobol_token_kindt::RPAREN) && !at_eof())
        subs.push_back(parse_expr());
      if(!is_kind(cobol_token_kindt::RPAREN))
        error("expected ')' after subscript");
      advance();

      if(subs.size() != strides.size())
        error(
          "wrong number of subscripts for '" + name + "' (expected " +
          std::to_string(strides.size()) + ")");

      // COBOL subscripts are 1-based; offset += sum_k (subscript_k - 1)*stride.
      for(std::size_t k = 0; k < subs.size(); ++k)
      {
        const exprt idx0 = minus_exprt{
          typecast_exprt{rescale(subs[k].expr, subs[k].scale, 0), size_type()},
          from_integer(1, size_type())};
        offset = plus_exprt{
          offset, mult_exprt{idx0, from_integer(strides[k], size_type())}};
      }

      // Optional trailing reference modification: data-name(subs)(start:len).
      if(is_kind(cobol_token_kindt::LPAREN))
      {
        advance();
        valuet start = parse_expr();
        if(!is_colon())
          error("expected ':' in reference modification");
        advance();
        std::optional<valuet> len;
        if(!is_kind(cobol_token_kindt::RPAREN))
          len = parse_expr();
        if(!is_kind(cobol_token_kindt::RPAREN))
          error("expected ')' after reference modification");
        advance();
        apply_refmod(start, len);
      }
    }
  }
  return reft{info, record_expr(item.record_symbol), offset};
}

valuet cobol_typecheckt::parse_primary()
{
  if(at_intrinsic())
  {
    last_ref.reset();
    cond_operandt o = parse_intrinsic();
    if(!o.numeric)
      error("alphanumeric intrinsic used in an arithmetic expression");
    return o.num;
  }
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
    last_ref.reset();
    auto r = parse_decimal(cur().text);
    advance();
    return valuet{from_integer(r.first, cobol_value_type()), r.second};
  }
  // The figurative constant ZERO/ZEROS is the numeric value 0 in an arithmetic
  // context (IBM LR "Figurative constants").
  if(
    cur().kind == cobol_token_kindt::WORD &&
    (cur().text == "ZERO" || cur().text == "ZEROS" || cur().text == "ZEROES"))
  {
    last_ref.reset();
    advance();
    return valuet{from_integer(0, cobol_value_type()), 0};
  }
  if(cur().kind == cobol_token_kindt::WORD)
  {
    const std::string nm = cur().text;
    reft r = parse_ref();
    if(!r.info->is_numeric)
      error("non-numeric item '" + nm + "' used in expression");
    last_ref = r;
    return read_field(r);
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
  return parse_term_from(parse_factor());
}

valuet cobol_typecheckt::parse_term_from(valuet a)
{
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
  return parse_expr_from(parse_term());
}

valuet cobol_typecheckt::parse_expr_from(valuet a)
{
  a = parse_term_from(a);
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

bool cobol_typecheckt::at_operand() const
{
  if(
    cur().kind == cobol_token_kindt::NUMBER ||
    cur().kind == cobol_token_kindt::LPAREN)
    return true;
  if(
    cur().kind == cobol_token_kindt::PUNCT &&
    (cur().text == "+" || cur().text == "-"))
    return true;
  if(at_intrinsic())
    return true;
  if(cur().kind == cobol_token_kindt::WORD)
    return is_item_word() || cur().text == "ZERO" || cur().text == "ZEROS" ||
           cur().text == "ZEROES";
  return false;
}

std::vector<valuet> cobol_typecheckt::parse_operand_list()
{
  std::vector<valuet> result;
  while(at_operand())
    result.push_back(parse_operand());
  return result;
}

// ---------------------------------------------------------------------------
// conditions
// ---------------------------------------------------------------------------

std::string cobol_typecheckt::parse_relop()
{
  // Relational operator only; a leading IS and a leading NOT are handled by
  // parse_relation (IBM LR "Relation condition": the operators may be written
  // as symbols or as the reserved words GREATER/LESS/EQUAL ...).
  if(cur().kind == cobol_token_kindt::PUNCT)
  {
    const std::string t = cur().text;
    if(t == "=" || t == "<" || t == ">" || t == "<=" || t == ">=" || t == "<>")
    {
      advance();
      return t;
    }
  }

  if(eat_word("GREATER"))
  {
    eat_word("THAN");
    if(eat_word("OR"))
    {
      expect_word("EQUAL");
      eat_word("TO");
      return ">=";
    }
    return ">";
  }
  if(eat_word("LESS"))
  {
    eat_word("THAN");
    if(eat_word("OR"))
    {
      expect_word("EQUAL");
      eat_word("TO");
      return "<=";
    }
    return "<";
  }
  if(eat_word("EQUAL") || eat_word("EQUALS"))
  {
    eat_word("TO");
    return "=";
  }
  if(eat_word("EXCEEDS"))
    return ">";
  error("expected a relational operator");
}

/// Reverse a relational operator (for a NOT-negated relation).
static std::string negate_relop(const std::string &op)
{
  if(op == "=")
    return "<>";
  if(op == "<>")
    return "=";
  if(op == "<")
    return ">=";
  if(op == ">")
    return "<=";
  if(op == "<=")
    return ">";
  if(op == ">=")
    return "<";
  return op;
}

reft cobol_typecheckt::apply_cond_subscript(const item_infot &item, reft r)
{
  // Apply OCCURS subscripts to a condition variable's reference, using the
  // same 1-based stride arithmetic as parse_ref (IBM LR "Subscripting").
  // Assumes the current token is the opening parenthesis.
  advance();
  std::vector<std::size_t> strides;
  for(std::size_t dim : item.occurs_dims)
    if(dim < all_items.size())
      strides.push_back(all_items[dim].info.byte_size);
  if(item.is_table)
    strides.push_back(item.byte_size);
  if(strides.empty())
    error("subscript on a non-table condition variable");

  std::vector<valuet> subs;
  subs.push_back(parse_expr());
  while(!is_kind(cobol_token_kindt::RPAREN) && !at_eof())
    subs.push_back(parse_expr());
  if(!is_kind(cobol_token_kindt::RPAREN))
    error("expected ')' after subscript");
  advance();
  if(subs.size() != strides.size())
    error("wrong number of subscripts for a condition variable");

  for(std::size_t k = 0; k < subs.size(); ++k)
  {
    const exprt idx0 = minus_exprt{
      typecast_exprt{rescale(subs[k].expr, subs[k].scale, 0), size_type()},
      from_integer(1, size_type())};
    r.offset = plus_exprt{
      r.offset, mult_exprt{idx0, from_integer(strides[k], size_type())}};
  }
  return r;
}

exprt cobol_typecheckt::cond_predicate(
  const std::string &name,
  const reft &parent_ref)
{
  const cond_infot &c = conds.at(name);

  if(!c.parent.is_numeric)
  {
    // Alphanumeric parent: OR of equalities against the byte-array constants,
    // comparing the field's bytes via byte_extract.
    const array_typet parent_type{
      unsignedbv_typet{8}, from_integer(c.parent.byte_size, size_type())};
    const exprt parent =
      make_byte_extract(parent_ref.record, parent_ref.offset, parent_type);
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

  const exprt parent = read_field(parent_ref).expr;
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
    // The conditional variable of a condition-name may itself be subscripted
    // (IBM LR "Condition-name"): SELECT-OK(I) tests the I-th table element.
    const cond_infot &c = conds.at(name);
    reft parent_ref = ref_of(c.parent);
    if(is_kind(cobol_token_kindt::LPAREN))
      parent_ref = apply_cond_subscript(c.parent, parent_ref);
    return cond_predicate(name, parent_ref);
  }

  cond_operandt a = parse_cond_operand();

  // An optional "IS" precedes the operator or the class/sign keyword
  // (IBM LR "Conditional expressions").
  eat_word("IS");
  const bool neg = eat_word("NOT");

  // Class condition: identifier IS [NOT] NUMERIC | ALPHABETIC[-LOWER|-UPPER]
  // (IBM LR "Class condition", p. 269). The test inspects the item's character
  // content. For an alphanumeric item the bytes are exactly its content (the
  // byte-array storage), so the test is built exactly: each byte must lie in
  // the class's character range. For a numeric operand the value-domain model
  // does not expose faithful character bytes, so it stays a nondeterministic
  // Boolean (a sound over-approximation).
  if(
    is_word("NUMERIC") || is_word("ALPHABETIC") ||
    is_word("ALPHABETIC-LOWER") || is_word("ALPHABETIC-UPPER") ||
    is_word("ALPHANUMERIC"))
  {
    const std::string cls = cur().text;
    const source_locationt loc = cur().location;
    advance();
    const signedbv_typet ct{32};
    const auto code = [&](char ch) { return from_integer(ch, ct); };
    const auto in_range = [&](const exprt &b, char lo, char hi) -> exprt
    {
      return and_exprt{
        binary_relation_exprt{b, ID_ge, code(lo)},
        binary_relation_exprt{b, ID_le, code(hi)}};
    };
    if(
      a.is_item && (cls == "NUMERIC" || cls == "ALPHABETIC" ||
                    cls == "ALPHABETIC-LOWER" || cls == "ALPHABETIC-UPPER"))
    {
      exprt acc = true_exprt{};
      for(std::size_t i = 0; i < a.length; ++i)
      {
        const exprt off =
          plus_exprt{a.offset, from_integer(i, a.offset.type())};
        const exprt b = typecast_exprt{
          make_byte_extract(a.record, off, unsignedbv_typet{8}), ct};
        exprt ok;
        if(cls == "NUMERIC")
          ok = in_range(b, '0', '9');
        else if(cls == "ALPHABETIC")
          ok = or_exprt{
            or_exprt{in_range(b, 'A', 'Z'), in_range(b, 'a', 'z')},
            equal_exprt{b, code(' ')}};
        else if(cls == "ALPHABETIC-UPPER")
          ok = or_exprt{in_range(b, 'A', 'Z'), equal_exprt{b, code(' ')}};
        else // ALPHABETIC-LOWER
          ok = or_exprt{in_range(b, 'a', 'z'), equal_exprt{b, code(' ')}};
        acc = and_exprt{acc, ok};
      }
      return neg ? static_cast<exprt>(not_exprt{acc}) : acc;
    }
    // Numeric operand, the user-class ALPHANUMERIC, or a non-item: the value
    // model has no faithful bytes, so the result is nondeterministic.
    (void)neg;
    return side_effect_expr_nondett{bool_typet{}, loc};
  }

  // Sign condition: identifier IS [NOT] POSITIVE | NEGATIVE | ZERO
  // (IBM LR "Sign condition", p. 283). This tests the algebraic value, which
  // the value-domain model represents exactly.
  if(is_word("POSITIVE") || is_word("NEGATIVE") || is_word("ZERO"))
  {
    const std::string sign = cur().text;
    advance();
    if(!a.numeric)
      error("sign condition requires a numeric operand");
    const exprt zero = from_integer(0, cobol_value_type());
    exprt c;
    if(sign == "POSITIVE")
      c = binary_relation_exprt{a.num.expr, ID_gt, zero};
    else if(sign == "NEGATIVE")
      c = binary_relation_exprt{a.num.expr, ID_lt, zero};
    else
      c = equal_exprt{a.num.expr, zero};
    return neg ? static_cast<exprt>(not_exprt{c}) : c;
  }

  // Relation condition, or a value-only abbreviated term (subject and operator
  // implied from the preceding relation; IBM LR p. 287).
  if(!is_relop_start() && have_abbr)
    return build_cond_relation(abbr_subject, abbr_op, std::move(a));

  std::string op = parse_relop();
  if(neg)
    op = negate_relop(op);
  cond_operandt b = parse_cond_operand();
  // Record the subject and operator for any following abbreviated terms.
  abbr_subject = a;
  abbr_op = op;
  have_abbr = true;
  return build_cond_relation(std::move(a), op, std::move(b));
}

exprt cobol_typecheckt::build_cond_relation(
  cond_operandt a,
  const std::string &op,
  cond_operandt b)
{
  // A figurative constant such as ZERO compared with a numeric operand is
  // numeric (value 0); SPACES etc. are alphanumeric.
  const auto numeric_like = [](const cond_operandt &o)
  {
    return o.numeric || (o.is_spec && o.spec.kind == value_spect::kindt::ZEROS);
  };
  const auto to_numeric = [](const cond_operandt &o) {
    return o.numeric ? o.num : valuet{from_integer(0, cobol_value_type()), 0};
  };

  if((a.numeric || b.numeric) && numeric_like(a) && numeric_like(b))
    return build_relation(to_numeric(a), op, to_numeric(b));

  // Comparison of a numeric operand with a nonnumeric one: the numeric operand
  // is compared by its display representation (IBM LR "Comparison of numeric
  // and nonnumeric operands"). The value-domain model does not represent that
  // representation, so the result is nondeterministic (sound).
  if(a.numeric || b.numeric)
    return side_effect_expr_nondett{bool_typet{}, source_locationt{}};

  return build_alnum_relation(a, op, b);
}

cond_operandt cobol_typecheckt::parse_cond_operand()
{
  cond_operandt op;
  if(at_intrinsic())
    return parse_intrinsic();
  if(cur().kind == cobol_token_kindt::STRING)
  {
    op.is_spec = true;
    op.spec = read_value_spec();
    return op;
  }
  if(
    cur().kind == cobol_token_kindt::WORD &&
    (is_figurative(cur().text) || cur().text == "ALL"))
  {
    op.is_spec = true;
    op.spec = read_value_spec();
    return op;
  }
  if(
    cur().kind == cobol_token_kindt::WORD &&
    items.find(cur().text) != items.end())
  {
    // Parse the full reference first (qualifiers, subscripts and reference
    // modification) and classify by the *result*: reference modification
    // always yields an alphanumeric data item, even over a numeric base (IBM
    // LR "Reference modification"), so the base item's category must not be
    // used to decide numeric vs alphanumeric.
    const reft r = parse_ref();
    if(r.info->is_numeric)
    {
      // A numeric operand may begin an arithmetic expression (e.g. A + B in a
      // relation condition), so continue the expression from this value.
      op.numeric = true;
      op.num = parse_expr_from(read_field(r));
      return op;
    }
    op.is_item = true;
    op.record = r.record;
    op.offset = r.offset;
    op.length = r.info->byte_size;
    op.item = r.info;
    return op;
  }
  op.numeric = true;
  op.num = parse_expr();
  return op;
}

bool cobol_typecheckt::at_intrinsic() const
{
  if(cur().kind != cobol_token_kindt::WORD)
    return false;
  const std::string &w = cur().text;
  if(w == "FUNCTION")
    return true;
  if(
    w == "LENGTH" && peek(1).kind == cobol_token_kindt::WORD &&
    peek(1).text == "OF")
    return true;
  if(
    (w == "DFHRESP" || w == "DFHVALUE") &&
    peek(1).kind == cobol_token_kindt::LPAREN)
    return true;
  return false;
}

/// A fresh nondeterministic alphanumeric operand of \p len bytes, backed by a
/// unique uninitialised (hence nondet) scratch record.
cond_operandt cobol_typecheckt::nondet_alnum_operand(std::size_t len)
{
  if(len == 0)
    len = 1;
  const std::string base = "$intr" + std::to_string(unique++);
  const irep_idt rec = "cobol::" + program_id + "::" + base;
  record_sizes[rec] = len;
  symbolt symbol{rec, record_type(rec), COBOL_MODE};
  symbol.base_name = base;
  symbol.is_static_lifetime = true;
  symbol.is_lvalue = true;
  symbol.is_state_var = true;
  symbol_table.add(symbol);

  item_infot synth;
  synth.record_symbol = rec;
  synth.byte_size = len;
  synth.char_count = len;
  synth.is_numeric = false;
  synth_items.push_back(synth);

  cond_operandt op;
  op.is_item = true;
  op.record = record_expr(rec);
  op.offset = from_integer(0, size_type());
  op.length = len;
  op.item = &synth_items.back();
  return op;
}

valuet
cobol_typecheckt::numval_of_string(const std::string &s, bool currency) const
{
  // Convert the character string to a number (IBM LR "NUMVAL"/"NUMVAL-C"):
  // ignore spaces; honour a leading or trailing sign (and a trailing CR/DB,
  // for NUMVAL-C, as negative); for NUMVAL-C also ignore the currency sign and
  // grouping commas. Digits after a decimal point set the scale.
  std::string upper = s;
  for(char &c : upper)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  bool negative = upper.find('-') != std::string::npos ||
                  (currency && (upper.find("CR") != std::string::npos ||
                                upper.find("DB") != std::string::npos));
  std::string digits;
  std::size_t scale = 0;
  bool seen_point = false;
  for(char c : upper)
  {
    if(c >= '0' && c <= '9')
    {
      digits.push_back(c);
      if(seen_point)
        ++scale;
    }
    else if(c == '.')
      seen_point = true;
    // spaces, sign, currency, commas, CR/DB letters: ignored
  }
  mp_integer v = digits.empty() ? mp_integer{0} : string2integer(digits);
  if(negative)
    v = -v;
  return valuet{from_integer(v, cobol_value_type()), scale};
}

/// Compute a numeric intrinsic exactly from its arguments (IBM LR "Intrinsic
/// functions"). \p args is non-empty.
valuet cobol_typecheckt::compute_numeric_intrinsic(
  const std::string &fname,
  std::vector<valuet> &args)
{
  const exprt zero = from_integer(0, cobol_value_type());

  if(fname == "ABS")
  {
    const valuet &a = args[0];
    return valuet{
      if_exprt{
        binary_relation_exprt{a.expr, ID_ge, zero},
        a.expr,
        unary_minus_exprt{a.expr}},
      a.scale};
  }
  if(fname == "MAX" || fname == "MIN")
  {
    const irep_idt rel = fname == "MAX" ? ID_ge : ID_le;
    valuet r = args[0];
    for(std::size_t i = 1; i < args.size(); ++i)
    {
      valuet a = args[i];
      const std::size_t s = align(r, a);
      r = valuet{
        if_exprt{binary_relation_exprt{r.expr, rel, a.expr}, r.expr, a.expr},
        s};
    }
    return r;
  }
  if(fname == "SUM")
  {
    valuet r = args[0];
    for(std::size_t i = 1; i < args.size(); ++i)
    {
      valuet a = args[i];
      const std::size_t s = align(r, a);
      r = valuet{plus_exprt{r.expr, a.expr}, s};
    }
    return r;
  }
  if(fname == "INTEGER-PART")
  {
    // Truncate towards zero (rescaling to scale 0 uses truncating division).
    return valuet{rescale(args[0].expr, args[0].scale, 0), 0};
  }
  if(fname == "INTEGER")
  {
    // Greatest integer not greater than the argument (floor).
    const valuet &a = args[0];
    if(a.scale == 0)
      return a;
    const exprt d = from_integer(power10(a.scale), cobol_value_type());
    const exprt q = div_exprt{a.expr, d}; // truncates toward zero
    const exprt rem = mod_exprt{a.expr, d};
    const exprt needs_floor = and_exprt{
      binary_relation_exprt{a.expr, ID_lt, zero}, notequal_exprt{rem, zero}};
    return valuet{
      if_exprt{
        needs_floor, minus_exprt{q, from_integer(1, cobol_value_type())}, q},
      0};
  }
  if(fname == "REM" || fname == "MOD")
  {
    valuet a = args[0];
    valuet b = args.size() > 1 ? args[1] : valuet{zero, 0};
    const std::size_t s = align(a, b);
    const exprt rem = mod_exprt{a.expr, b.expr}; // truncated remainder
    if(fname == "REM")
      return valuet{rem, s};
    // FUNCTION MOD is a floored modulo: adjust the truncated remainder when it
    // is nonzero and its sign differs from the divisor's.
    const exprt signs_differ = notequal_exprt{
      binary_relation_exprt{rem, ID_lt, zero},
      binary_relation_exprt{b.expr, ID_lt, zero}};
    const exprt cond = and_exprt{notequal_exprt{rem, zero}, signs_differ};
    return valuet{if_exprt{cond, plus_exprt{rem, b.expr}, rem}, s};
  }

  // Should not reach here for the listed functions.
  return args[0];
}

/// Parse a special register / intrinsic-function / CICS-built-in reference and
/// model its result (IBM LR "LENGTH OF special register" p. 2323, "Intrinsic
/// functions"; CICS DFHRESP). LENGTH is exact; other numeric results are
/// nondeterministic; alphanumeric results are nondet byte sequences.
cond_operandt cobol_typecheckt::parse_intrinsic()
{
  cond_operandt op;

  // LENGTH OF identifier -> the item's byte size.
  if(is_word("LENGTH"))
  {
    advance();
    expect_word("OF");
    const reft r = parse_ref();
    op.numeric = true;
    op.num = valuet{from_integer(r.info->byte_size, cobol_value_type()), 0};
    return op;
  }

  // DFHRESP(name) / DFHVALUE(name): a distinct constant per CICS condition.
  if(eat_word("DFHRESP") || eat_word("DFHVALUE"))
  {
    std::string name;
    if(is_kind(cobol_token_kindt::LPAREN))
    {
      advance();
      if(cur().kind == cobol_token_kindt::WORD)
        name = cur().text;
      while(!is_kind(cobol_token_kindt::RPAREN) && !at_eof())
        advance();
      if(is_kind(cobol_token_kindt::RPAREN))
        advance();
    }
    // NORMAL is 0; other conditions get a distinct nonzero code (the exact
    // value is irrelevant: it is compared against a nondet RESP field).
    mp_integer value = 0;
    if(name != "NORMAL")
    {
      std::size_t h = 1;
      for(char c : name)
        h = h * 31 + static_cast<unsigned char>(c);
      value = static_cast<long>(h % 1000) + 1;
    }
    op.numeric = true;
    op.num = valuet{from_integer(value, cobol_value_type()), 0};
    return op;
  }

  // FUNCTION function-name(args).
  expect_word("FUNCTION");
  std::string fname;
  if(cur().kind == cobol_token_kindt::WORD)
  {
    fname = cur().text;
    advance();
  }

  // Numeric intrinsics computed exactly from their numeric arguments (IBM LR
  // "Intrinsic functions"). Their arguments are parsed as operands rather than
  // skipped.
  static const std::set<std::string> numeric_exact = {
    "ABS", "MAX", "MIN", "SUM", "MOD", "REM", "INTEGER", "INTEGER-PART"};
  if(numeric_exact.count(fname) != 0)
  {
    std::vector<valuet> args;
    if(is_kind(cobol_token_kindt::LPAREN))
    {
      advance();
      while(at_operand())
        args.push_back(parse_operand());
      if(is_kind(cobol_token_kindt::RPAREN))
        advance();
    }
    if(!args.empty())
    {
      op.numeric = true;
      op.num = compute_numeric_intrinsic(fname, args);
      return op;
    }
    // No usable arguments: fall through to the nondet result below.
  }

  // NUMVAL / NUMVAL-C: the numeric value of a character string (IBM LR
  // "NUMVAL"/"NUMVAL-C function"). A string *literal* argument is converted at
  // compile time; for an item argument the character content is not modelled,
  // so the result is nondeterministic.
  if(fname == "NUMVAL" || fname == "NUMVAL-C")
  {
    const bool currency = fname == "NUMVAL-C";
    bool exact = false;
    if(is_kind(cobol_token_kindt::LPAREN))
    {
      advance();
      if(cur().kind == cobol_token_kindt::STRING)
      {
        op.numeric = true;
        op.num = numval_of_string(cur().text, currency);
        advance();
        exact = true;
      }
      // Consume the remaining argument tokens up to the matching ')',
      // tracking nested parentheses (the argument may be reference-modified,
      // e.g. NUMVAL(X(1:N))).
      int depth = 1;
      while(!at_eof() && depth > 0)
      {
        if(is_kind(cobol_token_kindt::LPAREN))
          ++depth;
        else if(is_kind(cobol_token_kindt::RPAREN))
          --depth;
        advance();
      }
    }
    if(exact)
      return op;
    op.numeric = true;
    op.num = valuet{
      typecast_exprt{
        side_effect_expr_nondett{cobol_value_type(), cur().location},
        cobol_value_type()},
      0};
    return op;
  }

  // UPPER-CASE / LOWER-CASE / REVERSE of a string *literal* are computed at
  // compile time (IBM LR "Intrinsic functions"); an item argument's content is
  // not modelled and falls through to a nondeterministic result below.
  if(
    (fname == "UPPER-CASE" || fname == "LOWER-CASE" || fname == "REVERSE") &&
    is_kind(cobol_token_kindt::LPAREN) &&
    peek(1).kind == cobol_token_kindt::STRING)
  {
    advance(); // (
    std::string s = cur().text;
    advance(); // the string literal
    if(fname == "UPPER-CASE")
      for(char &c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    else if(fname == "LOWER-CASE")
      for(char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    else
      std::reverse(s.begin(), s.end());
    int depth = 1;
    while(!at_eof() && depth > 0)
    {
      if(is_kind(cobol_token_kindt::LPAREN))
        ++depth;
      else if(is_kind(cobol_token_kindt::RPAREN))
        --depth;
      advance();
    }
    op.is_spec = true;
    op.spec.kind = value_spect::kindt::STRING;
    op.spec.str = std::move(s);
    return op;
  }

  std::size_t first_item_len = 0;
  bool first_item_len_set = false;
  if(is_kind(cobol_token_kindt::LPAREN))
  {
    advance();
    int depth = 1;
    while(!at_eof() && depth > 0)
    {
      if(is_kind(cobol_token_kindt::LPAREN))
        ++depth;
      else if(is_kind(cobol_token_kindt::RPAREN))
      {
        --depth;
        if(depth == 0)
        {
          advance();
          break;
        }
      }
      else if(
        !first_item_len_set && cur().kind == cobol_token_kindt::WORD &&
        items.find(cur().text) != items.end())
      {
        first_item_len = items.at(cur().text).byte_size;
        first_item_len_set = true;
      }
      advance();
    }
  }

  // Alphanumeric-returning intrinsics.
  static const std::set<std::string> alnum_funcs = {
    "TRIM",
    "UPPER-CASE",
    "LOWER-CASE",
    "REVERSE",
    "CURRENT-DATE",
    "WHEN-COMPILED",
    "SUBSTITUTE",
    "CHAR"};
  if(alnum_funcs.count(fname) != 0)
  {
    std::size_t len = first_item_len_set ? first_item_len : 64;
    if(fname == "CURRENT-DATE")
      len = 21;
    if(fname == "WHEN-COMPILED")
      len = 16;
    if(fname == "CHAR")
      len = 1;
    return nondet_alnum_operand(len);
  }

  // LENGTH function: exact byte size of the (item) argument.
  if(fname == "LENGTH" && first_item_len_set)
  {
    op.numeric = true;
    op.num = valuet{from_integer(first_item_len, cobol_value_type()), 0};
    return op;
  }

  // Other numeric-returning intrinsics: nondeterministic.
  op.numeric = true;
  op.num = valuet{
    typecast_exprt{
      side_effect_expr_nondett{cobol_value_type(), cur().location},
      cobol_value_type()},
    0};
  return op;
}

exprt cobol_typecheckt::build_alnum_relation(
  cond_operandt &l,
  const std::string &op,
  cond_operandt &r)
{
  // IBM Enterprise COBOL for z/OS 6.4 Language Reference, "Comparison of two
  // alphanumeric operands" (pp. 276-277): corresponding character positions
  // are compared left to right by the collating sequence (ASCII = hexadecimal
  // value), and the shorter operand is treated as if padded on the right with
  // spaces.
  const unsignedbv_typet byte_type{8};
  const auto length_of = [](const cond_operandt &o) -> std::size_t
  {
    if(o.is_item)
      return o.length;
    if(o.is_spec && o.spec.kind == value_spect::kindt::STRING)
      return o.spec.str.size();
    return 0; // figurative: takes the size of the other operand
  };
  std::size_t n = std::max(length_of(l), length_of(r));
  if(n == 0)
    n = 1;

  const exprt space = from_integer(' ', byte_type);
  const auto bytes_of = [&](const cond_operandt &o)
  {
    std::vector<exprt> v;
    v.reserve(n);
    if(o.is_item)
    {
      for(std::size_t i = 0; i < n; ++i)
      {
        if(i < o.length)
          v.push_back(make_byte_extract(
            o.record,
            plus_exprt{o.offset, from_integer(i, size_type())},
            byte_type));
        else
          v.push_back(space); // right-padding with spaces (LR p. 277)
      }
    }
    else
    {
      // Materialise the literal / figurative to n bytes; make_alnum_constant
      // right-pads a short literal with spaces, matching the LR rule.
      const exprt c = make_alnum_constant(o.spec, n);
      for(std::size_t i = 0; i < n; ++i)
        v.push_back(index_exprt{c, from_integer(i, size_type())});
    }
    return v;
  };

  const std::vector<exprt> a = bytes_of(l);
  const std::vector<exprt> b = bytes_of(r);

  exprt eq = true_exprt{};
  for(std::size_t i = 0; i < n; ++i)
    eq = and_exprt{eq, equal_exprt{a[i], b[i]}};

  if(op == "=")
    return eq;
  if(op == "<>")
    return not_exprt{eq};

  // Lexicographic ordering: at the first differing position the operand with
  // the higher hexadecimal value is greater (LR p. 277).
  exprt lt = false_exprt{};
  exprt gt = false_exprt{};
  for(std::size_t k = n; k-- > 0;)
  {
    lt = or_exprt{
      binary_relation_exprt{a[k], ID_lt, b[k]},
      and_exprt{equal_exprt{a[k], b[k]}, lt}};
    gt = or_exprt{
      binary_relation_exprt{a[k], ID_gt, b[k]},
      and_exprt{equal_exprt{a[k], b[k]}, gt}};
  }
  if(op == "<")
    return lt;
  if(op == ">")
    return gt;
  if(op == "<=")
    return not_exprt{gt};
  if(op == ">=")
    return not_exprt{lt};
  error("unsupported relational operator '" + op + "'");
}

exprt cobol_typecheckt::parse_not_condition()
{
  if(eat_word("NOT"))
    return not_exprt{parse_not_condition()};
  // A parenthesised condition, e.g. (A = B AND C = D). Distinguished from a
  // parenthesised arithmetic operand by a top-level condition marker inside.
  if(is_kind(cobol_token_kindt::LPAREN) && paren_is_condition())
  {
    // Abbreviation does not carry into/out of a parenthesised condition.
    const cond_operandt saved_subject = abbr_subject;
    const std::string saved_op = abbr_op;
    const bool saved_have = have_abbr;
    advance();
    exprt c = parse_condition();
    if(!is_kind(cobol_token_kindt::RPAREN))
      error("expected ')' after parenthesised condition");
    advance();
    abbr_subject = saved_subject;
    abbr_op = saved_op;
    have_abbr = saved_have;
    return c;
  }
  // Abbreviated combined relation condition with the subject omitted: the term
  // begins directly with a relational operator (IBM LR p. 287). The operator
  // may itself follow it; the implied subject is the previous one.
  if(have_abbr && is_relop_start())
  {
    std::string op = parse_relop();
    cond_operandt b = parse_cond_operand();
    abbr_op = op;
    return build_cond_relation(abbr_subject, op, b);
  }
  return parse_relation();
}

bool cobol_typecheckt::is_relop_start() const
{
  if(cur().kind == cobol_token_kindt::PUNCT)
  {
    const std::string &t = cur().text;
    return t == "=" || t == "<" || t == ">" || t == "<=" || t == ">=" ||
           t == "<>";
  }
  if(cur().kind == cobol_token_kindt::WORD)
  {
    const std::string &t = cur().text;
    return t == "GREATER" || t == "LESS" || t == "EQUAL" || t == "EQUALS" ||
           t == "EXCEEDS";
  }
  return false;
}

/// Look ahead from the current '(' to its matching ')' and decide whether the
/// parenthesised group is a condition (contains a top-level relational/logical
/// operator or class/sign keyword) rather than an arithmetic operand.
bool cobol_typecheckt::paren_is_condition() const
{
  int depth = 0;
  for(std::size_t p = pos; p < tokens.size(); ++p)
  {
    const cobol_tokent &t = tokens[p];
    if(t.kind == cobol_token_kindt::END_OF_FILE)
      break;
    if(t.kind == cobol_token_kindt::LPAREN)
      ++depth;
    else if(t.kind == cobol_token_kindt::RPAREN)
    {
      --depth;
      if(depth == 0)
        break;
    }
    else if(depth == 1)
    {
      if(
        t.kind == cobol_token_kindt::PUNCT &&
        (t.text == "=" || t.text == "<" || t.text == ">" || t.text == "<=" ||
         t.text == ">=" || t.text == "<>"))
        return true;
      if(t.kind == cobol_token_kindt::WORD)
      {
        static const std::set<std::string> markers = {
          "AND",
          "OR",
          "IS",
          "GREATER",
          "LESS",
          "EQUAL",
          "EQUALS",
          "EXCEEDS",
          "NUMERIC",
          "ALPHABETIC",
          "ALPHABETIC-LOWER",
          "ALPHABETIC-UPPER",
          "POSITIVE",
          "NEGATIVE"};
        if(markers.count(t.text) != 0)
          return true;
        // A bare condition-name inside parentheses is also a condition.
        if(conds.find(t.text) != conds.end())
          return true;
      }
    }
  }
  return false;
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
  // Abbreviation context is local to each (sub-)condition; parse_not_condition
  // saves and restores it around a parenthesised condition.
  have_abbr = false;
  exprt l = parse_and_condition();
  while(eat_word("OR"))
    l = or_exprt{l, parse_and_condition()};
  return l;
}

// ---------------------------------------------------------------------------
// statements
// ---------------------------------------------------------------------------

stmtt cobol_typecheckt::make_assign_ref(
  const reft &target,
  valuet v,
  source_locationt loc,
  bool rounded)
{
  // Store a numeric value into a field: record := byte_update(record, offset,
  // encode(value)). encode_numeric applies the receiver's PICTURE on store
  // (scale alignment and truncation mod 10^digits, the IBM LR "MOVE
  // statement" / arithmetic store semantics; optional ROUNDED).
  stmtt s;
  s.kind = stmtt::kindt::ASSIGN;
  s.location = loc;
  s.lhs = target.record;
  s.rhs = make_byte_update(
    target.record,
    target.offset,
    encode_numeric(*target.info, std::move(v), rounded));
  return s;
}

stmtt cobol_typecheckt::make_move_group(
  const reft &target,
  const reft &src,
  source_locationt loc)
{
  // Group / alphanumeric MOVE is a byte copy (IBM LR "MOVE statement": a group
  // move is an unconverted copy). The receiver is left-justified and, when it
  // is longer than the source, space-filled on the right (0x20 on the ASCII
  // host); a shorter receiver truncates on the right.
  const std::size_t tsize = target.info->byte_size;
  const std::size_t ssize = src.info->byte_size;
  const std::size_t n = std::min(tsize, ssize);
  const unsignedbv_typet u8{8};
  const array_typet copy_type{u8, from_integer(n, size_type())};
  const exprt src_bytes = make_byte_extract(src.record, src.offset, copy_type);
  exprt rhs = make_byte_update(target.record, target.offset, src_bytes);
  if(tsize > n)
  {
    array_exprt::operandst pad;
    pad.reserve(tsize - n);
    for(std::size_t i = n; i < tsize; ++i)
      pad.push_back(from_integer(' ', u8));
    const array_typet pad_type{u8, from_integer(tsize - n, size_type())};
    const exprt pad_off =
      plus_exprt{target.offset, from_integer(n, target.offset.type())};
    rhs = make_byte_update(rhs, pad_off, array_exprt{std::move(pad), pad_type});
  }
  stmtt s;
  s.kind = stmtt::kindt::ASSIGN;
  s.location = loc;
  s.lhs = target.record;
  s.rhs = rhs;
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
  if(verb == "STRING")
    return parse_string();
  if(verb == "INITIALIZE")
    return parse_initialize();
  if(verb == "SEARCH")
    return parse_search();
  if(verb == "INSPECT")
    return parse_inspect();
  if(verb == "UNSTRING")
    return parse_unstring();
  if(verb == "OPEN" || verb == "CLOSE")
    return parse_open_close();
  if(verb == "READ")
    return parse_read();
  if(
    verb == "WRITE" || verb == "REWRITE" || verb == "DELETE" || verb == "START")
    return parse_write();
  if(verb == "SORT" || verb == "MERGE")
    return parse_sort();
  if(verb == "RELEASE")
    return parse_open_close(); // external output, no modelled effect (as WRITE)
  if(verb == "RETURN")
    return parse_read(); // RETURN a sorted record is modelled like READ
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
    const source_locationt go_loc = cur().location;
    advance();
    eat_word("TO");
    if(cur().kind != cobol_token_kindt::WORD)
      error("expected paragraph name after GO TO");
    // Collect the procedure-name list (one for an unconditional GO TO; several
    // for the DEPENDING ON form, IBM LR "GO TO statement" formats 1 and 3).
    // Stop at DEPENDING or any statement boundary (a verb, a scope terminator,
    // ELSE or WHEN) so an unconditional GO TO inside IF/EVALUATE/etc. does not
    // swallow the following keyword.
    std::vector<std::string> labels;
    while(cur().kind == cobol_token_kindt::WORD && !is_word("DEPENDING") &&
          !is_verb(cur().text) && !is_word("ELSE") && !is_word("WHEN") &&
          cur().text.rfind("END-", 0) != 0)
    {
      labels.push_back(cur().text);
      advance();
    }
    if(eat_word("DEPENDING"))
    {
      // GO TO p-1 ... p-n DEPENDING ON id: transfer to the id-th procedure
      // (1-based); if id is outside 1..n, control falls through.
      eat_word("ON");
      const valuet sel = parse_operand();
      std::vector<stmtt> result;
      for(std::size_t k = 0; k < labels.size(); ++k)
      {
        stmtt go;
        go.kind = stmtt::kindt::GOTO;
        go.location = go_loc;
        go.target = labels[k];
        stmtt s;
        s.kind = stmtt::kindt::IFTE;
        s.location = go_loc;
        s.cond = equal_exprt{
          sel.expr,
          rescale(
            from_integer(static_cast<int>(k) + 1, cobol_value_type()),
            0,
            sel.scale)};
        s.then_stmts = {go};
        result.push_back(s);
      }
      return result;
    }
    if(labels.size() != 1)
      error("GO TO with multiple targets requires DEPENDING ON");
    stmtt s;
    s.kind = stmtt::kindt::GOTO;
    s.location = go_loc;
    s.target = labels.front();
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
    // IBM LR "EXIT statement": plain EXIT (a common end point for a range of
    // paragraphs) is a no-op; the explicit forms transfer control. EXIT
    // PROGRAM returns to the caller (modelled as a program halt for a single
    // program, like GOBACK).
    const source_locationt loc = cur().location;
    advance();
    stmtt s;
    s.location = loc;
    if(eat_word("PROGRAM"))
      s.kind = stmtt::kindt::STOP;
    else if(eat_word("PERFORM"))
      s.kind = eat_word("CYCLE") ? stmtt::kindt::EXIT_CYCLE
                                 : stmtt::kindt::EXIT_PERFORM;
    else if(eat_word("PARAGRAPH"))
      s.kind = stmtt::kindt::EXIT_PARAGRAPH;
    else if(eat_word("SECTION"))
      s.kind = stmtt::kindt::EXIT_SECTION;
    else
      return {}; // plain EXIT: no-op
    return {s};
  }
  if(verb == "CONTINUE" || verb == "NEXT")
  {
    advance();
    eat_word("SENTENCE");
    return {};
  }
  if(verb == "DISPLAY")
  {
    // DISPLAY writes to the console; no modelled effect.
    advance();
    skip_to_sentence_end();
    return {};
  }
  if(verb == "ACCEPT")
  {
    // ACCEPT identifier [FROM DATE|DAY|TIME|mnemonic|...]: the value comes from
    // the run-time environment (date/time/operator input), which is unknown at
    // verification time, so the receiver is havoced (IBM LR "ACCEPT
    // statement").
    const source_locationt loc = cur().location;
    advance();
    std::vector<stmtt> result;
    if(is_item_word())
      result.push_back(havoc_field(parse_ref(), loc));
    skip_to_sentence_end();
    return result;
  }
  if(verb == "SET")
    return parse_set();
  if(verb == "EXEC")
    return parse_exec();
  error("unsupported statement '" + verb + "'");
}

std::vector<stmtt> cobol_typecheckt::parse_move()
{
  const source_locationt loc = cur().location;
  expect_word("MOVE");

  // The MOVE source is a single sending operand: a literal, figurative
  // constant, (group/alphanumeric/numeric) item, reference-modified item, or
  // intrinsic (IBM LR "MOVE statement"). It is parsed through the one shared
  // operand parser used by conditions and arithmetic.
  cond_operandt src = parse_cond_operand();
  expect_word("TO");

  std::vector<stmtt> result;
  while(is_item_word())
  {
    reft t = parse_ref();
    if(t.info->is_numeric)
    {
      if(src.numeric)
        result.push_back(make_assign_ref(t, src.num, loc));
      else if(
        src.is_spec && spec_to_numeric(src.spec, t.info->scale).has_value())
        result.push_back(make_assign_ref(
          t,
          valuet{
            from_integer(
              *spec_to_numeric(src.spec, t.info->scale), cobol_value_type()),
            t.info->scale},
          loc));
      else
        // Alphanumeric source to a numeric receiver: a de-editing conversion
        // of the source characters that the value model does not represent
        // (IBM LR "MOVE statement") -> nondeterministic.
        result.push_back(make_assign_ref(
          t,
          valuet{side_effect_expr_nondett{cobol_value_type(), loc}, 0},
          loc));
    }
    else if(src.is_item)
    {
      // Group / alphanumeric copy.
      result.push_back(
        make_move_group(t, reft{src.item, src.record, src.offset}, loc));
    }
    else if(src.is_spec)
    {
      // Literal / figurative into an alphanumeric field: write its bytes.
      const exprt c = make_alnum_constant(src.spec, t.info->byte_size);
      stmtt s;
      s.kind = stmtt::kindt::ASSIGN;
      s.location = loc;
      s.lhs = t.record;
      s.rhs = make_byte_update(t.record, t.offset, c);
      result.push_back(s);
    }
    else
    {
      // Numeric source to an alphanumeric (edited) receiver: a formatting move
      // not modelled exactly -> nondeterministic bytes.
      const array_typet bytes_type{
        unsignedbv_typet{8}, from_integer(t.info->byte_size, size_type())};
      stmtt s;
      s.kind = stmtt::kindt::ASSIGN;
      s.location = loc;
      s.lhs = t.record;
      s.rhs = make_byte_update(
        t.record, t.offset, side_effect_expr_nondett{bytes_type, loc});
      result.push_back(s);
    }
  }
  if(result.empty())
    error("MOVE without a target");
  return result;
}

void cobol_typecheckt::parse_overflow_phrase(
  std::vector<stmtt> &result,
  const char *end_kw,
  source_locationt loc)
{
  // [ON OVERFLOW imperative-1] [NOT ON OVERFLOW imperative-2] [END-verb]
  // (IBM LR "STRING"/"UNSTRING statement", ON OVERFLOW phrase). Whether the
  // operation overflows depends on character content the value model does not
  // represent, so the two phrases are guarded by a nondeterministic choice
  // rather than ignored.
  std::vector<stmtt> on_overflow;
  std::vector<stmtt> not_overflow;
  bool has_phrase = false;
  if(eat_word("ON") || is_word("OVERFLOW"))
  {
    expect_word("OVERFLOW");
    on_overflow = parse_statements();
    has_phrase = true;
  }
  if(eat_word("NOT"))
  {
    eat_word("ON");
    expect_word("OVERFLOW");
    not_overflow = parse_statements();
    has_phrase = true;
  }
  eat_word(end_kw);
  if(!has_phrase)
    return;
  stmtt s;
  s.kind = stmtt::kindt::IFTE;
  s.location = loc;
  s.cond = side_effect_expr_nondett{bool_typet{}, loc};
  s.then_stmts = std::move(on_overflow);
  s.else_stmts = std::move(not_overflow);
  result.push_back(std::move(s));
}

std::vector<stmtt> cobol_typecheckt::parse_string()
{
  // STRING concatenates the sending operands (each governed by a DELIMITED BY
  // phrase) into a single receiver (IBM LR "STRING statement"). The exact
  // concatenation depends on the character content the value-domain model does
  // not represent, so the receiver is assigned a nondeterministic value (a
  // sound over-approximation). The optional WITH POINTER and ON OVERFLOW
  // phrases are parsed but, lacking a content model, not given precise
  // semantics.
  const source_locationt loc = cur().location;
  expect_word("STRING");

  // Skip the sending operands and their DELIMITED BY phrases up to INTO.
  while(!at_eof() && !is_word("INTO") && !is_kind(cobol_token_kindt::PERIOD))
    advance();
  expect_word("INTO");
  const reft t = parse_ref();

  // Optional WITH POINTER phrase.
  eat_word("WITH");
  if(eat_word("POINTER"))
    (void)parse_ref();

  // The receiver content is over-approximated.
  const array_typet bytes_type{
    unsignedbv_typet{8}, from_integer(t.info->byte_size, size_type())};
  stmtt s;
  s.kind = stmtt::kindt::ASSIGN;
  s.location = loc;
  s.lhs = t.record;
  s.rhs = make_byte_update(
    t.record, t.offset, side_effect_expr_nondett{bytes_type, loc});

  std::vector<stmtt> result{s};
  // Optional ON OVERFLOW / NOT ON OVERFLOW phrases and END-STRING.
  parse_overflow_phrase(result, "END-STRING", loc);
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_initialize()
{
  // INITIALIZE sets the elementary items of each receiver to their category
  // default: numeric / numeric-edited to ZERO, alphanumeric / alphabetic to
  // SPACES (IBM LR "INITIALIZE statement"). A group is initialised
  // field-by-field.
  const source_locationt loc = cur().location;
  expect_word("INITIALIZE");

  std::vector<reft> targets;
  while(is_item_word())
    targets.push_back(parse_ref());

  // Optional REPLACING category DATA BY value ...: the category defaults above
  // already match the common "REPLACING NUMERIC BY ZEROES" usage, so the
  // phrase is parsed and skipped rather than modelled in detail.
  if(eat_word("REPLACING"))
    skip_to_sentence_end();

  value_spect spaces;
  spaces.kind = value_spect::kindt::SPACES;

  std::vector<stmtt> result;

  // Emit the category-default assignment(s) for one elementary field `info`
  // contained in target `t`. A field inside one or more OCCURS is initialised
  // for *every* occurrence (IBM LR "INITIALIZE statement": all occurrences of
  // a table element are affected), enumerating the OCCURS dimensions that lie
  // within the target. Enumeration is capped; a larger table's later
  // occurrences are left unconstrained (sound: nondet over-approximates the
  // category default).
  const auto init_field = [&](const item_infot &info, const reft &t)
  {
    // Dimensions (count, stride) nested within the target: enclosing OCCURS
    // groups inside t, plus the field's own OCCURS if any.
    std::vector<std::pair<std::size_t, std::size_t>> dims;
    for(std::size_t dim : info.occurs_dims)
      if(
        dim < all_items.size() &&
        all_items[dim].info.offset >= t.info->offset &&
        all_items[dim].info.offset < t.info->offset + t.info->byte_size)
        dims.push_back(
          {std::max<std::size_t>(all_items[dim].info.occurs, 1),
           all_items[dim].info.byte_size});
    if(info.is_table)
      dims.push_back({std::max<std::size_t>(info.occurs, 1), info.byte_size});

    std::size_t total = 1;
    for(const auto &d : dims)
      total *= d.first;
    static const std::size_t cap = 4096;
    if(total > cap)
      total = 1; // too large to enumerate; initialise the first occurrence

    const std::size_t base = info.offset - t.info->offset;
    std::vector<std::size_t> ix(dims.size(), 0);
    for(std::size_t n = 0; n < total; ++n)
    {
      std::size_t delta = 0;
      for(std::size_t i = 0; i < dims.size(); ++i)
        delta += ix[i] * dims[i].second;
      const exprt offset =
        plus_exprt{t.offset, from_integer(base + delta, size_type())};
      if(info.is_numeric)
        result.push_back(make_assign_ref(
          reft{&info, t.record, offset},
          valuet{from_integer(0, cobol_value_type()), info.scale},
          loc));
      else
      {
        stmtt s;
        s.kind = stmtt::kindt::ASSIGN;
        s.location = loc;
        s.lhs = t.record;
        s.rhs = make_byte_update(
          t.record, offset, make_alnum_constant(spaces, info.byte_size));
        result.push_back(s);
      }
      for(std::size_t i = dims.size(); i-- > 0;)
      {
        if(++ix[i] < dims[i].first)
          break;
        ix[i] = 0;
      }
    }
  };

  for(const reft &t : targets)
  {
    // Initialise the elementary fields contained in the receiver.
    bool any_child = false;
    for(const entryt &e : all_items)
    {
      const item_infot &info = e.info;
      if(
        info.is_group || info.record_symbol != t.info->record_symbol ||
        info.offset < t.info->offset ||
        info.offset >= t.info->offset + t.info->byte_size)
        continue;
      any_child = true;
      init_field(info, t);
    }
    if(!any_child)
      init_field(*t.info, t); // the receiver is itself elementary
  }
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_search()
{
  // SEARCH [ALL] table-name [VARYING index]
  //   [AT END imperative] {WHEN condition imperative}... [END-SEARCH]
  // (IBM LR "SEARCH statement"). A serial SEARCH scans from the table's
  // current index; SEARCH ALL is a binary search of an ordered table. Both are
  // modelled here as a serial scan, which finds an index satisfying a WHEN (or
  // reaches AT END) exactly when the binary search would; SEARCH ALL only
  // additionally starts the scan at the first element.
  const source_locationt loc = cur().location;
  expect_word("SEARCH");
  const bool search_all = eat_word("ALL");

  if(cur().kind != cobol_token_kindt::WORD)
    error("expected a table name after SEARCH");
  const std::string table_name = cur().text;
  const reft table = parse_ref();
  if(!table.info->is_table)
    error("SEARCH requires a table (OCCURS) item");

  // The index to vary: an explicit VARYING index, else the table's first
  // INDEXED BY index-name.
  std::string index_name;
  if(eat_word("VARYING"))
  {
    if(cur().kind != cobol_token_kindt::WORD)
      error("expected an index after VARYING");
    index_name = cur().text;
    (void)parse_ref();
  }
  else
  {
    auto it = table_indexes.find(table_name);
    if(it == table_indexes.end() || it->second.empty())
      error("SEARCH table has no INDEXED BY index");
    index_name = it->second.front();
  }
  const reft index_ref = ref_of(lookup_item(index_name));

  stmtt s;
  s.kind = stmtt::kindt::SEARCH;
  s.location = loc;

  // SEARCH ALL starts the scan at the first element; serial SEARCH continues
  // from the current index value.
  if(search_all)
    s.var_init.push_back(make_assign_ref(
      index_ref, valuet{from_integer(1, cobol_value_type()), 0}, loc));

  // Loop bound: index past the last occurrence.
  const valuet idx_val = read_field(index_ref);
  s.cond = binary_relation_exprt{
    idx_val.expr, ID_gt, from_integer(table.info->occurs, cobol_value_type())};

  // Index increment.
  const valuet step_val = read_field(index_ref);
  s.var_step.push_back(make_assign_ref(
    index_ref,
    valuet{
      plus_exprt{step_val.expr, from_integer(1, cobol_value_type())},
      step_val.scale},
    loc));

  if(eat_word("AT"))
  {
    expect_word("END");
    s.other_stmts = parse_statements();
  }
  while(eat_word("WHEN"))
  {
    exprt cond = parse_condition();
    std::vector<stmtt> imp = parse_statements();
    s.when_clauses.emplace_back(std::move(cond), std::move(imp));
  }
  eat_word("END-SEARCH");
  return {s};
}

std::vector<stmtt> cobol_typecheckt::parse_inspect()
{
  // INSPECT identifier {TALLYING ... | REPLACING ... | CONVERTING ...}
  // (IBM LR "INSPECT statement"). TALLYING counting over the inspected item's
  // bytes is modelled exactly for the common forms below; REPLACING/CONVERTING
  // (which rewrite the item's content) and the forms not handled here fall
  // back to havocking the affected item (a sound over-approximation).
  const source_locationt loc = cur().location;
  expect_word("INSPECT");
  eat_word("BACKWARD");
  const reft item = parse_ref();
  const std::size_t n = item.info->byte_size;

  std::vector<stmtt> result;
  if(eat_word("TALLYING"))
  {
    // {counter FOR {ALL|LEADING|CHARACTERS} value}...: the counter is the item
    // before FOR. We model exactly a single FOR phrase whose argument is a
    // one-character literal and that has no BEFORE/AFTER delimiter:
    //   FOR ALL c       -> count of positions equal to c,
    //   FOR LEADING c   -> length of the leading run of c,
    //   FOR CHARACTERS  -> the number of character positions (n).
    // Anything else (multi-char value, item value, BEFORE/AFTER, extra
    // phrases) havocs the counter.
    while(is_item_word())
    {
      reft counter = parse_ref();
      if(!eat_word("FOR"))
      {
        result.push_back(havoc_field(counter, loc));
        break;
      }

      exprt add; // exact contribution, when modelled
      bool exact = false;
      if(eat_word("CHARACTERS"))
      {
        add = from_integer(n, cobol_value_type());
        exact = true;
      }
      else if(is_word("ALL") || is_word("LEADING"))
      {
        const bool leading = is_word("LEADING");
        advance();
        // A one-character literal argument, with the phrase ending right after
        // it (no BEFORE/AFTER, no further value) is the exact case.
        if(
          cur().kind == cobol_token_kindt::STRING && cur().text.size() == 1 &&
          !(peek(1).kind == cobol_token_kindt::WORD &&
            (peek(1).text == "BEFORE" || peek(1).text == "AFTER")))
        {
          const exprt c = from_integer(
            static_cast<unsigned char>(cur().text[0]), cobol_value_type());
          advance();
          const exprt one = from_integer(1, cobol_value_type());
          const exprt zero = from_integer(0, cobol_value_type());
          add = zero;
          if(leading)
          {
            // Leading run: positions 0..i count while every byte 0..i equals c.
            exprt still = true_exprt{};
            for(std::size_t i = 0; i < n; ++i)
            {
              still = and_exprt{still, equal_exprt{byte_of(item, i), c}};
              add = plus_exprt{add, if_exprt{still, one, zero}};
            }
          }
          else
          {
            for(std::size_t i = 0; i < n; ++i)
              add = plus_exprt{
                add, if_exprt{equal_exprt{byte_of(item, i), c}, one, zero}};
          }
          exact = true;
        }
      }

      if(exact)
      {
        // The clean phrase has been fully consumed. Apply it exactly for an
        // alphanumeric inspected item; for a numeric item the record bytes are
        // not necessarily its character content, so havoc the counter (sound).
        if(!item.info->is_numeric)
        {
          const valuet cur_v = read_field(counter);
          valuet nv{plus_exprt{cur_v.expr, add}, cur_v.scale};
          result.push_back(make_assign_ref(counter, nv, loc));
        }
        else
          result.push_back(havoc_field(counter, loc));
      }
      else
      {
        // Unmodelled FOR form: havoc the counter and skip its phrase tokens.
        result.push_back(havoc_field(counter, loc));
        while(!is_kind(cobol_token_kindt::PERIOD) && !at_eof() &&
              !is_word("REPLACING") && !is_word("CONVERTING") &&
              !(is_item_word() && peek(1).kind == cobol_token_kindt::WORD &&
                peek(1).text == "FOR"))
          advance();
      }
      if(is_word("REPLACING") || is_word("CONVERTING"))
        break;
    }
  }
  if(eat_word("REPLACING") || eat_word("CONVERTING"))
  {
    // The inspected item is rewritten; its new content is not modelled.
    skip_to_sentence_end();
    result.push_back(havoc_field(item, loc));
  }
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_unstring()
{
  // UNSTRING source [DELIMITED BY ...] INTO r-1 [DELIMITER IN d-1]
  //   [COUNT IN c-1] ... [WITH POINTER p] [TALLYING IN t]
  //   [ON OVERFLOW imp] [END-UNSTRING] (IBM LR "UNSTRING statement"). The
  // split is not represented exactly, so every receiving item gets a
  // nondeterministic value (sound over-approximation), as for STRING.
  const source_locationt loc = cur().location;
  expect_word("UNSTRING");
  (void)parse_ref(); // source
  // Skip the DELIMITED BY phrase up to INTO.
  while(!at_eof() && !is_word("INTO") && !is_kind(cobol_token_kindt::PERIOD))
    advance();
  expect_word("INTO");

  std::vector<stmtt> result;
  const auto havoc_next_item = [&]()
  {
    if(is_item_word())
      result.push_back(havoc_field(parse_ref(), loc));
  };
  // Receiving items and their DELIMITER IN / COUNT IN sub-receivers.
  while(is_item_word() || is_word("DELIMITER") || is_word("COUNT"))
  {
    if(eat_word("DELIMITER") || eat_word("COUNT"))
    {
      eat_word("IN");
      havoc_next_item();
      continue;
    }
    havoc_next_item();
  }
  eat_word("WITH");
  if(eat_word("POINTER"))
    havoc_next_item();
  if(eat_word("TALLYING"))
  {
    eat_word("IN");
    havoc_next_item();
  }
  // Optional ON OVERFLOW / NOT ON OVERFLOW phrases and END-UNSTRING.
  parse_overflow_phrase(result, "END-UNSTRING", loc);
  return result;
}

void cobol_typecheckt::parse_io_exception(
  std::vector<stmtt> &result,
  const char *end_kw,
  source_locationt loc)
{
  // [AT END imp | INVALID KEY imp] [NOT AT END imp | NOT INVALID KEY imp]
  // [END-verb] (IBM LR "READ"/"WRITE"/... statements). Whether end-of-file or
  // an invalid key occurs is not modelled, so the phrases are guarded by a
  // nondeterministic choice (see "conditional-imperative phrases").
  std::vector<stmtt> exc;
  std::vector<stmtt> not_exc;
  bool has_phrase = false;
  if(eat_word("AT"))
  {
    expect_word("END");
    exc = parse_statements();
    has_phrase = true;
  }
  else if(eat_word("INVALID"))
  {
    eat_word("KEY");
    exc = parse_statements();
    has_phrase = true;
  }
  if(eat_word("NOT"))
  {
    eat_word("AT");
    eat_word("END");
    eat_word("INVALID");
    eat_word("KEY");
    not_exc = parse_statements();
    has_phrase = true;
  }
  eat_word(end_kw);
  if(!has_phrase)
    return;
  stmtt s;
  s.kind = stmtt::kindt::IFTE;
  s.location = loc;
  s.cond = side_effect_expr_nondett{bool_typet{}, loc};
  s.then_stmts = std::move(exc);
  s.else_stmts = std::move(not_exc);
  result.push_back(std::move(s));
}

std::vector<stmtt> cobol_typecheckt::parse_open_close()
{
  // OPEN {INPUT|OUTPUT|I-O|EXTEND} file-name... / CLOSE file-name...: files are
  // external; opening and closing have no modelled state effect (IBM LR
  // "OPEN"/"CLOSE statement").
  advance(); // OPEN or CLOSE
  skip_to_sentence_end();
  return {};
}

std::vector<stmtt> cobol_typecheckt::parse_read()
{
  // READ file-name [NEXT|PREVIOUS|RECORD] [INTO id] [KEY IS id]
  //   [AT END imp][NOT AT END imp] | [INVALID KEY imp][NOT INVALID KEY imp]
  //   [END-READ] (IBM LR "READ statement"). A read delivers an unknown record
  //   or reaches end-of-file / an invalid key: the file's record area and the
  //   INTO receiver are havoced, and the exception phrases are guarded
  //   nondeterministically. RETURN (of a sorted record) is modelled the same
  //   way (IBM LR "RETURN statement").
  const source_locationt loc = cur().location;
  const std::string verb = cur().text; // READ or RETURN
  advance();
  const std::string end_kw = "END-" + verb;
  std::string file;
  if(cur().kind == cobol_token_kindt::WORD)
  {
    file = cur().text;
    advance();
  }
  eat_word("NEXT");
  eat_word("PREVIOUS");
  eat_word("RECORD");

  std::vector<stmtt> result;
  // Havoc the file's record area so its fields read as unknown.
  bool have_record = false;
  reft record_ref;
  auto it = file_records.find(file);
  if(it != file_records.end())
  {
    const irep_idt rec = "cobol::" + program_id + "::" + it->second;
    if(record_sizes.count(rec) != 0)
    {
      stmtt s;
      s.kind = stmtt::kindt::ASSIGN;
      s.location = loc;
      s.lhs = record_expr(rec);
      s.rhs = side_effect_expr_nondett{record_type(rec), loc};
      result.push_back(std::move(s));
    }
    auto rit = items.find(it->second);
    if(rit != items.end())
    {
      record_ref = ref_of(rit->second);
      have_record = true;
    }
  }
  if(eat_word("INTO"))
  {
    // READ ... INTO id is equivalent to the read followed by a group MOVE of
    // the record to id (IBM LR "READ statement"): copy the (now unknown)
    // record so id and the record agree. If the record is unknown, havoc id.
    if(is_item_word())
    {
      const reft tgt = parse_ref();
      if(have_record)
        result.push_back(make_move_group(tgt, record_ref, loc));
      else
        result.push_back(havoc_field(tgt, loc));
    }
  }
  eat_word("WITH");
  eat_word("NO");
  eat_word("LOCK");
  if(eat_word("KEY"))
  {
    eat_word("IS");
    if(is_item_word())
      (void)parse_ref();
  }
  parse_io_exception(result, end_kw.c_str(), loc);
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_write()
{
  // WRITE record [FROM id] [{BEFORE|AFTER} ADVANCING ...] / REWRITE / DELETE /
  // START, each with an optional INVALID KEY / AT END-OF-PAGE phrase and scope
  // terminator (IBM LR "WRITE"/"REWRITE"/"DELETE"/"START statements"). The
  // output is external; only the nondeterministic exception outcome is
  // modelled.
  const source_locationt loc = cur().location;
  const std::string verb = cur().text;
  advance();
  const std::string end_kw = "END-" + verb;

  std::vector<stmtt> result;
  // record-name / file-name operand and an optional FROM source. WRITE/REWRITE
  // record FROM id is equivalent to MOVE id TO record then the write (IBM LR
  // "WRITE statement", FROM phrase), so the record is updated; the write
  // itself is external and not otherwise modelled.
  std::optional<reft> record_ref;
  if(is_item_word())
    record_ref = parse_ref();
  if(eat_word("FROM") && is_item_word())
  {
    const reft src = parse_ref();
    if(record_ref.has_value())
      result.push_back(make_move_group(*record_ref, src, loc));
  }

  // Skip intervening phrases (ADVANCING, KEY, ...) up to an exception phrase or
  // a statement boundary.
  while(!at_eof() && !is_kind(cobol_token_kindt::PERIOD) &&
        !is_word("INVALID") && !is_word("AT") && !is_word("NOT") &&
        !is_word(end_kw.c_str()) && !is_verb(cur().text) && !is_word("ELSE") &&
        !is_word("WHEN") && cur().text.rfind("END-", 0) != 0)
    advance();

  parse_io_exception(result, end_kw.c_str(), loc);
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_sort()
{
  // SORT/MERGE file ON {ASCENDING|DESCENDING} KEY key...
  //   [INPUT PROCEDURE IS proc-1 [{THRU|THROUGH} proc-2] | USING file...]
  //   [OUTPUT PROCEDURE IS proc-3 [{THRU|THROUGH} proc-4] | GIVING file...]
  // (IBM LR "SORT"/"MERGE statement"). The external sort/merge itself is not
  // modelled (records flow through RELEASE/RETURN, which are abstracted), but
  // any INPUT PROCEDURE and OUTPUT PROCEDURE are PERFORMed so their logic is
  // analysed. The ASCENDING/DESCENDING KEY and USING/GIVING phrases are
  // skipped.
  const source_locationt loc = cur().location;
  advance(); // SORT or MERGE
  if(cur().kind == cobol_token_kindt::WORD)
    advance(); // sort/merge work-file name

  std::vector<stmtt> result;
  const auto perform_procedure = [&]()
  {
    eat_word("PROCEDURE");
    eat_word("IS");
    if(cur().kind != cobol_token_kindt::WORD)
      return;
    stmtt p;
    p.kind = stmtt::kindt::PERFORM;
    p.location = loc;
    p.pkind = stmtt::perform_kindt::ONCE;
    p.target = cur().text;
    advance();
    if(eat_word("THRU") || eat_word("THROUGH"))
    {
      if(cur().kind == cobol_token_kindt::WORD)
      {
        p.target_end = cur().text;
        advance();
      }
    }
    result.push_back(std::move(p));
  };

  while(!at_eof() && !is_kind(cobol_token_kindt::PERIOD) &&
        !is_verb(cur().text) && !is_word("ELSE") && !is_word("WHEN") &&
        cur().text.rfind("END-", 0) != 0)
  {
    if(eat_word("INPUT") || eat_word("OUTPUT"))
      perform_procedure();
    else
      advance(); // KEY / ASCENDING / USING / GIVING / file-names / ...
  }
  return result;
}

exprt cobol_typecheckt::size_error_cond(const reft &r, const valuet &v)
{
  // A size error occurs when the result, aligned to the receiver's scale, has
  // more integer positions than the receiver can hold, i.e. its magnitude
  // reaches 10^digits (IBM LR "SIZE ERROR phrases").
  if(r.info->digits == 0 || r.info->digits > 18)
    return false_exprt{};
  const exprt scaled = rescale(v.expr, v.scale, r.info->scale);
  const mp_integer limit = power10(r.info->digits);
  return or_exprt{
    binary_relation_exprt{
      scaled, ID_ge, from_integer(limit, cobol_value_type())},
    binary_relation_exprt{
      scaled, ID_le, from_integer(-limit, cobol_value_type())}};
}

void cobol_typecheckt::assign_giving(
  const valuet &v,
  std::vector<stmtt> &result,
  exprt &overflow,
  source_locationt loc)
{
  while(is_item_word())
  {
    reft r = parse_ref();
    const bool rounded = eat_word("ROUNDED");
    result.push_back(make_assign_ref(r, v, loc, rounded));
    overflow = or_exprt{overflow, size_error_cond(r, v)};
  }
}

std::vector<stmtt> cobol_typecheckt::finish_arith(
  std::vector<stmtt> assigns,
  exprt overflow,
  const char *end_kw,
  source_locationt loc)
{
  // [ON SIZE ERROR imperative-1] [NOT ON SIZE ERROR imperative-2] [END-verb].
  std::vector<stmtt> on_size;
  std::vector<stmtt> not_on_size;
  bool has_phrase = false;
  if(eat_word("ON") || is_word("SIZE"))
  {
    expect_word("SIZE");
    expect_word("ERROR");
    on_size = parse_statements();
    has_phrase = true;
  }
  if(eat_word("NOT"))
  {
    eat_word("ON");
    expect_word("SIZE");
    expect_word("ERROR");
    not_on_size = parse_statements();
    has_phrase = true;
  }
  eat_word(end_kw);

  if(!has_phrase)
    return assigns;

  // On a size error the result is not stored and the ON SIZE ERROR imperative
  // runs; otherwise the result is stored and any NOT ON SIZE ERROR imperative
  // runs (IBM LR "SIZE ERROR phrases"). With several receivers this uses the
  // disjunction of their conditions (a documented simplification of the
  // per-receiver rule).
  std::vector<stmtt> else_stmts = std::move(assigns);
  for(stmtt &s : not_on_size)
    else_stmts.push_back(std::move(s));
  stmtt s;
  s.kind = stmtt::kindt::IFTE;
  s.location = loc;
  s.cond = std::move(overflow);
  s.then_stmts = std::move(on_size);
  s.else_stmts = std::move(else_stmts);
  return {s};
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
  exprt overflow = false_exprt{};
  if(eat_word("TO"))
  {
    // The items/literals after TO are addends when GIVING follows (their sum
    // is added to the pre-TO sum) and receivers otherwise. Parse them as
    // operands, capturing the receiver reference and a trailing ROUNDED for
    // the no-GIVING case (IBM LR "ADD statement").
    struct postt
    {
      valuet v;
      std::optional<reft> ref;
      bool rounded;
    };
    std::vector<postt> post;
    while(at_operand())
    {
      valuet v = parse_operand();
      std::optional<reft> ref = last_ref;
      post.push_back({v, ref, eat_word("ROUNDED")});
    }
    if(eat_word("GIVING"))
    {
      // result = sum(pre-TO operands) + sum(post-TO operands)
      for(auto &p : post)
      {
        align(sum, p.v);
        sum = valuet{plus_exprt{sum.expr, p.v.expr}, sum.scale};
      }
      assign_giving(sum, result, overflow, loc);
    }
    else
    {
      // ADD ops TO receivers: each receiver += sum.
      for(auto &p : post)
      {
        if(!p.ref.has_value())
          error("ADD ... TO requires a receiving item");
        valuet cur_val = read_field(*p.ref);
        valuet s = sum;
        align(cur_val, s);
        const valuet nv{plus_exprt{cur_val.expr, s.expr}, s.scale};
        result.push_back(make_assign_ref(*p.ref, nv, loc, p.rounded));
        overflow = or_exprt{overflow, size_error_cond(*p.ref, nv)};
      }
    }
  }
  else if(eat_word("GIVING"))
  {
    assign_giving(sum, result, overflow, loc);
  }
  else
    error("ADD requires TO or GIVING");

  if(result.empty())
    error("ADD without a target");
  return finish_arith(std::move(result), std::move(overflow), "END-ADD", loc);
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

  // The items/literals after FROM are minuends: with GIVING the (single)
  // minuend's value is used and the difference stored in the results; without
  // GIVING each minuend is a receiver that is decremented (IBM LR "SUBTRACT
  // statement"). Parse as operands, capturing the receiver reference and a
  // trailing ROUNDED.
  struct minuendt
  {
    valuet v;
    std::optional<reft> ref;
    bool rounded;
  };
  std::vector<minuendt> minuends;
  while(at_operand())
  {
    valuet v = parse_operand();
    std::optional<reft> ref = last_ref;
    minuends.push_back({v, ref, eat_word("ROUNDED")});
  }

  std::vector<stmtt> result;
  exprt overflow = false_exprt{};
  if(eat_word("GIVING"))
  {
    if(minuends.size() != 1)
      error("SUBTRACT ... FROM ... GIVING expects a single minuend");
    valuet mv = minuends[0].v;
    align(mv, sum);
    const valuet diff{minus_exprt{mv.expr, sum.expr}, mv.scale};
    assign_giving(diff, result, overflow, loc);
  }
  else
  {
    for(auto &m : minuends)
    {
      if(!m.ref.has_value())
        error("SUBTRACT ... FROM requires a receiving item");
      valuet mv = read_field(*m.ref);
      valuet s = sum;
      align(mv, s);
      const valuet nv{minus_exprt{mv.expr, s.expr}, s.scale};
      result.push_back(make_assign_ref(*m.ref, nv, loc, m.rounded));
      overflow = or_exprt{overflow, size_error_cond(*m.ref, nv)};
    }
  }
  if(result.empty())
    error("SUBTRACT without a target");
  return finish_arith(
    std::move(result), std::move(overflow), "END-SUBTRACT", loc);
}

std::vector<stmtt> cobol_typecheckt::parse_multiply()
{
  const source_locationt loc = cur().location;
  expect_word("MULTIPLY");
  valuet a = parse_operand();
  expect_word("BY");
  valuet b = parse_operand();
  const std::optional<reft> b_ref = last_ref;
  const bool b_rounded = eat_word("ROUNDED");
  valuet product{mult_exprt{a.expr, b.expr}, a.scale + b.scale};
  std::vector<stmtt> result;
  exprt overflow = false_exprt{};
  if(eat_word("GIVING"))
  {
    assign_giving(product, result, overflow, loc);
  }
  else
  {
    // MULTIPLY a BY b : b = a * b (b must be a data item)
    if(!b_ref.has_value())
      error("MULTIPLY without GIVING requires an item operand");
    result.push_back(make_assign_ref(*b_ref, product, loc, b_rounded));
    overflow = size_error_cond(*b_ref, product);
  }
  if(result.empty())
    error("MULTIPLY without a target");
  return finish_arith(
    std::move(result), std::move(overflow), "END-MULTIPLY", loc);
}

std::vector<stmtt> cobol_typecheckt::parse_divide()
{
  const source_locationt loc = cur().location;
  expect_word("DIVIDE");
  valuet first = parse_operand();
  std::vector<stmtt> result;
  exprt overflow = false_exprt{};
  // Compute quotient = dividend / divisor and, for the REMAINDER phrase,
  // remainder = dividend - quotient * divisor (IBM LR "DIVIDE statement").
  const auto do_remainder =
    [&](const valuet &dividend, const valuet &divisor, const valuet &quotient)
  {
    if(!eat_word("REMAINDER"))
      return;
    const valuet rem{
      minus_exprt{dividend.expr, mult_exprt{quotient.expr, divisor.expr}},
      dividend.scale};
    reft r = parse_ref();
    const bool rounded = eat_word("ROUNDED");
    result.push_back(make_assign_ref(r, rem, loc, rounded));
    overflow = or_exprt{overflow, size_error_cond(r, rem)};
  };
  if(eat_word("INTO"))
  {
    valuet dividend = parse_operand();
    const std::optional<reft> dividend_ref = last_ref;
    const bool dividend_rounded = eat_word("ROUNDED");
    valuet divisor = first;
    align(dividend, divisor);
    valuet quotient{div_exprt{dividend.expr, divisor.expr}, 0};
    if(eat_word("GIVING"))
    {
      assign_giving(quotient, result, overflow, loc);
      do_remainder(dividend, divisor, quotient);
    }
    else
    {
      if(!dividend_ref.has_value())
        error("DIVIDE without GIVING requires an item operand");
      result.push_back(
        make_assign_ref(*dividend_ref, quotient, loc, dividend_rounded));
      overflow = size_error_cond(*dividend_ref, quotient);
    }
  }
  else if(eat_word("BY"))
  {
    valuet divisor = parse_operand();
    valuet dividend = first;
    align(dividend, divisor);
    valuet quotient{div_exprt{dividend.expr, divisor.expr}, 0};
    expect_word("GIVING");
    assign_giving(quotient, result, overflow, loc);
    do_remainder(dividend, divisor, quotient);
  }
  else
    error("DIVIDE requires INTO or BY");

  if(result.empty())
    error("DIVIDE without a target");
  return finish_arith(
    std::move(result), std::move(overflow), "END-DIVIDE", loc);
}

std::vector<stmtt> cobol_typecheckt::parse_compute()
{
  const source_locationt loc = cur().location;
  expect_word("COMPUTE");
  std::vector<std::pair<reft, bool>> targets;
  while(is_item_word())
  {
    reft r = parse_ref();
    targets.push_back({r, eat_word("ROUNDED")});
  }
  if(!is_kind(cobol_token_kindt::PUNCT) || cur().text != "=")
    error("expected '=' in COMPUTE");
  advance();
  valuet v = parse_expr();
  std::vector<stmtt> result;
  exprt overflow = false_exprt{};
  for(const auto &t : targets)
  {
    result.push_back(make_assign_ref(t.first, v, loc, t.second));
    overflow = or_exprt{overflow, size_error_cond(t.first, v)};
  }
  if(result.empty())
    error("COMPUTE without a target");
  return finish_arith(
    std::move(result), std::move(overflow), "END-COMPUTE", loc);
}

std::vector<stmtt> cobol_typecheckt::parse_call()
{
  const source_locationt loc = cur().location;
  expect_word("CALL");

  // The CPROVER verification primitives, exposed via the static-CALL syntax.
  if(
    cur().kind == cobol_token_kindt::STRING &&
    (cur().text == "__CPROVER_assert" || cur().text == "__CPROVER_assume"))
  {
    const std::string name = cur().text;
    advance();
    expect_word("USING");
    exprt cond = parse_condition();
    stmtt s;
    s.kind =
      name == "__CPROVER_assert" ? stmtt::kindt::ASSERT : stmtt::kindt::ASSUME;
    s.cond = cond;
    s.location = loc;
    return {s};
  }

  // Any other CALL is to a separately-compiled program that the frontend does
  // not link. We stub it (approach.md "day-one feature scope"): a called
  // program may modify its BY REFERENCE arguments and its RETURNING value, so
  // those receivers are havoced; BY CONTENT / BY VALUE arguments are not
  // modified at the caller. The program-name operand (a literal or, for a
  // dynamic call, a data item) is consumed.
  if(
    cur().kind == cobol_token_kindt::STRING ||
    cur().kind == cobol_token_kindt::WORD)
    advance();

  std::vector<stmtt> result;
  if(eat_word("USING"))
  {
    bool by_reference = true; // BY REFERENCE is the default
    while(!at_eof())
    {
      if(eat_word("BY"))
      {
        if(eat_word("REFERENCE"))
          by_reference = true;
        else if(eat_word("CONTENT") || eat_word("VALUE"))
          by_reference = false;
        continue;
      }
      if(is_item_word())
      {
        reft r = parse_ref();
        if(by_reference)
          result.push_back(havoc_field(r, loc));
        continue;
      }
      break;
    }
  }
  if(eat_word("RETURNING") || eat_word("GIVING"))
  {
    if(is_item_word())
    {
      reft r = parse_ref();
      result.push_back(havoc_field(r, loc));
    }
  }
  return result;
}

std::vector<stmtt> cobol_typecheckt::parse_set()
{
  const source_locationt loc = cur().location;
  expect_word("SET");

  // Collect target names up to TO / UP / DOWN.
  std::vector<std::string> targets;
  while(cur().kind == cobol_token_kindt::WORD && !is_word("TO") &&
        !is_word("UP") && !is_word("DOWN"))
  {
    targets.push_back(cur().text);
    advance();
  }

  std::vector<stmtt> result;

  // SET index-name ... {UP | DOWN} BY n: increment/decrement each index
  // (IBM LR "SET statement", format 4).
  if(is_word("UP") || is_word("DOWN"))
  {
    const bool up = is_word("UP");
    advance();
    eat_word("BY");
    valuet n = parse_operand();
    for(const std::string &t : targets)
    {
      auto it = items.find(t);
      if(it == items.end() || !it->second.is_numeric)
        continue;
      const reft r = ref_of(it->second);
      valuet cur_v = read_field(r);
      valuet step = n;
      align(cur_v, step);
      const exprt e =
        up ? static_cast<exprt>(plus_exprt{cur_v.expr, step.expr})
           : static_cast<exprt>(minus_exprt{cur_v.expr, step.expr});
      result.push_back(make_assign_ref(r, valuet{e, cur_v.scale}, loc));
    }
    return result;
  }

  if(!eat_word("TO"))
  {
    // Unrecognised SET form (e.g. SET ADDRESS OF ...): no-op.
    skip_to_sentence_end();
    return {};
  }

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
        result.push_back(make_assign_ref(
          ref_of(c.parent),
          valuet{
            from_integer(c.num_ranges.front().first, cobol_value_type()),
            c.parent.scale},
          loc));
      }
      else if(!c.parent.is_numeric && !c.alnum_values.empty())
      {
        const reft target = ref_of(c.parent);
        stmtt s;
        s.kind = stmtt::kindt::ASSIGN;
        s.location = loc;
        s.lhs = target.record;
        s.rhs = make_byte_update(
          target.record, target.offset, c.alnum_values.front());
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
        result.push_back(make_assign_ref(ref_of(it->second), v, loc));
    }
    return result;
  }

  // Unsupported value form: no-op.
  skip_to_sentence_end();
  return result;
}

stmtt cobol_typecheckt::havoc_field(const reft &r, source_locationt loc)
{
  // Set the field's storage bytes to a nondeterministic value (a sound
  // over-approximation of an unknown result).
  const array_typet bytes_type{
    unsignedbv_typet{8}, from_integer(r.info->byte_size, size_type())};
  stmtt s;
  s.kind = stmtt::kindt::ASSIGN;
  s.location = loc;
  s.lhs = r.record;
  s.rhs = make_byte_update(
    r.record, r.offset, side_effect_expr_nondett{bytes_type, loc});
  return s;
}

std::vector<stmtt> cobol_typecheckt::parse_exec()
{
  // EXEC CICS|SQL|DLI ... END-EXEC is an embedded sub-language processed by a
  // separate translator (the COBOL LR treats EXEC ... END-EXEC as embedded
  // text). We stub it as the project notes prescribe (cobol-semantics.md S8):
  // output operands and the per-command EIB status fields are havoced (nondet)
  // and command verbs that return control (CICS RETURN / XCTL) terminate the
  // run unit. Input operands (FROM, COMMAREA, MAP, ...) are ignored.
  const source_locationt loc = cur().location;
  expect_word("EXEC");

  // sub-language (CICS / SQL / DLI)
  if(cur().kind == cobol_token_kindt::WORD)
    advance();

  // Output operands whose named item receives data and is therefore havoced.
  const auto is_output_kw = [](const std::string &w)
  {
    return w == "INTO" || w == "SET" || w == "RESP" || w == "RESP2" ||
           w == "LENGTH" || w == "FLENGTH" || w == "RETURNING" ||
           w == "NUMITEMS" || w == "COUNTER" || w == "TOLENGTH";
  };

  std::string command;
  std::vector<std::string> output_names;
  bool first = true;
  while(!at_eof() && !is_word("END-EXEC"))
  {
    if(first && cur().kind == cobol_token_kindt::WORD)
    {
      command = cur().text;
      first = false;
      advance();
      continue;
    }
    first = false;
    if(cur().kind == cobol_token_kindt::WORD && is_output_kw(cur().text))
    {
      advance();
      if(is_kind(cobol_token_kindt::LPAREN))
      {
        advance();
        if(cur().kind == cobol_token_kindt::WORD)
          output_names.push_back(cur().text);
        // skip to the matching ')'
        int depth = 1;
        while(!at_eof() && depth > 0)
        {
          if(is_kind(cobol_token_kindt::LPAREN))
            ++depth;
          else if(is_kind(cobol_token_kindt::RPAREN))
            --depth;
          advance();
        }
      }
      continue;
    }
    advance();
  }
  eat_word("END-EXEC");

  std::vector<stmtt> result;

  // CICS RETURN / XCTL pass control away from this program: terminate the path
  // (CICS Application Programming Reference, RETURN / XCTL).
  if(command == "RETURN" || command == "XCTL")
  {
    stmtt s;
    s.kind = stmtt::kindt::STOP;
    s.location = loc;
    result.push_back(s);
    return result;
  }
  if(command == "ABEND")
  {
    stmtt s;
    s.kind = stmtt::kindt::STOP;
    s.location = loc;
    result.push_back(s);
    return result;
  }

  // Havoc the named output operands.
  for(const std::string &name : output_names)
  {
    auto it = items.find(name);
    if(it != items.end())
      result.push_back(havoc_field(ref_of(it->second), loc));
  }
  // Havoc the per-command EIB status fields (set by every CICS command).
  for(const char *eib : {"EIBRESP", "EIBRESP2", "EIBAID", "EIBRCODE", "EIBFN"})
  {
    auto it = items.find(eib);
    if(it != items.end())
      result.push_back(havoc_field(ref_of(it->second), loc));
  }
  return result;
}

void cobol_typecheckt::inject_eib()
{
  // Synthesise the CICS EXEC INTERFACE BLOCK (DFHEIBLK), which the integrated
  // CICS translator injects into the LINKAGE SECTION, plus the CICS-supplied
  // DFHAID (attention-id values, e.g. DFHENTER) and DFHBMSCA (BMS attribute
  // constants) copybooks, which are not part of the application source. Fields
  // resolve so references type-check (cobol-semantics.md S8). The EIB is
  // nondeterministic; DFHAID/DFHBMSCA constants are given distinct byte values.
  static const std::vector<builtin_fieldt> eib = {
    {"EIBTIME", true, 7, 0, "COMP-3"}, {"EIBDATE", true, 7, 0, "COMP-3"},
    {"EIBTRNID", false, 0, 4, ""},     {"EIBTASKN", true, 7, 0, "COMP-3"},
    {"EIBTRMID", false, 0, 4, ""},     {"EIBCPOSN", true, 4, 0, "COMP"},
    {"EIBCALEN", true, 4, 0, "COMP"},  {"EIBAID", false, 0, 1, ""},
    {"EIBFN", false, 0, 2, ""},        {"EIBRCODE", false, 0, 6, ""},
    {"EIBDS", false, 0, 8, ""},        {"EIBREQID", false, 0, 8, ""},
    {"EIBRSRCE", false, 0, 8, ""},     {"EIBSYNC", false, 0, 1, ""},
    {"EIBFREE", false, 0, 1, ""},      {"EIBRECV", false, 0, 1, ""},
    {"EIBATT", false, 0, 1, ""},       {"EIBEOC", false, 0, 1, ""},
    {"EIBFMH", false, 0, 1, ""},       {"EIBCOMPL", false, 0, 1, ""},
    {"EIBSIG", false, 0, 1, ""},       {"EIBCONF", false, 0, 1, ""},
    {"EIBERR", false, 0, 1, ""},       {"EIBERRCD", false, 0, 4, ""},
    {"EIBSYNRB", false, 0, 1, ""},     {"EIBNODAT", false, 0, 1, ""},
    {"EIBRESP", true, 8, 0, "COMP"},   {"EIBRESP2", true, 8, 0, "COMP"},
    {"EIBRLDBK", false, 0, 1, ""}};
  inject_builtin_record("DFHEIBLK", eib, 0);

  // The CICS-supplied DFHAID (attention identifiers) and DFHBMSCA (BMS
  // attribute constants) copybooks, the IBM MQ CMQ*V copybooks, and similar
  // subsystem copybooks are provided by the bundled copybook library
  // (cobol_copybooks.h) and expanded where the program COPYs them, rather than
  // synthesised here.

  // SQL communication area (SQLCA), referenced by EXEC SQL programs; nondet.
  static const std::vector<builtin_fieldt> sqlca = {
    {"SQLCODE", true, 9, 0, "COMP"},
    {"SQLSTATE", false, 0, 5, ""},
    {"SQLERRM", false, 0, 72, ""},
    {"SQLERRMC", false, 0, 70, ""},
    {"SQLERRP", false, 0, 8, ""},
    {"SQLWARN0", false, 0, 1, ""},
    {"SQLWARN1", false, 0, 1, ""},
    {"SQLWARN2", false, 0, 1, ""},
    {"SQLWARN3", false, 0, 1, ""},
    {"SQLWARN4", false, 0, 1, ""},
    {"SQLWARN5", false, 0, 1, ""},
    {"SQLWARN6", false, 0, 1, ""},
    {"SQLWARN7", false, 0, 1, ""}};
  inject_builtin_record("SQLCA", sqlca, 0);

  // Special registers (IBM LR "Special registers"): RETURN-CODE etc. are
  // numeric globals with an initial value of zero.
  static const std::vector<builtin_fieldt> special = {
    {"RETURN-CODE", true, 9, 0, "COMP"},
    {"SORT-RETURN", true, 9, 0, "COMP"},
    {"TALLY", true, 9, 0, "COMP"}};
  inject_builtin_record("$SPECIAL", special, 2);

  // The IBM MQ trigger message (CMQTML) is provided by the bundled copybook
  // library and expanded where the program COPYs it.

  // IMS DL/I interface block (DIB), referenced by IMS programs. Nondet.
  static const std::vector<builtin_fieldt> dib = {
    {"DIBVER", false, 0, 2, ""},
    {"DIBSTAT", false, 0, 2, ""},
    {"DIBSEGM", false, 0, 8, ""},
    {"DIBSEGLV", false, 0, 2, ""},
    {"DIBDBORG", false, 0, 8, ""},
    {"DIBDBMOD", false, 0, 8, ""}};
  inject_builtin_record("DLIDIB", dib, 0);
}

void cobol_typecheckt::inject_builtin_record(
  const std::string &base,
  const std::vector<builtin_fieldt> &fields,
  int value_mode)
{
  // value_mode: 0 = nondeterministic (no initial value); 1 = distinct constant
  // byte per field; 2 = zero-initialised.
  const irep_idt rec = "cobol::" + program_id + "::" + base;
  std::size_t off = 0;
  std::vector<unsigned char> init;
  unsigned char value_counter = 1;
  for(const builtin_fieldt &f : fields)
  {
    item_infot info;
    info.record_symbol = rec;
    info.offset = off;
    info.is_numeric = f.numeric;
    info.digits = f.digits;
    info.char_count = f.chars;
    info.is_signed = f.numeric;
    info.byte_size = phys_size_of(f.usage, f.numeric, f.digits, f.chars);
    items[f.name] = info;
    all_items.push_back(entryt{f.name, info, {base}});
    if(value_mode == 1)
    {
      // Distinct byte value per field (the exact code is implementation-
      // defined and irrelevant: it is compared against the nondet EIBAID etc.).
      for(std::size_t b = 0; b < info.byte_size; ++b)
        init.push_back(value_counter);
      ++value_counter;
    }
    else if(value_mode == 2)
    {
      for(std::size_t b = 0; b < info.byte_size; ++b)
        init.push_back(0);
    }
    off += info.byte_size;
  }
  record_sizes[rec] = std::max<std::size_t>(off, 1);

  symbolt symbol{rec, record_type(rec), COBOL_MODE};
  symbol.base_name = base;
  symbol.is_static_lifetime = true;
  symbol.is_lvalue = true;
  symbol.is_state_var = true;
  if(value_mode != 0)
  {
    init.resize(record_sizes[rec], 0);
    const unsignedbv_typet byte_type{8};
    array_exprt::operandst ops;
    ops.reserve(init.size());
    for(unsigned char b : init)
      ops.push_back(from_integer(b, byte_type));
    symbol.value = array_exprt{std::move(ops), to_array_type(record_type(rec))};
  }
  symbol_table.add(symbol);
}

void cobol_typecheckt::register_index(const std::string &name)
{
  // An index-name from an OCCURS ... INDEXED BY phrase names an index data
  // item associated with the table (IBM LR "INDEXED BY phrase"). It is not
  // part of any record's storage; model it as a standalone binary integer so
  // it can be SET, used as a subscript, and varied by PERFORM.
  const irep_idt rec = "cobol::" + program_id + "::IDX$" + name;
  item_infot info;
  info.record_symbol = rec;
  info.offset = 0;
  info.is_numeric = true;
  info.is_signed = true;
  info.digits = 9;
  info.scale = 0;
  info.byte_size = phys_size_of("COMP", true, 9, 0);
  items[name] = info;
  all_items.push_back(entryt{name, info, {}});
  record_sizes[rec] = info.byte_size;

  symbolt symbol{rec, record_type(rec), COBOL_MODE};
  symbol.base_name = name;
  symbol.is_static_lifetime = true;
  symbol.is_lvalue = true;
  symbol.is_state_var = true;
  symbol_table.add(symbol);
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

  // EVALUATE supports several selection subjects separated by ALSO; each WHEN
  // lists one selection object per subject, also separated by ALSO, and
  // matches when every object matches its subject (IBM LR "EVALUATE
  // statement"). A subject is the constant TRUE/FALSE (its objects are
  // conditions) or an operand (its objects are values compared for equality or
  // a THRU range).
  struct subjectt
  {
    bool is_const = false; ///< subject is the word TRUE or FALSE
    bool value = true;     ///< TRUE -> true, FALSE -> false
    cond_operandt operand; ///< when !is_const
  };
  const auto parse_subject = [&]()
  {
    subjectt sub;
    if(eat_word("TRUE"))
    {
      sub.is_const = true;
      sub.value = true;
    }
    else if(eat_word("FALSE"))
    {
      sub.is_const = true;
      sub.value = false;
    }
    else
      sub.operand = parse_cond_operand();
    return sub;
  };

  std::vector<subjectt> subjects;
  subjects.push_back(parse_subject());
  while(eat_word("ALSO"))
    subjects.push_back(parse_subject());

  // Build the condition for one selection object against its subject.
  const auto parse_object = [&](const subjectt &subj) -> exprt
  {
    if(eat_word("ANY"))
      return true_exprt{};
    if(subj.is_const)
    {
      // The object is a condition; match it against the subject's truth value.
      const exprt c = parse_condition();
      return subj.value ? c : static_cast<exprt>(not_exprt{c});
    }
    const bool neg = eat_word("NOT");
    cond_operandt w = parse_cond_operand();
    exprt c;
    if(eat_word("THRU") || eat_word("THROUGH"))
    {
      cond_operandt hi = parse_cond_operand();
      c = and_exprt{
        build_cond_relation(subj.operand, ">=", w),
        build_cond_relation(subj.operand, "<=", hi)};
    }
    else
      c = build_cond_relation(subj.operand, "=", w);
    return neg ? static_cast<exprt>(not_exprt{c}) : c;
  };

  while(eat_word("WHEN"))
  {
    if(eat_word("OTHER"))
    {
      s.other_stmts = parse_statements();
      continue;
    }
    // One object per subject, separated by ALSO; the WHEN matches when all
    // objects match (logical AND).
    exprt cond = parse_object(subjects[0]);
    for(std::size_t i = 1; i < subjects.size(); ++i)
    {
      expect_word("ALSO");
      cond = and_exprt{cond, parse_object(subjects[i])};
    }
    std::vector<stmtt> body = parse_statements();
    s.when_clauses.emplace_back(std::move(cond), std::move(body));
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

  // Out-of-line: PERFORM proc-name [THRU proc-name] [control]. A leading word
  // followed by TIMES is a loop count (PERFORM n TIMES with n an identifier),
  // not a procedure name.
  const bool out_of_line =
    cur().kind == cobol_token_kindt::WORD && !is_word("VARYING") &&
    !is_word("UNTIL") && !is_word("WITH") && !is_word("TEST") &&
    !is_word("FOREVER") &&
    !(peek(1).kind == cobol_token_kindt::WORD && peek(1).text == "TIMES");
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
  // Optional WITH TEST {BEFORE|AFTER}: TEST AFTER makes UNTIL a do-while
  // (the body runs once before the condition is tested; IBM LR "PERFORM
  // statement", TEST phrase).
  bool test_after = false;
  eat_word("WITH");
  if(eat_word("TEST"))
  {
    if(eat_word("AFTER"))
      test_after = true;
    else
      eat_word("BEFORE");
  }

  if(eat_word("VARYING"))
  {
    // PERFORM ... VARYING id FROM x BY y UNTIL c [AFTER id2 FROM .. UNTIL c2]...
    // (IBM LR "PERFORM statement", format 4). Each dimension is its own
    // counted loop; an AFTER dimension is re-initialised on every iteration of
    // the dimension before it. This is exactly the behaviour of *nesting* a
    // single-dimension VARYING loop inside the body of the previous one, so
    // the statement is lowered as nested VARYING PERFORMs and reuses the
    // single-dimension code generation.
    struct dimt
    {
      std::vector<stmtt> init;
      std::vector<stmtt> step;
      exprt cond;
    };
    const auto parse_dim = [&]()
    {
      dimt d;
      const reft var_ref = parse_ref();
      if(!var_ref.info->is_numeric)
        error("PERFORM VARYING requires a numeric loop variable");
      expect_word("FROM");
      valuet from = parse_operand();
      d.init.push_back(make_assign_ref(var_ref, from, s.location));
      expect_word("BY");
      valuet by = parse_operand();
      valuet cur_v = read_field(var_ref);
      align(cur_v, by);
      d.step.push_back(make_assign_ref(
        var_ref,
        valuet{plus_exprt{cur_v.expr, by.expr}, cur_v.scale},
        s.location));
      expect_word("UNTIL");
      d.cond = parse_condition();
      return d;
    };

    std::vector<dimt> dims;
    dims.push_back(parse_dim());
    while(eat_word("AFTER"))
      dims.push_back(parse_dim());

    // The actual loop body (out-of-line target or inline statements) belongs
    // to the innermost dimension.
    std::vector<stmtt> body;
    if(s.inline_body)
    {
      body = parse_statements();
      expect_word("END-PERFORM");
    }

    stmtt inner;
    inner.kind = stmtt::kindt::PERFORM;
    inner.location = s.location;
    inner.pkind = stmtt::perform_kindt::VARYING;
    inner.var_init = dims.back().init;
    inner.var_step = dims.back().step;
    inner.cond = dims.back().cond;
    inner.inline_body = s.inline_body;
    inner.body = std::move(body);
    inner.target = s.target;
    inner.target_end = s.target_end;
    for(std::size_t k = dims.size() - 1; k-- > 0;)
    {
      stmtt wrapper;
      wrapper.kind = stmtt::kindt::PERFORM;
      wrapper.location = s.location;
      wrapper.pkind = stmtt::perform_kindt::VARYING;
      wrapper.var_init = dims[k].init;
      wrapper.var_step = dims[k].step;
      wrapper.cond = dims[k].cond;
      wrapper.inline_body = true;
      wrapper.body = {std::move(inner)};
      inner = std::move(wrapper);
    }
    return inner;
  }
  else if(eat_word("UNTIL"))
  {
    s.pkind = stmtt::perform_kindt::UNTIL;
    s.test_after = test_after;
    s.cond = parse_condition();
  }
  else if(
    cur().kind == cobol_token_kindt::NUMBER ||
    (is_item_word() && peek(1).kind == cobol_token_kindt::WORD &&
     peek(1).text == "TIMES"))
  {
    // PERFORM [proc] {integer | identifier} TIMES.
    s.pkind = stmtt::perform_kindt::TIMES;
    if(cur().kind == cobol_token_kindt::NUMBER)
    {
      auto r = parse_decimal(cur().text);
      advance();
      s.times =
        from_integer(rescale_int(r.first, r.second, 0), cobol_value_type());
    }
    else
    {
      const valuet v = read_field(parse_ref());
      s.times = rescale(v.expr, v.scale, 0);
    }
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
      const bool is_section = is_word("SECTION");
      eat_word("SECTION");
      expect_period();
      paragraphs.push_back(paragrapht{name, {}, is_section});
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
    // STOP RUN / GOBACK halt the program: set the shared flag and unwind out
    // of the current $proc frame; each caller propagates it (IBM LR "STOP
    // statement" / "GOBACK statement").
    out.add(code_frontend_assignt{stopped_expr(), true_exprt{}});
    out.add(code_gotot{proc_ret_label()});
    break;
  case stmtt::kindt::EXIT_PERFORM:
    // Leave the innermost inline PERFORM (IBM LR "EXIT statement").
    if(!perform_loop_stack.empty())
      out.add(code_gotot{perform_loop_stack.back().second});
    break;
  case stmtt::kindt::EXIT_CYCLE:
    // Begin the next iteration of the innermost inline PERFORM.
    if(!perform_loop_stack.empty())
      out.add(code_gotot{perform_loop_stack.back().first});
    break;
  case stmtt::kindt::EXIT_PARAGRAPH:
    // Transfer to the end of the current paragraph (IBM LR "EXIT statement").
    if(!cur_para_end_label.empty())
      out.add(code_gotot{cur_para_end_label});
    break;
  case stmtt::kindt::EXIT_SECTION:
    // Transfer to the end of the current section (IBM LR "EXIT statement").
    if(!cur_section_end_label.empty())
      out.add(code_gotot{cur_section_end_label});
    break;
  case stmtt::kindt::PERFORM:
  {
    switch(s.pkind)
    {
    case stmtt::perform_kindt::ONCE:
      if(s.inline_body)
      {
        // An inline PERFORM (one execution) is still a loop scope for EXIT
        // PERFORM / EXIT PERFORM CYCLE, which both fall to its single end.
        const std::string done = fresh_label("pdone");
        perform_loop_stack.emplace_back(done, done);
        gen_perform_invocation(s, out, inlining);
        perform_loop_stack.pop_back();
        out.add(code_labelt{done, code_skipt{}});
      }
      else
        gen_perform_invocation(s, out, inlining);
      break;
    case stmtt::perform_kindt::TIMES:
    {
      const symbol_exprt ctr = make_counter();
      const std::string test = fresh_label("ptest");
      const std::string done = fresh_label("pdone");
      const std::string cycle = fresh_label("pcycle");
      out.add(code_frontend_assignt{ctr, from_integer(0, cobol_value_type())});
      out.add(code_labelt{test, code_skipt{}});
      out.add(code_ifthenelset{
        not_exprt{binary_relation_exprt{ctr, ID_lt, s.times}},
        code_gotot{done}});
      perform_loop_stack.emplace_back(cycle, done);
      gen_perform_invocation(s, out, inlining);
      perform_loop_stack.pop_back();
      out.add(code_labelt{cycle, code_skipt{}});
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
      const std::string cycle = fresh_label("pcycle");
      if(s.test_after)
      {
        out.add(code_labelt{test, code_skipt{}});
        perform_loop_stack.emplace_back(cycle, done);
        gen_perform_invocation(s, out, inlining);
        perform_loop_stack.pop_back();
        out.add(code_labelt{cycle, code_skipt{}});
        out.add(code_ifthenelset{not_exprt{s.cond}, code_gotot{test}});
        out.add(code_labelt{done, code_skipt{}});
      }
      else
      {
        out.add(code_labelt{test, code_skipt{}});
        out.add(code_ifthenelset{s.cond, code_gotot{done}});
        perform_loop_stack.emplace_back(cycle, done);
        gen_perform_invocation(s, out, inlining);
        perform_loop_stack.pop_back();
        out.add(code_labelt{cycle, code_skipt{}});
        out.add(code_gotot{test});
        out.add(code_labelt{done, code_skipt{}});
      }
      break;
    }
    case stmtt::perform_kindt::VARYING:
    {
      const std::string test = fresh_label("ptest");
      const std::string done = fresh_label("pdone");
      const std::string cycle = fresh_label("pcycle");
      gen_statements(s.var_init, out, inlining);
      out.add(code_labelt{test, code_skipt{}});
      out.add(code_ifthenelset{s.cond, code_gotot{done}});
      perform_loop_stack.emplace_back(cycle, done);
      gen_perform_invocation(s, out, inlining);
      perform_loop_stack.pop_back();
      out.add(code_labelt{cycle, code_skipt{}});
      gen_statements(s.var_step, out, inlining);
      out.add(code_gotot{test});
      out.add(code_labelt{done, code_skipt{}});
      break;
    }
    }
    break;
  }
  case stmtt::kindt::SEARCH:
  {
    // Serial scan: at each step, break to AT END if the index is past the
    // table, or to a matching WHEN; otherwise step the index and repeat.
    const std::string test = fresh_label("stest");
    const std::string done = fresh_label("sdone");
    gen_statements(s.var_init, out, inlining);
    out.add(code_labelt{test, code_skipt{}});
    {
      code_blockt at_end;
      gen_statements(s.other_stmts, at_end, inlining);
      at_end.add(code_gotot{done});
      out.add(code_ifthenelset{s.cond, std::move(at_end)});
    }
    for(const auto &when : s.when_clauses)
    {
      code_blockt when_block;
      gen_statements(when.second, when_block, inlining);
      when_block.add(code_gotot{done});
      out.add(code_ifthenelset{when.first, std::move(when_block)});
    }
    gen_statements(s.var_step, out, inlining);
    out.add(code_gotot{test});
    out.add(code_labelt{done, code_skipt{}});
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
    range_end(s.target_end.empty() ? s.target : s.target_end);
  if(end < start)
    error("PERFORM THRU range ends before it starts");

  // PERFORM proc-1 [THRU proc-2] is a call to the re-entrant procedure
  // function, entering at proc-1 and returning at the end of the range
  // (IBM LR "PERFORM statement"). A recursive PERFORM is therefore a recursive
  // call, which CBMC bounds by unwinding (no translation-time pruning).
  code_function_callt call{
    proc_symbol_expr(),
    {from_integer(start, cobol_value_type()),
     from_integer(end, cobol_value_type())}};
  call.add_source_location() = s.location;
  out.add(std::move(call));

  // A STOP RUN reached inside the performed range halts the program: unwind
  // out of the current frame too.
  out.add(code_ifthenelset{stopped_expr(), code_gotot{proc_ret_label()}});
}

void cobol_typecheckt::build_function()
{
  const typet idx_type = cobol_value_type();
  const std::size_t n = paragraphs.size();

  // The whole PROCEDURE DIVISION becomes one re-entrant function
  //   $proc(entry, exit)
  // holding every paragraph as a labelled region, so GO TO and fall-through
  // stay ordinary branches within it (IBM LR "GO TO statement", "Explicit and
  // implicit transfers of control"). Control enters at paragraph `entry` and
  // returns when it reaches the end of paragraph `exit`; PERFORM is a
  // (possibly recursive) call to $proc, so a recursive PERFORM is bounded by
  // CBMC's unwinding rather than pruned (IBM LR "PERFORM statement", the
  // return mechanism). See
  // doc/architectural/cobol-perform-control-flow-design.md.

  // The program-halt flag (STOP RUN / GOBACK), shared across all $proc frames.
  {
    symbolt flag{stopped_name(), bool_typet{}, COBOL_MODE};
    flag.base_name = "$stopped";
    flag.is_static_lifetime = true;
    flag.is_lvalue = true;
    flag.is_state_var = true;
    flag.value = false_exprt{};
    symbol_table.add(flag);
  }

  // $proc parameters.
  const auto add_param = [&](const irep_idt &name, const char *base)
  {
    symbolt p{name, idx_type, COBOL_MODE};
    p.base_name = base;
    p.is_parameter = true;
    p.is_lvalue = true;
    p.is_thread_local = true;
    p.is_file_local = true;
    symbol_table.add(p);
  };
  add_param(entry_param_name(), "entry");
  add_param(exit_param_name(), "exit");
  const symbol_exprt entry_arg{entry_param_name(), idx_type};
  const symbol_exprt exit_arg{exit_param_name(), idx_type};

  code_blockt proc_body;
  std::set<std::string> inlining; // retained for the gen_* signatures

  // $proc is only ever entered at a PERFORM range start (or paragraph 0 for
  // the main run) and only ever returns at a range end (or the last paragraph
  // for the main run), so the entry dispatch and the per-paragraph end checks
  // are emitted only for those indices rather than for every paragraph.
  std::set<std::size_t> entry_targets{0};
  std::set<std::size_t> exit_targets;
  if(n > 0)
    exit_targets.insert(n - 1);
  for(const paragrapht &p : paragraphs)
    collect_perform_targets(p.statements, entry_targets, exit_targets);

  // Entry dispatch: jump to paragraph `entry`.
  for(std::size_t i : entry_targets)
    proc_body.add(code_ifthenelset{
      equal_exprt{entry_arg, from_integer(i, idx_type)},
      code_gotot{para_label(paragraphs[i].name)}});

  // One section-end label per section (keyed by the section's last paragraph),
  // the target of EXIT SECTION; the last paragraph index of the section
  // containing paragraph i is found by scanning back to its SECTION header and
  // forward to the next one (IBM LR "EXIT statement").
  std::map<std::size_t, std::string> section_end_labels;
  const auto section_last_of = [&](std::size_t i, bool &in_sec) -> std::size_t
  {
    std::size_t h = i;
    while(h > 0 && !paragraphs[h].is_section)
      --h;
    in_sec = paragraphs[h].is_section;
    for(std::size_t j = h + 1; j < n; ++j)
      if(paragraphs[j].is_section)
        return j - 1;
    return n == 0 ? 0 : n - 1;
  };

  for(std::size_t i = 0; i < n; ++i)
  {
    proc_body.add(code_labelt{para_label(paragraphs[i].name), code_skipt{}});
    cur_para_end_label = fresh_label("paraend");
    bool in_sec = false;
    const std::size_t sl = section_last_of(i, in_sec);
    if(in_sec)
    {
      auto it = section_end_labels.find(sl);
      if(it == section_end_labels.end())
        it = section_end_labels.emplace(sl, fresh_label("sectend")).first;
      cur_section_end_label = it->second;
    }
    else
      cur_section_end_label.clear();

    gen_statements(paragraphs[i].statements, proc_body, inlining);

    // EXIT PARAGRAPH lands here, then normal end-of-paragraph flow applies.
    proc_body.add(code_labelt{cur_para_end_label, code_skipt{}});
    // EXIT SECTION lands here: emitted after the section's last paragraph.
    if(in_sec && i == sl)
      proc_body.add(code_labelt{section_end_labels[sl], code_skipt{}});
    // Return to the activating PERFORM when control reaches the end of the
    // range-exit paragraph (IBM LR "PERFORM statement").
    if(exit_targets.count(i) != 0)
      proc_body.add(code_ifthenelset{
        equal_exprt{from_integer(i, idx_type), exit_arg},
        code_gotot{proc_ret_label()}});
  }
  proc_body.add(code_labelt{proc_ret_label(), code_skipt{}});

  {
    symbolt proc{proc_name(), proc_type(), COBOL_MODE};
    proc.base_name = "$proc";
    proc.value = std::move(proc_body);
    symbol_table.add(proc);
  }

  // The public program function runs the whole range once: $proc(0, n-1).
  code_blockt body;
  code_function_callt call{
    proc_symbol_expr(),
    {from_integer(0, idx_type), from_integer(n == 0 ? 0 : n - 1, idx_type)}};
  body.add(std::move(call));

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
