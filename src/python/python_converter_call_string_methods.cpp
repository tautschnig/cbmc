/// Python to GOTO converter — string-method dispatch
/// (PLR §6.4.6 str methods). Handles split, replace,
/// format, format_map, startswith, endswith, the
/// is{alpha,digit,space,alnum,...} predicates, upper,
/// lower, capitalize, title, swapcase, find, rfind,
/// index, rindex, encode, decode, join, strip variants,
/// count, partition, rpartition, ljust, rjust, center,
/// expandtabs, zfill. Extracted from
/// python_converter_call_method.cpp per
/// doc/python-frontend-architecture.md.
/// Pure source-split — semantics are preserved.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>
#include <util/unicode.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

// Properties of the Latin-1 characters, as CPython 3.12 reports them for
// one-character strings (str.isalpha and so on; the identifier ones are
// c.isidentifier() and ('a' + c).isidentifier()).
enum : unsigned
{
  latin1_alpha = 1u << 0,
  latin1_alnum = 1u << 1,
  latin1_digit = 1u << 2,
  latin1_numeric = 1u << 3,
  latin1_decimal = 1u << 4,
  latin1_space = 1u << 5,
  latin1_upper = 1u << 6,
  latin1_lower = 1u << 7,
  latin1_id_start = 1u << 8,
  latin1_id_continue = 1u << 9
};
static const std::uint16_t latin1_properties[256] = {
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x020, 0x020,
  0x020, 0x020, 0x020, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x020, 0x020, 0x020, 0x020, 0x020,
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x000, 0x000, 0x000, 0x000, 0x21e, 0x21e, 0x21e, 0x21e, 0x21e, 0x21e, 0x21e,
  0x21e, 0x21e, 0x21e, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x343,
  0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343,
  0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343,
  0x343, 0x343, 0x343, 0x000, 0x000, 0x000, 0x000, 0x300, 0x000, 0x383, 0x383,
  0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383,
  0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383,
  0x383, 0x383, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x000, 0x020, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x020, 0x000, 0x000, 0x000, 0x000,
  0x000, 0x000, 0x000, 0x000, 0x000, 0x383, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x000, 0x000, 0x00e, 0x00e, 0x000, 0x383, 0x000, 0x200, 0x000, 0x00e, 0x383,
  0x000, 0x00a, 0x00a, 0x00a, 0x000, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343,
  0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x343,
  0x343, 0x343, 0x343, 0x343, 0x343, 0x343, 0x000, 0x343, 0x343, 0x343, 0x343,
  0x343, 0x343, 0x343, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383,
  0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383, 0x383,
  0x383, 0x383, 0x383, 0x383, 0x383, 0x000, 0x383, 0x383, 0x383, 0x383, 0x383,
  0x383, 0x383, 0x383,
};

/// True when \p s has only ASCII characters.
static bool is_ascii_string(const std::string &s)
{
  return std::all_of(
    s.begin(),
    s.end(),
    [](char c) { return (static_cast<unsigned char>(c) & 0x80) == 0; });
}

std::optional<exprt> python_convertert::native_affix_or_padding(
  const jsont &expr,
  const exprt &s,
  const std::string &method,
  const jsont &args)
{
  const string_typet str_t;
  const integer_typet int_t;
  if(!args.is_array() || as_array(args).empty())
    return std::nullopt;
  const std::size_t nargs = as_array(args).size();
  auto len = [&](const exprt &x)
  {
    return native_string_app(
      ID_cprover_string_length_func, {str_t}, {x}, int_t);
  };
  auto substr = [&](const exprt &x, const exprt &start, const exprt &count)
  {
    return native_string_app(
      ID_cprover_string_substring_func,
      {str_t, int_t, int_t},
      {x, start, count},
      str_t);
  };
  auto concat = [&](const exprt &a, const exprt &b)
  {
    return native_string_app(
      ID_cprover_string_concat_func, {str_t, str_t}, {a, b}, str_t);
  };
  const exprt zero = from_integer(0, int_t);

  if((method == "removeprefix" || method == "removesuffix") && nargs == 1)
  {
    const exprt affix = convert_expression(*as_array(args).begin());
    if(affix.type().id() != ID_string)
      return std::nullopt;
    if(method == "removeprefix")
      return if_exprt{
        native_string_app(
          ID_cprover_string_startswith_func,
          {str_t, str_t},
          {s, affix},
          bool_typet{}),
        substr(s, len(affix), len(s)),
        s};
    return if_exprt{
      native_string_app(
        ID_cprover_string_endswith_func,
        {str_t, str_t},
        {s, affix},
        bool_typet{}),
      substr(s, zero, minus_exprt{len(s), len(affix)}),
      s};
  }

  const bool padding = method == "ljust" || method == "rjust" ||
                       method == "center" || method == "zfill";
  if(!padding || nargs > (method == "zfill" ? 1u : 2u))
    return std::nullopt;
  auto it = as_array(args).begin();
  const exprt width = convert_expression(*it);
  if(width.type().id() != ID_signedbv)
    return std::nullopt;
  std::string fill = method == "zfill" ? "0" : " ";
  if(++it != as_array(args).end())
  {
    const auto fv = extract_string_value(convert_expression(*it));
    // a symbolic or non-ASCII fill (or one that raises TypeError) is not
    // modelled here
    if(!fv.has_value() || fv->size() != 1 || !is_ascii_string(*fv))
      return std::nullopt;
    fill = *fv;
  }
  const exprt w = typecast_exprt{width, int_t};
  // The padding: a fresh string of `count` fill characters.
  auto pad = [&](const exprt &count) -> exprt
  {
    static unsigned pad_ctr = 0;
    const std::string nm = "__pad_" + std::to_string(pad_ctr++);
    const irep_idt id{qualify_name(nm)};
    if(symbol_table.lookup(id) == nullptr)
    {
      symbolt sy{id, str_t, "python"};
      sy.base_name = nm;
      sy.is_lvalue = true;
      sy.is_state_var = true;
      symbol_table.add(sy);
    }
    const symbol_exprt p = symbol_table.lookup_ref(id).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      p, side_effect_expr_nondett{str_t, get_location(expr)}});
    const typet re_t{ID_regex};
    pending_checks.push_back(code_assumet{and_exprt{
      equal_exprt{len(p), count},
      native_string_app(
        ID_cprover_string_in_regex_func,
        {str_t, re_t},
        {p,
         native_string_app(
           ID_cprover_regex_star_func,
           {re_t},
           {native_string_app(
             ID_cprover_string_to_regex_func,
             {str_t},
             {constant_exprt{fill, str_t}},
             re_t)},
           re_t)},
        bool_typet{})}});
    return p;
  };
  // the number of fill characters: max(0, width - len(s))
  const exprt margin = minus_exprt{w, len(s)};
  const exprt count =
    if_exprt{binary_relation_exprt{margin, ID_lt, zero}, zero, margin};
  if(method == "ljust")
    return concat(s, pad(count));
  if(method == "rjust")
    return concat(pad(count), s);
  if(method == "center")
  {
    // CPython: left = marg / 2 + (marg & width & 1)
    const exprt two = from_integer(2, int_t);
    const exprt one = from_integer(1, int_t);
    const exprt odd_both = and_exprt{
      equal_exprt{mod_exprt{count, two}, one},
      equal_exprt{mod_exprt{w, two}, one}};
    const exprt left =
      plus_exprt{div_exprt{count, two}, if_exprt{odd_both, one, zero}};
    return concat(concat(pad(left), s), pad(minus_exprt{count, left}));
  }
  // zfill: the zeros go after a leading sign
  const exprt first = substr(s, zero, from_integer(1, int_t));
  const exprt sign = or_exprt{
    equal_exprt{first, constant_exprt{"+", str_t}},
    equal_exprt{first, constant_exprt{"-", str_t}}};
  const exprt zeros = pad(count);
  return if_exprt{
    sign,
    concat(concat(first, zeros), substr(s, from_integer(1, int_t), len(s))),
    concat(zeros, s)};
}

exprt python_convertert::native_string_predicate(
  const exprt &s,
  const std::string &method,
  const source_locationt &loc)
{
  const typet re_t{ID_regex};
  const string_typet str_t;
  auto character = [&](char32_t c)
  {
    return constant_exprt{
      utf32_native_endian_to_utf8(std::u32string(1, c)), str_t};
  };
  auto range = [&](char32_t lo, char32_t hi)
  {
    return native_string_app(
      ID_cprover_regex_range_func,
      {str_t, str_t},
      {character(lo), character(hi)},
      re_t);
  };
  auto re2 = [&](const irep_idt &fn, const exprt &a, const exprt &b) {
    return native_string_app(fn, {re_t, re_t}, {a, b}, re_t);
  };
  auto re1 = [&](const irep_idt &fn, const exprt &a)
  { return native_string_app(fn, {re_t}, {a}, re_t); };
  const exprt all =
    native_string_app(ID_cprover_regex_allchar_func, {}, {}, re_t);
  const exprt any_string = re1(ID_cprover_regex_star_func, all);
  // The Latin-1 characters with a property, as a union of ranges.
  auto latin1_class = [&](const std::function<bool(unsigned)> &has) -> exprt
  {
    std::optional<exprt> result;
    for(unsigned c = 0; c < 256;)
    {
      if(!has(latin1_properties[c]))
      {
        ++c;
        continue;
      }
      unsigned end = c;
      while(end + 1 < 256 && has(latin1_properties[end + 1]))
        ++end;
      const exprt r = range(c, end);
      result =
        result.has_value() ? re2(ID_cprover_regex_union_func, *result, r) : r;
      c = end + 1;
    }
    return result.has_value()
             ? *result
             : native_string_app(ID_cprover_regex_none_func, {}, {}, re_t);
  };
  const exprt beyond = range(0x100, 0x2ffff);
  auto in = [&](const exprt &re)
  {
    return native_string_app(
      ID_cprover_string_in_regex_func, {str_t, re_t}, {s, re}, bool_typet{});
  };
  auto decide = [&](const exprt &yes, const exprt &no) -> exprt
  {
    return if_exprt{
      yes,
      true_exprt{},
      if_exprt{no, false_exprt{}, side_effect_expr_nondett{bool_typet{}, loc}}};
  };
  auto with = [](unsigned property)
  {
    return std::function<bool(unsigned)>{[property](unsigned p)
                                         { return (p & property) != 0; }};
  };
  auto without = [](unsigned property)
  {
    return std::function<bool(unsigned)>{[property](unsigned p)
                                         { return (p & property) == 0; }};
  };
  auto containing = [&](const exprt &re)
  {
    return re2(
      ID_cprover_regex_concat_func,
      any_string,
      re2(ID_cprover_regex_concat_func, re, any_string));
  };
  const exprt empty = equal_exprt{s, constant_exprt{"", str_t}};

  if(method == "isascii")
    return in(re1(ID_cprover_regex_star_func, range(0, 0x7f)));
  if(method == "isupper" || method == "islower")
  {
    const unsigned want = method == "isupper" ? latin1_upper : latin1_lower;
    const unsigned other = method == "isupper" ? latin1_lower : latin1_upper;
    // all cased characters in the case, and at least one
    const exprt not_other =
      re1(ID_cprover_regex_star_func, latin1_class(without(other)));
    const exprt yes = in(re2(
      ID_cprover_regex_concat_func,
      not_other,
      re2(ID_cprover_regex_concat_func, latin1_class(with(want)), not_other)));
    const exprt no = or_exprt{
      in(containing(latin1_class(with(other)))),
      in(re1(ID_cprover_regex_star_func, latin1_class(without(want | other))))};
    return decide(yes, no);
  }
  if(method == "isidentifier")
  {
    const exprt yes = in(re2(
      ID_cprover_regex_concat_func,
      latin1_class(with(latin1_id_start)),
      re1(ID_cprover_regex_star_func, latin1_class(with(latin1_id_continue)))));
    const exprt no = or_exprt{
      empty,
      or_exprt{
        in(re2(
          ID_cprover_regex_concat_func,
          latin1_class(without(latin1_id_start)),
          any_string)),
        in(re2(
          ID_cprover_regex_concat_func,
          all,
          containing(latin1_class(without(latin1_id_continue)))))}};
    return decide(yes, no);
  }
  const unsigned property = method == "isalpha"     ? latin1_alpha
                            : method == "isalnum"   ? latin1_alnum
                            : method == "isdigit"   ? latin1_digit
                            : method == "isnumeric" ? latin1_numeric
                            : method == "isdecimal" ? latin1_decimal
                            : method == "isspace"   ? latin1_space
                                                    : 0;
  if(property == 0)
    return side_effect_expr_nondett{bool_typet{}, loc};
  // all characters with the property (and at least one); not so if a
  // Latin-1 character lacks it or the string is empty
  const exprt yes =
    in(re1(ID_cprover_regex_plus_func, latin1_class(with(property))));
  const exprt no = or_exprt{
    empty,
    not_exprt{in(re1(
      ID_cprover_regex_star_func,
      re2(
        ID_cprover_regex_union_func, latin1_class(with(property)), beyond)))}};
  return decide(yes, no);
}

exprt python_convertert::native_replace_with_count(
  const jsont &expr,
  const exprt &s,
  const exprt &old,
  const exprt &new_s,
  const exprt &count)
{
  const string_typet str_t;
  const integer_typet int_t;
  const source_locationt loc = get_location(expr);
  auto len = [&](const exprt &x)
  {
    return native_string_app(
      ID_cprover_string_length_func, {str_t}, {x}, int_t);
  };
  auto substr = [&](const exprt &x, const exprt &start, const exprt &n)
  {
    return native_string_app(
      ID_cprover_string_substring_func,
      {str_t, int_t, int_t},
      {x, start, n},
      str_t);
  };
  auto concat = [&](const exprt &a, const exprt &b)
  {
    return native_string_app(
      ID_cprover_string_concat_func, {str_t, str_t}, {a, b}, str_t);
  };
  auto replace_all = [&](const exprt &x, const exprt &by)
  {
    return native_string_app(
      ID_cprover_string_replace_all_func,
      {str_t, str_t, str_t},
      {x, old, by},
      str_t);
  };
  // A fresh variable equal to \p value (nullopt: unconstrained): the
  // repeated terms stay small, and z3 4.16 proved wrong results with
  // nested positions.
  auto fresh = [&](const typet &t, const std::optional<exprt> &value) -> exprt
  {
    static unsigned ctr = 0;
    const std::string nm = "__replace_count_" + std::to_string(ctr++);
    const irep_idt id{qualify_name(nm)};
    if(symbol_table.lookup(id) == nullptr)
    {
      symbolt sy{id, t, "python"};
      sy.base_name = nm;
      sy.is_lvalue = true;
      sy.is_state_var = true;
      symbol_table.add(sy);
    }
    const symbol_exprt v = symbol_table.lookup_ref(id).symbol_expr();
    pending_checks.push_back(
      code_frontend_assignt{v, side_effect_expr_nondett{t, loc}});
    if(value.has_value())
      pending_checks.push_back(code_assumet{equal_exprt{v, *value}});
    return v;
  };
  const exprt zero = from_integer(0, int_t);
  const exprt one = from_integer(1, int_t);
  const exprt k = fresh(int_t, exprt{typecast_exprt{count, int_t}});
  const exprt old_len = len(old);
  const exprt empty_old = equal_exprt{old_len, zero};

  // The replacements are made one by one, up to `levels` of them; x[j] is
  // what is left of s after j of them. A non-empty `old` is replaced at its
  // first occurrence in x[j]. An empty `old` is inserted before the first
  // character of x[j], which is then kept, and once more at the end.
  constexpr std::size_t max_levels = 8;
  std::size_t levels = max_levels;
  if(count.is_constant())
  {
    const auto n = numeric_cast<mp_integer>(to_constant_expr(count));
    if(n.has_value() && *n >= 0 && *n < max_levels)
      levels = numeric_cast_v<std::size_t>(*n);
  }
  std::vector<exprt> x{s};
  std::vector<exprt> found;
  std::vector<exprt> head;
  exprt alive = true_exprt{};
  for(std::size_t j = 0; j < levels; ++j)
  {
    const exprt at = fresh(
      int_t,
      exprt{native_string_app(
        ID_cprover_string_index_of_func,
        {str_t, str_t, int_t},
        {x[j], old, zero},
        int_t)});
    found.push_back(and_exprt{
      alive, or_exprt{empty_old, binary_relation_exprt{at, ID_ge, zero}}});
    head.push_back(if_exprt{
      empty_old,
      concat(new_s, substr(x[j], zero, one)),
      concat(substr(x[j], zero, at), new_s)});
    const exprt consumed = if_exprt{empty_old, one, plus_exprt{at, old_len}};
    // after the insertion at the end, an empty `old` has no place left
    alive = or_exprt{
      not_exprt{empty_old}, binary_relation_exprt{len(x[j]), ID_gt, zero}};
    x.push_back(fresh(str_t, substr(x[j], consumed, len(x[j]))));
  }

  // After the unrolled replacements: none is left to make when the count is
  // used up or `old` does not occur any more. A non-empty `old` is replaced
  // in all of the rest when the count is negative or covers all of its
  // occurrences. Otherwise the rest is unconstrained.
  const exprt rest = x[levels];
  const exprt more = and_exprt{
    alive,
    or_exprt{
      empty_old,
      native_string_app(
        ID_cprover_string_contains_func,
        {str_t, str_t},
        {rest, old},
        bool_typet{})}};
  const exprt occurrences = div_exprt{
    minus_exprt{len(rest), len(replace_all(rest, constant_exprt{"", str_t}))},
    old_len};
  const exprt remaining = minus_exprt{k, from_integer(levels, int_t)};
  const exprt all_covered = and_exprt{
    not_exprt{empty_old},
    or_exprt{
      binary_relation_exprt{k, ID_lt, zero},
      binary_relation_exprt{remaining, ID_ge, occurrences}}};
  const exprt done =
    or_exprt{equal_exprt{k, from_integer(levels, int_t)}, not_exprt{more}};
  // When the count is neither used up nor covers all occurrences of a
  // non-empty `old`, exactly `remaining` more replacements are made, each
  // changing the length by len(new) - len(old); the characters are
  // unconstrained.
  const exprt unknown = fresh(str_t, std::nullopt);
  pending_checks.push_back(code_assumet{implies_exprt{
    and_exprt{not_exprt{done}, not_exprt{all_covered}, not_exprt{empty_old}},
    equal_exprt{
      len(unknown),
      plus_exprt{
        len(rest), mult_exprt{remaining, minus_exprt{len(new_s), old_len}}}}}});
  if(levels == max_levels)
    log_overapprox(
      "replace() of a native string with a count that may exceed " +
      std::to_string(max_levels) +
      ": beyond that, only the length of the result is known, if the count "
      "does not cover all occurrences of a non-empty `old`");
  exprt out = if_exprt{
    done, rest, if_exprt{all_covered, replace_all(rest, new_s), unknown}};
  for(std::size_t j = levels; j-- > 0;)
  {
    out = fresh(
      str_t,
      exprt{if_exprt{
        or_exprt{equal_exprt{k, from_integer(j, int_t)}, not_exprt{found[j]}},
        x[j],
        concat(head[j], out)}});
  }
  return out;
}

std::optional<exprt> python_convertert::try_string_method(
  const jsont &expr,
  const exprt &obj,
  const typet &obj_base_type,
  const std::string &method_name,
  const jsont &args)
{
  // PLib stdtypes: split/rsplit/partition/rpartition raise ValueError
  // ("empty separator") for an empty separator.
  if(
    (method_name == "split" || method_name == "rsplit" ||
     method_name == "partition" || method_name == "rpartition") &&
    args.is_array() && !as_array(args).empty() &&
    !(is_node_type(*as_array(args).begin(), "Constant") &&
      json_member(*as_array(args).begin(), "value").is_null()))
  {
    const exprt sep = convert_expression(*as_array(args).begin());
    if(is_python_string_type(sep.type()))
    {
      const auto sep_sv = extract_string_value(sep);
      if(sep_sv.has_value())
      {
        if(sep_sv->empty())
          emit_conditional_exception(true_exprt{}, "ValueError");
      }
      else
      {
        const exprt sep_len = native_or_member_string_length(sep);
        emit_conditional_exception(
          equal_exprt{sep_len, from_integer(0, sep_len.type())}, "ValueError");
      }
    }
  }
  // PLib stdtypes: String methods
  if(method_name == "splitlines")
  {
    // PLib stdtypes: str.splitlines(keepends=False).
    // Splits on line boundaries (\n, \r, \r\n, plus \v, \f,
    // \x1c, \x1d, \x1e, \x85, U+2028, U+2029). Empty string
    // produces []; trailing newline does NOT produce an
    // empty trailing element.
    bool keepends = false;
    if(args.is_array() && !as_array(args).empty())
    {
      auto cv = try_eval_double(convert_expression(*as_array(args).begin()));
      if(cv.has_value())
        keepends = cv.value() != 0;
    }
    auto obj_sv = extract_string_value(obj);
    if(obj_sv.has_value())
    {
      const std::string &s = obj_sv.value();
      std::vector<std::string> parts;
      auto is_line_break = [](char c)
      {
        return c == '\n' || c == '\r' || c == '\v' || c == '\f' ||
               c == '\x1c' || c == '\x1d' || c == '\x1e' || c == '\x85';
      };
      std::size_t i = 0, n = s.size();
      while(i < n)
      {
        std::size_t start = i;
        while(i < n && !is_line_break(s[i]))
          ++i;
        std::size_t end_no_sep = i;
        if(i < n)
        {
          // \r\n is a single boundary.
          if(s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
            i += 2;
          else
            ++i;
        }
        std::size_t end = keepends ? i : end_no_sep;
        parts.push_back(s.substr(start, end - start));
      }
      typet list_type = python_list_type(python_string_type());
      const auto &data_type =
        to_array_type(to_struct_type(list_type).components()[1].type());
      exprt::operandst list_elems;
      for(const auto &p : parts)
        list_elems.push_back(
          coerce_element(python_string_literal(p), data_type.element_type()));
      return struct_exprt{
        {from_integer(static_cast<long long>(parts.size()), python_int_type()),
         build_list_data(std::move(list_elems), data_type)},
        list_type};
    }
  }
  if(method_name == "split")
  {
    // PLib stdtypes: str.split() / str.split(None[, maxsplit]) —
    // whitespace mode. Splits on runs of any whitespace,
    // skipping empty tokens entirely. Triggered when no
    // separator is provided OR the separator argument is None.
    bool whitespace_mode = false;
    long long ws_max_split = -1;
    if(!args.is_array() || as_array(args).empty())
    {
      whitespace_mode = true;
    }
    else
    {
      auto wit = as_array(args).begin();
      // Check if first arg is None (Constant with null value).
      if(is_node_type(*wit, "Constant") && json_member(*wit, "value").is_null())
      {
        whitespace_mode = true;
        ++wit;
        if(wit != as_array(args).end())
        {
          auto cv = try_eval_double(convert_expression(*wit));
          if(cv.has_value())
            ws_max_split = static_cast<long long>(cv.value());
        }
      }
    }
    if(whitespace_mode)
    {
      auto obj_sv = extract_string_value(obj);
      if(obj_sv.has_value())
      {
        std::string s = obj_sv.value();
        std::vector<std::string> parts;
        auto is_ws = [](char c)
        {
          return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
                 c == '\f';
        };
        std::size_t i = 0, n = s.size();
        long long splits = 0;
        while(i < n)
        {
          while(i < n && is_ws(s[i]))
            ++i;
          if(i >= n)
            break;
          if(ws_max_split >= 0 && splits >= ws_max_split)
          {
            // Remaining string (with leading whitespace
            // already stripped) is the final token.
            parts.push_back(s.substr(i));
            i = n;
            break;
          }
          std::size_t start = i;
          while(i < n && !is_ws(s[i]))
            ++i;
          parts.push_back(s.substr(start, i - start));
          ++splits;
        }
        typet list_type = python_list_type(python_string_type());
        const auto &data_type =
          to_array_type(to_struct_type(list_type).components()[1].type());
        exprt::operandst list_elems;
        for(const auto &p : parts)
          list_elems.push_back(
            coerce_element(python_string_literal(p), data_type.element_type()));
        return struct_exprt{
          {from_integer(
             static_cast<long long>(parts.size()), python_int_type()),
           build_list_data(std::move(list_elems), data_type)},
          list_type};
      }
    }
    // PLib stdtypes: str.split(sep[, maxsplit]) — constant optimization
    if(args.is_array() && !as_array(args).empty())
    {
      auto ait = as_array(args).begin();
      exprt delim_expr = convert_expression(*ait);
      // PLR: optional 2nd arg is maxsplit (max number of
      // splits performed; default unlimited = -1).
      long long max_split = -1;
      ++ait;
      if(ait != as_array(args).end())
      {
        auto cv = try_eval_double(convert_expression(*ait));
        if(cv.has_value())
          max_split = static_cast<long long>(cv.value());
      }
      auto obj_sv = extract_string_value(obj);
      auto delim_sv = extract_string_value(delim_expr);
      if(obj_sv.has_value() && delim_sv.has_value())
      {
        std::string s = obj_sv.value();
        std::string d = delim_sv.value();
        std::vector<std::string> parts;
        if(!d.empty())
        {
          std::size_t pos = 0;
          long long splits = 0;
          while(pos <= s.size())
          {
            if(max_split >= 0 && splits >= max_split)
            {
              parts.push_back(s.substr(pos));
              break;
            }
            auto found = s.find(d, pos);
            if(found == std::string::npos)
            {
              parts.push_back(s.substr(pos));
              break;
            }
            parts.push_back(s.substr(pos, found - pos));
            pos = found + d.size();
            splits++;
          }
        }
        else
          parts.push_back(s);

        typet str_type = python_string_type();
        typet list_type = python_list_type(python_string_type());
        const auto &data_type =
          to_array_type(to_struct_type(list_type).components()[1].type());
        exprt::operandst list_elems;
        for(const auto &p : parts)
          list_elems.push_back(
            coerce_element(python_string_literal(p), data_type.element_type()));
        return struct_exprt{
          {from_integer(
             static_cast<long long>(parts.size()), python_int_type()),
           build_list_data(std::move(list_elems), data_type)},
          list_type};
      }
    }
    // Fallback: original struct-literal handler
    if(obj.id() == ID_struct && args.is_array() && !as_array(args).empty())
    {
      exprt delim_expr = convert_expression(*as_array(args).begin());
      // Extract constant string bytes from obj
      if(
        obj.operands().size() == 2 && obj.operands()[0].is_constant() &&
        delim_expr.id() == ID_struct && delim_expr.operands().size() == 2 &&
        delim_expr.operands()[0].is_constant())
      {
        mp_integer slen, dlen;
        if(
          !to_integer(to_constant_expr(obj.operands()[0]), slen) &&
          !to_integer(to_constant_expr(delim_expr.operands()[0]), dlen) &&
          dlen == 1 && slen <= PYTHON_MAX_STRING_LENGTH)
        {
          // Get delimiter byte
          const auto &delim_data = delim_expr.operands()[1];
          mp_integer delim_byte;
          if(
            delim_data.operands().size() > 0 &&
            delim_data.operands()[0].is_constant() &&
            !to_integer(to_constant_expr(delim_data.operands()[0]), delim_byte))
          {
            // Scan string for delimiter, build parts
            const auto &str_data = obj.operands()[1];
            std::vector<std::string> parts;
            std::string current;
            for(mp_integer i = 0; i < slen; ++i)
            {
              std::size_t idx = i.to_ulong();
              if(
                idx < str_data.operands().size() &&
                str_data.operands()[idx].is_constant())
              {
                mp_integer ch;
                if(!to_integer(to_constant_expr(str_data.operands()[idx]), ch))
                {
                  if(ch == delim_byte)
                  {
                    parts.push_back(current);
                    current.clear();
                  }
                  else
                    current += static_cast<char>(ch.to_ulong());
                }
              }
            }
            parts.push_back(current);

            // Build list of string structs
            typet str_type = python_string_type();
            const auto &data_type = array_typet(
              unsignedbv_typet{8},
              from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
            typet list_type = python_list_type(python_string_type());
            const auto &list_data_type =
              to_array_type(to_struct_type(list_type).components()[1].type());

            exprt::operandst list_elems;
            for(const auto &part : parts)
            {
              list_elems.push_back(coerce_element(
                python_string_literal(part), data_type.element_type()));
            }
            exprt len_expr = from_integer(
              static_cast<long long>(parts.size()), python_int_type());
            return struct_exprt{
              {len_expr,
               build_list_data(std::move(list_elems), list_data_type)},
              list_type};
          }
        }
      }
    }
    // Fallback (symbolic / non-constant subject): a nondet list of strings,
    // with its length constrained to [0, PYTHON_MAX_LIST_LENGTH]. Without the
    // bound the nondet length field (a signed int) could be negative, which
    // would be a false alarm on the always-true len(split(...)) >= 0.
    {
      const typet list_type = python_list_type(python_string_type());
      static unsigned split_ctr = 0;
      const std::string nm = "__split_nondet_" + std::to_string(split_ctr++);
      const irep_idt id{qualify_name(nm)};
      if(symbol_table.lookup(id) == nullptr)
      {
        symbolt sy{id, list_type, "python"};
        sy.base_name = nm;
        sy.is_lvalue = true;
        sy.is_state_var = true;
        symbol_table.add(sy);
      }
      const symbol_exprt sym = symbol_table.lookup_ref(id).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        sym, side_effect_expr_nondett{list_type, get_location(expr)}});
      const member_exprt len{sym, "length", signedbv_typet{64}};
      // Sound bounds on the (otherwise unconstrained) segment count:
      //  * with an explicit separator, split always returns >= 1 element
      //    (even "".split(",") == ['']); whitespace mode may return [] (count
      //    0, e.g. "".split()), so only the explicit-separator case gets the
      //    >= 1 lower bound.
      //  * a separator has length >= 1 (split("") raises ValueError), so there
      //    can be at most len(s) + 1 segments; this prunes the spurious
      //    over-count that made `len(s.split(d)) <= len(s) + 1` a false alarm.
      //  * the list capacity bound (PYTHON_MAX_LIST_LENGTH) is retained.
      exprt::operandst bounds;
      bounds.push_back(binary_relation_exprt{
        len, ID_ge, from_integer(whitespace_mode ? 0 : 1, len.type())});
      bounds.push_back(binary_relation_exprt{
        len, ID_le, from_integer(PYTHON_MAX_LIST_LENGTH, len.type())});
      if(obj.type().id() == ID_string)
        bounds.push_back(binary_relation_exprt{
          len,
          ID_le,
          [&]
          {
            // Metadata arithmetic in the length expression's OWN type
            // (i64): a python_int_type() literal diverges to integer
            // under --python-unbounded-ints and emits a bare Int inside
            // a bvadd (cvc5 parse error).
            exprt sl = native_or_member_string_length(obj);
            exprt one = from_integer(1, sl.type());
            return plus_exprt{std::move(sl), std::move(one)};
          }()});
      pending_checks.push_back(code_assumet{conjunction(bounds)});
      return std::move(sym);
    }
  }
  // Native SMT-String back-end: ASCII case mapping for
  // upper/lower/casefold/swapcase and capitalize/title. SMT-LIB String has no
  // case operation, so the result is built per character via str.to_code /
  // ASCII arithmetic / str.from_code, concatenated into the result string.
  // The naive `str.++` concat alone makes the content exact but makes a
  // `len()` over the result expensive: the solver must reconstruct the
  // concatenation's length position by position, which times out on an
  // unconstrained symbolic string. We therefore define a fresh result symbol r
  // by the concat (see below for inputs longer than the concat covers) and
  // `str.len(r) == str.len(obj)`; the length equality lets `len(result)`
  // queries short-circuit via congruence on `len(r)` without folding the
  // 64-deep concat. The mapping is exact for ASCII (A-Z <-> a-z); see
  // case_mapped below for other characters.
  //
  // mapped_char(i) returns the transformed smt_string for position i of obj
  // (str.from_code(-1) == "" for i >= len(obj)).
  // The mapped character at position i of obj: `ascii` (a code point) for
  // an ASCII `code`, and otherwise the value of an uninterpreted function of
  // obj and the position. Beyond ASCII, case maps can change the length
  // ('ß'.upper() == 'SS') and depend on the context (a final 'Σ' lowers to
  // 'ς'), so the result is a string and depends on the whole of obj.
  [[maybe_unused]] auto case_mapped =
    [&](const exprt &code, const exprt &ascii, std::size_t i) -> exprt
  {
    const exprt ascii_char = native_string_app(
      ID_cprover_string_from_code_func,
      {integer_typet{}},
      {typecast_exprt::conditional_cast(ascii, integer_typet{})},
      string_typet{});
    const exprt other = native_string_app(
      irep_idt{"python::__case_map_" + method_name},
      {string_typet{}, integer_typet{}},
      {obj, from_integer(i, integer_typet{})},
      string_typet{});
    return if_exprt{
      binary_relation_exprt{code, ID_lt, from_integer(0x80, code.type())},
      ascii_char,
      other};
  };
  [[maybe_unused]] auto build_length_preserving =
    [&](const std::function<exprt(std::size_t)> &mapped_char) -> exprt
  {
    exprt concat = constant_exprt{irep_idt{""}, string_typet{}};
    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
      concat = string_concat(concat, mapped_char(i));
    log_overapprox(
      method_name + "() of a native string: characters after the first " +
      std::to_string(PYTHON_MAX_STRING_LENGTH) + " are unconstrained");

    static unsigned case_ctr = 0;
    const std::string nm = "__case_" + std::to_string(case_ctr++);
    const irep_idt id{qualify_name(nm)};
    if(symbol_table.lookup(id) == nullptr)
    {
      symbolt sy{id, string_typet{}, "python"};
      sy.base_name = nm;
      sy.is_lvalue = true;
      sy.is_state_var = true;
      symbol_table.add(sy);
    }
    const symbol_exprt r = symbol_table.lookup_ref(id).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      r, side_effect_expr_nondett{string_typet{}, get_location(expr)}});
    // r is exactly the concatenated characters when obj has at most
    // PYTHON_MAX_STRING_LENGTH of them ...
    const exprt obj_len = native_or_member_string_length(obj);
    const exprt fits = binary_relation_exprt{
      obj_len, ID_le, from_integer(PYTHON_MAX_STRING_LENGTH, obj_len.type())};
    pending_checks.push_back(
      code_assumet{implies_exprt{fits, equal_exprt{r, concat}}});
    // ... and otherwise starts with them: a longer obj (a concatenation of
    // bounded strings, say) gets an r whose remaining characters are
    // unconstrained, an over-approximation. Asserting r == concat
    // unconditionally would rule out every execution with a longer obj.
    pending_checks.push_back(code_assumet{or_exprt{
      fits,
      native_string_app(
        ID_cprover_string_startswith_func,
        {string_typet{}, string_typet{}},
        {r, concat},
        bool_typet{})}});
    // In both cases r has obj's length if obj is ASCII, which also lets len()
    // queries over r short-circuit. (Beyond ASCII, case maps can change the
    // length.)
    const typet regex_type{ID_regex};
    const exprt ascii = native_string_app(
      ID_cprover_string_in_regex_func,
      {string_typet{}, regex_type},
      {obj,
       native_string_app(
         ID_cprover_regex_star_func,
         {regex_type},
         {native_string_app(
           ID_cprover_regex_range_func,
           {string_typet{}, string_typet{}},
           {constant_exprt{std::string(1, '\0'), string_typet{}},
            constant_exprt{std::string(1, '\x7f'), string_typet{}}},
           regex_type)},
         regex_type)},
      bool_typet{});
    pending_checks.push_back(code_assumet{implies_exprt{
      ascii, equal_exprt{native_or_member_string_length(r), obj_len}}});
    return std::move(r);
  };

  if(
    obj.type().id() == ID_string &&
    (method_name == "upper" || method_name == "lower" ||
     method_name == "casefold" || method_name == "swapcase"))
  {
    // upper / swapcase lower a-z to A-Z (offset -32); lower / casefold /
    // swapcase raise A-Z to a-z (offset +32). swapcase does both (the ranges
    // are disjoint, so the order of the two conditionals is irrelevant).
    const bool to_upper = (method_name == "upper" || method_name == "swapcase");
    const bool to_lower =
      (method_name == "lower" || method_name == "casefold" ||
       method_name == "swapcase");
    const integer_typet code_t{};
    const signedbv_typet i64{64};
    return build_length_preserving(
      [&](std::size_t i) -> exprt
      {
        exprt ch =
          string_substr(obj, from_integer(i, i64), from_integer(1, i64));
        exprt code = exprt{typecast_exprt{
          native_string_app(
            ID_cprover_string_to_code_func,
            {string_typet{}},
            {ch},
            integer_typet{}),
          code_t}};
        exprt mapped = code;
        if(to_upper)
          mapped = if_exprt{
            and_exprt{
              binary_relation_exprt{code, ID_ge, from_integer('a', code_t)},
              binary_relation_exprt{code, ID_le, from_integer('z', code_t)}},
            minus_exprt{code, from_integer(32, code_t)},
            mapped};
        if(to_lower)
          mapped = if_exprt{
            and_exprt{
              binary_relation_exprt{code, ID_ge, from_integer('A', code_t)},
              binary_relation_exprt{code, ID_le, from_integer('Z', code_t)}},
            plus_exprt{code, from_integer(32, code_t)},
            mapped};
        return case_mapped(code, mapped, i);
      });
  }

  // Native SMT-String back-end: capitalize / title. Like the case maps above
  // but the direction is position-dependent: capitalize upper-cases index 0
  // and lower-cases the rest; title upper-cases the first letter of each word
  // (a position whose predecessor is not an ASCII letter) and lower-cases the
  // rest. ASCII only; non-letters pass through.
  if(
    obj.type().id() == ID_string &&
    (method_name == "capitalize" || method_name == "title"))
  {
    const bool is_title = (method_name == "title");
    const integer_typet code_t{};
    const signedbv_typet i64{64};
    auto in_range = [&](const exprt &c, int lo, int hi) -> exprt
    {
      return and_exprt{
        binary_relation_exprt{c, ID_ge, from_integer(lo, code_t)},
        binary_relation_exprt{c, ID_le, from_integer(hi, code_t)}};
    };
    auto upper_map = [&](const exprt &c) -> exprt
    {
      return if_exprt{
        in_range(c, 'a', 'z'), minus_exprt{c, from_integer(32, code_t)}, c};
    };
    auto lower_map = [&](const exprt &c) -> exprt
    {
      return if_exprt{
        in_range(c, 'A', 'Z'), plus_exprt{c, from_integer(32, code_t)}, c};
    };
    return build_length_preserving(
      [&](std::size_t i) -> exprt
      {
        exprt ch =
          string_substr(obj, from_integer(i, i64), from_integer(1, i64));
        exprt code = exprt{typecast_exprt{
          native_string_app(
            ID_cprover_string_to_code_func,
            {string_typet{}},
            {ch},
            integer_typet{}),
          code_t}};
        exprt mapped;
        if(i == 0)
          mapped = upper_map(code); // start of string is always a word start
        else if(!is_title)
          mapped = lower_map(code); // capitalize: everything after index 0
        else
        {
          // title: word start iff the previous character is not an ASCII letter.
          exprt prev =
            string_substr(obj, from_integer(i - 1, i64), from_integer(1, i64));
          exprt prev_code = exprt{typecast_exprt{
            native_string_app(
              ID_cprover_string_to_code_func,
              {string_typet{}},
              {prev},
              integer_typet{}),
            code_t}};
          // A character beyond ASCII may be cased or not.
          exprt prev_is_alpha = if_exprt{
            binary_relation_exprt{
              prev_code, ID_lt, from_integer(0x80, prev_code.type())},
            or_exprt{
              in_range(prev_code, 'a', 'z'), in_range(prev_code, 'A', 'Z')},
            native_string_app(
              irep_idt{"python::__case_is_cased"},
              {integer_typet{}},
              {typecast_exprt::conditional_cast(prev_code, integer_typet{})},
              bool_typet{})};
          // (Whether a character is cased depends on it alone.)
          mapped = if_exprt{
            not_exprt{prev_is_alpha}, upper_map(code), lower_map(code)};
        }
        return case_mapped(code, mapped, i);
      });
  }

  // PLib stdtypes: upper/lower — exact byte transformation
  if(method_name == "upper" || method_name == "lower")
  {
    auto sv = extract_string_value(obj);
    if(!sv.has_value() && (obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      auto it = string_constants.find(
        obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                              : irep_idt{});
      if(it != string_constants.end())
        sv = it->second;
    }
    // Characters beyond ASCII are not caseless ('é'.upper() == 'É').
    if(sv.has_value() && is_ascii_string(*sv))
    {
      std::string result = sv.value();
      for(auto &c : result)
        c = (method_name == "upper") ? toupper(c) : tolower(c);
      return python_string_literal(result);
    }
    // 1-char struct fast-path: when obj is a single-char
    // struct {1, address_of(arr[0])} (e.g. from the symbolic-
    // string genexp unroll where each loop iteration binds
    // c to such a struct), transform the byte directly via
    // an if_exprt instead of routing through the refined-
    // string solver. This unblocks
    // 'all(c.lower() == ... for c in s)' patterns
    // (e.g. github_3036) that would otherwise emit one
    // cprover_string_to_lower_case_func call per iteration —
    // those cumulate into the SAT-loop crash territory.
    if(
      obj.id() == ID_struct && obj.operands().size() == 2 &&
      obj.operands()[0].is_constant())
    {
      mp_integer slen;
      if(!to_integer(to_constant_expr(obj.operands()[0]), slen) && slen == 1)
      {
        const exprt &data_op = obj.operands()[1];
        const exprt *byte = nullptr;
        if(
          data_op.id() == ID_address_of && data_op.operands().size() == 1 &&
          data_op.operands()[0].id() == ID_index &&
          data_op.operands()[0].operands().size() == 2 &&
          data_op.operands()[0].operands()[0].id() == ID_array &&
          data_op.operands()[0].operands()[0].operands().size() == 1)
        {
          byte = &data_op.operands()[0].operands()[0].operands()[0];
        }
        if(byte != nullptr && byte->type().id() == ID_unsignedbv)
        {
          // ASCII-only byte transform. Non-ASCII (>= 0x80)
          // passes through unchanged — Unicode case folding
          // would need cprover_string_to_*_case_func.
          exprt new_byte;
          if(method_name == "lower")
          {
            // 'A' (0x41) <= b <= 'Z' (0x5A)  ->  b + 32
            exprt is_upper = and_exprt{
              binary_relation_exprt{
                *byte, ID_ge, from_integer('A', byte->type())},
              binary_relation_exprt{
                *byte, ID_le, from_integer('Z', byte->type())}};
            new_byte = if_exprt{
              is_upper,
              plus_exprt{*byte, from_integer(32, byte->type())},
              *byte};
          }
          else
          {
            // 'a' (0x61) <= b <= 'z' (0x7A)  ->  b - 32
            exprt is_lower = and_exprt{
              binary_relation_exprt{
                *byte, ID_ge, from_integer('a', byte->type())},
              binary_relation_exprt{
                *byte, ID_le, from_integer('z', byte->type())}};
            new_byte = if_exprt{
              is_lower,
              minus_exprt{*byte, from_integer(32, byte->type())},
              *byte};
          }
          exprt::operandst chars;
          chars.push_back(new_byte);
          array_typet at(
            unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
          array_exprt new_arr(std::move(chars), at);
          exprt ptr = address_of_exprt(index_exprt(
            new_arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
          exprt len_one = from_integer(1, signedbv_typet{64});
          return struct_exprt({len_one, ptr}, python_string_type());
        }
      }
    }
    // Use string solver for non-constant upper/lower
    {
      // Decompose obj into struct for the solver
      exprt src =
        (obj.id() == ID_struct && obj.operands().size() == 2)
          ? obj
          : exprt(struct_exprt(
              {member_exprt(obj, "length", signedbv_typet{64}),
               member_exprt(
                 obj, "data", pointer_typet(unsignedbv_typet{8}, 64))},
              obj.type()));
      return emit_string_function(
        method_name == "upper" ? ID_cprover_string_to_upper_case_func
                               : ID_cprover_string_to_lower_case_func,
        {src},
        symbol_table,
        pending_checks,
        loop_depth > 0,
        // global (documented): see emit_string_function scope invariant.
        std::string{});
    }
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt src_data{obj, "data", data_type};
    member_exprt src_len{obj, "length", signedbv_typet{64}};

    static unsigned case_ctr = 0;
    std::string tn = "__case_" + std::to_string(case_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, python_string_type(), "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}}, src_len});
    member_exprt dst_data{tmp, "data", data_type};

    exprt lo =
      from_integer(method_name == "upper" ? 'a' : 'A', unsignedbv_typet{8});
    exprt hi =
      from_integer(method_name == "upper" ? 'z' : 'Z', unsignedbv_typet{8});
    exprt offset =
      from_integer(method_name == "upper" ? -32 : 32, signedbv_typet{8});

    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt ch = index_exprt{src_data, idx};
      exprt in_range = and_exprt{
        binary_relation_exprt{ch, ID_ge, lo},
        binary_relation_exprt{ch, ID_le, hi}};
      exprt converted =
        plus_exprt{ch, typecast_exprt{offset, unsignedbv_typet{8}}};
      pending_checks.push_back(code_frontend_assignt{
        index_exprt{dst_data, idx}, if_exprt{in_range, converted, ch}});
    }
    return std::move(tmp);
  }
  if(
    method_name == "strip" || method_name == "lstrip" ||
    method_name == "rstrip" || method_name == "title" ||
    method_name == "capitalize" || method_name == "swapcase" ||
    method_name == "zfill" || method_name == "casefold" ||
    method_name == "center" || method_name == "ljust" ||
    method_name == "rjust" || method_name == "expandtabs" ||
    method_name == "zfill" || method_name == "encode" ||
    method_name == "decode" || method_name == "removeprefix" ||
    method_name == "removesuffix")
  {
    // PLR: str.encode(enc) / bytes.decode(enc) with an unknown CONSTANT codec
    // name raises LookupError ('unknown encoding'). Validate a constant codec
    // argument against the standard-encodings set (normalised: lowercased,
    // '_'/' ' -> '-'); a symbolic codec is never flagged. Generous set to avoid
    // false-positives on valid-but-uncommon codecs.
    if(
      (method_name == "encode" || method_name == "decode") && args.is_array() &&
      !as_array(args).empty())
    {
      if(
        auto codec =
          extract_string_value(convert_expression(*as_array(args).begin())))
      {
        std::string nm;
        for(char ch : *codec)
        {
          ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          if(ch == '_' || ch == ' ')
            ch = '-';
          nm += ch;
        }
        static const std::set<std::string> known = {
          "utf-8",
          "utf8",
          "u8",
          "utf",
          "cp65001",
          "ascii",
          "us-ascii",
          "646",
          "latin-1",
          "latin1",
          "latin",
          "iso-8859-1",
          "iso8859-1",
          "8859",
          "cp819",
          "l1",
          "utf-16",
          "utf16",
          "u16",
          "utf-16-le",
          "utf-16-be",
          "utf-32",
          "utf32",
          "u32",
          "utf-32-le",
          "utf-32-be",
          "utf-7",
          "u7",
          "cp1252",
          "windows-1252",
          "cp437",
          "cp850",
          "cp1251",
          "windows-1251",
          "mac-roman",
          "macroman",
          "koi8-r",
          "koi8-u",
          "big5",
          "gbk",
          "gb2312",
          "gb18030",
          "shift-jis",
          "sjis",
          "euc-jp",
          "euc-kr",
          "iso-8859-2",
          "iso-8859-15",
          "cp1250",
          "hex",
          "base64",
          "rot-13",
          "rot13",
          "zlib",
          "bz2",
          "idna",
          "punycode",
          "unicode-escape",
          "raw-unicode-escape",
          "string-escape"};
        if(known.find(nm) == known.end())
        {
          emit_conditional_exception(true_exprt{}, "LookupError");
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
      }
    }
    // Try exact computation for constant strings
    auto str_val = extract_string_value(obj);
    // The folds below treat characters beyond ASCII as caseless and not
    // whitespace, which is wrong for most of them ('é'.upper() == 'É',
    // '\xa0'.strip() == '').
    if(
      str_val.has_value() && !is_ascii_string(*str_val) &&
      (method_name == "strip" || method_name == "lstrip" ||
       method_name == "rstrip" || method_name == "capitalize" ||
       method_name == "title" || method_name == "swapcase" ||
       method_name == "casefold" || method_name == "center" ||
       method_name == "ljust" || method_name == "rjust" ||
       method_name == "zfill" || method_name == "expandtabs"))
    {
      // (and widths count characters, not bytes)
      str_val.reset();
    }
    if(str_val.has_value())
    {
      std::string s = str_val.value();
      std::string result;
      if(
        method_name == "strip" || method_name == "lstrip" ||
        method_name == "rstrip")
      {
        std::string chars_to_strip = " \t\n\r\f\v\x1c\x1d\x1e\x1f";
        if(
          args.is_array() && !as_array(args).empty() &&
          !(is_node_type(*as_array(args).begin(), "Constant") &&
            json_member(*as_array(args).begin(), "value").is_null()))
        {
          auto cv =
            extract_string_value(convert_expression(*as_array(args).begin()));
          // Characters that are not known (or not ASCII, which the byte-wise
          // stripping below would split): a nondet result.
          if(!cv.has_value() || !is_ascii_string(*cv))
          {
            log_overapprox(
              method_name +
              "() of a constant string with characters that "
              "are not known constants: returning nondet");
            return bounded_nondet_string(get_location(expr));
          }
          chars_to_strip = cv.value();
        }
        if(method_name == "strip")
        {
          size_t start = s.find_first_not_of(chars_to_strip);
          size_t end = s.find_last_not_of(chars_to_strip);
          result = (start == std::string::npos)
                     ? ""
                     : s.substr(start, end - start + 1);
        }
        else if(method_name == "lstrip")
        {
          size_t start = s.find_first_not_of(chars_to_strip);
          result = (start == std::string::npos) ? "" : s.substr(start);
        }
        else // rstrip
        {
          size_t end = s.find_last_not_of(chars_to_strip);
          result = (end == std::string::npos) ? "" : s.substr(0, end + 1);
        }
      }
      else if(method_name == "capitalize")
      {
        result = s;
        if(!result.empty())
        {
          result[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(result[0])));
          for(size_t i = 1; i < result.size(); i++)
            result[i] = static_cast<char>(
              std::tolower(static_cast<unsigned char>(result[i])));
        }
      }
      else if(method_name == "title")
      {
        result = s;
        bool next_upper = true;
        for(size_t i = 0; i < result.size(); i++)
        {
          if(std::isalpha(static_cast<unsigned char>(result[i])))
          {
            result[i] = next_upper ? static_cast<char>(std::toupper(
                                       static_cast<unsigned char>(result[i])))
                                   : static_cast<char>(std::tolower(
                                       static_cast<unsigned char>(result[i])));
            next_upper = false;
          }
          else
            next_upper = true;
        }
      }
      else if(method_name == "swapcase")
      {
        result = s;
        for(auto &c : result)
        {
          if(std::isupper(static_cast<unsigned char>(c)))
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          else if(std::islower(static_cast<unsigned char>(c)))
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
      }
      else if(method_name == "casefold")
      {
        result = s;
        for(auto &c : result)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      else if(
        method_name == "center" || method_name == "ljust" ||
        method_name == "rjust")
      {
        int width = 0;
        char fill = ' ';
        if(args.is_array() && !as_array(args).empty())
        {
          auto ait = as_array(args).begin();
          auto wv = try_eval_double(convert_expression(*ait));
          if(!wv.has_value())
            return bounded_nondet_string(get_location(expr));
          width = static_cast<int>(wv.value());
          ++ait;
          if(ait != as_array(args).end())
          {
            // A fill character that is not a known ASCII constant: the
            // result is left nondet (a non-ASCII one has several bytes).
            auto fv = extract_string_value(convert_expression(*ait));
            if(!fv.has_value() || fv->size() != 1 || !is_ascii_string(*fv))
              return bounded_nondet_string(get_location(expr));
            fill = fv.value()[0];
          }
        }
        if(static_cast<int>(s.size()) >= width)
          result = s;
        else
        {
          int pad = width - static_cast<int>(s.size());
          if(method_name == "center")
          {
            // CPython str.center: the EXTRA pad goes LEFT when both the
            // margin and the width are odd (marg & width & 1), not right
            // (ESBMC str_center_odd_padding_fail: "ab".center(7,"-") is
            // "---ab--", 3 left / 2 right).
            int left_pad = pad / 2 + (pad & width & 1);
            int right_pad = pad - left_pad;
            result =
              std::string(left_pad, fill) + s + std::string(right_pad, fill);
          }
          else if(method_name == "ljust")
            result = s + std::string(pad, fill);
          else
            result = std::string(pad, fill) + s;
        }
      }
      else if(method_name == "expandtabs")
      {
        int tabsize = 8;
        if(args.is_array() && !as_array(args).empty())
        {
          auto tv =
            try_eval_double(convert_expression(*as_array(args).begin()));
          if(!tv.has_value())
            return bounded_nondet_string(get_location(expr));
          tabsize = static_cast<int>(tv.value());
        }
        result.clear();
        int col = 0;
        for(char c : s)
        {
          if(c == '\t')
          {
            int spaces = tabsize - (col % tabsize);
            result += std::string(spaces, ' ');
            col += spaces;
          }
          else if(c == '\n' || c == '\r')
          {
            result += c;
            col = 0;
          }
          else
          {
            result += c;
            col++;
          }
        }
      }
      else if(method_name == "removeprefix")
      {
        std::string prefix;
        if(args.is_array() && !as_array(args).empty())
        {
          auto pv =
            extract_string_value(convert_expression(*as_array(args).begin()));
          if(!pv.has_value())
            return bounded_nondet_string(get_location(expr));
          prefix = pv.value();
        }
        if(!prefix.empty() && s.find(prefix) == 0)
          result = s.substr(prefix.size());
        else
          result = s;
      }
      else if(method_name == "removesuffix")
      {
        std::string suffix;
        if(args.is_array() && !as_array(args).empty())
        {
          auto sv2 =
            extract_string_value(convert_expression(*as_array(args).begin()));
          if(!sv2.has_value())
            return bounded_nondet_string(get_location(expr));
          suffix = sv2.value();
        }
        if(
          !suffix.empty() && s.size() >= suffix.size() &&
          s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
          result = s.substr(0, s.size() - suffix.size());
        else
          result = s;
      }
      else if(method_name == "zfill")
      {
        int width = 0;
        if(args.is_array() && !as_array(args).empty())
        {
          auto wv =
            try_eval_double(convert_expression(*as_array(args).begin()));
          if(!wv.has_value())
            return bounded_nondet_string(get_location(expr));
          width = static_cast<int>(wv.value());
        }
        if(static_cast<int>(s.size()) >= width)
          result = s;
        else
        {
          int pad = width - static_cast<int>(s.size());
          if(!s.empty() && (s[0] == '+' || s[0] == '-'))
            result = s[0] + std::string(pad, '0') + s.substr(1);
          else
            result = std::string(pad, '0') + s;
        }
      }
      else
        result = s; // fallback for unhandled methods
      if(result.size() <= PYTHON_MAX_STRING_LENGTH)
        return python_string_literal(result);
    }
    // Native SMT-String back-end: strip/lstrip/rstrip with an explicit
    // (compile-time-constant) `chars` set. Same decomposition as the
    // whitespace form below, but membership is "the character is in `chars`",
    // expressed as a regex character class [chars] reused through the existing
    // match/fullmatch intrinsics (so it inherits the hardened translator):
    //   s = p ++ r ++ q, with p,q in [chars]* and r's boundary characters NOT
    // in [chars] (maximality), which uniquely determines r = the stripped
    // string. A symbolic / non-constant `chars` falls through to the sound
    // nondet path below.
    if(
      use_smt_string_native &&
      (method_name == "strip" || method_name == "lstrip" ||
       method_name == "rstrip") &&
      args.is_array() && !as_array(args).empty())
    {
      auto cv =
        extract_string_value(convert_expression(*as_array(args).begin()));
      if(cv.has_value())
      {
        const std::string &chars = cv.value();
        if(chars.empty())
          return obj; // strip("") strips nothing
        // Build the [chars] character class, escaping the class
        // metacharacters so each becomes a literal member.
        std::string cls = "[";
        for(char c : chars)
        {
          if(c == ']' || c == '\\' || c == '^' || c == '-')
            cls += '\\';
          cls += c;
        }
        cls += "]";
        const bool strip_l = method_name != "rstrip";
        const bool strip_r = method_name != "lstrip";
        auto mk = [&]() -> symbol_exprt
        {
          std::string nm =
            "__smt_stripc_" + std::to_string(symbol_table.symbols.size());
          irep_idt id{qualify_name(nm)};
          symbolt sy{id, string_typet{}, "python"};
          sy.base_name = nm;
          sy.is_lvalue = true;
          sy.is_state_var = true;
          symbol_table.add(sy);
          return symbol_table.lookup_ref(id).symbol_expr();
        };
        auto pat = [](const std::string &p) -> exprt {
          return constant_exprt{irep_idt{p}, string_typet{}};
        };
        // all_in(x): x is entirely composed of `chars` (fullmatch [chars]*).
        auto all_in = [&](const exprt &x) -> exprt
        {
          return emit_string_bool_function(
            ID_cprover_string_fullmatch_func,
            pat(cls + "*"),
            x,
            symbol_table,
            pending_checks);
        };
        // starts_in(x): x starts with a `chars` member (match [chars]).
        auto starts_in = [&](const exprt &x) -> exprt
        {
          return emit_string_bool_function(
            ID_cprover_string_match_func,
            pat(cls),
            x,
            symbol_table,
            pending_checks);
        };
        // ends_in(x): x ends with a `chars` member (fullmatch (?s).*[chars];
        // (?s) so '.' spans newlines).
        auto ends_in = [&](const exprt &x) -> exprt
        {
          return emit_string_bool_function(
            ID_cprover_string_fullmatch_func,
            pat("(?s).*" + cls),
            x,
            symbol_table,
            pending_checks);
        };
        symbol_exprt r = mk();
        exprt recon = r;
        if(strip_l)
        {
          symbol_exprt p = mk();
          recon = string_concat(p, recon);
          pending_checks.push_back(code_assumet{all_in(p)});
          pending_checks.push_back(code_assumet{not_exprt{starts_in(r)}});
        }
        if(strip_r)
        {
          symbol_exprt q = mk();
          recon = string_concat(recon, q);
          pending_checks.push_back(code_assumet{all_in(q)});
          pending_checks.push_back(code_assumet{not_exprt{ends_in(r)}});
        }
        pending_checks.push_back(code_assumet{equal_exprt{obj, recon}});
        return std::move(r);
      }
    }
    // Native SMT-String back-end (Plan A): strip/lstrip/rstrip (whitespace,
    // no `chars` arg) encoded with SMT-LIB regex. Introduce the result r and
    // whitespace prefix p / suffix q with s = p ++ r ++ q, p,q in (re.* WS),
    // and maximality (r empty, or its boundary chars are non-whitespace) so
    // p/q capture ALL leading/trailing whitespace. This uniquely determines
    // r = the stripped string. WS is the Python whitespace set.
    if(
      use_smt_string_native &&
      (method_name == "strip" || method_name == "lstrip" ||
       method_name == "rstrip") &&
      (!args.is_array() || as_array(args).empty()))
    {
      const bool strip_l = method_name != "rstrip";
      const bool strip_r = method_name != "lstrip";
      const typet i64 = signedbv_typet{64};
      const typet bt = c_bool_typet{8};
      auto mk = [&]() -> symbol_exprt
      {
        std::string nm =
          "__smt_strip_" + std::to_string(symbol_table.symbols.size());
        irep_idt id{qualify_name(nm)};
        symbolt s{id, string_typet{}, "python"};
        s.base_name = nm;
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
        return symbol_table.lookup_ref(id).symbol_expr();
      };
      auto reg = [&](
                   const irep_idt &fn,
                   std::vector<typet> ats,
                   const typet &ret) -> symbol_exprt
      {
        if(symbol_table.lookup(fn) == nullptr)
        {
          symbolt fs{
            fn, mathematical_function_typet(std::move(ats), ret), "python"};
          fs.base_name = id2string(fn);
          symbol_table.add(fs);
        }
        return symbol_table.lookup_ref(fn).symbol_expr();
      };
      auto strcat = [&](const exprt &a, const exprt &b) -> exprt
      {
        function_application_exprt app{
          reg(
            ID_cprover_string_concat_func,
            {a.type(), b.type()},
            string_typet{}),
          {a, b}};
        app.type() = string_typet{};
        return std::move(app);
      };
      // re_ws(x, mode): mode 0 = x all whitespace; 1 = starts with ws;
      // 2 = ends with ws. Composed from the GENERIC regex vocabulary the
      // shared encoder lowers (str.to_re / re.union / re.* / re.++ /
      // re.allchar / str.in_re) -- no Python-specific back-end op. WS is the
      // ASCII-range whitespace set {\t \n \v \f \r \x1c \x1d \x1e \x1f space}
      // (CPython str.isspace()/strip(); 0x1c-0x1f are FS GS RS US), which
      // deliberately KEEPS other control bytes (unlike Java trim).
      const typet regex_type{ID_regex};
      auto re_app = [&](
                      const irep_idt &fn,
                      std::vector<typet> ats,
                      exprt::operandst as,
                      const typet &ret) -> exprt
      {
        function_application_exprt app{
          reg(fn, std::move(ats), ret), std::move(as)};
        app.type() = ret;
        return std::move(app);
      };
      auto ws_regex = [&]() -> exprt
      {
        // Two contiguous ranges: 0x09..0x0d and 0x1c..0x20 (see the refined
        // predicate in add_axioms_for_python_strip).
        auto range = [&](char lo, char hi) -> exprt
        {
          return re_app(
            ID_cprover_regex_range_func,
            {string_typet{}, string_typet{}},
            {constant_exprt{std::string(1, lo), string_typet{}},
             constant_exprt{std::string(1, hi), string_typet{}}},
            regex_type);
        };
        return re_app(
          ID_cprover_regex_union_func,
          {regex_type, regex_type},
          {range('\x09', '\x0d'), range('\x1c', '\x20')},
          regex_type);
      };
      auto re_ws = [&](const exprt &x, int mode) -> exprt
      {
        const exprt ws = ws_regex();
        const exprt any_star = re_app(
          ID_cprover_regex_star_func,
          {regex_type},
          {re_app(ID_cprover_regex_allchar_func, {}, {}, regex_type)},
          regex_type);
        exprt re;
        if(mode == 1)
          re = re_app(
            ID_cprover_regex_concat_func,
            {regex_type, regex_type},
            {ws, any_star},
            regex_type);
        else if(mode == 2)
          re = re_app(
            ID_cprover_regex_concat_func,
            {regex_type, regex_type},
            {any_star, ws},
            regex_type);
        else
          re =
            re_app(ID_cprover_regex_star_func, {regex_type}, {ws}, regex_type);
        return re_app(
          ID_cprover_string_in_regex_func,
          {x.type(), regex_type},
          {x, re},
          bool_typet{});
      };
      symbol_exprt r = mk();
      exprt recon = r;
      if(strip_l)
      {
        symbol_exprt p = mk();
        recon = strcat(p, recon);
        pending_checks.push_back(code_assumet{re_ws(p, 0)});
        // Maximality: r does not start with whitespace (so p captured ALL
        // leading whitespace). Vacuously holds for empty r.
        pending_checks.push_back(code_assumet{not_exprt{re_ws(r, 1)}});
      }
      if(strip_r)
      {
        symbol_exprt q = mk();
        recon = strcat(recon, q);
        pending_checks.push_back(code_assumet{re_ws(q, 0)});
        // Maximality: r does not end with whitespace.
        pending_checks.push_back(code_assumet{not_exprt{re_ws(r, 2)}});
      }
      pending_checks.push_back(code_assumet{equal_exprt{obj, recon}});
      return std::move(r);
    }
    // Precise symbolic strip/lstrip/rstrip (whitespace form, no `chars`
    // argument) via the Python-whitespace strip axiom. With a `chars`
    // argument the semantics differ (arbitrary character set), so that case
    // falls through to the sound nondet model below.
    if(
      (method_name == "strip" || method_name == "lstrip" ||
       method_name == "rstrip") &&
      (!args.is_array() || as_array(args).empty()))
    {
      const exprt src =
        obj.id() == ID_struct
          ? obj
          : exprt(struct_exprt(
              {member_exprt(obj, "length", signedbv_typet{64}),
               member_exprt(
                 obj, "data", pointer_typet(unsignedbv_typet{8}, 64))},
              obj.type()));
      const int mode =
        method_name == "strip" ? 0 : (method_name == "lstrip" ? 1 : 2);
      return emit_string_function(
        ID_cprover_string_strip_func,
        {src, from_integer(mode, signedbv_typet{32})},
        symbol_table,
        pending_checks,
        loop_depth > 0,
        current_function);
    }
    // Native strings: removeprefix/removesuffix and the padding methods with
    // a constant fill character, exactly.
    if(obj.type().id() == ID_string)
    {
      if(auto r = native_affix_or_padding(expr, obj, method_name, args))
        return std::move(*r);
    }
    // Refined strings: removeprefix/removesuffix by is_prefix/is_suffix and a
    // slice.
    else if(
      (method_name == "removeprefix" || method_name == "removesuffix") &&
      args.is_array() && as_array(args).size() == 1)
    {
      const exprt affix = convert_expression(*as_array(args).begin());
      if(
        is_python_string_type(affix.type()) &&
        is_python_string_type(obj.type()))
      {
        const bool prefix = method_name == "removeprefix";
        const exprt has_affix = emit_string_bool_function(
          prefix ? ID_cprover_string_is_prefix_func
                 : ID_cprover_string_is_suffix_func,
          affix,
          obj,
          symbol_table,
          pending_checks);
        const exprt affix_len = native_or_member_string_length(affix);
        const exprt rest =
          prefix
            ? python_string_slice(obj, affix_len, std::nullopt)
            : python_string_slice(
                obj,
                std::nullopt,
                minus_exprt{native_or_member_string_length(obj), affix_len});
        if(rest.type() == obj.type())
          return if_exprt{has_affix, rest, obj};
      }
      // The argument is converted: it must not be evaluated again.
      return bounded_nondet_string(get_location(expr));
    }
    // Return nondet string with constraints for symbolic strings
    {
      // The arguments are still evaluated, for their exceptions and other
      // side effects. (For a constant receiver the folding above has
      // converted them.)
      if(!extract_string_value(obj).has_value() && args.is_array())
      {
        for(const auto &a : as_array(args))
          convert_expression(a);
      }
      static unsigned str_method_ctr = 0;
      std::string tn = "__str_meth_" + std::to_string(str_method_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_string_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        tv,
        side_effect_expr_nondett{python_string_type(), get_location(expr)}});
      // Both ASSUMEs use cprover_string_length_func so the
      // len()-consumer (which also goes through the intrinsic)
      // sees a value coordinated with the input's length.
      // The .length member-access path remains valid too
      // because refine-strings' array_pool.find() respects
      // the struct's .length field for non-constant
      // pointers.
      exprt result_len_intr = emit_string_int_function(
        ID_cprover_string_length_func, tv, symbol_table, pending_checks);
      exprt input_len_intr = emit_string_int_function(
        ID_cprover_string_length_func, obj, symbol_table, pending_checks);
      // strip/lstrip/rstrip (also with characters), removeprefix and
      // removesuffix: result length <= input length
      if(
        method_name == "strip" || method_name == "lstrip" ||
        method_name == "rstrip" || method_name == "removeprefix" ||
        method_name == "removesuffix")
      {
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{
            result_len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
          binary_relation_exprt{result_len_intr, ID_le, input_len_intr}}});
      }
      // capitalize/title/swapcase/casefold: empty exactly for an empty
      // input. (Case maps can change the length beyond ASCII, and refined
      // strings count UTF-8 bytes: 'ı'.upper() == 'I' is shorter.)
      else if(
        method_name == "capitalize" || method_name == "title" ||
        method_name == "swapcase" || method_name == "casefold")
      {
        const exprt zero = from_integer(0, signedbv_typet{64});
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{result_len_intr, ID_ge, zero},
          equal_exprt{
            equal_exprt{result_len_intr, zero},
            equal_exprt{input_len_intr, zero}}}});
      }
      // center/ljust/rjust/zfill/expandtabs: result length >= input length
      else if(
        method_name == "center" || method_name == "ljust" ||
        method_name == "rjust" || method_name == "zfill" ||
        method_name == "expandtabs")
      {
        pending_checks.push_back(code_assumet{
          binary_relation_exprt{result_len_intr, ID_ge, input_len_intr}});
      }
      return std::move(tv);
    }
  }
  if(
    method_name == "replace" || method_name == "format" ||
    method_name == "format_map")
  {
    // PLib stdtypes: str.replace(old, new[, count]) for constant strings
    if(
      method_name == "replace" && args.is_array() && as_array(args).size() >= 2)
    {
      auto ait = as_array(args).begin();
      exprt old_expr = convert_expression(*ait);
      ++ait;
      exprt new_expr = convert_expression(*ait);
      // PLR: optional 3rd arg is the maximum number of
      // replacements; default is 'unlimited'. -1 sentinel.
      long long max_count = -1;
      // The count when it is not a constant: neither folding nor
      // str.replace_all applies then.
      std::optional<exprt> symbolic_count;
      ++ait;
      if(ait != as_array(args).end())
      {
        const exprt count_expr = convert_expression(*ait);
        auto cv = try_eval_double(count_expr);
        if(cv.has_value())
          max_count = static_cast<long long>(cv.value());
        else
          symbolic_count = count_expr;
      }
      // Extract all three as constant strings, distinguishing a constant
      // EMPTY string (has_value()=="") from a non-constant/symbolic
      // operand (no value) — the latter must fall through to the symbolic
      // path, the former is handled precisely here.
      auto src_opt = extract_string_value(obj);
      auto old_opt = extract_string_value(old_expr);
      auto new_opt = extract_string_value(new_expr);
      if(
        !symbolic_count.has_value() && src_opt.has_value() &&
        old_opt.has_value() && new_opt.has_value())
      {
        const std::string &src = src_opt.value();
        const std::string &old_s = old_opt.value();
        const std::string &new_s = new_opt.value();
        std::string result;
        if(old_s.empty())
        {
          // PLR / CPython: an EMPTY pattern inserts `new` at every one of
          // the len(src)+1 boundaries (before each character and at the
          // end), left to right, limited to `max_count` insertions; the
          // source characters always appear. e.g.
          //   "ab".replace("", "-")    == "-a-b-"
          //   "abc".replace("", "-", 2) == "-a-bc"
          //   "".replace("", "x")      == "x"
          long long inserted = 0;
          for(std::size_t i = 0; i <= src.size(); ++i)
          {
            if(max_count < 0 || inserted < max_count)
            {
              result += new_s;
              ++inserted;
            }
            if(i < src.size())
              result += src[i];
          }
        }
        else
        {
          // Non-empty pattern: replace up to max_count occurrences (an
          // empty `src` yields "" — no occurrence to replace).
          std::size_t pos = 0;
          long long replaced = 0;
          while(pos < src.size())
          {
            if(max_count >= 0 && replaced >= max_count)
            {
              result += src.substr(pos);
              break;
            }
            auto found = src.find(old_s, pos);
            if(found == std::string::npos)
            {
              result += src.substr(pos);
              break;
            }
            result += src.substr(pos, found - pos) + new_s;
            pos = found + old_s.size();
            replaced++;
          }
        }
        // Build string literal
        return python_string_literal(result);
      }
      // Symbolic source, CONSTANT EMPTY pattern, CONSTANT replacement, no
      // count limit: CPython inserts `new` at each of the len(src)+1
      // boundaries, so the number of insertions is DETERMINED (unlike a
      // general pattern) and the result length is EXACTLY
      //   len(result) = len(src) + (len(src)+1)*len(new)
      //              = len(src)*(1+len(new)) + len(new)
      // — linear in len(src). Bind that length precisely on a fresh nondet
      // result (content stays nondet, which is sound: the true result is one
      // such string), making len() relations decidable on both back-ends.
      {
        auto old_sv = extract_string_value(old_expr);
        auto new_sv = extract_string_value(new_expr);
        if(
          max_count < 0 && !symbolic_count.has_value() && old_sv.has_value() &&
          old_sv->empty() && new_sv.has_value())
        {
          const long long ln = static_cast<long long>(new_sv->size());
          static unsigned er_ctr = 0;
          const std::string nm = "__replace_empty_" + std::to_string(er_ctr++);
          const irep_idt rid{qualify_name(nm)};
          const typet rt = obj.type();
          if(symbol_table.lookup(rid) == nullptr)
          {
            symbolt sy{rid, rt, "python"};
            sy.base_name = nm;
            sy.is_lvalue = true;
            sy.is_state_var = true;
            symbol_table.add(sy);
          }
          const symbol_exprt r = symbol_table.lookup_ref(rid).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            r, side_effect_expr_nondett{rt, get_location(expr)}});
          const signedbv_typet i64{64};
          if(rt.id() == ID_string)
          {
            // Native: len() routes through cprover_string_length_func.
            const exprt rl = native_or_member_string_length(r);
            const exprt sl = native_or_member_string_length(obj);
            const exprt rhs = plus_exprt{
              mult_exprt{sl, from_integer(1 + ln, sl.type())},
              from_integer(ln, sl.type())};
            pending_checks.push_back(code_assumet{equal_exprt{rl, rhs}});
          }
          else
          {
            // Refined: bind through the length intrinsic + sync the struct's
            // .length field (mirrors the nondet_str refined path).
            const exprt rl = emit_string_int_function(
              ID_cprover_string_length_func, r, symbol_table, pending_checks);
            pending_checks.push_back(
              code_assumet{equal_exprt{member_exprt{r, "length", i64}, rl}});
            const exprt sl = emit_string_int_function(
              ID_cprover_string_length_func, obj, symbol_table, pending_checks);
            const exprt rhs = plus_exprt{
              mult_exprt{sl, from_integer(1 + ln, i64)}, from_integer(ln, i64)};
            pending_checks.push_back(code_assumet{equal_exprt{rl, rhs}});
          }
          return std::move(r);
        }
      }
      // Symbolic-string replace not wired: the solver's
      // cprover_string_replace_func handles char-to-char
      // replacement only (5 args, chars not strings).
      // str.replace(old, new) on general strings needs
      // a new multi-char solver intrinsic. Falls through
      // to nondet for now.
      //
      // Native strings with a count: the replacements one by one.
      if(
        use_smt_string_native && obj.type().id() == ID_string &&
        old_expr.type().id() == ID_string &&
        new_expr.type().id() == ID_string &&
        (max_count >= 0 || (symbolic_count.has_value() &&
                            (symbolic_count->type().id() == ID_signedbv ||
                             symbolic_count->type().id() == ID_integer))))
      {
        return native_replace_with_count(
          expr,
          obj,
          old_expr,
          new_expr,
          symbolic_count.has_value()
            ? *symbolic_count
            : from_integer(max_count, signedbv_typet{64}));
      }
      // Native SMT-String back-end (Plan A): replace(old, new) with no count
      // limit is exactly str.replace_all over native SMT Strings.
      if(
        use_smt_string_native && max_count < 0 && !symbolic_count.has_value() &&
        is_python_string_type(old_expr.type()) &&
        is_python_string_type(new_expr.type()))
      {
        const irep_idt fn{ID_cprover_string_replace_all_func};
        if(symbol_table.lookup(fn) == nullptr)
        {
          std::vector<typet> ats{obj.type(), old_expr.type(), new_expr.type()};
          symbolt fs{
            fn,
            mathematical_function_typet(std::move(ats), string_typet{}),
            "python"};
          fs.base_name = id2string(fn);
          symbol_table.add(fs);
        }
        function_application_exprt app{
          symbol_table.lookup_ref(fn).symbol_expr(), {obj, old_expr, new_expr}};
        app.type() = string_typet{};

        auto old_sv = extract_string_value(old_expr);
        auto new_sv = extract_string_value(new_expr);

        // An empty `old` makes CPython insert `new` at each of the
        // len(obj)+1 boundaries, whereas SMT-LIB's str.replace_all leaves
        // the string unchanged. For an `old` that may be empty, use a fresh
        // string that starts and ends with `new` in that case (sound: the
        // true result is one such string).
        if(!old_sv.has_value() || old_sv->empty())
        {
          log_overapprox(
            "replace() of a native string with an `old` that may be empty: "
            "the result is a nondet string starting and ending with `new`");
          static unsigned er_ctr = 0;
          const std::string nm =
            "__replace_sym_empty_" + std::to_string(er_ctr++);
          const irep_idt rid{qualify_name(nm)};
          if(symbol_table.lookup(rid) == nullptr)
          {
            symbolt sy{rid, string_typet{}, "python"};
            sy.base_name = nm;
            sy.is_lvalue = true;
            sy.is_state_var = true;
            symbol_table.add(sy);
          }
          const symbol_exprt r = symbol_table.lookup_ref(rid).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            r, side_effect_expr_nondett{string_typet{}, get_location(expr)}});
          pending_checks.push_back(code_assumet{native_string_app(
            ID_cprover_string_startswith_func,
            {string_typet{}, string_typet{}},
            {r, new_expr},
            bool_typet{})});
          pending_checks.push_back(code_assumet{native_string_app(
            ID_cprover_string_endswith_func,
            {string_typet{}, string_typet{}},
            {r, new_expr},
            bool_typet{})});
          if(old_sv.has_value())
            return std::move(r);
          return if_exprt{
            equal_exprt{old_expr, constant_exprt{irep_idt{""}, string_typet{}}},
            r,
            std::move(app)};
        }

        // Length hint: str.replace_all changes the length by
        // (len(new) - len(old)) per replaced occurrence. The exact result
        // length is count-dependent (hard for the solver), but with CONSTANT
        // old/new we know the *direction*, which is a sound bound that makes a
        // len() relation over the result decidable instead of timing out:
        //   len(new) == len(old)  =>  len(result) == len(s)
        //   len(new)  > len(old)  =>  len(result) >= len(s)
        //   len(new)  < len(old)  =>  len(result) <= len(s)
        if(new_sv.has_value())
        {
          static unsigned rep_ctr = 0;
          const std::string nm = "__replace_len_" + std::to_string(rep_ctr++);
          const irep_idt rid{qualify_name(nm)};
          if(symbol_table.lookup(rid) == nullptr)
          {
            symbolt sy{rid, string_typet{}, "python"};
            sy.base_name = nm;
            sy.is_lvalue = true;
            sy.is_state_var = true;
            symbol_table.add(sy);
          }
          const symbol_exprt r = symbol_table.lookup_ref(rid).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            r, side_effect_expr_nondett{string_typet{}, get_location(expr)}});
          pending_checks.push_back(code_assumet{equal_exprt{r, app}});
          const exprt rl = native_or_member_string_length(r);
          const exprt ol = native_or_member_string_length(obj);
          if(new_sv->size() == old_sv->size())
            pending_checks.push_back(code_assumet{equal_exprt{rl, ol}});
          else if(new_sv->size() > old_sv->size())
            pending_checks.push_back(
              code_assumet{binary_relation_exprt{rl, ID_ge, ol}});
          else
            pending_checks.push_back(
              code_assumet{binary_relation_exprt{rl, ID_le, ol}});
          return std::move(r);
        }
        return std::move(app);
      }
    }
    // PLib stdtypes: str.format_map(mapping) — raise
    // KeyError when a {name} placeholder isn't a key in
    // the mapping. We handle the common case where the
    // mapping argument is a dict literal whose keys are
    // resolvable at conversion time.
    if(method_name == "format_map" && args.is_array())
    {
      auto fmt_sv = extract_string_value(obj);
      if(fmt_sv.has_value())
      {
        std::string fmt = fmt_sv.value();
        // Collect named placeholders.
        std::set<std::string> placeholders;
        for(std::size_t i = 0; i + 1 < fmt.size(); i++)
        {
          if(fmt[i] != '{' || fmt[i + 1] == '{')
            continue;
          auto close = fmt.find('}', i + 1);
          if(close == std::string::npos)
            continue;
          std::string spec = fmt.substr(i + 1, close - i - 1);
          if(spec.empty() || std::isdigit(spec[0]) || spec[0] == ':')
          {
            i = close;
            continue;
          }
          auto colon = spec.find(':');
          std::string name =
            colon == std::string::npos ? spec : spec.substr(0, colon);
          placeholders.insert(name);
          i = close;
        }
        // Resolve the mapping keys AND values when it's a
        // literal. We capture each (key, AST-value) pair so
        // we can substitute below.
        std::set<std::string> mapping_keys;
        std::map<std::string, jsont> mapping_pairs;
        bool keys_known = false;
        auto a_it = as_array(args).begin();
        if(a_it != as_array(args).end())
        {
          if(is_node_type(*a_it, "Dict"))
          {
            keys_known = true;
            const jsont &kn = json_member(*a_it, "keys");
            const jsont &vn = json_member(*a_it, "values");
            if(kn.is_array() && vn.is_array())
            {
              auto ki = as_array(kn).begin();
              auto vi = as_array(vn).begin();
              for(; ki != as_array(kn).end() && vi != as_array(vn).end();
                  ++ki, ++vi)
              {
                if(is_node_type(*ki, "Constant"))
                {
                  const jsont &v = json_member(*ki, "value");
                  if(v.is_string())
                  {
                    mapping_keys.insert(v.value);
                    mapping_pairs[v.value] = *vi;
                  }
                }
              }
            }
          }
        }
        if(keys_known)
        {
          bool missing = false;
          for(const auto &name : placeholders)
          {
            if(mapping_keys.count(name) == 0)
            {
              missing = true;
              const symbolt *exc_sym =
                symbol_table.lookup("python::__exception_active");
              const symbolt *exc_type_sym =
                symbol_table.lookup("python::__exception_type");
              if(exc_sym != nullptr)
              {
                pending_checks.push_back(
                  code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
                if(exc_type_sym != nullptr)
                {
                  long h = exception_type_hash("KeyError");
                  pending_checks.push_back(code_frontend_assignt{
                    exc_type_sym->symbol_expr(),
                    from_integer(h, python_int_type())});
                }
              }
              break;
            }
          }
          if(!missing)
          {
            // PLR §6.1.4: substitute every {name} placeholder
            // with str(mapping[name]) when both the format
            // string and all referenced values are constants.
            std::string result;
            bool all_const = true;
            for(std::size_t i = 0; i < fmt.size(); i++)
            {
              if(fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{')
              {
                result += '{';
                ++i;
                continue;
              }
              if(fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}')
              {
                result += '}';
                ++i;
                continue;
              }
              if(fmt[i] == '{' && i + 1 < fmt.size())
              {
                auto close = fmt.find('}', i + 1);
                if(close == std::string::npos)
                {
                  result += fmt[i];
                  continue;
                }
                std::string spec = fmt.substr(i + 1, close - i - 1);
                i = close;
                auto colon = spec.find(':');
                std::string name =
                  colon == std::string::npos ? spec : spec.substr(0, colon);
                auto pi = mapping_pairs.find(name);
                if(pi == mapping_pairs.end())
                {
                  all_const = false;
                  break;
                }
                const jsont &val_ast = pi->second;
                if(is_node_type(val_ast, "Constant"))
                {
                  const jsont &v = json_member(val_ast, "value");
                  if(v.is_string())
                    result += v.value;
                  else if(v.is_number())
                  {
                    // e.g. "{x}".format_map({"x": 1}) -> "1"
                    double d = std::stod(v.value);
                    if(d == std::floor(d) && std::abs(d) < 1e15)
                      result += std::to_string(static_cast<long long>(d));
                    else
                      result += v.value;
                  }
                  else if(v.is_true())
                    result += "True";
                  else if(v.is_false())
                    result += "False";
                  else
                    all_const = false;
                }
                else
                  all_const = false;
              }
              else
                result += fmt[i];
            }
            if(all_const)
              return python_string_literal(result);
          }
        }
      }
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }
    // PLib stdtypes: str.format() — substitute {} placeholders
    if(method_name == "format")
    {
      auto fmt_sv = extract_string_value(obj);
      if(fmt_sv.has_value())
      {
        std::string fmt = fmt_sv.value();
        // Simple {} substitution with string args
        std::string result;
        std::size_t arg_idx = 0;
        exprt::operandst arg_exprs;
        if(args.is_array())
        {
          for(const auto &a : as_array(args))
            arg_exprs.push_back(convert_expression(a));
        }
        bool all_const = true;
        for(std::size_t i = 0; i < fmt.size(); i++)
        {
          // PLR §6.1.3: '{{' is an escape for a literal '{',
          // and '}}' is an escape for a literal '}'.
          if(fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{')
          {
            result += '{';
            ++i;
            continue;
          }
          if(fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}')
          {
            result += '}';
            ++i;
            continue;
          }
          if(fmt[i] == '{' && i + 1 < fmt.size())
          {
            // Find closing }
            auto close = fmt.find('}', i + 1);
            if(close == std::string::npos)
            {
              result += fmt[i];
              continue;
            }
            std::string spec = fmt.substr(i + 1, close - i - 1);
            i = close; // skip to }

            // Determine which arg to use
            std::size_t use_idx = arg_idx;
            std::string fmt_spec;
            if(spec.empty())
            {
              // {} — use next arg
              use_idx = arg_idx++;
            }
            else if(std::isdigit(spec[0]))
            {
              // {0}, {1}, {0:d}, etc.
              auto colon = spec.find(':');
              use_idx = std::stoul(spec.substr(
                0, colon != std::string::npos ? colon : spec.size()));
              if(colon != std::string::npos)
                fmt_spec = spec.substr(colon + 1);
            }
            else if(spec[0] == ':')
            {
              // {:d}, {:.2f}, etc.
              fmt_spec = spec.substr(1);
              use_idx = arg_idx++;
            }
            else
            {
              // PLR §6.1.4: format with a named placeholder
              // like '{name}'. If no keyword arg with this
              // name is provided, raise KeyError. We check
              // the call's 'keywords' field for a matching
              // arg name; missing → KeyError.
              std::string key_name = spec;
              auto colon_pos = key_name.find(':');
              std::string named_fmt_spec;
              if(colon_pos != std::string::npos)
              {
                named_fmt_spec = key_name.substr(colon_pos + 1);
                key_name = key_name.substr(0, colon_pos);
              }
              bool found_kw = false;
              std::string kw_value;
              bool kw_value_const = false;
              const jsont &kws = json_member(expr, "keywords");
              if(kws.is_array())
              {
                for(const auto &kw : as_array(kws))
                {
                  if(json_string(json_member(kw, "arg")) == key_name)
                  {
                    found_kw = true;
                    // Try to extract the kwarg's value as a
                    // constant string for fold.
                    exprt v_expr = convert_expression(json_member(kw, "value"));
                    auto sv = extract_string_value(v_expr);
                    if(sv.has_value())
                    {
                      kw_value = sv.value();
                      kw_value_const = true;
                    }
                    break;
                  }
                }
              }
              if(!found_kw)
              {
                const symbolt *exc_sym =
                  symbol_table.lookup("python::__exception_active");
                const symbolt *exc_type_sym =
                  symbol_table.lookup("python::__exception_type");
                if(exc_sym != nullptr)
                {
                  pending_checks.push_back(code_frontend_assignt{
                    exc_sym->symbol_expr(), true_exprt{}});
                  if(exc_type_sym != nullptr)
                  {
                    long h = exception_type_hash("KeyError");
                    pending_checks.push_back(code_frontend_assignt{
                      exc_type_sym->symbol_expr(),
                      from_integer(h, python_int_type())});
                  }
                }
                all_const = false;
                continue;
              }
              if(kw_value_const)
              {
                result += kw_value;
              }
              else
              {
                all_const = false;
              }
              continue;
            }

            if(use_idx >= arg_exprs.size())
            {
              // PLR §6.1.4: format() raises IndexError
              // when a positional placeholder index has
              // no matching argument (e.g. '{1}'.format('a')
              // or '{} {}'.format('a')).
              const symbolt *exc_sym =
                symbol_table.lookup("python::__exception_active");
              const symbolt *exc_type_sym =
                symbol_table.lookup("python::__exception_type");
              if(exc_sym != nullptr)
              {
                pending_checks.push_back(
                  code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
                if(exc_type_sym != nullptr)
                {
                  long h = exception_type_hash("IndexError");
                  pending_checks.push_back(code_frontend_assignt{
                    exc_type_sym->symbol_expr(),
                    from_integer(h, python_int_type())});
                }
              }
              all_const = false;
              continue;
            }

            // PLR §6.1.3: validate the format presentation code against the
            // arg's concrete type ({:d} on a str -> ValueError, {:s} on an int
            // -> ValueError, ...). The type char is the trailing letter of the
            // spec; gated on a concrete arg type (symbolic/Any never flagged).
            if(!fmt_spec.empty())
            {
              const char last = fmt_spec.back();
              const char code = std::isalpha(static_cast<unsigned char>(last))
                                  ? static_cast<char>(std::tolower(
                                      static_cast<unsigned char>(last)))
                                  : char{0};
              if(code != 0)
              {
                const char *exc = format_code_violation(
                  code,
                  value_format_category(arg_exprs[use_idx].type()),
                  false);
                if(exc != nullptr)
                  emit_conditional_exception(true_exprt{}, exc);
              }
            }

            // Extract value
            // Detect None argument by AST inspection (not type)
            // since Python's None becomes a signedbv sentinel
            // value indistinguishable from an int by type.
            bool is_none_arg = false;
            if(args.is_array() && use_idx < as_array(args).size())
            {
              auto ait_n = as_array(args).begin();
              std::advance(ait_n, use_idx);
              if(is_node_type(*ait_n, "Constant"))
              {
                const jsont &v = json_member(*ait_n, "value");
                if(v.is_null())
                  is_none_arg = true;
              }
            }
            if(is_none_arg)
            {
              result += "None";
              continue;
            }
            auto sv = extract_string_value(arg_exprs[use_idx]);
            if(sv.has_value())
            {
              result += sv.value();
            }
            else if(
              arg_exprs[use_idx].type().id() == ID_bool &&
              arg_exprs[use_idx].is_constant())
            {
              // PLR §6.1.4: format() of a bool yields its
              // str() spelling ("True" / "False"), not the
              // numeric value 1 / 0.
              bool bv = to_constant_expr(arg_exprs[use_idx]).is_true();
              result += bv ? "True" : "False";
            }
            else
            {
              auto fv = try_eval_double(arg_exprs[use_idx]);
              if(fv.has_value())
              {
                double d = fv.value();
                const bool arg_is_float =
                  arg_exprs[use_idx].type().id() == ID_floatbv;
                if(fmt_spec == "d" || fmt_spec == "n")
                {
                  // explicit integer presentation
                  result += std::to_string(static_cast<long long>(d));
                }
                else if(fmt_spec.empty())
                {
                  // default format == str(): a FLOAT arg uses CPython
                  // repr (py_float_repr) -- the ostream path dropped the
                  // ".0" ("{}".format(1.0) -> "1") and truncated
                  // precision ("{}".format(1.23456789) -> "1.23457"),
                  // WRONG constants (false proofs; ESBMC str_format_*).
                  if(arg_is_float)
                    result += py_float_repr(d);
                  else if(d == std::floor(d) && std::abs(d) < 1e15)
                    result += std::to_string(static_cast<long long>(d));
                  else
                    result += py_float_repr(d);
                }
                else if(fmt_spec[0] == '.')
                {
                  // .Nf format
                  int prec = std::stoi(fmt_spec.substr(1));
                  std::ostringstream oss;
                  oss << std::fixed << std::setprecision(prec) << d;
                  result += oss.str();
                }
                else
                  all_const = false;
              }
              else
                all_const = false;
            }
          }
          else
            result += fmt[i];
        }
        if(all_const)
        {
          return python_string_literal(result);
        }
        // Symbolic int fast path: template is just "{}"
        // or "{0}" with no format spec, single int arg.
        // Emit cprover_string_of_int_func.
        if(
          (fmt == "{}" || fmt == "{0}") && arg_exprs.size() == 1 &&
          (arg_exprs[0].type().id() == ID_signedbv ||
           arg_exprs[0].type().id() == ID_integer))
        {
          exprt as_i64 = arg_exprs[0].type() == signedbv_typet{64}
                           ? arg_exprs[0]
                           : safe_typecast(arg_exprs[0], signedbv_typet{64});
          exprt r = emit_string_function(
            ID_cprover_string_of_int_func,
            {as_i64},
            symbol_table,
            pending_checks,
            loop_depth > 0,
            // global (documented): see emit_string_function scope invariant.
            std::string{});
          auto ensure_fn = [&](const irep_idt &fid)
          {
            if(symbol_table.lookup(fid) == nullptr)
            {
              array_typet inf_array_type{
                unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64})};
              std::vector<typet> at;
              if(fid == ID_cprover_associate_array_to_pointer_func)
              {
                at.push_back(inf_array_type);
                at.push_back(pointer_typet(unsignedbv_typet{8}, 64));
              }
              else
              {
                at.push_back(inf_array_type);
                at.push_back(signedbv_typet{64});
              }
              symbolt fs{
                fid,
                mathematical_function_typet(std::move(at), signedbv_typet{32}),
                "python"};
              fs.base_name = id2string(fid);
              symbol_table.add(fs);
            }
          };
          ensure_fn(ID_cprover_associate_array_to_pointer_func);
          ensure_fn(ID_cprover_associate_length_to_array_func);
          return r;
        }
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // PLib stdtypes: startswith/endswith — constant fast path.
  // For non-constant string operands the byte-by-byte
  // comparison via member_exprt(.., "data", array_typet)
  // type-confused the field (the actual struct field is a
  // pointer, not an inline array), producing
  // `(select <pointer-bv> <idx>)` SMT — rejected by cvc5
  // with "array select operating on non-array". The
  // semantically-correct refinement-string path is taken
  // by the second startswith/endswith handler below
  // (line ~1693), which emits
  // cprover_string_is_prefix_func / is_suffix_func.
  if(
    (method_name == "startswith" || method_name == "endswith") &&
    args.is_array() && !as_array(args).empty() &&
    // Fold-soundness rule: the start/end POSITION arguments restrict the
    // window ("abcabc".startswith("ab", 1) folded to the plain prefix
    // test -- a false proof). Positioned calls fall through to the
    // nondet tail of the predicate dispatcher.
    fold_covers_call_shape(expr, 1))
  {
    exprt prefix = convert_expression(*as_array(args).begin());
    auto obj_sv = extract_string_value(obj);
    auto pre_sv = extract_string_value(prefix);
    if(obj_sv.has_value() && pre_sv.has_value())
    {
      bool result;
      if(method_name == "startswith")
        result =
          obj_sv.value().substr(0, pre_sv.value().size()) == pre_sv.value();
      else
        result =
          obj_sv.value().size() >= pre_sv.value().size() &&
          obj_sv.value().substr(
            obj_sv.value().size() - pre_sv.value().size()) == pre_sv.value();
      return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
    }
    // Non-constant case: fall through to refinement-string path.
  }
  // PLib stdtypes: exact string predicates
  if(
    method_name == "isdigit" || method_name == "isalpha" ||
    method_name == "isalnum" || method_name == "isupper" ||
    method_name == "islower" || method_name == "isspace" ||
    method_name == "isascii" || method_name == "isnumeric" ||
    method_name == "isidentifier" || method_name == "isdecimal")
  {
    // Constant-string optimization, for strings of Latin-1 characters (whose
    // properties are tabulated); others are left to the symbolic model.
    auto sv = extract_string_value(obj);
    if(sv.has_value())
    {
      const std::u32string chars = utf8_to_utf32(*sv);
      const bool latin1 = std::all_of(
        chars.begin(), chars.end(), [](char32_t c) { return c <= 0xff; });
      if(method_name == "isascii")
      {
        const bool ascii = std::all_of(
          chars.begin(), chars.end(), [](char32_t c) { return c < 0x80; });
        return ascii ? exprt{true_exprt{}} : exprt{false_exprt{}};
      }
      if(latin1)
      {
        auto has = [](char32_t c, unsigned property)
        { return (latin1_properties[c] & property) != 0; };
        bool result;
        if(method_name == "isidentifier")
        {
          result = !chars.empty() && has(chars[0], latin1_id_start) &&
                   std::all_of(
                     chars.begin() + 1,
                     chars.end(),
                     [&](char32_t c) { return has(c, latin1_id_continue); });
        }
        else if(method_name == "isupper" || method_name == "islower")
        {
          // all cased characters in the case, and at least one
          const unsigned want =
            method_name == "isupper" ? latin1_upper : latin1_lower;
          const unsigned other =
            method_name == "isupper" ? latin1_lower : latin1_upper;
          result = std::any_of(
                     chars.begin(),
                     chars.end(),
                     [&](char32_t c) { return has(c, want); }) &&
                   std::none_of(
                     chars.begin(),
                     chars.end(),
                     [&](char32_t c) { return has(c, other); });
        }
        else
        {
          const unsigned property =
            method_name == "isalpha"     ? latin1_alpha
            : method_name == "isalnum"   ? latin1_alnum
            : method_name == "isdigit"   ? latin1_digit
            : method_name == "isnumeric" ? latin1_numeric
            : method_name == "isdecimal" ? latin1_decimal
                                         : latin1_space;
          result =
            !chars.empty() && std::all_of(
                                chars.begin(),
                                chars.end(),
                                [&](char32_t c) { return has(c, property); });
        }
        return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
      }
    }
    // Native strings: regular-expression membership over the classes of the
    // Latin-1 characters (latin1_properties); a string with a character
    // beyond Latin-1 that the classes do not decide gives a nondet result.
    if(obj.type().id() == ID_string)
      return native_string_predicate(obj, method_name, get_location(expr));
    // Non-constant string predicate: try to read the first
    // byte through the string struct's data pointer and apply
    // the predicate as a byte-level check. This is exact for
    // 1-char strings (the s[i] / 'for c in s' patterns) and
    // a safe over-approximation for longer ones (we only
    // examine data[0]).
    if(
      method_name == "isdigit" || method_name == "isalpha" ||
      method_name == "isalnum" || method_name == "isupper" ||
      method_name == "islower" || method_name == "isspace" ||
      method_name == "isascii" || method_name == "isnumeric")
    {
      // Resolve the string source: either obj is the literal
      // struct (1-char from s[i]), or obj is a Name pointing
      // to a python_string symbol.
      const exprt *src = &obj;
      if(obj.id() == ID_symbol)
        src = &obj;
      // Read the first byte via *(data + 0). Empty string
      // (length=0) returns False per Python semantics
      // (isalpha returns False on empty).
      member_exprt data_ptr{
        *src, "data", pointer_typet{unsignedbv_typet{8}, 64}};
      member_exprt len{*src, "length", signedbv_typet{64}};
      exprt zero = from_integer(0, signedbv_typet{64});
      exprt is_empty = equal_exprt{len, zero};
      exprt is_single = equal_exprt{len, from_integer(1, signedbv_typet{64})};
      // Dereference data[0]. Cast to unsignedbv to make ranges
      // unambiguous.
      exprt byte0 = dereference_exprt{plus_exprt{data_ptr, zero}};
      // Per-method predicate on byte0:
      auto byte_in_range = [&](unsigned char lo, unsigned char hi)
      {
        return and_exprt{
          binary_relation_exprt{byte0, ID_ge, from_integer(lo, byte0.type())},
          binary_relation_exprt{byte0, ID_le, from_integer(hi, byte0.type())}};
      };
      exprt byte_pred = false_exprt{};
      if(method_name == "isdigit" || method_name == "isnumeric")
        byte_pred = byte_in_range('0', '9');
      else if(method_name == "isalpha")
      {
        byte_pred = or_exprt{byte_in_range('A', 'Z'), byte_in_range('a', 'z')};
      }
      else if(method_name == "isalnum")
      {
        byte_pred = or_exprt{
          byte_in_range('0', '9'),
          or_exprt{byte_in_range('A', 'Z'), byte_in_range('a', 'z')}};
      }
      else if(method_name == "isupper")
        byte_pred = byte_in_range('A', 'Z');
      else if(method_name == "islower")
        byte_pred = byte_in_range('a', 'z');
      else if(method_name == "isspace")
      {
        byte_pred = or_exprt{
          equal_exprt{byte0, from_integer(' ', byte0.type())},
          or_exprt{
            equal_exprt{byte0, from_integer('\t', byte0.type())},
            or_exprt{
              equal_exprt{byte0, from_integer('\n', byte0.type())},
              or_exprt{
                equal_exprt{byte0, from_integer('\r', byte0.type())},
                or_exprt{
                  equal_exprt{byte0, from_integer('\v', byte0.type())},
                  or_exprt{
                    equal_exprt{byte0, from_integer('\f', byte0.type())},
                    // CPython also counts \x1c-\x1f (FS GS RS US).
                    byte_in_range('\x1c', '\x1f')}}}}}};
      }
      else if(method_name == "isascii")
      {
        byte_pred =
          binary_relation_exprt{byte0, ID_lt, from_integer(0x80, byte0.type())};
      }
      // A character beyond ASCII (whose first UTF-8 byte is at least 0x80)
      // is not classified here: '€'.isalpha() is False, 'é'.isalpha() True.
      if(method_name != "isascii")
        byte_pred = if_exprt{
          binary_relation_exprt{byte0, ID_ge, from_integer(0x80, byte0.type())},
          side_effect_expr_nondett{bool_typet{}, source_locationt{}},
          byte_pred};
      // PLR semantics: empty string returns False for all
      // these predicates except isascii (which returns True).
      exprt empty_result =
        (method_name == "isascii") ? exprt{true_exprt{}} : exprt{false_exprt{}};
      // For length >= 1, the predicate holds iff byte0 matches.
      // (For longer strings, we under-check by only looking at
      // byte0, which would over-approximate the result —
      // soundly, a stricter compile-time check would walk
      // every byte. For 1-char strings this is exact.)
      // Use 'is_single' to decide whether to gate on byte0
      // alone or fall through to nondet for length>1.
      return if_exprt{
        is_empty,
        empty_result,
        if_exprt{
          is_single,
          byte_pred,
          side_effect_expr_nondett{bool_typet{}, source_locationt{}}}};
    }
    // Other unsupported predicates: nondet bool.
    return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
  }
  if(
    method_name == "startswith" || method_name == "endswith" ||
    method_name == "istitle" || method_name == "isidentifier" ||
    method_name == "isprintable")
  {
    // Use solver for startswith/endswith on non-constant strings
    if(
      (method_name == "startswith" || method_name == "endswith") &&
      args.is_array() && !as_array(args).empty() &&
      fold_covers_call_shape(expr, 1)) // see the constant handler's note
    {
      exprt prefix = convert_expression(*as_array(args).begin());
      if(is_python_string_type(prefix.type()))
      {
        return emit_string_bool_function(
          method_name == "startswith" ? ID_cprover_string_is_prefix_func
                                      : ID_cprover_string_is_suffix_func,
          prefix,
          obj,
          symbol_table,
          pending_checks);
      }
    }
    // Positioned form: s.startswith(p, start, end) tests s[start:end],
    // and is False when the adjusted start is after the adjusted end (also
    // for an empty p, unlike the slice: "ab".startswith("", 3) is False).
    if(
      (method_name == "startswith" || method_name == "endswith") &&
      args.is_array() && as_array(args).size() >= 2 &&
      fold_covers_call_shape(expr, 3))
    {
      auto it = as_array(args).begin();
      const exprt prefix = convert_expression(*it);
      auto position = [&](const jsont &j) -> std::optional<exprt>
      {
        if(is_node_type(j, "Constant") && json_member(j, "value").is_null())
          return std::nullopt;
        return convert_expression(j);
      };
      const std::optional<exprt> start = position(*++it);
      std::optional<exprt> end;
      if(++it != as_array(args).end())
        end = position(*it);
      const bool int_positions =
        (!start.has_value() || start->type().id() == ID_signedbv) &&
        (!end.has_value() || end->type().id() == ID_signedbv);
      if(is_python_string_type(prefix.type()) && int_positions)
      {
        const exprt len = native_or_member_string_length(obj);
        const auto [start_n, end_n] = python_adjusted_indices(len, start, end);
        const exprt slice = python_string_slice(obj, start, end);
        const exprt matches = emit_string_bool_function(
          method_name == "startswith" ? ID_cprover_string_is_prefix_func
                                      : ID_cprover_string_is_suffix_func,
          prefix,
          slice,
          symbol_table,
          pending_checks);
        return and_exprt{
          typecast_exprt::conditional_cast(matches, bool_typet{}),
          binary_relation_exprt{start_n, ID_le, end_n}};
      }
      return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    }
    // A positioned call is not converted above: its arguments are still
    // evaluated, for their exceptions.
    if(
      (method_name == "startswith" || method_name == "endswith") &&
      args.is_array() && !fold_covers_call_shape(expr, 1))
    {
      for(const auto &a : as_array(args))
        convert_expression(a);
    }
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  if(
    method_name == "find" || method_name == "index" || method_name == "rfind" ||
    method_name == "rindex" || method_name == "count")
  {
    auto str_val = extract_string_value(obj);
    if(
      !str_val.has_value() &&
      (obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      auto it = string_constants.find(
        obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                              : irep_idt{});
      if(it != string_constants.end())
        str_val = it->second;
    }
    // Each argument is converted once, here or below.
    std::vector<std::optional<exprt>> converted(
      args.is_array() ? as_array(args).size() : 0);
    auto arg_at = [&](std::size_t i) -> exprt
    {
      if(!converted[i].has_value())
        converted[i] =
          convert_expression(*std::next(as_array(args).begin(), i));
      return *converted[i];
    };
    if(str_val.has_value() && args.is_array() && !as_array(args).empty())
    {
      exprt arg_expr = arg_at(0);
      auto arg_val = extract_string_value(arg_expr);
      if(!arg_val.has_value() && arg_expr.id() == ID_symbol)
      {
        auto it2 =
          string_constants.find(to_symbol_expr(arg_expr).get_identifier());
        if(it2 != string_constants.end())
          arg_val = it2->second;
      }
      // Parse optional start/end range; a symbolic one prevents folding.
      std::optional<long long> start_arg, end_arg;
      bool range_known = true;
      for(std::size_t i = 1; i < converted.size() && i < 3; ++i)
      {
        const auto v = try_eval_double(arg_at(i));
        if(!v.has_value())
          range_known = false;
        else if(i == 1)
          start_arg = static_cast<long long>(*v);
        else
          end_arg = static_cast<long long>(*v);
      }
      // Positions are code points, so the folding is ASCII only.
      if(
        arg_val.has_value() && range_known && is_ascii_string(*str_val) &&
        is_ascii_string(*arg_val))
      {
        std::string s = str_val.value();
        const int slen = static_cast<int>(s.size());
        // PLR: start and end are interpreted as in slice notation.
        long long start_ll = start_arg.value_or(0);
        if(start_ll < 0)
          start_ll += slen;
        if(start_ll < 0)
          start_ll = 0;
        long long end_ll = end_arg.value_or(slen);
        if(end_ll < 0)
          end_ll += slen;
        if(end_ll < 0)
          end_ll = 0;
        if(end_ll > slen)
          end_ll = slen;
        // start may exceed slen (then nothing is found, not even "").
        const int start =
          static_cast<int>(std::min<long long>(start_ll, slen + 1));
        const int end = static_cast<int>(end_ll);
        std::string slice = (start < end) ? s.substr(start, end - start) : "";
        const std::string &sub = arg_val.value();
        // CPython empty-substring search: the empty string is "found" only
        // when the (normalized) start lies within [0, len] AND start <= end.
        // slice.find("")/rfind("") would otherwise always succeed (0 /
        // slice length), wrongly reporting a match for inverted or
        // out-of-range bounds (e.g. "abc".index("", 2, 1) must raise
        // ValueError). Forward position is `start`; backward is `end`.
        const bool empty_found = sub.empty() && start <= slen && start <= end;
        const std::size_t npos = std::string::npos;
        if(method_name == "find")
        {
          auto pos = sub.empty() ? (empty_found ? std::size_t{0} : npos)
                                 : slice.find(sub);
          return from_integer(
            pos == std::string::npos ? -1 : static_cast<long long>(pos + start),
            python_int_type());
        }
        if(method_name == "rfind")
        {
          auto pos = sub.empty()
                       ? (empty_found ? std::size_t(end - start) : npos)
                       : slice.rfind(sub);
          return from_integer(
            pos == std::string::npos ? -1 : static_cast<long long>(pos + start),
            python_int_type());
        }
        if(method_name == "index" || method_name == "rindex")
        {
          // PLR: str.index(sub, start, end) searches the
          // substring s[start:end], NOT the full string.
          auto pos =
            (method_name == "index")
              ? (sub.empty() ? (empty_found ? std::size_t{0} : npos)
                             : slice.find(sub))
              : (sub.empty() ? (empty_found ? std::size_t(end - start) : npos)
                             : slice.rfind(sub));
          if(pos == std::string::npos)
          {
            // Raise ValueError (set BOTH the active flag and the type, so
            // `except ValueError` matches — a bare active-flag set leaves the
            // type unset and the handler can't dispatch).
            emit_conditional_exception(true_exprt{}, "ValueError");
            return from_integer(-1, python_int_type());
          }
          // Translate slice-relative position back to the
          // original-string position.
          return from_integer(
            static_cast<long long>(pos) + static_cast<long long>(start),
            python_int_type());
        }
        if(method_name == "count")
        {
          long long cnt = 0;
          size_t pos = 0;
          while((pos = slice.find(sub, pos)) != std::string::npos)
          {
            cnt++;
            pos += sub.empty() ? 1 : sub.size();
          }
          if(sub.empty())
            cnt = empty_found ? static_cast<long long>(slice.size() + 1) : 0;
          return from_integer(cnt, python_int_type());
        }
      }
    }
    // Native count: the occurrences that str.replace_all removes are the
    // non-overlapping ones that count() counts, so count(t) is
    // (len(s) - len(s.replace_all(t, ""))) / len(t), and len(s) + 1 for an
    // empty t. With positions, of the slice s[start:end].
    if(
      method_name == "count" && obj.type().id() == ID_string &&
      args.is_array() && !as_array(args).empty() &&
      fold_covers_call_shape(expr, 3))
    {
      const exprt needle = arg_at(0);
      std::optional<exprt> start, end;
      for(std::size_t i = 1; i < converted.size() && i < 3; ++i)
      {
        const jsont &j = *std::next(as_array(args).begin(), i);
        const exprt e = arg_at(i);
        if(is_node_type(j, "Constant") && json_member(j, "value").is_null())
          continue;
        (i == 1 ? start : end) = e;
      }
      const bool int_positions =
        (!start.has_value() || start->type().id() == ID_signedbv) &&
        (!end.has_value() || end->type().id() == ID_signedbv);
      if(needle.type().id() == ID_string && int_positions)
      {
        const string_typet str_t;
        const integer_typet int_t;
        auto length = [&](const exprt &x)
        {
          return native_string_app(
            ID_cprover_string_length_func, {str_t}, {x}, int_t);
        };
        auto as_int = [&](const std::optional<exprt> &p) -> std::optional<exprt>
        {
          if(!p.has_value())
            return p;
          return exprt{typecast_exprt{*p, int_t}};
        };
        const exprt len = length(obj);
        const auto [start_n, end_n] =
          python_adjusted_indices(len, as_int(start), as_int(end));
        const exprt zero = from_integer(0, int_t);
        exprt hay = obj;
        if(start.has_value() || end.has_value())
        {
          const exprt lo =
            if_exprt{binary_relation_exprt{start_n, ID_gt, len}, len, start_n};
          const exprt hi =
            if_exprt{binary_relation_exprt{end_n, ID_lt, lo}, lo, end_n};
          hay = native_string_app(
            ID_cprover_string_substring_func,
            {str_t, int_t, int_t},
            {obj, lo, minus_exprt{hi, lo}},
            str_t);
        }
        const exprt ndl_len = length(needle);
        const exprt removed = native_string_app(
          ID_cprover_string_replace_all_func,
          {str_t, str_t, str_t},
          {hay, needle, constant_exprt{"", str_t}},
          str_t);
        const exprt counted = if_exprt{
          equal_exprt{ndl_len, zero},
          // "" occurs at each position of the slice, if it is not empty
          // because the start is after the end
          if_exprt{
            binary_relation_exprt{start_n, ID_le, end_n},
            plus_exprt{length(hay), from_integer(1, int_t)},
            zero},
          div_exprt{minus_exprt{length(hay), length(removed)}, ndl_len}};
        // The count is the length of a fresh string, with bounds that hold
        // for every string. As an integer variable, or as a bit-vector
        // related by bv2nat, the conversion to a 64-bit integer took cvc5
        // minutes even when the strings were constants; str.len is fast.
        static unsigned count_ctr = 0;
        const std::string nm = "__native_count_" + std::to_string(count_ctr++);
        const irep_idt id{qualify_name(nm)};
        if(symbol_table.lookup(id) == nullptr)
        {
          symbolt sy{id, str_t, "python"};
          sy.base_name = nm;
          sy.is_lvalue = true;
          sy.is_state_var = true;
          symbol_table.add(sy);
        }
        const symbol_exprt c = symbol_table.lookup_ref(id).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{
          c, side_effect_expr_nondett{str_t, get_location(expr)}});
        pending_checks.push_back(code_assumet{and_exprt{
          equal_exprt{length(c), counted},
          binary_relation_exprt{
            length(c),
            ID_le,
            plus_exprt{length(hay), from_integer(1, int_t)}}}});
        // len(s) < 2^63 bounds the length of every slice of s
        native_or_member_string_length(obj);
        return typecast_exprt{length(c), python_int_type()};
      }
    }
    // Precise symbolic find/index/rfind/rindex via the index_of /
    // last_index_of intrinsics (refined) or str.indexof (native). These are
    // queries (return an int; no result string, hence no backing), so they
    // are safe in loops. A forward search with only a start searches from
    // that start; any other positioned search searches the slice
    // s[start:end] and adds the start. Native strings have no
    // last_index_of: the last occurrence is constrained instead.
    {
      const bool is_forward = (method_name == "find" || method_name == "index");
      const bool is_backward =
        (method_name == "rfind" || method_name == "rindex");
      if(
        (is_forward || is_backward) && args.is_array() &&
        !as_array(args).empty() && fold_covers_call_shape(expr, 3))
      {
        exprt needle = arg_at(0);
        auto position = [&](std::size_t i) -> std::optional<exprt>
        {
          if(i >= converted.size())
            return std::nullopt;
          const jsont &j = *std::next(as_array(args).begin(), i);
          const exprt e = arg_at(i);
          if(is_node_type(j, "Constant") && json_member(j, "value").is_null())
            return std::nullopt;
          return e;
        };
        const std::optional<exprt> start = position(1);
        const std::optional<exprt> end = position(2);
        const bool int_positions =
          (!start.has_value() || start->type().id() == ID_signedbv) &&
          (!end.has_value() || end->type().id() == ID_signedbv);
        if(is_python_string_type(needle.type()) && int_positions)
        {
          auto as_str_struct = [](const exprt &s) -> exprt
          {
            if(s.type().id() == ID_string)
              return s;
            if(s.id() == ID_struct && s.operands().size() == 2)
              return s;
            return struct_exprt(
              {member_exprt(s, "length", signedbv_typet{64}),
               member_exprt(s, "data", pointer_typet(unsignedbv_typet{8}, 64))},
              s.type());
          };
          const typet int_type = signedbv_typet{64};
          const exprt minus_one = from_integer(-1, int_type);
          const exprt ndl = as_str_struct(needle);
          const bool native = obj.type().id() == ID_string;
          // index_of / last_index_of application over refined or native
          // operands, as an i64. Direction B (native): Int positions and
          // result, bridged by typecasts.
          auto intrinsic = [&](const irep_idt &fn, exprt::operandst call_args)
          {
            const typet app_type =
              native ? typet{integer_typet{}} : typet{int_type};
            if(symbol_table.lookup(fn) == nullptr)
            {
              std::vector<typet> ats;
              for(const auto &x : call_args)
                ats.push_back(x.type());
              symbolt fs{
                fn,
                mathematical_function_typet(std::move(ats), app_type),
                "python"};
              fs.base_name = id2string(fn);
              symbol_table.add(fs);
            }
            function_application_exprt app{
              symbol_table.lookup_ref(fn).symbol_expr(), std::move(call_args)};
            app.type() = app_type;
            return native ? exprt{typecast_exprt{app, int_type}} : exprt{app};
          };
          auto forward_search = [&](const exprt &hay, const exprt &from)
          {
            return intrinsic(
              ID_cprover_string_index_of_func,
              {as_str_struct(hay),
               ndl,
               native ? exprt{typecast_exprt{from, integer_typet{}}} : from});
          };
          // The last occurrence in a native string: -1 exactly when there is
          // none, and otherwise a position where the needle occurs with no
          // occurrence after it; an empty needle is found at the end.
          auto native_backward_search = [&](const exprt &hay) -> exprt
          {
            // In mathematical integers: bit-vector positions converted to
            // and from Int made cvc5 time out on these constraints. The
            // result r is the length of a fresh string minus one: cvc5 took
            // seconds to convert an integer variable to a bit-vector, even
            // to prove r >= -1, and is fast on str.len.
            const integer_typet int_t;
            static unsigned last_ctr = 0;
            const std::string nm =
              "__last_index_of_" + std::to_string(last_ctr++);
            const irep_idt rid{qualify_name(nm)};
            if(symbol_table.lookup(rid) == nullptr)
            {
              symbolt rs{rid, string_typet{}, "python"};
              rs.base_name = nm;
              rs.is_lvalue = true;
              rs.is_state_var = true;
              symbol_table.add(rs);
            }
            const symbol_exprt w = symbol_table.lookup_ref(rid).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              w, side_effect_expr_nondett{string_typet{}, get_location(expr)}});
            auto str_len = [&](const exprt &x)
            {
              return native_string_app(
                ID_cprover_string_length_func, {string_typet{}}, {x}, int_t);
            };
            const exprt r = minus_exprt{str_len(w), from_integer(1, int_t)};
            const exprt hay_len = str_len(hay);
            const exprt ndl_len = str_len(needle);
            const exprt zero = from_integer(0, int_t);
            const exprt minus_one_int = from_integer(-1, int_t);
            auto index_of = [&](const exprt &from)
            {
              return native_string_app(
                ID_cprover_string_index_of_func,
                {string_typet{}, string_typet{}, int_t},
                {hay, needle, from},
                int_t);
            };
            // (str.prefixof rather than an equality with str.substr: z3 4.16
            // proved wrong results with the latter.)
            const exprt occurs_at_r = native_string_app(
              ID_cprover_string_startswith_func,
              {string_typet{}, string_typet{}},
              {native_string_app(
                 ID_cprover_string_substring_func,
                 {string_typet{}, int_t, int_t},
                 {hay, r, hay_len},
                 string_typet{}),
               needle},
              bool_typet{});
            const exprt empty_needle = equal_exprt{ndl_len, zero};
            pending_checks.push_back(code_assumet{
              implies_exprt{empty_needle, equal_exprt{r, hay_len}}});
            pending_checks.push_back(code_assumet{implies_exprt{
              not_exprt{empty_needle},
              and_exprt{
                equal_exprt{
                  equal_exprt{r, minus_one_int},
                  equal_exprt{index_of(zero), minus_one_int}},
                implies_exprt{
                  binary_relation_exprt{r, ID_ge, zero},
                  and_exprt{
                    occurs_at_r,
                    // no occurrence from r + 1 on
                    not_exprt{native_string_app(
                      ID_cprover_string_contains_func,
                      {string_typet{}, string_typet{}},
                      {native_string_app(
                         ID_cprover_string_substring_func,
                         {string_typet{}, int_t, int_t},
                         {hay, plus_exprt{r, from_integer(1, int_t)}, hay_len},
                         string_typet{}),
                       needle},
                      bool_typet{})}}}}}});
            pending_checks.push_back(
              code_assumet{binary_relation_exprt{r, ID_le, hay_len}});
            return r;
          };
          auto backward_search = [&](const exprt &hay) -> exprt
          {
            if(native)
              return typecast_exprt{native_backward_search(hay), int_type};
            return intrinsic(
              ID_cprover_string_last_index_of_func, {as_str_struct(hay), ndl});
          };

          exprt found;
          if(is_forward && !end.has_value())
          {
            // Python's start: a negative one counts from the end (and is at
            // least 0), and beyond the end nothing is found, not even an
            // empty substring. index_of searches from a start in [0, len]
            // (Java's semantics; SMT-LIB's for native strings).
            const exprt len = typecast_exprt::conditional_cast(
              native_or_member_string_length(obj), int_type);
            const exprt from =
              python_adjusted_indices(len, start, std::nullopt).first;
            found = forward_search(obj, from);
            if(start.has_value())
              found = if_exprt{
                binary_relation_exprt{from, ID_gt, len}, minus_one, found};
          }
          else if(!start.has_value() && !end.has_value())
            found = backward_search(obj);
          else
          {
            // s[start:end], and nothing found when it is shorter than the
            // needle (as CPython checks first, also for an empty needle).
            // Native strings compute this in mathematical integers, which
            // cvc5 solves far better than bit-vector positions.
            const typet pos_type =
              native ? typet{integer_typet{}} : typet{int_type};
            auto length_of = [&](const exprt &x) -> exprt
            {
              if(native)
                return native_string_app(
                  ID_cprover_string_length_func,
                  {string_typet{}},
                  {x},
                  integer_typet{});
              return typecast_exprt::conditional_cast(
                native_or_member_string_length(x), int_type);
            };
            // Native positions and bounds as fresh integers: the nested
            // conversions and conditionals, repeated in every string term,
            // made z3 4.16 prove wrong results and cvc5 slow.
            auto fresh_int = [&](const exprt &value) -> exprt
            {
              static unsigned pos_ctr = 0;
              const std::string nm =
                "__search_position_" + std::to_string(pos_ctr++);
              const irep_idt id{qualify_name(nm)};
              if(symbol_table.lookup(id) == nullptr)
              {
                symbolt sy{id, integer_typet{}, "python"};
                sy.base_name = nm;
                sy.is_lvalue = true;
                sy.is_state_var = true;
                symbol_table.add(sy);
              }
              const symbol_exprt v = symbol_table.lookup_ref(id).symbol_expr();
              pending_checks.push_back(code_frontend_assignt{
                v,
                side_effect_expr_nondett{integer_typet{}, get_location(expr)}});
              pending_checks.push_back(code_assumet{equal_exprt{v, value}});
              return v;
            };
            const exprt len = length_of(obj);
            auto as_int =
              [&](const std::optional<exprt> &p) -> std::optional<exprt>
            {
              if(!native || !p.has_value())
                return p;
              return fresh_int(typecast_exprt{*p, integer_typet{}});
            };
            const auto [start_n, end_n] =
              python_adjusted_indices(len, as_int(start), as_int(end));
            exprt in_slice;
            if(native)
            {
              const exprt lo = fresh_int(if_exprt{
                binary_relation_exprt{start_n, ID_gt, len}, len, start_n});
              const exprt hi = fresh_int(
                if_exprt{binary_relation_exprt{end_n, ID_lt, lo}, lo, end_n});
              const exprt slice = native_string_app(
                ID_cprover_string_substring_func,
                {string_typet{}, integer_typet{}, integer_typet{}},
                {obj, lo, minus_exprt{hi, lo}},
                string_typet{});
              in_slice =
                is_forward
                  ? native_string_app(
                      ID_cprover_string_index_of_func,
                      {string_typet{}, string_typet{}, integer_typet{}},
                      {slice, needle, from_integer(0, integer_typet{})},
                      integer_typet{})
                  : native_backward_search(slice);
            }
            else
            {
              const exprt slice = python_string_slice(obj, start, end);
              in_slice = is_forward
                           ? forward_search(slice, from_integer(0, int_type))
                           : backward_search(slice);
            }
            const exprt not_found = from_integer(-1, pos_type);
            found = typecast_exprt::conditional_cast(
              if_exprt{
                binary_relation_exprt{
                  minus_exprt{end_n, start_n}, ID_lt, length_of(needle)},
                not_found,
                if_exprt{
                  equal_exprt{in_slice, not_found},
                  not_found,
                  plus_exprt{in_slice, start_n}}},
              int_type);
          }

          static unsigned iof_ctr = 0;
          const std::string rnm = "__index_of_" + std::to_string(iof_ctr++);
          const irep_idt rid{"python::" + rnm};
          if(symbol_table.lookup(rid) == nullptr)
          {
            symbolt rs{rid, int_type, "python"};
            rs.base_name = rnm;
            rs.is_lvalue = true;
            rs.is_state_var = true;
            symbol_table.add(rs);
          }
          const symbol_exprt res = symbol_table.lookup_ref(rid).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{res, found});
          if(method_name == "index" || method_name == "rindex")
          {
            // str.index/str.rindex raise ValueError when the substring is
            // absent (index_of/last_index_of return -1).
            emit_conditional_exception(
              equal_exprt{res, minus_one}, "ValueError");
          }
          return safe_typecast(res, python_int_type());
        }
      }
    }
    // Constrained nondet for symbolic find/count
    {
      // The arguments are still evaluated, for their exceptions.
      for(std::size_t i = 0; i < converted.size(); ++i)
        arg_at(i);
      static unsigned find_ctr = 0;
      std::string tn = "__find_" + std::to_string(find_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_int_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        tv, side_effect_expr_nondett{python_int_type(), get_location(expr)}});
      // Native-safe length: smt_string has no "length" member, so use the
      // representation-neutral helper (str.len under native).
      // Bounds live in the RESULT's Python-int domain (tv is a
      // Python-visible value): lift the length into it
      // (i64 -> integer is exact under --python-unbounded-ints).
      exprt slen = native_or_member_string_length(obj);
      if(slen.type() != signedbv_typet{64} && slen.type() != tv.type())
        slen = safe_typecast(slen, signedbv_typet{64});
      slen = python_lift_to_index_domain(std::move(slen), tv.type());
      // count('') is len + 1, and find('', start) may be len.
      if(method_name == "count")
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{tv, ID_ge, from_integer(0, python_int_type())},
          binary_relation_exprt{
            tv, ID_le, plus_exprt{slen, from_integer(1, slen.type())}}}});
      else // find, rfind, index, rindex
      {
        pending_checks.push_back(code_assumet{and_exprt{
          binary_relation_exprt{tv, ID_ge, from_integer(-1, python_int_type())},
          binary_relation_exprt{tv, ID_le, slen}}});
        // index/rindex raise ValueError where find/rfind return -1.
        if(method_name == "index" || method_name == "rindex")
          emit_conditional_exception(
            equal_exprt{tv, from_integer(-1, python_int_type())}, "ValueError");
      }
      return std::move(tv);
    }
  }
  if(method_name == "join")
  {
    // sep.join(lst) — for constant sep and list of constant strings
    if(args.is_array() && !as_array(args).empty())
    {
      exprt list_arg = convert_expression(*as_array(args).begin());
      auto sep_val = extract_string_value(obj);
      if(is_python_list_type(list_arg.type()))
      {
        // Look up list literal for symbols
        const exprt *list_val = &list_arg;
        if(list_arg.id() == ID_symbol)
        {
          auto it =
            list_literals.find(to_symbol_expr(list_arg).get_identifier());
          if(it != list_literals.end())
            list_val = &it->second;
        }
        const auto &list_st = to_struct_type(list_arg.type());
        to_array_type(list_st.components()[1].type());
        // An empty list literal joins to "" (its element type says nothing).
        if(
          list_val->id() == ID_struct && list_val->operands().size() >= 2 &&
          list_val->operands()[0].is_zero())
        {
          return python_string_literal("");
        }
        // PLR §4.7.3: str.join requires every element to be a str. A concretely
        // non-str element type raises TypeError ('sequence item: expected str').
        // python_value (Any) is not flagged -- a violation cannot be proven.
        {
          const typet &jet =
            to_array_type(list_st.components()[1].type()).element_type();
          if(!is_python_string_type(jet) && !is_python_value_type(jet))
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            return side_effect_expr_nondett{
              python_string_type(), get_location(expr)};
          }
        }
        if(
          list_val->id() == ID_struct && list_val->operands().size() >= 2 &&
          list_val->operands()[0].is_constant())
        {
          mp_integer len;
          if(!to_integer(to_constant_expr(list_val->operands()[0]), len))
          {
            std::string result;
            bool all_const = sep_val.has_value();
            const exprt &data_arr = list_val->operands()[1];
            for(mp_integer i = 0; i < len && all_const; ++i)
            {
              auto idx = i.to_ulong();
              if(idx < data_arr.operands().size())
              {
                auto sv = extract_string_value(data_arr.operands()[idx]);
                if(sv.has_value())
                {
                  if(i > 0)
                    result += sep_val.value();
                  result += sv.value();
                }
                else
                  all_const = false;
              }
            }
            if(all_const)
              return python_string_literal(result);
            // A list literal of strings: the concatenation of its elements
            // separated by the (possibly symbolic) separator. (Only for a
            // literal argument: a named list may have changed since.)
            const bool all_strings =
              list_val == &list_arg && data_arr.id() == ID_array &&
              len <= data_arr.operands().size() &&
              std::all_of(
                data_arr.operands().begin(),
                data_arr.operands().begin() + len.to_long(),
                [&](const exprt &e)
                {
                  return is_python_string_type(e.type()) &&
                         e.type().id() == obj.type().id();
                });
            if(all_strings && is_python_string_type(obj.type()))
            {
              if(len == 0)
                return python_string_literal("");
              exprt joined = data_arr.operands()[0];
              for(std::size_t i = 1; i < len.to_ulong(); ++i)
                joined = string_concat(
                  string_concat(joined, obj), data_arr.operands()[i]);
              return joined;
            }
          }
        }
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  if(method_name == "partition" || method_name == "rpartition")
  {
    // Constant-string-receiver constant-fold: produce the
    // 3-tuple (head, sep_or_empty, tail) per Python's
    // str.partition / str.rpartition semantics.
    auto sv = extract_string_value(obj);
    if(sv.has_value() && args.is_array() && !as_array(args).empty())
    {
      exprt sep_expr = convert_expression(*as_array(args).begin());
      auto sep_v = extract_string_value(sep_expr);
      if(sep_v.has_value() && !sep_v->empty())
      {
        const std::string &s = sv.value();
        const std::string &sep = sep_v.value();
        std::string head, mid, tail;
        std::size_t pos =
          (method_name == "partition") ? s.find(sep) : s.rfind(sep);
        if(pos == std::string::npos)
        {
          // Not found: per Python, partition returns
          // (whole, "", "") and rpartition returns
          // ("", "", whole).
          if(method_name == "partition")
          {
            head = s;
            mid = "";
            tail = "";
          }
          else
          {
            head = "";
            mid = "";
            tail = s;
          }
        }
        else
        {
          head = s.substr(0, pos);
          mid = sep;
          tail = s.substr(pos + sep.size());
        }
        // Build a python_tuple (struct with three string
        // components).
        struct_typet ttype = python_tuple_type(
          {python_string_type(), python_string_type(), python_string_type()});
        return struct_exprt{
          {python_string_literal(head),
           python_string_literal(mid),
           python_string_literal(tail)},
          ttype};
      }
    }
    // Non-constant fallback: nondet 3-tuple.
    struct_typet ttype = python_tuple_type(
      {python_string_type(), python_string_type(), python_string_type()});
    return side_effect_expr_nondett{ttype, get_location(expr)};
  }

  return std::nullopt;
}
