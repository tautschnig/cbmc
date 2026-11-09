/// Module: String solver
/// Author: Diffblue Ltd.

#include "string_builtin_function.h"

#include "array_pool.h"
#include "string_constraint_generator.h"
#include "string_format_builtin_function.h"

#include <algorithm>
#include <iterator>
#include <vector>

string_transformation_builtin_functiont::
  string_transformation_builtin_functiont(
    const exprt &return_code,
    const std::vector<exprt> &fun_args,
    array_poolt &array_pool)
  : string_builtin_functiont(return_code, array_pool)
{
  PRECONDITION(fun_args.size() > 2);
  const auto arg1 = expr_try_dynamic_cast<struct_exprt>(fun_args[2]);
  if(arg1 && arg1->operands().size() == 2)
    input = array_pool.find(arg1->op1(), arg1->op0());
  else
    input = get_string_expr(array_pool, fun_args[2]);
  result = array_pool.find(fun_args[1], fun_args[0]);
}

std::optional<std::vector<mp_integer>> eval_string(
  const array_string_exprt &a,
  const std::function<exprt(const exprt &)> &get_value)
{
  if(a.id() == ID_if)
  {
    const if_exprt &ite = to_if_expr(a);
    const exprt cond = get_value(ite.cond());
    if(!cond.is_constant())
      return {};
    return cond == true
             ? eval_string(to_array_string_expr(ite.true_case()), get_value)
             : eval_string(to_array_string_expr(ite.false_case()), get_value);
  }

  const exprt content = get_value(a.content());
  const auto &array = expr_try_dynamic_cast<array_exprt>(content);
  if(!array)
    return {};

  const auto &ops = array->operands();
  std::vector<mp_integer> result;
  const mp_integer unknown('?');
  const auto &insert = std::back_inserter(result);
  std::transform(
    ops.begin(), ops.end(), insert, [unknown](const exprt &e) { // NOLINT
      if(const auto i = numeric_cast<mp_integer>(e))
        return *i;
      return unknown;
    });
  return result;
}

template <typename Iter>
static array_string_exprt
make_string(Iter begin, Iter end, const array_typet &array_type)
{
  const typet &char_type = array_type.element_type();
  array_exprt array_expr({}, array_type);
  const auto &insert = std::back_inserter(array_expr.operands());
  std::transform(begin, end, insert, [&](const mp_integer &i) {
    return from_integer(i, char_type);
  });
  return to_array_string_expr(array_expr);
}

array_string_exprt
make_string(const std::vector<mp_integer> &array, const array_typet &array_type)
{
  return make_string(array.begin(), array.end(), array_type);
}

std::optional<exprt> string_concat_char_builtin_functiont::eval(
  const std::function<exprt(const exprt &)> &get_value) const
{
  auto input_opt = eval_string(input, get_value);
  if(!input_opt.has_value())
    return {};
  const mp_integer char_val = [&] {
    if(const auto val = numeric_cast<mp_integer>(get_value(character)))
      return *val;
    INVARIANT(
      get_value(character).id() == ID_unknown,
      "character valuation should only contain constants and unknown");
    return mp_integer(CHARACTER_FOR_UNKNOWN);
  }();
  input_opt.value().push_back(char_val);
  const auto length =
    from_integer(input_opt.value().size(), result.length_type());
  const array_typet type(to_type_with_subtype(result.type()).subtype(), length);
  return make_string(input_opt.value(), type);
}

/// Set of constraints enforcing that `result` is the concatenation
/// of `input` with `character`. These constraints are :
///   * result.length = input.length + 1
///   * forall i < max(0, result.length). result[i] = input[i]
///   * result[input.length] = character
///   * return_code = 0
string_constraintst string_concat_char_builtin_functiont::constraints(
  string_constraint_generatort &generator,
  message_handlert &message_handler) const
{
  string_constraintst constraints;
  constraints.existential.push_back(length_constraint());
  constraints.universal.push_back([&] {
    const symbol_exprt idx =
      generator.fresh_symbol("QA_index_concat_char", result.length_type());
    const exprt upper_bound =
      zero_if_negative(array_pool.get_or_create_length(input));
    return string_constraintt(
      idx,
      upper_bound,
      equal_exprt(input[idx], result[idx]),
      generator.ns,
      message_handler);
  }());
  constraints.existential.push_back(
    equal_exprt(result[array_pool.get_or_create_length(input)], character));
  constraints.existential.push_back(
    equal_exprt(return_code, from_integer(0, return_code.type())));
  return constraints;
}

exprt string_concat_char_builtin_functiont::length_constraint() const
{
  return length_constraint_for_concat_char(result, input, array_pool);
}

std::optional<exprt> string_set_char_builtin_functiont::eval(
  const std::function<exprt(const exprt &)> &get_value) const
{
  auto input_opt = eval_string(input, get_value);
  const auto char_opt = numeric_cast<mp_integer>(get_value(character));
  const auto position_opt = numeric_cast<mp_integer>(get_value(position));
  if(!input_opt || !char_opt || !position_opt)
    return {};
  if(0 <= *position_opt && *position_opt < input_opt.value().size())
    input_opt.value()[numeric_cast_v<std::size_t>(*position_opt)] = *char_opt;
  const auto length =
    from_integer(input_opt.value().size(), result.length_type());
  const array_typet type(to_type_with_subtype(result.type()).subtype(), length);
  return make_string(input_opt.value(), type);
}

/// Set of constraints ensuring that `result` is similar to `input`
/// where the character at index `position` is set to `character`.
/// If `position` is out of bounds, `input` and `result` are identical.
/// These constraints are:
///   1. res.length = str.length
///      && return_code = (position >= res.length || position < 0) ? 1 : 0
///   2. 0 <= pos < res.length ==> res[pos]=char
///   3. forall 0 <= i < max(0, min(res.length, pos)). res[i] = str[i]
///   4. forall max(0, pos+1) <= i < res.length. res[i] = str[i]
string_constraintst string_set_char_builtin_functiont::constraints(
  string_constraint_generatort &generator,
  message_handlert &message_handler) const
{
  string_constraintst constraints;
  constraints.existential.push_back(length_constraint());
  constraints.existential.push_back(implies_exprt(
    and_exprt(
      binary_relation_exprt(from_integer(0, position.type()), ID_le, position),
      binary_relation_exprt(
        position, ID_lt, array_pool.get_or_create_length(result))),
    equal_exprt(result[position], character)));
  constraints.universal.push_back([&] {
    const symbol_exprt q =
      generator.fresh_symbol("QA_char_set", position.type());
    const equal_exprt a3_body(result[q], input[q]);
    return string_constraintt(
      q,
      zero_if_negative(
        minimum(array_pool.get_or_create_length(result), position)),
      a3_body,
      generator.ns,
      message_handler);
  }());
  constraints.universal.push_back([&] {
    const symbol_exprt q2 =
      generator.fresh_symbol("QA_char_set2", position.type());
    const equal_exprt a4_body(result[q2], input[q2]);
    return string_constraintt(
      q2,
      zero_if_negative(plus_exprt(position, from_integer(1, position.type()))),
      zero_if_negative(array_pool.get_or_create_length(result)),
      a4_body,
      generator.ns,
      message_handler);
  }());
  return constraints;
}

exprt string_set_char_builtin_functiont::length_constraint() const
{
  const exprt out_of_bounds = or_exprt(
    binary_relation_exprt(
      position, ID_ge, array_pool.get_or_create_length(input)),
    binary_relation_exprt(
      position, ID_lt, from_integer(0, input.length_type())));
  const exprt return_value = if_exprt(
    out_of_bounds,
    from_integer(1, return_code.type()),
    from_integer(0, return_code.type()));
  return and_exprt(
    equal_exprt(
      array_pool.get_or_create_length(result),
      array_pool.get_or_create_length(input)),
    equal_exprt(return_code, return_value));
}

/// Whether characters of \p width bits are bytes of UTF-8, rather than
/// UTF-16 code units.
static bool is_utf8(std::size_t width)
{
  return width <= 8;
}

/// Whether the case conversion of character \p c is modelled: for UTF-8
/// bytes, those of ASCII, as other characters have several bytes; for UTF-16,
/// those below U+0100 that convert within that range, as the upper case of
/// µ (U+00B5), ß (U+00DF, "SS") and ÿ (U+00FF) is outside it.
static bool
eval_case_is_modelled(const mp_integer &c, std::size_t width, bool to_upper)
{
  if(is_utf8(width))
    return c < 0x80;
  return c < 0x100 && !(to_upper && (c == 0xb5 || c == 0xdf || c == 0xff));
}

/// Whether \p c is an upper-case letter of ASCII or, for UTF-16, of
/// Latin-1.
static bool eval_is_upper_case(const mp_integer &c, std::size_t width)
{
  return ('A' <= c && c <= 'Z') ||
         (!is_utf8(width) &&
          ((0xc0 <= c && c <= 0xd6) || (0xd8 <= c && c <= 0xde)));
}

/// The case conversion of a modelled character \p c.
static mp_integer
eval_case_convert(const mp_integer &c, std::size_t width, bool to_upper)
{
  if(to_upper)
    return eval_is_upper_case(c - 0x20, width) ? c - 0x20 : c;
  return eval_is_upper_case(c, width) ? c + 0x20 : c;
}

static std::size_t char_width(const array_string_exprt &s)
{
  return to_bitvector_type(to_type_with_subtype(s.content().type()).subtype())
    .get_width();
}

/// The case conversion of \p input, if all its characters are modelled.
static std::optional<exprt> eval_case_conversion(
  const array_string_exprt &input,
  const array_string_exprt &result,
  bool to_upper,
  const std::function<exprt(const exprt &)> &get_value)
{
  auto input_opt = eval_string(input, get_value);
  if(!input_opt)
    return {};
  const std::size_t width = char_width(input);
  for(mp_integer &c : input_opt.value())
  {
    if(!eval_case_is_modelled(c, width, to_upper))
      return {};
    c = eval_case_convert(c, width, to_upper);
  }
  const auto length =
    from_integer(input_opt.value().size(), result.length_type());
  const array_typet type(to_type_with_subtype(result.type()).subtype(), length);
  return make_string(input_opt.value(), type);
}

std::optional<exprt> string_to_lower_case_builtin_functiont::eval(
  const std::function<exprt(const exprt &)> &get_value) const
{
  return eval_case_conversion(input, result, false, get_value);
}

std::optional<exprt> string_to_upper_case_builtin_functiont::eval(
  const std::function<exprt(const exprt &)> &get_value) const
{
  return eval_case_conversion(input, result, true, get_value);
}

static exprt in_range(const exprt &c, unsigned lower, unsigned upper)
{
  return and_exprt{
    binary_relation_exprt{from_integer(lower, c.type()), ID_le, c},
    binary_relation_exprt{c, ID_le, from_integer(upper, c.type())}};
}

/// As eval_is_upper_case, for a character expression.
static exprt is_upper_case(const exprt &c, std::size_t width)
{
  if(is_utf8(width))
    return in_range(c, 'A', 'Z');
  return or_exprt{
    in_range(c, 'A', 'Z'), in_range(c, 0xc0, 0xd6), in_range(c, 0xd8, 0xde)};
}

/// As eval_case_is_modelled, for a character expression.
static exprt case_is_modelled(const exprt &c, std::size_t width, bool to_upper)
{
  if(is_utf8(width))
    return binary_relation_exprt{c, ID_lt, from_integer(0x80, c.type())};
  exprt::operandst conjuncts{
    binary_relation_exprt{c, ID_lt, from_integer(0x100, c.type())}};
  if(to_upper)
  {
    for(const unsigned special : {0xb5, 0xdf, 0xff})
      conjuncts.push_back(notequal_exprt{c, from_integer(special, c.type())});
  }
  return conjunction(conjuncts);
}

/// As eval_case_convert, for a character expression.
static exprt case_convert(const exprt &c, std::size_t width, bool to_upper)
{
  const exprt diff = from_integer(0x20, c.type());
  if(to_upper)
  {
    // c - 0x20 wraps around for c < 0x20, which is not upper case then.
    const minus_exprt shifted{c, diff};
    return if_exprt{is_upper_case(shifted, width), shifted, c};
  }
  return if_exprt{is_upper_case(c, width), plus_exprt{c, diff}, c};
}

/// Whether the case conversion of \p c is a single character, so that it
/// keeps the positions of the other characters: for UTF-8 bytes and for
/// upper case, exactly where it is modelled; Java's lower case only maps
/// U+0130 (I with dot above) to two characters.
static exprt case_keeps_length(const exprt &c, std::size_t width, bool to_upper)
{
  if(is_utf8(width))
    return case_is_modelled(c, width, to_upper);
  if(!to_upper)
    return notequal_exprt{c, from_integer(0x130, c.type())};
  // The characters of the Basic Multilingual Plane whose upper case has
  // several characters (SpecialCasing.txt of Unicode).
  static const std::vector<std::pair<unsigned, unsigned>> several = {
    {0xdf, 0xdf},     {0x149, 0x149},   {0x1f0, 0x1f0},   {0x390, 0x390},
    {0x3b0, 0x3b0},   {0x587, 0x587},   {0x1e96, 0x1e9a}, {0x1f50, 0x1f50},
    {0x1f52, 0x1f52}, {0x1f54, 0x1f54}, {0x1f56, 0x1f56}, {0x1f80, 0x1faf},
    {0x1fb2, 0x1fb4}, {0x1fb6, 0x1fb7}, {0x1fbc, 0x1fbc}, {0x1fc2, 0x1fc4},
    {0x1fc6, 0x1fc7}, {0x1fcc, 0x1fcc}, {0x1fd2, 0x1fd3}, {0x1fd6, 0x1fd7},
    {0x1fe2, 0x1fe4}, {0x1fe6, 0x1fe7}, {0x1ff2, 0x1ff4}, {0x1ff6, 0x1ff7},
    {0x1ffc, 0x1ffc}, {0xfb00, 0xfb06}, {0xfb13, 0xfb17}};
  exprt::operandst conjuncts;
  for(const auto &range : several)
    conjuncts.push_back(not_exprt{in_range(c, range.first, range.second)});
  return conjunction(conjuncts);
}

/// Constraints ensuring that \p result is \p input with its case converted.
/// The conversion of a character whose conversion keeps the length (see
/// case_keeps_length) is a single character, of another one is longer. So
/// with first the position of the first of the others (|input| if there is
/// none):
///   1. return_code = 0
///   2. forall i < first. keeps(input[i]) &&
///        (is_modelled(input[i]) => result[i] = convert(input[i]))
///   3. there is none => |result| = |input|
///   4. there is one => 0 <= first < |input| && !keeps(input[first]) &&
///        |input| < |result| <= growth * |input|
///   5. there is one => keeps(input[|input| - 1]) &&
///        is_modelled(input[|input| - 1]) =>
///          result[|result| - 1] = convert(input[|input| - 1])
/// The other characters of the result are unconstrained. (Constraining all
/// those after the last such character, with a universal axiom at a shift by
/// |result| - |input|, made the index sets grow without bound.)
static string_constraintst case_conversion_constraints(
  const exprt &return_code,
  const array_string_exprt &result,
  const array_string_exprt &input,
  bool to_upper,
  string_constraint_generatort &generator,
  message_handlert &message_handler)
{
  array_poolt &array_pool = generator.array_pool;
  const std::size_t width = char_width(input);
  const typet &index_type = input.length_type();
  const exprt input_length = array_pool.get_or_create_length(input);
  const exprt result_length = array_pool.get_or_create_length(result);
  string_constraintst constraints;
  constraints.existential.push_back(
    equal_exprt{return_code, from_integer(0, return_code.type())});

  // All characters are modelled when the input is constant and they are.
  bool known_modelled = false;
  const auto length_value = numeric_cast<std::size_t>(input_length);
  if(
    length_value.has_value() && input.content().id() == ID_array &&
    input.content().operands().size() >= *length_value)
  {
    const auto &operands = input.content().operands();
    known_modelled = std::all_of(
      operands.begin(),
      operands.begin() + *length_value,
      [&](const exprt &c)
      {
        const auto value = numeric_cast<mp_integer>(c);
        return value.has_value() &&
               eval_case_is_modelled(*value, width, to_upper);
      });
  }
  if(!known_modelled)
  {
    generator.approximation_warning(
      to_upper ? "upper-case conversion" : "lower-case conversion",
      is_utf8(width) ? "is only modelled for ASCII characters"
                     : "is only modelled for characters below U+0100 that "
                       "convert within that range");
  }

  // The conversion of a character at position i of the input to position j
  // of the result.
  const auto converted = [&](const exprt &i, const exprt &j)
  {
    const exprt c = input[i];
    return and_exprt{
      case_keeps_length(c, width, to_upper),
      implies_exprt{
        case_is_modelled(c, width, to_upper),
        equal_exprt{result[j], case_convert(c, width, to_upper)}}};
  };

  if(known_modelled)
  {
    constraints.existential.push_back(equal_exprt{result_length, input_length});
    const symbol_exprt idx = generator.fresh_symbol(
      to_upper ? "QA_upper_case" : "QA_lower_case", index_type);
    constraints.universal.emplace_back(
      idx,
      zero_if_negative(input_length),
      converted(idx, idx),
      generator.ns,
      message_handler);
    return constraints;
  }

  const symbol_exprt keeps = generator.fresh_symbol("case_conversion_keeps");
  const symbol_exprt first =
    generator.fresh_symbol("case_conversion_first", index_type);
  constraints.existential.push_back(implies_exprt{
    keeps,
    and_exprt{
      equal_exprt{first, input_length},
      equal_exprt{result_length, input_length}}});
  constraints.existential.push_back(implies_exprt{
    not_exprt{keeps},
    conjunction(
      {binary_relation_exprt{first, ID_ge, from_integer(0, index_type)},
       binary_relation_exprt{first, ID_lt, input_length},
       not_exprt{case_keeps_length(input[first], width, to_upper)},
       binary_relation_exprt{result_length, ID_gt, input_length},
       // A character converts to at most three (two for lower case in
       // Java, and for UTF-8 bytes a byte of a character of up to four
       // bytes converts to a character of up to four bytes).
       binary_relation_exprt{
         result_length,
         ID_le,
         mult_exprt{
           input_length,
           from_integer(
             is_utf8(width) ? 4
             : to_upper     ? 3
                            : 2,
             index_type)}}})});

  // The last character of the input, where its conversion keeps the
  // length, is converted to the last of the result. (The last two made a
  // test with strings of up to 1000 characters go from 0.4 s to 26 s.)
  {
    const exprt one = from_integer(1, index_type);
    const minus_exprt i{input_length, one};
    const exprt c = input[i];
    constraints.existential.push_back(implies_exprt{
      conjunction(
        {not_exprt{keeps},
         case_keeps_length(c, width, to_upper),
         case_is_modelled(c, width, to_upper)}),
      equal_exprt{
        result[minus_exprt{result_length, one}],
        case_convert(c, width, to_upper)}});
  }

  const symbol_exprt prefix_idx = generator.fresh_symbol(
    to_upper ? "QA_upper_case" : "QA_lower_case", index_type);
  constraints.universal.emplace_back(
    prefix_idx,
    zero_if_negative(first),
    converted(prefix_idx, prefix_idx),
    generator.ns,
    message_handler);

  return constraints;
}

string_constraintst string_to_lower_case_builtin_functiont::constraints(
  string_constraint_generatort &generator,
  message_handlert &message_handler) const
{
  return case_conversion_constraints(
    return_code, result, input, false, generator, message_handler);
}

string_constraintst string_to_upper_case_builtin_functiont::constraints(
  string_constraint_generatort &generator,
  message_handlert &message_handler) const
{
  return case_conversion_constraints(
    return_code, result, input, true, generator, message_handler);
}

/// Constructor from arguments of a function application.
/// The arguments in `fun_args` should be in order:
/// an integer `result.length`, a character pointer `&result[0]`,
/// an expression `arg` which is to be converted to a string.
/// Other arguments are ignored by this constructor.
string_creation_builtin_functiont::string_creation_builtin_functiont(
  const exprt &return_code,
  const std::vector<exprt> &fun_args,
  array_poolt &array_pool)
  : string_builtin_functiont(return_code, array_pool)
{
  PRECONDITION(fun_args.size() >= 3);
  result = array_pool.find(fun_args[1], fun_args[0]);
  arg = fun_args[2];
}

std::optional<exprt> string_of_int_builtin_functiont::eval(
  const std::function<exprt(const exprt &)> &get_value) const
{
  const auto arg_value = numeric_cast<mp_integer>(get_value(arg));
  if(!arg_value)
    return {};
  static std::string digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  const auto radix_value = numeric_cast<mp_integer>(get_value(radix));
  if(!radix_value || *radix_value > digits.length())
    return {};

  mp_integer current_value = *arg_value;
  std::vector<mp_integer> right_to_left_characters;
  if(current_value < 0)
    current_value = -current_value;
  if(current_value == 0)
    right_to_left_characters.emplace_back('0');
  while(current_value > 0)
  {
    const auto digit_value = (current_value % *radix_value).to_ulong();
    right_to_left_characters.emplace_back(digits.at(digit_value));
    current_value /= *radix_value;
  }
  if(*arg_value < 0)
    right_to_left_characters.emplace_back('-');

  const auto length = right_to_left_characters.size();
  const auto length_expr = from_integer(length, result.length_type());
  const array_typet type(
    to_type_with_subtype(result.type()).subtype(), length_expr);
  return make_string(
    right_to_left_characters.rbegin(), right_to_left_characters.rend(), type);
}

string_constraintst string_of_int_builtin_functiont::constraints(
  string_constraint_generatort &generator,
  message_handlert &message_handler) const
{
  auto pair =
    generator.add_axioms_for_string_of_int_with_radix(result, arg, radix, 0);
  pair.second.existential.push_back(equal_exprt(pair.first, return_code));
  return pair.second;
}

bool string_of_int_builtin_functiont::length_is_exact() const
{
  const auto radix_value = numeric_cast<unsigned long>(radix);
  return radix_value.has_value() && *radix_value >= 2 && *radix_value <= 36 &&
         arg.type().id() == ID_signedbv;
}

exprt string_of_int_builtin_functiont::length_constraint() const
{
  const auto radix_value = numeric_cast<unsigned long>(radix);
  // Without a constant radix the length is left to the content axioms.
  if(
    !radix_value.has_value() || *radix_value < 2 || *radix_value > 36 ||
    arg.type().id() != ID_signedbv)
  {
    return true_exprt{};
  }
  return equal_exprt{
    array_pool.get_or_create_length(result),
    length_of_decimal_int(arg, result.length_type(), *radix_value)};
}

string_builtin_function_with_no_evalt::string_builtin_function_with_no_evalt(
  const exprt &return_code,
  const function_application_exprt &f,
  array_poolt &array_pool)
  : string_builtin_functiont(return_code, array_pool), function_application(f)
{
  const std::vector<exprt> &fun_args = f.arguments();
  std::size_t i = 0;
  if(fun_args.size() >= 2 && fun_args[1].type().id() == ID_pointer)
  {
    string_res = array_pool.find(fun_args[1], fun_args[0]);
    i = 2;
  }

  for(; i < fun_args.size(); ++i)
  {
    const auto arg = expr_try_dynamic_cast<struct_exprt>(fun_args[i]);
    // TODO: use is_refined_string_type ?
    if(
      arg && arg->operands().size() == 2 &&
      arg->op1().type().id() == ID_pointer)
    {
      INVARIANT(is_refined_string_type(arg->type()), "should be a string");
      string_args.push_back(array_pool.find(arg->op1(), arg->op0()));
    }
    else
      args.push_back(fun_args[i]);
  }
}

string_constraintst string_builtin_function_with_no_evalt::constraints(
  string_constraint_generatort &generator,
  message_handlert &message_handler) const
{
  auto pair =
    generator.add_axioms_for_function_application(function_application);
  pair.second.existential.push_back(equal_exprt(pair.first, return_code));
  return pair.second;
}
