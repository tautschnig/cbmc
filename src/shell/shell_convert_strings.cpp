/// \file
/// Shell converter: the representations of strings (--shell-strings).
///
/// Bounded strings are C structs handled by the runtime (shell_runtime.h).
/// The unbounded representations wrap a payload in a struct with the `set`
/// flag, which needs no byte-level layout because shell has no way to
/// reinterpret memory:
///   * refined: a refined string {length, content} for the string
///     refinement solver. Operations are `cprover_string_*` applications
///     whose results are fresh temporaries, declared where the operation
///     is, so each execution constrains new symbols.
///   * native: the SMT-LIB String sort; operations are terms that
///     smt2_conv lowers to str.* and re.* operators.
///
/// Neither unbounded representation limits the length of strings, so
/// --shell-max-string-length does not apply to them.

#include "shell_convert_class.h"

#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/refined_string_type.h>
#include <util/std_types.h>
#include <util/string_expr.h>

// ------------------------------------------------------------------
// Types

typet shell_convertert::payload_type() const
{
  PRECONDITION(!bounded());
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
  PRECONDITION(!bounded());
  return struct_exprt{
    {from_integer(1, c_bool_type()),
     payload_of(value),
     member_exprt{value, "is_int", c_bool_type()},
     member_exprt{value, "int_value", integer_type()}},
    string_type()};
}

exprt shell_convertert::known_int(const exprt &value) const
{
  PRECONDITION(!bounded());
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
  PRECONDITION(!bounded());
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
  if(bounded())
  {
    return equal_exprt{
      length_of(string), from_integer(0, component_type("length"))};
  }
  if(native())
    return equal_exprt{payload_of(string), payload_literal("")};
  return equal_exprt{
    refined_length(payload_of(string)), from_integer(0, integer_type())};
}

exprt shell_convertert::string_literal(
  const std::string &text,
  const source_locationt &loc)
{
  if(!bounded())
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
  if(text.size() > max_length)
  {
    log.error().source_location = loc;
    log.error() << "string literal of " << text.size()
                << " bytes exceeds --shell-max-string-length " << max_length
                << messaget::eom;
    throw shell_conversion_errort{};
  }
  const array_typet &data_type = to_array_type(component_type("data"));
  exprt::operandst bytes;
  for(std::size_t i = 0; i < max_length; ++i)
  {
    const unsigned char c =
      i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
    bytes.push_back(from_integer(c, data_type.element_type()));
  }
  // Follow the components of the C struct, which include padding.
  exprt::operandst operands;
  for(const auto &component : string_struct().components())
  {
    if(component.get_name() == "set")
      operands.push_back(from_integer(1, component.type()));
    else if(component.get_name() == "length")
      operands.push_back(from_integer(text.size(), component.type()));
    else if(component.get_name() == "data")
      operands.push_back(array_exprt{std::move(bytes), data_type});
    else
      operands.push_back(from_integer(0, component.type()));
  }
  return struct_exprt{std::move(operands), string_type()};
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
  if(!bounded())
  {
    return make_value(payload_concat(payload_of(a), payload_of(b), dest, loc));
  }
  check(
    dest,
    binary_relation_exprt{
      plus_exprt{length_of(a), length_of(b)},
      ID_le,
      from_integer(max_length, component_type("length"))},
    "shell-string-length",
    "string exceeds --shell-max-string-length (" + std::to_string(max_length) +
      ")",
    loc);
  return call(SHELL_RT_CONCAT, {a, b}, dest, loc);
}

exprt shell_convertert::equal(
  const exprt &a,
  const exprt &b,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(native())
    return equal_exprt{payload_of(a), payload_of(b)};
  if(!bounded())
  {
    return string_function(
      ID_cprover_string_equal_func,
      {payload_of(a), payload_of(b)},
      bool_typet{},
      dest,
      loc);
  }
  const symbol_exprt result = call(SHELL_RT_EQUAL, {a, b}, dest, loc);
  return notequal_exprt{result, from_integer(0, result.type())};
}

exprt shell_convertert::from_integer_expr(
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt v = typecast_exprt::conditional_cast(value, integer_type());
  if(bounded())
    return call(SHELL_RT_FROM_INTEGER, {v}, dest, loc);
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
  if(bounded())
    return typecast_exprt{length_of(string), integer_type()};
  return payload_length(payload_of(string), dest, loc);
}

exprt shell_convertert::nondet_string(
  code_blockt &dest,
  const source_locationt &loc)
{
  if(bounded())
    return call(SHELL_RT_NONDET_STRING, {}, dest, loc);
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
        // bytes; inputs are bounded as with bounded strings.
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
  if(bounded())
    return call(SHELL_RT_ENVIRONMENT, {}, dest, loc);
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
  bool trailing_space,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(bounded())
    return parse_test_integer(string, trailing_space, value, dest, loc);
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
    parse_test_integer(string, trailing_space, parsed, parse, loc);
  assign(parse, value, parsed, loc);
  assign(parse, valid, parsed_valid, loc);
  add(dest, code_ifthenelset{known, std::move(use_int), std::move(parse)}, loc);
  return valid;
}

exprt shell_convertert::parse_test_integer(
  const exprt &string,
  bool trailing_space,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  const typet &bool_type = c_bool_type();
  if(bounded())
  {
    const symbol_exprt valid = call(
      SHELL_RT_TEST_INTEGER,
      {string,
       from_integer(trailing_space ? 1 : 0, bool_type),
       address_of_exprt{value}},
      dest,
      loc);
    return notequal_exprt{valid, from_integer(0, valid.type())};
  }
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
    std::vector<exprt> sequence{blanks, sign, digits};
    if(trailing_space)
      sequence.push_back(blanks);
    const exprt syntax = re.matches(payload, re.sequence(sequence));

    const symbol_exprt leading = temporary(payload_type(), dest, loc);
    const symbol_exprt sign_part = temporary(payload_type(), dest, loc);
    const symbol_exprt digit_part = temporary(payload_type(), dest, loc);
    const symbol_exprt trailing = temporary(payload_type(), dest, loc);
    exprt rebuilt = payload_concat(leading, sign_part, dest, loc);
    rebuilt = payload_concat(rebuilt, digit_part, dest, loc);
    rebuilt = payload_concat(rebuilt, trailing, dest, loc);
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
           trailing_space
             ? re.matches(trailing, blanks)
             : exprt{equal_exprt{trailing, payload_literal("")}}})}},
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
  // digits, up to 20 bytes. Blanks around the number, and numbers of more
  // than 18 bytes (whose conversion the solver constrains to not
  // overflow), are over-approximated by a nondeterministic result.
  const exprt length = payload_length(payload, dest, loc);
  const exprt syntax = string_function(
    ID_cprover_string_is_valid_long_func, {payload}, bool_typet{}, dest, loc);
  const exprt is_short =
    binary_relation_exprt{length, ID_le, from_integer(18, integer_type())};
  // Parse a copy that is empty unless the number is short and valid, so
  // that the solver's axioms do not constrain other strings.
  const exprt guarded = payload_substring(
    payload,
    zero,
    if_exprt{and_exprt{syntax, is_short}, length, zero},
    dest,
    loc);
  const exprt parsed = string_function(
    ID_cprover_string_parse_int_func, {guarded}, integer_type(), dest, loc);
  const exprt last = if_exprt{
    binary_relation_exprt{length, ID_gt, zero},
    minus_exprt{length, from_integer(1, integer_type())},
    zero};
  const exprt first_char = string_function(
    ID_cprover_string_char_at_func,
    {payload, zero},
    unsigned_char_type(),
    dest,
    loc);
  const exprt last_char = string_function(
    ID_cprover_string_char_at_func,
    {payload, last},
    unsigned_char_type(),
    dest,
    loc);
  const exprt edge_blank = and_exprt{
    binary_relation_exprt{length, ID_gt, zero},
    or_exprt{is_blank(first_char), is_blank(last_char)}};
  const exprt precise =
    and_exprt{not_exprt{edge_blank}, or_exprt{is_short, not_exprt{syntax}}};
  assign(
    dest, value, if_exprt{precise, parsed, nondet(integer_type(), loc)}, loc);
  return if_exprt{precise, syntax, nondet(bool_typet{}, loc)};
}

exprt shell_convertert::arith_integer(
  const exprt &string,
  bool leading_zero_octal,
  bool any_space,
  const symbol_exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  const typet &bool_type = c_bool_type();
  if(bounded())
  {
    const symbol_exprt valid = call(
      SHELL_RT_ARITH_INTEGER,
      {string,
       from_integer(leading_zero_octal ? 1 : 0, bool_type),
       from_integer(any_space ? 1 : 0, bool_type),
       address_of_exprt{value}},
      dest,
      loc);
    return notequal_exprt{valid, from_integer(0, valid.type())};
  }
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
  exprt valid = parse_test_integer(string, true, number, dest, loc);
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
      // Refined strings: is_valid_long rejected blanks, so the number
      // starts at 0 or 1.
      const exprt first = string_function(
        ID_cprover_string_char_at_func,
        {payload, zero},
        unsigned_char_type(),
        dest,
        loc);
      const exprt signed_number = or_exprt{
        equal_exprt{first, from_integer('-', unsigned_char_type())},
        equal_exprt{first, from_integer('+', unsigned_char_type())}};
      const exprt start =
        if_exprt{signed_number, from_integer(1, integer_type()), zero};
      const exprt at_start = string_function(
        ID_cprover_string_char_at_func,
        {payload, start},
        unsigned_char_type(),
        dest,
        loc);
      const exprt length = payload_length(payload, dest, loc);
      octal = and_exprt{
        equal_exprt{at_start, from_integer('0', unsigned_char_type())},
        binary_relation_exprt{
          length, ID_gt, plus_exprt{start, from_integer(1, integer_type())}}};
    }
    valid = and_exprt{valid, not_exprt{octal}};
  }
  if(native() && !any_space)
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

// ------------------------------------------------------------------
// Patterns (XCU 2.14)
//
// A pattern of characters and stars, L0*L1*...*Ln, matches s if s starts
// with L0, ends with Ln, and L1, ..., Ln-1 occur in order in between; the
// leftmost occurrence of each is the best choice. The pattern removal
// expansions with one star choose the first or last occurrence of a
// segment. Native strings match other patterns with regular expressions.

std::optional<std::vector<std::string>>
shell_convertert::star_segments(const shell_patternt &pattern)
{
  using kindt = shell_pattern_elementt::kindt;
  std::vector<std::string> segments{1};
  for(const auto &element : pattern.elements)
  {
    if(element.kind == kindt::CHARACTER)
      segments.back() += static_cast<char>(element.character);
    else if(element.kind == kindt::STAR)
      segments.emplace_back();
    else
      return {};
  }
  return segments;
}

exprt shell_convertert::pattern_regex(const shell_patternt &pattern)
{
  using kindt = shell_pattern_elementt::kindt;
  regex_buildert re{symbol_table};
  std::vector<exprt> sequence;
  std::string pending;
  for(const auto &element : pattern.elements)
  {
    if(element.kind == kindt::CHARACTER)
    {
      pending += static_cast<char>(element.character);
      continue;
    }
    if(!pending.empty())
    {
      sequence.push_back(re.literal(pending));
      pending.clear();
    }
    if(element.kind == kindt::ANY)
      sequence.push_back(re.any());
    else if(element.kind == kindt::STAR)
      sequence.push_back(re.star(re.any()));
    else if(element.kind == kindt::SET)
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

exprt shell_convertert::unbounded_pattern_matches(
  const shell_patternt &pattern,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt s = payload_of(subject);
  const auto segments = star_segments(pattern);
  if(!segments.has_value())
  {
    if(!native())
    {
      unsupported(
        loc,
        "pattern with ? or a bracket expression with --shell-strings "
        "refined");
    }
    regex_buildert re{symbol_table};
    return re.matches(s, pattern_regex(pattern));
  }
  const std::string &first = segments->front();
  const std::string &last = segments->back();
  const exprt length = payload_length(s, dest, loc);
  exprt::operandst conditions;
  conditions.push_back(binary_relation_exprt{
    length, ID_ge, from_integer(first.size() + last.size(), integer_type())});
  if(!first.empty())
    conditions.push_back(
      payload_has_prefix(s, payload_literal(first), dest, loc));
  if(!last.empty())
    conditions.push_back(
      payload_has_suffix(s, payload_literal(last), dest, loc));
  exprt position = from_integer(first.size(), integer_type());
  for(std::size_t k = 1; k + 1 < segments->size(); ++k)
  {
    const std::string &segment = (*segments)[k];
    if(segment.empty())
      continue;
    const exprt found =
      payload_index_of(s, payload_literal(segment), position, dest, loc);
    conditions.push_back(
      binary_relation_exprt{found, ID_ge, from_integer(0, integer_type())});
    position = plus_exprt{found, from_integer(segment.size(), integer_type())};
  }
  if(segments->size() > 2)
  {
    conditions.push_back(binary_relation_exprt{
      position,
      ID_le,
      minus_exprt{length, from_integer(last.size(), integer_type())}});
  }
  return conjunction(conditions);
}

exprt shell_convertert::unbounded_remove_pattern(
  const shell_patternt &pattern,
  const exprt &value,
  bool prefix,
  bool longest,
  code_blockt &dest,
  const source_locationt &loc)
{
  const auto segments = star_segments(pattern);
  if(!segments.has_value() || segments->size() != 2)
  {
    unsupported(
      loc,
      "pattern removal with a pattern other than L*, *L, or L1*L2 with "
      "unbounded strings");
  }
  // The pattern is a*b; the result is s[start, end).
  const std::string &a = (*segments)[0];
  const std::string &b = (*segments)[1];
  const exprt s = payload_of(value);
  const exprt length = payload_length(s, dest, loc);
  const exprt zero = from_integer(0, integer_type());
  const exprt a_size = from_integer(a.size(), integer_type());
  const exprt b_size = from_integer(b.size(), integer_type());
  // The bytes of s that neither a nor b may use.
  const exprt free_length = minus_exprt{length, plus_exprt{a_size, b_size}};

  exprt::operandst conditions;
  conditions.push_back(binary_relation_exprt{free_length, ID_ge, zero});
  exprt start = zero;
  exprt end = length;
  if(prefix)
  {
    // Remove a ... b from the start: a is a prefix, and b's first (for the
    // shortest) or last (for the longest) occurrence after it ends the
    // match.
    if(!a.empty())
      conditions.push_back(
        payload_has_prefix(s, payload_literal(a), dest, loc));
    if(b.empty())
      start = longest ? length : a_size;
    else
    {
      const exprt found =
        longest ? payload_last_index_of(
                    s,
                    payload_literal(b),
                    minus_exprt{length, b_size},
                    true,
                    dest,
                    loc)
                : payload_index_of(s, payload_literal(b), a_size, dest, loc);
      conditions.push_back(binary_relation_exprt{found, ID_ge, a_size});
      start = plus_exprt{found, b_size};
    }
  }
  else
  {
    // Remove a ... b from the end: b is a suffix, and a's last (for the
    // shortest) or first (for the longest) occurrence before it starts
    // the match.
    if(!b.empty())
      conditions.push_back(
        payload_has_suffix(s, payload_literal(b), dest, loc));
    if(a.empty())
      end = longest ? zero : exprt{minus_exprt{length, b_size}};
    else
    {
      const exprt bound = minus_exprt{length, plus_exprt{a_size, b_size}};
      const exprt found =
        longest ? payload_index_of(s, payload_literal(a), zero, dest, loc)
                : payload_last_index_of(
                    s, payload_literal(a), bound, b.empty(), dest, loc);
      conditions.push_back(binary_relation_exprt{found, ID_ge, zero});
      conditions.push_back(binary_relation_exprt{found, ID_le, bound});
      end = found;
    }
  }
  const exprt matched = conjunction(conditions);
  const exprt rest = payload_substring(
    s,
    if_exprt{matched, start, zero},
    if_exprt{matched, end, length},
    dest,
    loc);
  return make_value(rest);
}
