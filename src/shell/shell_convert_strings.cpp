/// \file
/// Shell converter: the representations of strings (--shell-strings).
///
/// Both representations wrap a payload in a struct with the `set` flag,
/// which needs no byte-level layout because shell has no way to
/// reinterpret memory:
///   * refined: a refined string {length, content} for the string
///     refinement solver. Operations are `cprover_string_*` applications
///     whose results are fresh temporaries, declared where the operation
///     is, so each execution constrains new symbols.
///   * native: the SMT-LIB String sort; operations are terms that
///     smt2_conv lowers to str.* and re.* operators.
///
/// Neither representation limits the length of the strings that a script
/// builds. Refined strings bound the inputs (the environment, arguments,
/// command output) to --shell-max-string-length bytes, as the solver only
/// checks its models up to a length.

#include "shell_convert_class.h"

#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/refined_string_type.h>
#include <util/std_types.h>
#include <util/string_expr.h>

#include <algorithm>
#include <bitset>
#include <cstdlib>

// ------------------------------------------------------------------
// Types

typet shell_convertert::payload_type() const
{
  if(native())
    return string_typet{};
  return refined_string_typet{
    integer_type(), pointer_type(unsigned_char_type())};
}

void shell_convertert::add_value_type()
{
  const irep_idt name = "tag-" SHELL_VALUE_TAG;
  if(symbol_table.has_symbol(name))
    return;
  // is_int: the string is the decimal representation of int_value, as
  // it was made from an integer. Integer operands then need no parsing:
  // shell values are often integers, and parsing is the most expensive
  // operation for the solvers.
  struct_typet type{
    {{"set", c_bool_type()},
     {"value", payload_type()},
     {"is_int", c_bool_type()},
     {"int_value", integer_type()}}};
  type.set_tag(SHELL_VALUE_TAG);
  symbolt symbol{name, type, shell_mode()};
  symbol.base_name = SHELL_VALUE_TAG;
  symbol.pretty_name = SHELL_VALUE_TAG;
  symbol.is_type = true;
  symbol_table.add(symbol);

  if(!native())
  {
    // goto-symex associates the arrays of constant strings with their
    // pointers through this function, which must be declared.
    const array_typet array_type{
      unsigned_char_type(), infinity_exprt{integer_type()}};
    for(const irep_idt &id :
        {ID_cprover_associate_array_to_pointer_func,
         ID_cprover_associate_length_to_array_func})
    {
      const typet second = id == ID_cprover_associate_array_to_pointer_func
                             ? typet{pointer_type(unsigned_char_type())}
                             : integer_type();
      symbolt function{
        id,
        mathematical_function_typet{{array_type, second}, signedbv_typet{32}},
        shell_mode()};
      function.base_name = id;
      symbol_table.add(function);
    }
  }
}

exprt shell_convertert::make_value(exprt payload) const
{
  return struct_exprt{
    {from_integer(1, c_bool_type()),
     std::move(payload),
     from_integer(0, c_bool_type()),
     from_integer(0, integer_type())},
    string_type()};
}

exprt shell_convertert::make_int_value(exprt payload, const exprt &value) const
{
  return struct_exprt{
    {from_integer(1, c_bool_type()),
     std::move(payload),
     from_integer(1, c_bool_type()),
     typecast_exprt::conditional_cast(value, integer_type())},
    string_type()};
}

exprt shell_convertert::set_value(const exprt &value) const
{
  return struct_exprt{
    {from_integer(1, c_bool_type()),
     payload_of(value),
     member_exprt{value, "is_int", c_bool_type()},
     member_exprt{value, "int_value", integer_type()}},
    string_type()};
}

exprt shell_convertert::known_int(const exprt &value) const
{
  return notequal_exprt{
    member_exprt{value, "is_int", c_bool_type()},
    from_integer(0, c_bool_type())};
}

namespace
{
/// The refined string \p payload as an explicit {length, content} struct,
/// which the string solver and goto-symex expect as operands.
exprt refined_operand(const exprt &payload)
{
  if(payload.id() == ID_struct && payload.operands().size() == 2)
    return payload;
  const struct_typet &type = to_struct_type(payload.type());
  return refined_string_exprt{
    member_exprt{payload, "length", type.components()[0].type()},
    member_exprt{payload, "content", type.components()[1].type()},
    payload.type()};
}

exprt refined_length(const exprt &payload)
{
  return refined_operand(payload).operands()[0];
}

/// An application of the function \p id, which is declared in
/// \p symbol_table with the types of \p arguments if it is not yet.
function_application_exprt apply_function(
  symbol_table_baset &symbol_table,
  const irep_idt &id,
  const exprt::operandst &arguments,
  const typet &codomain)
{
  if(!symbol_table.has_symbol(id))
  {
    mathematical_function_typet::domaint domain;
    for(const auto &argument : arguments)
      domain.push_back(argument.type());
    symbolt function{
      id, mathematical_function_typet{domain, codomain}, shell_mode()};
    function.base_name = id;
    symbol_table.add(function);
  }
  function_application_exprt application{
    symbol_table.lookup_ref(id).symbol_expr(), arguments};
  application.type() = codomain;
  return application;
}

/// Integers of the SMT-LIB Int sort, for native string operations.
exprt to_int_sort(const exprt &value)
{
  if(value.is_constant() && value.type().id() != ID_integer)
  {
    const auto n = numeric_cast<mp_integer>(value);
    if(n.has_value())
      return from_integer(*n, integer_typet{});
  }
  return typecast_exprt::conditional_cast(value, integer_typet{});
}

/// Regular expressions over the SMT-LIB RegLan sort.
class regex_buildert
{
public:
  explicit regex_buildert(symbol_table_baset &symbol_table)
    : symbol_table(symbol_table)
  {
  }

  exprt literal(const std::string &text)
  {
    return apply(
      ID_cprover_string_to_regex_func, {constant_exprt{text, string_typet{}}});
  }

  /// One byte in [low, high].
  exprt range(unsigned char low, unsigned char high)
  {
    return apply(
      ID_cprover_regex_range_func,
      {constant_exprt{std::string(1, static_cast<char>(low)), string_typet{}},
       constant_exprt{
         std::string(1, static_cast<char>(high)), string_typet{}}});
  }

  exprt any()
  {
    return apply(ID_cprover_regex_allchar_func, {});
  }

  exprt none()
  {
    return apply(ID_cprover_regex_none_func, {});
  }

  exprt star(const exprt &r)
  {
    return apply(ID_cprover_regex_star_func, {r});
  }

  exprt plus(const exprt &r)
  {
    return apply(ID_cprover_regex_plus_func, {r});
  }

  exprt optional(const exprt &r)
  {
    return apply(ID_cprover_regex_opt_func, {r});
  }

  exprt either(const exprt &a, const exprt &b)
  {
    return apply(ID_cprover_regex_union_func, {a, b});
  }

  exprt sequence(const std::vector<exprt> &rs)
  {
    PRECONDITION(!rs.empty());
    exprt result = rs.back();
    for(auto it = std::next(rs.rbegin()); it != rs.rend(); ++it)
      result = apply(ID_cprover_regex_concat_func, {*it, result});
    return result;
  }

  /// Whether the string \p s is in the language of \p r.
  exprt matches(const exprt &s, const exprt &r)
  {
    return apply_function(
      symbol_table, ID_cprover_string_in_regex_func, {s, r}, bool_typet{});
  }

private:
  symbol_table_baset &symbol_table;

  exprt apply(const irep_idt &id, const exprt::operandst &arguments)
  {
    return apply_function(symbol_table, id, arguments, typet{ID_regex});
  }
};

exprt is_blank(const exprt &c)
{
  // isspace() in the C locale: space, and \t through \r.
  return or_exprt{
    equal_exprt{c, from_integer(' ', c.type())},
    and_exprt{
      binary_relation_exprt{c, ID_ge, from_integer('\t', c.type())},
      binary_relation_exprt{c, ID_le, from_integer('\r', c.type())}}};
}

/// The number of blanks around an integer that refined strings skip.
const std::size_t refined_blanks = 2;
} // namespace

// ------------------------------------------------------------------
// Primitive operations on payloads

exprt shell_convertert::string_function(
  const irep_idt &id,
  const exprt::operandst &arguments,
  const typet &type,
  code_blockt &dest,
  const source_locationt &loc)
{
  exprt::operandst operands;
  // Refined strings are results through two output arguments, the length
  // and content of the result.
  const bool refined_result = !native() && type == payload_type();
  std::optional<refined_string_exprt> result;
  if(refined_result)
  {
    const symbol_exprt length = temporary(integer_type(), dest, loc);
    const symbol_exprt content =
      temporary(pointer_type(unsigned_char_type()), dest, loc);
    result = refined_string_exprt{length, content, payload_type()};
    back_refined_string(*result, dest, loc);
    operands.push_back(length);
    operands.push_back(content);
  }
  for(const auto &argument : arguments)
  {
    operands.push_back(
      !native() && argument.type() == payload_type() ? refined_operand(argument)
                                                     : argument);
  }
  // Native integer results are of the Int sort.
  const bool int_result = native() && type == integer_type();
  const typet codomain = refined_result ? typet{signedbv_typet{32}}
                         : int_result   ? typet{integer_typet{}}
                                        : type;

  function_application_exprt application =
    apply_function(symbol_table, id, operands, codomain);

  if(refined_result)
  {
    assign(dest, temporary(codomain, dest, loc), std::move(application), loc);
    return *result;
  }
  const symbol_exprt value = temporary(type, dest, loc);
  assign(dest, value, std::move(application), loc);
  return value;
}

void shell_convertert::back_refined_string(
  const exprt &payload,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Give the content an array of its own, of the string's length: else
  // the solver may choose a length for the array that is unrelated to the
  // length of the string. A temporary array is fresh on each execution.
  const exprt length = refined_length(payload);
  const exprt content = refined_operand(payload).operands()[1];
  const symbol_exprt array = temporary(
    array_typet{unsigned_char_type(), infinity_exprt{integer_type()}},
    dest,
    loc);
  assign(
    dest,
    content,
    address_of_exprt{
      index_exprt{array, from_integer(0, integer_type())},
      to_pointer_type(content.type())},
    loc);
  for(const auto &association :
      {std::pair<irep_idt, exprt>{
         ID_cprover_associate_array_to_pointer_func, content},
       std::pair<irep_idt, exprt>{
         ID_cprover_associate_length_to_array_func, length}})
  {
    function_application_exprt application{
      symbol_table.lookup_ref(association.first).symbol_expr(),
      {array, association.second}};
    application.type() = signedbv_typet{32};
    assign(
      dest,
      temporary(signedbv_typet{32}, dest, loc),
      std::move(application),
      loc);
  }
}

exprt shell_convertert::payload_literal(const std::string &text) const
{
  if(native())
    return constant_exprt{text, string_typet{}};
  exprt::operandst bytes;
  for(char c : text)
  {
    bytes.push_back(
      from_integer(static_cast<unsigned char>(c), unsigned_char_type()));
  }
  const array_exprt array{
    std::move(bytes),
    array_typet{
      unsigned_char_type(), from_integer(text.size(), integer_type())}};
  return refined_string_exprt{
    from_integer(text.size(), integer_type()),
    address_of_exprt{
      index_exprt{array, from_integer(0, integer_type())},
      pointer_type(unsigned_char_type())},
    payload_type()};
}

exprt shell_convertert::payload_length(
  const exprt &payload,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(native())
  {
    if(payload.is_constant())
    {
      return from_integer(
        to_constant_expr(payload).get_value().size(), integer_type());
    }
    return string_function(
      ID_cprover_string_length_func, {payload}, integer_type(), dest, loc);
  }
  return refined_length(payload);
}

exprt shell_convertert::payload_concat(
  const exprt &a,
  const exprt &b,
  code_blockt &dest,
  const source_locationt &loc)
{
  return string_function(
    ID_cprover_string_concat_func, {a, b}, payload_type(), dest, loc);
}

exprt shell_convertert::payload_substring(
  const exprt &payload,
  const exprt &start,
  const exprt &end,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(native())
  {
    // str.substr takes an offset and a length.
    return string_function(
      ID_cprover_string_substring_func,
      {payload, to_int_sort(start), to_int_sort(minus_exprt{end, start})},
      payload_type(),
      dest,
      loc);
  }
  return string_function(
    ID_cprover_string_substring_func,
    {payload, start, end},
    payload_type(),
    dest,
    loc);
}

exprt shell_convertert::payload_has_prefix(
  const exprt &payload,
  const exprt &prefix,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Both back ends take the prefix first.
  return string_function(
    ID_cprover_string_is_prefix_func,
    {prefix, payload},
    bool_typet{},
    dest,
    loc);
}

exprt shell_convertert::payload_has_suffix(
  const exprt &payload,
  const exprt &suffix,
  code_blockt &dest,
  const source_locationt &loc)
{
  return string_function(
    ID_cprover_string_is_suffix_func,
    {suffix, payload},
    bool_typet{},
    dest,
    loc);
}

exprt shell_convertert::payload_index_of(
  const exprt &payload,
  const exprt &needle,
  const exprt &from,
  code_blockt &dest,
  const source_locationt &loc)
{
  return string_function(
    ID_cprover_string_index_of_func,
    {payload, needle, native() ? to_int_sort(from) : from},
    integer_type(),
    dest,
    loc);
}

exprt shell_convertert::payload_last_index_of(
  const exprt &payload,
  const exprt &needle,
  const exprt &upto,
  bool anywhere,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!native())
  {
    return string_function(
      ID_cprover_string_last_index_of_func,
      {payload, needle, upto},
      integer_type(),
      dest,
      loc);
  }
  // SMT-LIB has no last-index operator. For a single byte c anywhere in
  // the string, the rest r after the last c is a suffix without c; this
  // is much faster in z3 than the index-based encoding below.
  if(
    anywhere && needle.is_constant() &&
    to_constant_expr(needle).get_value().size() == 1)
  {
    const symbol_exprt rest = temporary(payload_type(), dest, loc);
    const exprt has_c = string_function(
      ID_cprover_string_contains_func, {rest, needle}, bool_typet{}, dest, loc);
    const exprt c_rest = payload_concat(needle, rest, dest, loc);
    add(
      dest,
      code_assumet{conjunction(
        {string_function(
           ID_cprover_string_is_suffix_func,
           {rest, payload},
           bool_typet{},
           dest,
           loc),
         not_exprt{has_c},
         or_exprt{
           equal_exprt{rest, payload},
           string_function(
             ID_cprover_string_is_suffix_func,
             {c_rest, payload},
             bool_typet{},
             dest,
             loc)}})},
      loc);
    const exprt length = payload_length(payload, dest, loc);
    return if_exprt{
      equal_exprt{rest, payload},
      from_integer(-1, integer_type()),
      minus_exprt{
        minus_exprt{length, payload_length(rest, dest, loc)},
        from_integer(1, integer_type())}};
  }
  // Otherwise j is -1 or an occurrence at or before upto, and there is
  // none after j up to upto.
  const symbol_exprt j = temporary(integer_type(), dest, loc);
  const exprt needle_length = payload_length(needle, dest, loc);
  const exprt at_j = string_function(
    ID_cprover_string_substring_func,
    {payload, to_int_sort(j), to_int_sort(needle_length)},
    payload_type(),
    dest,
    loc);
  const exprt minus_one = from_integer(-1, integer_type());
  const exprt next = payload_index_of(
    payload, needle, plus_exprt{j, from_integer(1, integer_type())}, dest, loc);
  add(
    dest,
    code_assumet{and_exprt{
      or_exprt{
        equal_exprt{j, minus_one},
        and_exprt{
          binary_relation_exprt{j, ID_ge, from_integer(0, integer_type())},
          binary_relation_exprt{j, ID_le, upto},
          equal_exprt{at_j, needle}}},
      or_exprt{
        equal_exprt{next, minus_one},
        binary_relation_exprt{next, ID_gt, upto}}}},
    loc);
  return j;
}

// ------------------------------------------------------------------
// Values

exprt shell_convertert::is_empty(const exprt &string) const
{
  if(native())
    return equal_exprt{payload_of(string), payload_literal("")};
  return equal_exprt{
    refined_length(payload_of(string)), from_integer(0, integer_type())};
}

exprt shell_convertert::string_literal(
  const std::string &text,
  const source_locationt &)
{
  // Decimal numbers without leading zeros (which are octal in some
  // contexts) are integers whose value is known.
  const bool decimal =
    !text.empty() && text.size() <= 18 &&
    text.find_first_not_of("0123456789") == std::string::npos &&
    (text[0] != '0' || text.size() == 1);
  if(decimal)
  {
    return make_int_value(
      payload_literal(text),
      from_integer(string2integer(text), integer_type()));
  }
  return make_value(payload_literal(text));
}

exprt shell_convertert::unset_string(const source_locationt &loc)
{
  exprt result = string_literal("", loc);
  for(auto &operand : result.operands())
  {
    if(operand.type() == component_type("set"))
    {
      operand = from_integer(0, component_type("set"));
      break;
    }
  }
  return result;
}

exprt shell_convertert::concat(
  const exprt &a,
  const exprt &b,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Literals, such as known values, are joined here.
  const auto literal_a = literal_of(a);
  const auto literal_b = literal_of(b);
  if(literal_a.has_value() && literal_b.has_value())
    return string_literal(*literal_a + *literal_b, loc);
  return make_value(payload_concat(payload_of(a), payload_of(b), dest, loc));
}

exprt shell_convertert::equal(
  const exprt &a,
  const exprt &b,
  code_blockt &dest,
  const source_locationt &loc)
{
  return payload_equal(payload_of(a), payload_of(b), dest, loc);
}

exprt shell_convertert::payload_equal(
  const exprt &a,
  const exprt &b,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Refined payloads are a length and a pointer: equal_exprt would compare
  // the pointers.
  if(native())
    return equal_exprt{a, b};
  return string_function(
    ID_cprover_string_equal_func, {a, b}, bool_typet{}, dest, loc);
}

exprt shell_convertert::from_integer_expr(
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt v = typecast_exprt::conditional_cast(value, integer_type());
  if(!native())
  {
    return make_int_value(
      string_function(
        ID_cprover_string_of_int_func, {v}, payload_type(), dest, loc),
      v);
  }
  // str.from_int is "" for negative numbers.
  const exprt n = to_int_sort(v);
  const exprt negative =
    binary_relation_exprt{n, ID_lt, from_integer(0, integer_typet{})};
  const exprt digits = string_function(
    ID_cprover_string_from_int_func,
    {if_exprt{negative, unary_minus_exprt{n}, n}},
    payload_type(),
    dest,
    loc);
  const exprt signed_digits =
    payload_concat(payload_literal("-"), digits, dest, loc);
  return make_int_value(if_exprt{negative, signed_digits, digits}, v);
}

exprt shell_convertert::string_length(
  const exprt &string,
  code_blockt &dest,
  const source_locationt &loc)
{
  return payload_length(payload_of(string), dest, loc);
}

exprt shell_convertert::nondet_string(
  code_blockt &dest,
  const source_locationt &loc)
{
  const symbol_exprt payload = temporary(payload_type(), dest, loc);
  if(native())
  {
    // Shells do not store null bytes in variables, and SMT-LIB strings
    // have code points up to 0x2FFFF: the bytes are 1 to 255.
    regex_buildert re{symbol_table};
    add(
      dest, code_assumet{re.matches(payload, re.star(re.range(1, 255)))}, loc);
  }
  else
  {
    // Null bytes are not excluded, which over-approximates the inputs.
    const exprt length = refined_length(payload);
    back_refined_string(payload, dest, loc);
    add(
      dest,
      code_assumet{and_exprt{
        binary_relation_exprt{length, ID_ge, from_integer(0, integer_type())},
        // The solver checks its models only for strings of at most 2^26
        // bytes; inputs have at most --shell-max-string-length bytes.
        binary_relation_exprt{
          length, ID_le, from_integer(max_length, integer_type())}}},
      loc);
  }
  return make_value(payload);
}

exprt shell_convertert::environment_string(
  code_blockt &dest,
  const source_locationt &loc)
{
  // An unset value has an empty payload, which value_of relies on.
  const exprt value = nondet_string(dest, loc);
  const symbol_exprt set = temporary(bool_typet{}, dest, loc);
  add(dest, code_assumet{or_exprt{set, is_empty(value)}}, loc);
  return struct_exprt{
    {typecast_exprt{set, c_bool_type()},
     payload_of(value),
     from_integer(0, c_bool_type()),
     from_integer(0, integer_type())},
    string_type()};
}

// ------------------------------------------------------------------
// Integers in strings

exprt shell_convertert::test_integer(
  const exprt &string,
  trailing_blankst trailing,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Strings made from integers need no parsing. The parser is in a branch
  // of its own, which symbolic execution skips when the string is known to
  // be an integer, as for counters: the parser's string constraints are
  // then not generated at all.
  const exprt known = known_int(string);
  const symbol_exprt valid = temporary(bool_typet{}, dest, loc);
  code_blockt use_int;
  assign(
    use_int, value, member_exprt{string, "int_value", integer_type()}, loc);
  assign(use_int, valid, true_exprt{}, loc);
  code_blockt parse;
  const symbol_exprt parsed = temporary(integer_type(), parse, loc);
  const exprt parsed_valid =
    parse_test_integer(string, trailing, parsed, parse, loc);
  assign(parse, value, parsed, loc);
  assign(parse, valid, parsed_valid, loc);
  add(dest, code_ifthenelset{known, std::move(use_int), std::move(parse)}, loc);
  return valid;
}

exprt shell_convertert::parse_test_integer(
  const exprt &string,
  trailing_blankst trailing,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt payload = payload_of(string);
  const exprt zero = from_integer(0, integer_type());

  if(native())
  {
    // s = blanks sign digits blanks, where the parts are unique if s has
    // this syntax, as blanks, signs and digits are distinct bytes.
    regex_buildert re{symbol_table};
    const exprt blank = re.either(re.literal(" "), re.range('\t', '\r'));
    const exprt blanks = re.star(blank);
    const exprt sign = re.optional(re.either(re.literal("-"), re.literal("+")));
    const exprt digits = re.plus(re.range('0', '9'));
    const exprt trailing_blanks =
      trailing == trailing_blankst::SPACE_TAB
        ? re.star(re.either(re.literal(" "), re.literal("\t")))
        : blanks;
    std::vector<exprt> sequence{blanks, sign, digits};
    if(trailing != trailing_blankst::NONE)
      sequence.push_back(trailing_blanks);
    const exprt syntax = re.matches(payload, re.sequence(sequence));

    const symbol_exprt leading = temporary(payload_type(), dest, loc);
    const symbol_exprt sign_part = temporary(payload_type(), dest, loc);
    const symbol_exprt digit_part = temporary(payload_type(), dest, loc);
    const symbol_exprt trailing_part = temporary(payload_type(), dest, loc);
    exprt rebuilt = payload_concat(leading, sign_part, dest, loc);
    rebuilt = payload_concat(rebuilt, digit_part, dest, loc);
    rebuilt = payload_concat(rebuilt, trailing_part, dest, loc);
    // Most numbers are plain digits, or a sign and digits, which need no
    // decomposition: z3 is slow to convert parts of a string that are only
    // determined by a word equation, even for constants.
    const exprt plain = re.matches(payload, digits);
    const exprt sign_char = re.either(re.literal("-"), re.literal("+"));
    const exprt is_signed =
      re.matches(payload, re.sequence({sign_char, digits}));
    const exprt one = from_integer(1, integer_type());
    const exprt unsigned_part = payload_substring(
      payload, one, payload_length(payload, dest, loc), dest, loc);
    const exprt first = payload_substring(payload, zero, one, dest, loc);
    add(
      dest,
      code_assumet{implies_exprt{
        conjunction({syntax, not_exprt{plain}, not_exprt{is_signed}}),
        conjunction(
          {equal_exprt{payload, rebuilt},
           re.matches(leading, blanks),
           re.matches(sign_part, sign),
           re.matches(digit_part, digits),
           trailing != trailing_blankst::NONE
             ? re.matches(trailing_part, trailing_blanks)
             : exprt{equal_exprt{trailing_part, payload_literal("")}}})}},
      loc);
    // The magnitude, of the Int sort, so that values out of range are
    // detected rather than wrapped.
    const exprt to_int = apply_function(
      symbol_table,
      ID_cprover_string_parse_int_func,
      {if_exprt{
        plain, payload, if_exprt{is_signed, unsigned_part, digit_part}}},
      integer_typet{});
    const exprt negative = and_exprt{
      not_exprt{plain},
      equal_exprt{
        if_exprt{is_signed, first, exprt{sign_part}}, payload_literal("-")}};
    const exprt in_range = binary_relation_exprt{
      to_int,
      ID_le,
      if_exprt{
        negative,
        from_integer(power(2, 63), integer_typet{}),
        from_integer(power(2, 63) - 1, integer_typet{})}};
    assign(
      dest,
      value,
      typecast_exprt::conditional_cast(
        if_exprt{negative, unary_minus_exprt{to_int}, to_int}, integer_type()),
      loc);
    return and_exprt{syntax, in_range};
  }

  // Refined strings: is_valid_long accepts an optional sign and decimal
  // digits, up to 20 bytes. Up to refined_blanks blanks before (and, if
  // trailing blanks are allowed, after) the number are skipped; more
  // blanks, and numbers of more than 18 bytes (whose conversion the solver
  // constrains to not overflow), are over-approximated by a
  // nondeterministic result.
  const exprt length = payload_length(payload, dest, loc);
  const exprt leading = refined_blank_count(payload, false, false, dest, loc);
  const exprt trailing_count =
    trailing != trailing_blankst::NONE
      ? refined_blank_count(
          payload, true, trailing == trailing_blankst::SPACE_TAB, dest, loc)
      : exprt{zero};
  const exprt few_blanks = and_exprt{
    binary_relation_exprt{
      leading, ID_le, from_integer(refined_blanks, integer_type())},
    binary_relation_exprt{
      trailing_count, ID_le, from_integer(refined_blanks, integer_type())}};
  const exprt number_length =
    minus_exprt{length, plus_exprt{leading, trailing_count}};
  const exprt start = if_exprt{few_blanks, leading, zero};
  const exprt end = if_exprt{
    and_exprt{few_blanks, binary_relation_exprt{number_length, ID_ge, zero}},
    minus_exprt{length, trailing_count},
    start};
  const exprt number = payload_substring(payload, start, end, dest, loc);
  const exprt syntax = string_function(
    ID_cprover_string_is_valid_long_func, {number}, bool_typet{}, dest, loc);
  const exprt is_short = binary_relation_exprt{
    number_length, ID_le, from_integer(18, integer_type())};
  // Parse a copy that is empty unless the number is short and valid, so
  // that the solver's axioms do not constrain other strings.
  const exprt guarded = payload_substring(
    number,
    zero,
    if_exprt{and_exprt{syntax, is_short}, number_length, zero},
    dest,
    loc);
  const exprt parsed = string_function(
    ID_cprover_string_parse_int_func, {guarded}, integer_type(), dest, loc);
  const exprt precise =
    and_exprt{few_blanks, or_exprt{is_short, not_exprt{syntax}}};
  assign(
    dest, value, if_exprt{precise, parsed, nondet(integer_type(), loc)}, loc);
  return if_exprt{precise, syntax, nondet(bool_typet{}, loc)};
}

exprt shell_convertert::refined_blank_count(
  const exprt &payload,
  bool at_end,
  bool space_tab,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt length = payload_length(payload, dest, loc);
  // Nested choices: s[0] is not a blank ? 0 : s[1] is not ? 1 : ..., where
  // the positions count from the end if at_end.
  exprt count = from_integer(refined_blanks + 1, integer_type());
  for(std::size_t k = refined_blanks + 1; k-- > 0;)
  {
    const exprt position =
      at_end ? exprt{minus_exprt{length, from_integer(k + 1, integer_type())}}
             : from_integer(k, integer_type());
    const exprt c = string_function(
      ID_cprover_string_char_at_func,
      {payload, position},
      unsigned_char_type(),
      dest,
      loc);
    count = if_exprt{
      and_exprt{
        binary_relation_exprt{length, ID_gt, from_integer(k, integer_type())},
        space_tab ? exprt{or_exprt{
                      equal_exprt{c, from_integer(' ', c.type())},
                      equal_exprt{c, from_integer('\t', c.type())}}}
                  : is_blank(c)},
      count,
      from_integer(k, integer_type())};
  }
  return count;
}

exprt shell_convertert::arith_integer(
  const exprt &string,
  bool leading_zero_octal,
  bool any_space,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  // As in test_integer, the parser is in a branch that symbolic execution
  // skips for strings known to be integers.
  const symbol_exprt valid = temporary(bool_typet{}, dest, loc);
  code_blockt use_int;
  assign(
    use_int, value, member_exprt{string, "int_value", integer_type()}, loc);
  assign(use_int, valid, true_exprt{}, loc);
  code_blockt parse;
  const symbol_exprt parsed = temporary(integer_type(), parse, loc);
  const exprt parsed_valid = parse_arith_integer(
    string, leading_zero_octal, any_space, parsed, parse, loc);
  assign(parse, value, parsed, loc);
  assign(parse, valid, parsed_valid, loc);
  add(
    dest,
    code_ifthenelset{known_int(string), std::move(use_int), std::move(parse)},
    loc);
  return valid;
}

exprt shell_convertert::parse_arith_integer(
  const exprt &string,
  bool leading_zero_octal,
  bool any_space,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Decimal numbers only: octal and hexadecimal constants, and blanks
  // other than those of the test utility, are reported as unsupported by
  // the caller's check.
  const exprt payload = payload_of(string);
  const exprt zero = from_integer(0, integer_type());
  const exprt empty = is_empty(string);
  const symbol_exprt number = temporary(integer_type(), dest, loc);
  exprt valid =
    parse_test_integer(string, trailing_blankst::ANY, number, dest, loc);
  if(leading_zero_octal)
  {
    // A leading zero followed by more digits is octal.
    exprt octal = false_exprt{};
    if(native())
    {
      regex_buildert re{symbol_table};
      const exprt blank = re.either(re.literal(" "), re.range('\t', '\r'));
      octal = re.matches(
        payload,
        re.sequence(
          {re.star(blank),
           re.optional(re.either(re.literal("-"), re.literal("+"))),
           re.literal("0"),
           re.range('0', '9'),
           re.star(re.any())}));
    }
    else
    {
      // Refined strings: the number starts after the leading blanks, which
      // parse_test_integer only accepts if there are few.
      const exprt length = payload_length(payload, dest, loc);
      const exprt leading =
        refined_blank_count(payload, false, false, dest, loc);
      const auto char_at = [&](const exprt &i)
      {
        return string_function(
          ID_cprover_string_char_at_func,
          {payload, i},
          unsigned_char_type(),
          dest,
          loc);
      };
      const exprt first = char_at(leading);
      const exprt signed_number = or_exprt{
        equal_exprt{first, from_integer('-', unsigned_char_type())},
        equal_exprt{first, from_integer('+', unsigned_char_type())}};
      const exprt start = plus_exprt{
        leading,
        if_exprt{signed_number, from_integer(1, integer_type()), zero}};
      const exprt one = from_integer(1, integer_type());
      // A digit after the 0, as a 0 followed by blanks is zero.
      const exprt next = char_at(plus_exprt{start, one});
      octal = and_exprt{
        equal_exprt{char_at(start), from_integer('0', unsigned_char_type())},
        binary_relation_exprt{length, ID_gt, plus_exprt{start, one}},
        and_exprt{
          binary_relation_exprt{
            next, ID_ge, from_integer('0', unsigned_char_type())},
          binary_relation_exprt{
            next, ID_le, from_integer('9', unsigned_char_type())}}};
    }
    valid = and_exprt{valid, not_exprt{octal}};
  }
  if(!any_space)
  {
    // bash allows only space, tab and newline around the number, rather
    // than the test utility's blanks, which add \v, \f and \r. Those
    // bytes can only be blanks in a valid number.
    exprt::operandst other_blanks;
    for(const char *blank : {"\v", "\f", "\r"})
    {
      other_blanks.push_back(string_function(
        ID_cprover_string_contains_func,
        {payload, payload_literal(blank)},
        bool_typet{},
        dest,
        loc));
    }
    valid = and_exprt{valid, not_exprt{disjunction(other_blanks)}};
  }
  assign(dest, value, if_exprt{empty, zero, number}, loc);
  return or_exprt{empty, valid};
}

exprt shell_convertert::payload_byte(
  const exprt &s,
  const exprt &index,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(native())
  {
    // str.at yields a string of one character, or none past the end.
    return string_function(
      ID_cprover_string_to_code_func,
      {string_function(
        ID_cprover_string_char_at_func, {s, index}, payload_type(), dest, loc)},
      integer_type(),
      dest,
      loc);
  }
  return typecast_exprt{
    string_function(
      ID_cprover_string_char_at_func,
      {s, index},
      unsigned_char_type(),
      dest,
      loc),
    integer_type()};
}

exprt shell_convertert::parse_hex_digits(
  const exprt &string,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  // At most 15 digits, which fit in 64-bit arithmetic.
  const std::size_t max_digits = 15;
  const exprt payload = payload_of(string);
  const exprt length = payload_length(payload, dest, loc);
  exprt::operandst valid{
    binary_relation_exprt{length, ID_ge, from_integer(1, integer_type())},
    binary_relation_exprt{
      length, ID_le, from_integer(max_digits, integer_type())}};
  exprt accumulated = from_integer(0, integer_type());
  const auto in_range = [this](const exprt &b, char low, char high)
  {
    return and_exprt{
      binary_relation_exprt{b, ID_ge, from_integer(low, integer_type())},
      binary_relation_exprt{b, ID_le, from_integer(high, integer_type())}};
  };
  for(std::size_t i = 0; i < max_digits; ++i)
  {
    const exprt index = from_integer(i, integer_type());
    const symbol_exprt byte = temporary(integer_type(), dest, loc);
    assign(dest, byte, payload_byte(payload, index, dest, loc), loc);
    const exprt decimal = in_range(byte, '0', '9');
    const exprt lower = in_range(byte, 'a', 'f');
    const exprt upper = in_range(byte, 'A', 'F');
    const exprt digit = if_exprt{
      decimal,
      minus_exprt{byte, from_integer('0', integer_type())},
      if_exprt{
        lower,
        minus_exprt{byte, from_integer('a' - 10, integer_type())},
        minus_exprt{byte, from_integer('A' - 10, integer_type())}}};
    const exprt inside = binary_relation_exprt{index, ID_lt, length};
    valid.push_back(
      or_exprt{not_exprt{inside}, or_exprt{decimal, or_exprt{lower, upper}}});
    const symbol_exprt next = temporary(integer_type(), dest, loc);
    assign(
      dest,
      next,
      if_exprt{
        inside,
        plus_exprt{
          mult_exprt{accumulated, from_integer(16, integer_type())}, digit},
        accumulated},
      loc);
    accumulated = next;
  }
  assign(dest, value, accumulated, loc);
  return conjunction(valid);
}

// ------------------------------------------------------------------
// Patterns (XCU 2.14)
//
// A pattern S0*S1*...*Sn, where the segments Si have no stars, matches s
// if S0 is at the start of s, Sn at its end, and S1, ..., Sn-1 occur in
// order in between, without overlapping: the leftmost occurrence of each
// is the best choice. The pattern removal expansions choose the first or
// last occurrence of the segment at the end that they search for.
//
// The segments at the start and the end are at known positions, so their
// elements are matched one by one. The other segments are searched for:
// a core of characters and quoted expansions with index_of, a single
// bracket expression with index_of_set (refined) or regular expressions
// (native), and cores with few bracket expressions as the alternatives of
// the characters in them. The ?s around a core only need room.

namespace
{
/// The most literal alternatives a searched segment may have.
const std::size_t max_pattern_alternatives = 16;

using pattern_elementst = std::vector<shell_pattern_elementt>;
using pattern_kindt = shell_pattern_elementt::kindt;

bool has_kind(const pattern_elementst &elements, pattern_kindt kind)
{
  return std::any_of(
    elements.begin(),
    elements.end(),
    [kind](const shell_pattern_elementt &e) { return e.kind == kind; });
}

/// Whether \p elements are only characters and quoted expansions.
bool is_literal(const pattern_elementst &elements)
{
  return !has_kind(elements, pattern_kindt::ANY) &&
         !has_kind(elements, pattern_kindt::SET);
}

/// The literal sequences that \p core, which has no ?, matches with the
/// characters in its bracket expressions, or nothing if there are more
/// than max_pattern_alternatives.
std::optional<std::vector<pattern_elementst>>
pattern_alternatives(const pattern_elementst &core)
{
  std::vector<pattern_elementst> result{1};
  for(const auto &element : core)
  {
    if(element.kind != pattern_kindt::SET)
    {
      for(auto &alternative : result)
        alternative.push_back(element);
      continue;
    }
    if(element.set.count() * result.size() > max_pattern_alternatives)
      return {};
    std::vector<pattern_elementst> next;
    for(unsigned c = 0; c < 256; ++c)
    {
      if(!element.set[c])
        continue;
      for(const auto &alternative : result)
      {
        next.push_back(alternative);
        shell_pattern_elementt character;
        character.character = static_cast<unsigned char>(c);
        next.back().push_back(character);
      }
    }
    result = std::move(next);
  }
  return result;
}

/// Whether byte \p c matches pattern element \p element, which is a
/// character, ? or a bracket expression.
exprt byte_matches(const shell_pattern_elementt &element, const exprt &c)
{
  if(element.kind == pattern_kindt::CHARACTER)
    return equal_exprt{c, from_integer(element.character, c.type())};
  if(element.kind == pattern_kindt::ANY)
    return true_exprt{};
  PRECONDITION(element.kind == pattern_kindt::SET);
  // The union of the ranges of consecutive bytes in the set.
  exprt::operandst ranges;
  for(unsigned low = 0; low < 256; ++low)
  {
    if(!element.set[low])
      continue;
    unsigned high = low;
    while(high + 1 < 256 && element.set[high + 1])
      ++high;
    if(low == high)
      ranges.push_back(equal_exprt{c, from_integer(low, c.type())});
    else
    {
      ranges.push_back(and_exprt{
        binary_relation_exprt{c, ID_ge, from_integer(low, c.type())},
        binary_relation_exprt{c, ID_le, from_integer(high, c.type())}});
    }
    low = high;
  }
  return disjunction(ranges);
}

/// The set of a bracket expression as the constant bit-vector that the
/// index_of_set functions of the string solver take: bit i is set if the
/// byte i is in the set.
exprt set_mask(const std::bitset<256> &set)
{
  mp_integer mask = 0;
  for(unsigned c = 0; c < 256; ++c)
  {
    if(set[c])
      mask += power(2, c);
  }
  return from_integer(mask, unsignedbv_typet{256});
}

exprt minimum_of(const exprt &a, const exprt &b)
{
  return if_exprt{binary_relation_exprt{a, ID_le, b}, a, b};
}

exprt maximum_of(const exprt &a, const exprt &b)
{
  return if_exprt{binary_relation_exprt{a, ID_ge, b}, a, b};
}
} // namespace

exprt shell_convertert::pattern_regex(const pattern_elementst &elements)
{
  regex_buildert re{symbol_table};
  std::vector<exprt> sequence;
  std::string pending;
  for(const auto &element : elements)
  {
    if(element.kind == pattern_kindt::CHARACTER)
    {
      pending += static_cast<char>(element.character);
      continue;
    }
    if(!pending.empty())
    {
      sequence.push_back(re.literal(pending));
      pending.clear();
    }
    if(element.kind == pattern_kindt::ANY)
      sequence.push_back(re.any());
    else if(element.kind == pattern_kindt::STAR)
      sequence.push_back(re.star(re.any()));
    else if(element.kind == pattern_kindt::SET)
    {
      // The union of the ranges of consecutive bytes in the set.
      std::optional<exprt> set;
      for(unsigned low = 0; low < 256; ++low)
      {
        if(!element.set[low])
          continue;
        unsigned high = low;
        while(high + 1 < 256 && element.set[high + 1])
          ++high;
        const exprt range = re.range(
          static_cast<unsigned char>(low), static_cast<unsigned char>(high));
        set = set.has_value() ? re.either(*set, range) : range;
        low = high;
      }
      sequence.push_back(set.has_value() ? *set : re.none());
    }
    else
      UNREACHABLE;
  }
  if(!pending.empty() || sequence.empty())
    sequence.push_back(re.literal(pending));
  return re.sequence(sequence);
}

std::vector<shell_convertert::pattern_segmentt>
shell_convertert::pattern_segments(
  const shell_patternt &pattern,
  pattern_contextt &context)
{
  for(const auto *part : pattern.dynamic_parts)
  {
    context.dynamic.push_back(payload_of(expand_part(
      *part, shell_word_contextt::CASE_SUBJECT, true, false, context.dest)));
  }

  std::vector<pattern_elementst> parts{1};
  for(const auto &element : pattern.elements)
  {
    if(element.kind == pattern_kindt::STAR)
      parts.emplace_back();
    else
      parts.back().push_back(element);
  }

  std::vector<pattern_segmentt> segments;
  for(const auto &part : parts)
  {
    pattern_segmentt segment;
    auto begin = part.begin();
    auto end = part.end();
    while(begin != end && begin->kind == pattern_kindt::ANY)
    {
      ++segment.lead;
      ++begin;
    }
    while(end != begin && std::prev(end)->kind == pattern_kindt::ANY)
    {
      ++segment.trail;
      --end;
    }
    segment.core.assign(begin, end);
    // The width of the core: one for each byte, and the lengths of the
    // quoted expansions.
    std::size_t bytes = 0;
    exprt::operandst widths;
    for(const auto &element : segment.core)
    {
      if(element.kind == pattern_kindt::DYNAMIC)
      {
        widths.push_back(payload_length(
          context.dynamic.at(element.dynamic_index),
          context.dest,
          context.loc));
      }
      else
        ++bytes;
    }
    exprt core_width = from_integer(bytes, integer_type());
    for(const auto &width : widths)
      core_width = plus_exprt{core_width, width};
    segment.core_width = core_width;
    segment.width = plus_exprt{
      core_width, from_integer(segment.lead + segment.trail, integer_type())};
    segments.push_back(std::move(segment));
  }
  return segments;
}

exprt shell_convertert::elements_payload(
  const pattern_elementst &elements,
  const pattern_contextt &context)
{
  std::optional<exprt> result;
  std::string pending;
  auto append = [&](const exprt &payload)
  {
    result = result.has_value()
               ? payload_concat(*result, payload, context.dest, context.loc)
               : payload;
  };
  for(const auto &element : elements)
  {
    if(element.kind == pattern_kindt::CHARACTER)
    {
      pending += static_cast<char>(element.character);
      continue;
    }
    PRECONDITION(element.kind == pattern_kindt::DYNAMIC);
    if(!pending.empty())
    {
      append(payload_literal(pending));
      pending.clear();
    }
    append(context.dynamic.at(element.dynamic_index));
  }
  if(!pending.empty() || !result.has_value())
    append(payload_literal(pending));
  return *result;
}

exprt shell_convertert::segment_at(
  const pattern_segmentt &segment,
  bool at_start,
  const pattern_contextt &context)
{
  const exprt &s = context.s;
  code_blockt &dest = context.dest;
  const source_locationt &loc = context.loc;
  if(segment.empty())
    return true_exprt{};
  if(segment.lead == 0 && segment.trail == 0 && is_literal(segment.core))
  {
    const exprt literal = elements_payload(segment.core, context);
    return at_start ? payload_has_prefix(s, literal, dest, loc)
                    : payload_has_suffix(s, literal, dest, loc);
  }

  exprt position = at_start ? from_integer(segment.lead, integer_type())
                            : exprt{plus_exprt{
                                minus_exprt{context.length, segment.width},
                                from_integer(segment.lead, integer_type())}};
  const auto advance = [&position](const exprt &width) {
    position = plus_exprt{position, width};
  };
  const exprt one = from_integer(1, integer_type());
  exprt::operandst conditions;
  const auto &core = segment.core;
  for(auto it = core.begin(); it != core.end();)
  {
    if(it->kind == pattern_kindt::DYNAMIC)
    {
      const exprt &value = context.dynamic.at(it->dynamic_index);
      const exprt width = payload_length(value, dest, loc);
      const exprt part =
        payload_substring(s, position, plus_exprt{position, width}, dest, loc);
      conditions.push_back(
        native() ? exprt{equal_exprt{part, value}}
                 : string_function(
                     ID_cprover_string_equal_func,
                     {part, value},
                     bool_typet{},
                     dest,
                     loc));
      advance(width);
      ++it;
    }
    else if(native())
    {
      // The bytes up to the next quoted expansion, as a regular
      // expression.
      const auto run_end = std::find_if(
        it,
        core.end(),
        [](const shell_pattern_elementt &e)
        { return e.kind == pattern_kindt::DYNAMIC; });
      const pattern_elementst run{it, run_end};
      const exprt width = from_integer(run.size(), integer_type());
      const exprt part =
        payload_substring(s, position, plus_exprt{position, width}, dest, loc);
      regex_buildert re{symbol_table};
      conditions.push_back(re.matches(part, pattern_regex(run)));
      advance(width);
      it = run_end;
    }
    else
    {
      if(it->kind != pattern_kindt::ANY)
      {
        const exprt byte = string_function(
          ID_cprover_string_char_at_func,
          {s, position},
          unsigned_char_type(),
          dest,
          loc);
        conditions.push_back(byte_matches(*it, byte));
      }
      advance(one);
      ++it;
    }
  }
  return conjunction(conditions);
}

exprt shell_convertert::payload_replace_all(
  const exprt &payload,
  const std::string &needle,
  const std::string &replacement,
  code_blockt &dest,
  const source_locationt &loc)
{
  PRECONDITION(!needle.empty());
  if(native())
  {
    return string_function(
      ID_cprover_string_replace_all_func,
      {payload, payload_literal(needle), payload_literal(replacement)},
      payload_type(),
      dest,
      loc);
  }
  // Each round replaces the first occurrence at or after a position; as
  // inputs have at most max_length bytes, that many rounds replace each.
  const exprt zero = from_integer(0, integer_type());
  const exprt needle_length = from_integer(needle.size(), integer_type());
  const exprt replacement_length =
    from_integer(replacement.size(), integer_type());
  const symbol_exprt result = temporary(payload_type(), dest, loc);
  assign(dest, result, payload, loc);
  const symbol_exprt from = temporary(integer_type(), dest, loc);
  assign(dest, from, zero, loc);
  const symbol_exprt more = temporary(bool_typet{}, dest, loc);
  assign(dest, more, true_exprt{}, loc);
  const std::size_t rounds = max_length / needle.size() + 1;
  for(std::size_t k = 0; k < rounds; ++k)
  {
    code_blockt round;
    const symbol_exprt found = temporary(integer_type(), round, loc);
    assign(
      round,
      found,
      payload_index_of(result, payload_literal(needle), from, round, loc),
      loc);
    code_blockt replace;
    const exprt length = payload_length(result, replace, loc);
    const exprt joined = payload_concat(
      payload_concat(
        payload_substring(result, zero, found, replace, loc),
        payload_literal(replacement),
        replace,
        loc),
      payload_substring(
        result, plus_exprt{found, needle_length}, length, replace, loc),
      replace,
      loc);
    assign(replace, from, plus_exprt{found, replacement_length}, loc);
    assign(replace, result, joined, loc);
    code_blockt stop;
    assign(stop, more, false_exprt{}, loc);
    add(
      round,
      code_ifthenelset{
        binary_relation_exprt{found, ID_ge, zero},
        std::move(replace),
        std::move(stop)},
      loc);
    add(dest, code_ifthenelset{more, std::move(round)}, loc);
  }
  // Any occurrence left (in a longer string) is reported.
  check(
    dest,
    not_exprt{more},
    "shell-unsupported",
    "a string of at most --shell-max-string-length bytes is replaced in",
    loc);
  return result;
}

exprt shell_convertert::payload_index_of_set(
  const exprt &s,
  const exprt &from,
  const std::bitset<256> &set,
  code_blockt &dest,
  const source_locationt &loc)
{
  PRECONDITION(!native());
  return string_function(
    ID_cprover_string_index_of_set_func,
    {s, from, set_mask(set)},
    integer_type(),
    dest,
    loc);
}

exprt shell_convertert::payload_contains_byte(
  const exprt &s,
  const std::bitset<256> &set,
  code_blockt &dest,
  const source_locationt &loc)
{
  shell_pattern_elementt element;
  element.kind = pattern_kindt::SET;
  element.set = set;
  if(native())
  {
    shell_pattern_elementt star;
    star.kind = pattern_kindt::STAR;
    regex_buildert re{symbol_table};
    return re.matches(s, pattern_regex({star, element, star}));
  }
  const exprt found = string_function(
    ID_cprover_string_index_of_set_func,
    {s, from_integer(0, integer_type()), set_mask(set)},
    integer_type(),
    dest,
    loc);
  return binary_relation_exprt{found, ID_ge, from_integer(0, integer_type())};
}

void shell_convertert::split_fields(
  const field_listt &list,
  const shell_word_partt &part,
  code_blockt &dest,
  const source_locationt &loc)
{
  // zsh only splits command substitutions (see split_known_parameter).
  const bool split = program.dialect != shell_dialectt::ZSH ||
                     part.kind != shell_word_partt::kindt::PARAMETER;
  std::string ifs = split ? ifs_for_splitting(dest, loc) : "";
  if(const auto literal = known_literal(part))
  {
    // A known value is split here.
    for(const auto &field : split_known_parameter(*literal, ifs))
    {
      const exprt value = string_literal(field, loc);
      if(field.find_first_of("*?[") != std::string::npos)
        check_no_pattern_characters(value, part, dest);
      append_field(list, value, dest, loc);
    }
    return;
  }
  const exprt value =
    expand_part(part, shell_word_contextt::CASE_SUBJECT, false, false, dest);
  split_value(list, value, part, ifs, dest, loc);
}

void shell_convertert::split_value(
  const field_listt &list,
  const exprt &value,
  const shell_word_partt &part,
  std::string ifs,
  code_blockt &dest,
  const source_locationt &loc)
{
  check_no_pattern_characters(value, part, dest);
  const std::size_t null = ifs.find('\0');
  if(null != std::string::npos)
  {
    // The default IFS of zsh: splitting at null bytes is not modelled.
    std::bitset<256> null_byte;
    null_byte.set(0);
    check(
      dest,
      not_exprt{payload_contains_byte(payload_of(value), null_byte, dest, loc)},
      "shell-unsupported",
      "the value of the unquoted " + display({part}) +
        " has no null byte (field splitting at a null byte in IFS is not "
        "supported)",
      loc);
    ifs.erase(null, 1);
  }
  if(!ifs_is_white(ifs) && !ifs_has_white(ifs))
  {
    // Each byte of IFS ends a field: the value has one field, or none if
    // it is empty, unless it has such a byte.
    const std::bitset<256> delimiters = ifs_set(ifs);
    code_blockt whole, append, search;
    append_field(list, value, append, loc);
    add(
      whole,
      code_ifthenelset{not_exprt{is_empty(value)}, std::move(append)},
      loc);
    split_fields_delimited(list, payload_of(value), delimiters, search, loc);
    add(
      dest,
      code_ifthenelset{
        payload_contains_byte(payload_of(value), delimiters, dest, loc),
        std::move(search),
        std::move(whole)},
      loc);
    return;
  }
  if(!ifs_is_white(ifs))
  {
    // Splitting at other bytes together with IFS white space is only
    // modelled for values without bytes of IFS: one field, or none.
    check(
      dest,
      not_exprt{
        payload_contains_byte(payload_of(value), ifs_set(ifs), dest, loc)},
      "shell-unsupported",
      "the value of the unquoted " + display({part}) +
        " has no byte of IFS (field splitting at bytes other than space, "
        "tab and newline is not supported here)",
      loc);
  }
  // Field splitting with the default IFS (XCU 2.6.5), which the script
  // cannot change (see variable()): the fields are the maximal runs of
  // bytes other than space, tab and newline. Most values have no blanks:
  // their one field (or none, for the empty value) needs no search.
  const std::bitset<256> blanks =
    ifs_is_white(ifs) ? ifs_set(ifs) : std::bitset<256>{};
  code_blockt whole, append, search;
  append_field(list, value, append, loc);
  add(
    whole,
    code_ifthenelset{not_exprt{is_empty(value)}, std::move(append)},
    loc);
  if(blanks.none())
  {
    // No splitting: the value is one field, or none if it is empty.
    dest.append(whole);
    return;
  }
  split_fields_searching(list, payload_of(value), blanks, search, loc);
  add(
    dest,
    code_ifthenelset{
      payload_contains_byte(payload_of(value), blanks, dest, loc),
      std::move(search),
      std::move(whole)},
    loc);
}

/// The fields of \p s, which has blanks. Up to max_arguments + 1 fields
/// are split off, so that check_field_list reports more.
void shell_convertert::split_fields_searching(
  const field_listt &list,
  const exprt &s,
  const std::bitset<256> &blanks,
  code_blockt &dest,
  const source_locationt &loc)
{
  pattern_contextt context{s, payload_length(s, dest, loc), {}, dest, loc};
  shell_pattern_elementt blank;
  blank.kind = shell_pattern_elementt::kindt::SET;
  blank.set = blanks;
  shell_pattern_elementt non_blank = blank;
  non_blank.set.flip();
  const exprt zero = from_integer(0, integer_type());
  if(native())
  {
    // The rest r of the value is b f r', where b are blanks, and f is the
    // next field, which r' is empty after, or starts with a blank; f and r'
    // are empty when only blanks are left.
    regex_buildert re{symbol_table};
    shell_pattern_elementt star;
    star.kind = shell_pattern_elementt::kindt::STAR;
    const exprt empty = payload_literal("");
    exprt rest = s;
    for(std::size_t k = 0; k <= max_arguments; ++k)
    {
      const symbol_exprt blanks = temporary(payload_type(), dest, loc);
      const symbol_exprt field = temporary(payload_type(), dest, loc);
      const symbol_exprt next = temporary(payload_type(), dest, loc);
      add(
        dest,
        code_assumet{and_exprt{
          equal_exprt{
            rest,
            payload_concat(
              blanks, payload_concat(field, next, dest, loc), dest, loc)},
          re.matches(blanks, re.star(pattern_regex({blank}))),
          or_exprt{
            and_exprt{
              re.matches(field, re.plus(pattern_regex({non_blank}))),
              or_exprt{
                equal_exprt{next, empty},
                re.matches(next, pattern_regex({blank, star}))}},
            and_exprt{equal_exprt{field, empty}, equal_exprt{next, empty}}}}},
        loc);
      code_blockt append;
      append_field(list, make_value(field), append, loc);
      add(
        dest,
        code_ifthenelset{
          not_exprt{equal_exprt{field, empty}}, std::move(append)},
        loc);
      rest = next;
    }
    return;
  }
  // Each field is searched for only after the previous one was found, so
  // that the searches past the last field are on paths of their own.
  std::function<void(const exprt &, std::size_t, code_blockt &)> next_field =
    [&](const exprt &position, std::size_t k, code_blockt &block)
  {
    pattern_contextt here{s, context.length, {}, block, loc};
    // The next field starts at the next byte that is not a blank, and ends
    // before the next blank after it, or at the end.
    const symbol_exprt start = temporary(integer_type(), block, loc);
    assign(block, start, core_index_of({non_blank}, position, here), loc);
    code_blockt field;
    pattern_contextt in_field{s, context.length, {}, field, loc};
    const symbol_exprt end = temporary(integer_type(), field, loc);
    assign(field, end, core_index_of({blank}, start, in_field), loc);
    assign(
      field,
      end,
      if_exprt{
        binary_relation_exprt{end, ID_lt, zero}, context.length, exprt{end}},
      loc);
    append_field(
      list,
      make_value(payload_substring(s, start, end, field, loc)),
      field,
      loc);
    if(k < max_arguments)
      next_field(end, k + 1, field);
    add(
      block,
      code_ifthenelset{
        binary_relation_exprt{start, ID_ge, zero}, std::move(field)},
      loc);
  };
  next_field(zero, 0, dest);
}

/// The fields of \p s, which has bytes of \p delimiters, which has no IFS
/// white space: each delimiter ends a field (XCU 2.6.5), and fields can be
/// empty. A delimiter at the end starts no further field, except in zsh,
/// where it starts an empty one. Up to max_arguments + 1 fields are split
/// off, so that check_field_list reports more.
void shell_convertert::split_fields_delimited(
  const field_listt &list,
  const exprt &s,
  const std::bitset<256> &delimiters,
  code_blockt &dest,
  const source_locationt &loc)
{
  const bool trailing_field = program.dialect == shell_dialectt::ZSH;
  const exprt length = payload_length(s, dest, loc);
  shell_pattern_elementt delimiter;
  delimiter.kind = shell_pattern_elementt::kindt::SET;
  delimiter.set = delimiters;
  shell_pattern_elementt other = delimiter;
  other.set.flip();
  const exprt zero = from_integer(0, integer_type());
  if(native())
  {
    // The rest r of the value is f d r', where f has no delimiters, and d
    // is one delimiter, or d and r' are empty at the end of the value.
    regex_buildert re{symbol_table};
    const exprt empty = payload_literal("");
    exprt rest = s;
    const symbol_exprt more = temporary(bool_typet{}, dest, loc);
    assign(dest, more, true_exprt{}, loc);
    for(std::size_t k = 0; k <= max_arguments; ++k)
    {
      const symbol_exprt field = temporary(payload_type(), dest, loc);
      const symbol_exprt separator = temporary(payload_type(), dest, loc);
      const symbol_exprt next = temporary(payload_type(), dest, loc);
      add(
        dest,
        code_assumet{and_exprt{
          equal_exprt{
            rest,
            payload_concat(
              field, payload_concat(separator, next, dest, loc), dest, loc)},
          re.matches(field, re.star(pattern_regex({other}))),
          or_exprt{
            re.matches(separator, pattern_regex({delimiter})),
            and_exprt{
              equal_exprt{separator, empty}, equal_exprt{next, empty}}}}},
        loc);
      code_blockt append;
      append_field(list, make_value(field), append, loc);
      add(dest, code_ifthenelset{more, std::move(append)}, loc);
      // Another field follows a delimiter, unless it ends the value.
      exprt follows = not_exprt{equal_exprt{separator, empty}};
      if(!trailing_field)
        follows = and_exprt{follows, not_exprt{equal_exprt{next, empty}}};
      assign(dest, more, and_exprt{more, follows}, loc);
      rest = next;
    }
    return;
  }
  // Each field is searched for only after the previous one was found, so
  // that the searches past the last field are on paths of their own.
  std::function<void(const exprt &, std::size_t, code_blockt &)> next_field =
    [&](const exprt &start, std::size_t k, code_blockt &block)
  {
    pattern_contextt here{s, length, {}, block, loc};
    // The field ends before the next delimiter, or at the end.
    const symbol_exprt found = temporary(integer_type(), block, loc);
    assign(block, found, core_index_of({delimiter}, start, here), loc);
    const exprt end =
      if_exprt{binary_relation_exprt{found, ID_lt, zero}, length, exprt{found}};
    append_field(
      list,
      make_value(payload_substring(s, start, end, block, loc)),
      block,
      loc);
    if(k == max_arguments)
      return;
    const exprt after = plus_exprt{found, from_integer(1, integer_type())};
    exprt follows = binary_relation_exprt{found, ID_ge, zero};
    if(!trailing_field)
      follows = and_exprt{follows, binary_relation_exprt{after, ID_lt, length}};
    code_blockt rest;
    next_field(after, k + 1, rest);
    add(block, code_ifthenelset{follows, std::move(rest)}, loc);
  };
  next_field(zero, 0, dest);
}

exprt shell_convertert::regex_index_of(
  const pattern_elementst &core,
  const exprt &from,
  const pattern_contextt &context)
{
  PRECONDITION(native());
  PRECONDITION(!core.empty() && !has_kind(core, pattern_kindt::DYNAMIC));
  const exprt &s = context.s;
  code_blockt &dest = context.dest;
  const source_locationt &loc = context.loc;
  const exprt minus_one = from_integer(-1, integer_type());
  shell_pattern_elementt star;
  star.kind = pattern_kindt::STAR;
  regex_buildert re{symbol_table};
  // The first occurrence starts at j if s[from, j + |core|) ends with an
  // occurrence and s[from, j + |core| - 1) has none.
  pattern_elementst ending{star};
  ending.insert(ending.end(), core.begin(), core.end());
  pattern_elementst containing = ending;
  containing.push_back(star);
  const exprt found = re.matches(
    payload_substring(s, from, context.length, dest, loc),
    pattern_regex(containing));
  const symbol_exprt j = temporary(integer_type(), dest, loc);
  const exprt end = plus_exprt{j, from_integer(core.size(), integer_type())};
  const exprt before_end = minus_exprt{end, from_integer(1, integer_type())};
  add(
    dest,
    code_assumet{if_exprt{
      found,
      and_exprt{
        binary_relation_exprt{j, ID_ge, from},
        binary_relation_exprt{end, ID_le, context.length},
        re.matches(
          payload_substring(s, from, end, dest, loc), pattern_regex(ending)),
        not_exprt{re.matches(
          payload_substring(s, from, before_end, dest, loc),
          pattern_regex(containing))}},
      equal_exprt{j, minus_one}}},
    loc);
  if(core.size() == 1 && core.front().kind == pattern_kindt::SET)
  {
    // For a bracket expression, also: s[from, |s|) is p r for p without
    // bytes of the set, and r empty or starting with one. The solvers fail
    // on different queries with either encoding alone, and on fewer with
    // both.
    shell_pattern_elementt outside = core.front();
    outside.set.flip();
    const symbol_exprt p = temporary(payload_type(), dest, loc);
    const symbol_exprt r = temporary(payload_type(), dest, loc);
    const exprt empty = payload_literal("");
    add(
      dest,
      code_assumet{conjunction(
        {equal_exprt{
           payload_substring(s, from, context.length, dest, loc),
           payload_concat(p, r, dest, loc)},
         re.matches(p, re.star(pattern_regex({outside}))),
         or_exprt{
           equal_exprt{r, empty},
           re.matches(r, pattern_regex({core.front(), star}))},
         equal_exprt{
           j,
           if_exprt{
             equal_exprt{r, empty},
             minus_one,
             plus_exprt{from, payload_length(p, dest, loc)}}}})},
      loc);
  }
  return j;
}

exprt shell_convertert::regex_last_index_of(
  const pattern_elementst &core,
  const exprt &upto,
  const pattern_contextt &context)
{
  PRECONDITION(native());
  PRECONDITION(!core.empty() && !has_kind(core, pattern_kindt::DYNAMIC));
  const exprt &s = context.s;
  code_blockt &dest = context.dest;
  const source_locationt &loc = context.loc;
  const exprt zero = from_integer(0, integer_type());
  const exprt one = from_integer(1, integer_type());
  shell_pattern_elementt star;
  star.kind = pattern_kindt::STAR;
  regex_buildert re{symbol_table};
  // The last occurrence at or before upto starts at x if s[x, end) starts
  // with an occurrence and s[x + 1, end) has none, for the end
  // upto + |core| of the latest occurrence, which the caller keeps within
  // s.
  pattern_elementst starting = core;
  starting.push_back(star);
  pattern_elementst containing{star};
  containing.insert(containing.end(), starting.begin(), starting.end());
  const exprt end = plus_exprt{upto, from_integer(core.size(), integer_type())};
  const exprt found = re.matches(
    payload_substring(s, zero, end, dest, loc), pattern_regex(containing));
  const symbol_exprt x = temporary(integer_type(), dest, loc);
  add(
    dest,
    code_assumet{if_exprt{
      found,
      and_exprt{
        binary_relation_exprt{x, ID_ge, zero},
        binary_relation_exprt{x, ID_le, upto},
        re.matches(
          payload_substring(s, x, end, dest, loc), pattern_regex(starting)),
        not_exprt{re.matches(
          payload_substring(s, plus_exprt{x, one}, end, dest, loc),
          pattern_regex(containing))}},
      equal_exprt{x, from_integer(-1, integer_type())}}},
    loc);
  if(core.size() == 1 && core.front().kind == pattern_kindt::SET)
  {
    // For a bracket expression, also: s[0, upto + 1) is a b for a empty or
    // ending with a byte of the set, and b without any (see
    // regex_index_of).
    shell_pattern_elementt outside = core.front();
    outside.set.flip();
    const symbol_exprt a = temporary(payload_type(), dest, loc);
    const symbol_exprt b = temporary(payload_type(), dest, loc);
    const exprt empty = payload_literal("");
    add(
      dest,
      code_assumet{conjunction(
        {equal_exprt{
           payload_substring(s, zero, plus_exprt{upto, one}, dest, loc),
           payload_concat(a, b, dest, loc)},
         re.matches(b, re.star(pattern_regex({outside}))),
         or_exprt{
           equal_exprt{a, empty},
           re.matches(a, pattern_regex({star, core.front()}))},
         equal_exprt{x, minus_exprt{payload_length(a, dest, loc), one}}})},
      loc);
  }
  return x;
}

exprt shell_convertert::core_index_of(
  const pattern_elementst &core,
  const exprt &from,
  const pattern_contextt &context)
{
  const exprt &s = context.s;
  code_blockt &dest = context.dest;
  const source_locationt &loc = context.loc;
  if(is_literal(core))
  {
    return payload_index_of(
      s, elements_payload(core, context), from, dest, loc);
  }
  const bool dynamic = has_kind(core, pattern_kindt::DYNAMIC);
  if(native() && !dynamic)
    return regex_index_of(core, from, context);
  if(core.size() == 1 && core.front().kind == pattern_kindt::SET)
  {
    return string_function(
      ID_cprover_string_index_of_set_func,
      {s, from, set_mask(core.front().set)},
      integer_type(),
      dest,
      loc);
  }
  const auto alternatives = has_kind(core, pattern_kindt::ANY)
                              ? std::nullopt
                              : pattern_alternatives(core);
  if(!alternatives.has_value())
  {
    unsupported(
      loc,
      "pattern that searches for a ? between other characters, or for "
      "bracket expressions with many characters next to other characters" +
        std::string{dynamic ? " or quoted expansions" : ""});
  }
  // The first occurrence of any alternative.
  const exprt minus_one = from_integer(-1, integer_type());
  std::optional<exprt> result;
  for(const auto &alternative : *alternatives)
  {
    const exprt found = payload_index_of(
      s, elements_payload(alternative, context), from, dest, loc);
    if(!result.has_value())
    {
      result = found;
      continue;
    }
    const symbol_exprt earliest = temporary(integer_type(), dest, loc);
    assign(
      dest,
      earliest,
      if_exprt{
        equal_exprt{*result, minus_one},
        found,
        if_exprt{
          equal_exprt{found, minus_one}, *result, minimum_of(*result, found)}},
      loc);
    result = earliest;
  }
  return *result;
}

exprt shell_convertert::core_last_index_of(
  const pattern_elementst &core,
  const exprt &upto,
  bool anywhere,
  const pattern_contextt &context)
{
  const exprt &s = context.s;
  code_blockt &dest = context.dest;
  const source_locationt &loc = context.loc;
  // Searching backward from before -1 finds nothing; the string solver
  // requires a position of at least -1.
  const exprt minus_one = from_integer(-1, integer_type());
  const exprt from = maximum_of(upto, minus_one);
  if(is_literal(core))
  {
    return payload_last_index_of(
      s, elements_payload(core, context), from, anywhere, dest, loc);
  }
  const bool dynamic = has_kind(core, pattern_kindt::DYNAMIC);
  if(native() && !dynamic)
    return regex_last_index_of(core, from, context);
  if(core.size() == 1 && core.front().kind == pattern_kindt::SET)
  {
    return string_function(
      ID_cprover_string_last_index_of_set_func,
      {s, from, set_mask(core.front().set)},
      integer_type(),
      dest,
      loc);
  }
  const auto alternatives = has_kind(core, pattern_kindt::ANY)
                              ? std::nullopt
                              : pattern_alternatives(core);
  if(!alternatives.has_value())
  {
    unsupported(
      loc,
      "pattern that searches for a ? between other characters, or for "
      "bracket expressions with many characters next to other characters" +
        std::string{dynamic ? " or quoted expansions" : ""});
  }
  // The last occurrence of any alternative: -1 is less than any position.
  std::optional<exprt> result;
  for(const auto &alternative : *alternatives)
  {
    const exprt found = payload_last_index_of(
      s, elements_payload(alternative, context), from, false, dest, loc);
    if(!result.has_value())
    {
      result = found;
      continue;
    }
    const symbol_exprt latest = temporary(integer_type(), dest, loc);
    assign(dest, latest, maximum_of(*result, found), loc);
    result = latest;
  }
  return *result;
}

exprt shell_convertert::segment_index_of(
  const pattern_segmentt &segment,
  const exprt &from,
  const pattern_contextt &context)
{
  const exprt minus_one = from_integer(-1, integer_type());
  if(segment.core.empty())
  {
    // Only ?s: the segment fits at from, or nowhere.
    return if_exprt{
      binary_relation_exprt{
        plus_exprt{from, segment.width}, ID_le, context.length},
      from,
      minus_one};
  }
  // The ?s before the core need room before it, which they have after
  // from; the ?s after it need room after it, and if the first occurrence
  // of the core has none, no other has.
  const exprt lead = from_integer(segment.lead, integer_type());
  const exprt j = core_index_of(segment.core, plus_exprt{from, lead}, context);
  const exprt end = plus_exprt{
    plus_exprt{j, segment.core_width},
    from_integer(segment.trail, integer_type())};
  return if_exprt{
    and_exprt{
      binary_relation_exprt{j, ID_ge, from_integer(0, integer_type())},
      binary_relation_exprt{end, ID_le, context.length}},
    minus_exprt{j, lead},
    minus_one};
}

exprt shell_convertert::segment_last_index_of(
  const pattern_segmentt &segment,
  const std::optional<exprt> &upto,
  const pattern_contextt &context)
{
  const exprt minus_one = from_integer(-1, integer_type());
  const exprt lead = from_integer(segment.lead, integer_type());
  // The last start of the core at which the ?s after it have room.
  const exprt latest = minus_exprt{
    minus_exprt{context.length, segment.core_width},
    from_integer(segment.trail, integer_type())};
  const exprt core_upto =
    upto.has_value() ? minimum_of(plus_exprt{*upto, lead}, latest) : latest;
  if(segment.core.empty())
  {
    // Only ?s: the segment fits at the latest position, or nowhere.
    const exprt start = minus_exprt{core_upto, lead};
    return if_exprt{
      binary_relation_exprt{start, ID_ge, from_integer(0, integer_type())},
      start,
      minus_one};
  }
  // The ?s before the core need room before it: if the last occurrence of
  // the core has none, no other has.
  const exprt j = core_last_index_of(
    segment.core, core_upto, !upto.has_value() && segment.trail == 0, context);
  return if_exprt{
    binary_relation_exprt{j, ID_ge, lead}, minus_exprt{j, lead}, minus_one};
}

std::pair<exprt, exprt> shell_convertert::segment_chain(
  std::vector<pattern_segmentt>::const_iterator begin,
  std::vector<pattern_segmentt>::const_iterator end,
  exprt from,
  bool backward,
  const pattern_contextt &context)
{
  // Once a segment does not occur, the positions that the later searches
  // start from are meaningless, but their results do not matter.
  exprt::operandst found;
  const exprt zero = from_integer(0, integer_type());
  const auto search = [&](const pattern_segmentt &segment)
  {
    if(segment.empty())
      return;
    const symbol_exprt position =
      temporary(integer_type(), context.dest, context.loc);
    if(backward)
    {
      assign(
        context.dest,
        position,
        segment_last_index_of(
          segment, minus_exprt{from, segment.width}, context),
        context.loc);
      from = position;
    }
    else
    {
      assign(
        context.dest,
        position,
        segment_index_of(segment, from, context),
        context.loc);
      from = plus_exprt{position, segment.width};
    }
    found.push_back(binary_relation_exprt{position, ID_ge, zero});
  };
  if(backward)
  {
    for(auto it = end; it != begin;)
      search(*--it);
  }
  else
  {
    for(auto it = begin; it != end; ++it)
      search(*it);
  }
  return {conjunction(found), from};
}

exprt shell_convertert::unbounded_pattern_matches(
  const shell_patternt &pattern,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt s = payload_of(subject);
  if(native() && pattern.dynamic_parts.empty())
  {
    regex_buildert re{symbol_table};
    return re.matches(s, pattern_regex(pattern.elements));
  }
  pattern_contextt context{s, payload_length(s, dest, loc), {}, dest, loc};
  const auto segments = pattern_segments(pattern, context);
  const pattern_segmentt &first = segments.front();
  if(segments.size() == 1)
  {
    return and_exprt{
      equal_exprt{context.length, first.width},
      segment_at(first, true, context)};
  }
  const pattern_segmentt &last = segments.back();
  const exprt last_start = minus_exprt{context.length, last.width};
  const auto chain = segment_chain(
    std::next(segments.begin()),
    std::prev(segments.end()),
    first.width,
    false,
    context);
  return conjunction(
    {binary_relation_exprt{last_start, ID_ge, first.width},
     segment_at(first, true, context),
     segment_at(last, false, context),
     chain.first,
     binary_relation_exprt{chain.second, ID_le, last_start}});
}

exprt shell_convertert::unbounded_remove_pattern(
  const shell_patternt &pattern,
  const exprt &value,
  bool prefix,
  bool longest,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt s = payload_of(value);
  pattern_contextt context{s, payload_length(s, dest, loc), {}, dest, loc};
  const exprt &length = context.length;
  const auto segments = pattern_segments(pattern, context);
  const pattern_segmentt &first = segments.front();
  const pattern_segmentt &last = segments.back();
  const exprt zero = from_integer(0, integer_type());

  // The result is s[start, end) if the pattern matches.
  exprt::operandst conditions;
  exprt start = zero;
  exprt end = length;
  if(segments.size() == 1)
  {
    // Shortest and longest agree.
    conditions.push_back(binary_relation_exprt{length, ID_ge, first.width});
    conditions.push_back(segment_at(first, prefix, context));
    if(prefix)
      start = first.width;
    else
      end = minus_exprt{length, first.width};
  }
  else if(prefix)
  {
    // The match starts with the first segment, which the middle ones
    // follow; it ends with the first (shortest) or last (longest)
    // occurrence of the last segment after them.
    conditions.push_back(binary_relation_exprt{
      length, ID_ge, plus_exprt{first.width, last.width}});
    conditions.push_back(segment_at(first, true, context));
    const auto chain = segment_chain(
      std::next(segments.begin()),
      std::prev(segments.end()),
      first.width,
      false,
      context);
    conditions.push_back(chain.first);
    if(last.empty())
    {
      start = longest ? length : chain.second;
      conditions.push_back(binary_relation_exprt{chain.second, ID_le, length});
    }
    else
    {
      const symbol_exprt found = temporary(integer_type(), dest, loc);
      assign(
        dest,
        found,
        longest ? segment_last_index_of(last, std::nullopt, context)
                : segment_index_of(last, chain.second, context),
        loc);
      conditions.push_back(binary_relation_exprt{found, ID_ge, chain.second});
      start = plus_exprt{found, last.width};
    }
  }
  else
  {
    // The match ends with the last segment, which the middle ones precede;
    // it starts with the last (shortest) or first (longest) occurrence of
    // the first segment before them.
    conditions.push_back(binary_relation_exprt{
      length, ID_ge, plus_exprt{first.width, last.width}});
    conditions.push_back(segment_at(last, false, context));
    const auto chain = segment_chain(
      std::next(segments.begin()),
      std::prev(segments.end()),
      minus_exprt{length, last.width},
      true,
      context);
    conditions.push_back(chain.first);
    if(first.empty())
    {
      end = longest ? zero : chain.second;
      conditions.push_back(binary_relation_exprt{chain.second, ID_ge, zero});
    }
    else
    {
      const symbol_exprt found = temporary(integer_type(), dest, loc);
      assign(
        dest,
        found,
        longest ? segment_index_of(first, zero, context)
                : segment_last_index_of(
                    first, minus_exprt{chain.second, first.width}, context),
        loc);
      conditions.push_back(binary_relation_exprt{found, ID_ge, zero});
      conditions.push_back(binary_relation_exprt{
        plus_exprt{found, first.width}, ID_le, chain.second});
      end = found;
    }
  }
  const symbol_exprt matched = temporary(bool_typet{}, dest, loc);
  assign(dest, matched, conjunction(conditions), loc);
  const exprt rest = payload_substring(
    s,
    if_exprt{matched, start, zero},
    if_exprt{matched, end, length},
    dest,
    loc);
  return make_value(rest);
}
