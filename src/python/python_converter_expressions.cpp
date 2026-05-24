/// Python to GOTO converter — IfExp (PLR §6.13),
/// Subscript (PLR §6.2.4), Tuple/List display
/// (PLR §6.2.5), Attribute (PLR §6.2.3), Dict display
/// (PLR §6.2.7) implementation.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cmath>

// PLR §6.13: Conditional expressions
// "x if C else y — first C is evaluated; if true, x is evaluated; else y."
exprt python_convertert::convert_if_exp(const jsont &expr)
{
  exprt test = convert_expression(json_member(expr, "test"));
  exprt body = convert_expression(json_member(expr, "body"));
  exprt orelse = convert_expression(json_member(expr, "orelse"));

  if(test.is_nil() || body.is_nil() || orelse.is_nil())
    return nil_exprt{};

  // Ensure both branches have the same type
  if(body.type() != orelse.type())
    orelse = safe_typecast(orelse, body.type());

  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  // PLR §6.13: if safe_typecast could not unify the branches
  // (e.g. list vs int), a raw if_exprt would violate CBMC's
  // goto_symex_state invariant that both branches share a
  // single type. Return a nondet of body.type() so the symex
  // graph stays well-typed; the caller's reasoning continues
  // with an over-approximation (sound — both branches are
  // possible at runtime in Python).
  if(body.type() != orelse.type())
  {
    log_overapprox(
      "IfExp branches have incompatible types — returning nondet");
    return side_effect_expr_nondett{body.type(), get_location(expr)};
  }

  return if_exprt{test, body, orelse};
}

// PLR §6.3.2: Subscriptions
// "The primary must evaluate to an object that supports subscription."
exprt python_convertert::convert_subscript(const jsont &expr)
{
  exprt value = convert_expression(json_member(expr, "value"));

  if(value.is_nil())
    return nil_exprt{};

  // PLR §3.3.1: custom __getitem__ dunder on class
  // instances. 'obj[key]' dispatches to
  // obj.__getitem__(key).
  {
    std::string tag;
    if(value.type().id() == ID_struct)
      tag = id2string(to_struct_type(value.type()).get_tag());
    else if(value.type().id() == ID_struct_tag)
      tag = id2string(to_struct_tag_type(value.type()).get_identifier());
    if(tag.substr(0, 13) == "python_class_")
    {
      std::string bare = tag.substr(13);
      for(const std::string &prefix :
          {std::string{"python::"} + tag + "::__getitem__",
           std::string{"python::"} + bare + "::__getitem__"})
      {
        const symbolt *gs = symbol_table.lookup(irep_idt{prefix});
        if(gs != nullptr)
        {
          exprt slice = convert_expression(json_member(expr, "slice"));
          if(slice.is_nil())
            return nil_exprt{};
          typet return_type = python_int_type();
          if(gs->type.id() == ID_code)
            return_type = to_code_type(gs->type).return_type();
          return side_effect_expr_function_callt{
            gs->symbol_expr(),
            {address_of_exprt{value}, slice},
            return_type,
            get_location(expr)};
        }
      }
    }
  }

  // Dict subscript: d["key"] → scan keys array for match
  if(is_python_dict_type(value.type()))
  {
    exprt slice = convert_expression(json_member(expr, "slice"));
    if(!slice.is_nil())
    {
      // Constant-key optimization: resolve at conversion time
      auto key_str = extract_string_value(slice);
      if(key_str.has_value())
      {
        const exprt *dict_val = nullptr;
        if(value.id() == ID_struct)
          dict_val = &value;
        else if(value.id() == ID_symbol)
        {
          auto it = dict_literals.find(to_symbol_expr(value).get_identifier());
          if(it != dict_literals.end())
            dict_val = &it->second;
        }
        if(
          dict_val != nullptr && dict_val->operands().size() >= 3 &&
          dict_val->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(dict_val->operands()[0]), len_val))
          {
            const exprt &keys_arr = dict_val->operands()[1];
            const exprt &vals_arr = dict_val->operands()[2];
            for(mp_integer i = 0; i < len_val; ++i)
            {
              auto idx = i.to_ulong();
              if(idx < keys_arr.operands().size())
              {
                auto kv = extract_string_value(keys_arr.operands()[idx]);
                if(kv.has_value() && kv.value() == key_str.value())
                  return vals_arr.operands()[idx];
              }
            }
          }
        }
      }

      const auto &dict_st = to_struct_type(value.type());
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      member_exprt length{value, "length", signedbv_typet{64}};
      member_exprt keys{value, "keys", keys_type};
      member_exprt vals{value, "values", vals_type};

      // PLR §6.10.1: when the key type is python_string, struct
      // equality compares the data POINTERS, which differ between
      // a literal "0" and a runtime-constructed str(0) even when
      // the contents match. Use the string solver for content
      // equality so dict comprehensions like {str(i): v for ...}
      // can be looked up via d["0"].
      bool keys_are_strings = is_python_string_type(keys_type.element_type());

      // Scan: result = values[i] where keys[i] == slice
      exprt result =
        safe_zero(vals_type.element_type()); // default if not found
      exprt found = false_exprt{};
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt key_i = index_exprt{keys, idx};
        if(key_i.type() != slice.type())
          key_i = safe_typecast(key_i, slice.type());
        exprt match;
        if(keys_are_strings && is_python_string_type(slice.type()))
        {
          match = emit_string_bool_function(
            ID_cprover_string_equal_func,
            key_i,
            slice,
            symbol_table,
            pending_checks);
          // emit_string_bool_function returns c_bool — coerce
          if(match.type() != bool_typet{})
            match = typecast_exprt{std::move(match), bool_typet{}};
        }
        else
        {
          match = equal_exprt{key_i, slice};
        }
        exprt cond = and_exprt{in_range, match};
        result = if_exprt{cond, index_exprt{vals, idx}, result};
        found = or_exprt{found, cond};
      }
      // KeyError if key not found — unless the dict has a
      // guaranteed-present key matching this slice (from the
      // 'if K not in D: D[K] = ...' idiom tracked earlier).
      bool skip_key_check = false;
      if(value.id() == ID_symbol)
      {
        irep_idt dict_id = to_symbol_expr(value).get_identifier();
        auto gki = dict_guaranteed_keys.find(dict_id);
        if(gki != dict_guaranteed_keys.end())
        {
          // Build structural key of the slice AST for comparison.
          const jsont &slice_ast = json_member(expr, "slice");
          std::string slice_key;
          if(is_node_type(slice_ast, "Name"))
            slice_key = "Name:" + json_string(json_member(slice_ast, "id"));
          else if(is_node_type(slice_ast, "Constant"))
          {
            const jsont &cv = json_member(slice_ast, "value");
            if(cv.is_string())
              slice_key = "Const:" + cv.value;
          }
          if(!slice_key.empty() && gki->second.count(slice_key) > 0)
            skip_key_check = true;
        }
      }
      if(!skip_key_check)
      {
        add_check(
          found,
          "exception",
          "KeyError: key not found in dict",
          get_location(expr));
      }
      return result;
    }
  }

  // List/string slicing: lst[1:4] or s[::-1]
  const jsont &slice_json = json_member(expr, "slice");
  if(
    is_node_type(slice_json, "Slice") &&
    (is_python_list_type(value.type()) || is_python_string_type(value.type())))
  {
    const jsont &lower_json = json_member(slice_json, "lower");
    const jsont &upper_json = json_member(slice_json, "upper");
    const jsont &step_json = json_member(slice_json, "step");

    member_exprt length{value, "length", signedbv_typet{64}};

    // Check for step=-1 (reverse)
    bool is_reverse = false;
    if(!step_json.is_null())
    {
      exprt step = convert_expression(step_json);
      if(step.is_constant())
      {
        mp_integer step_val;
        if(!to_integer(to_constant_expr(step), step_val) && step_val == -1)
          is_reverse = true;
      }
    }

    // Constant-string optimization for slicing
    if(is_python_string_type(value.type()))
    {
      auto sv = extract_string_value(value);
      if(!sv.has_value() && value.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(value).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
      {
        std::string s = sv.value();
        int len = static_cast<int>(s.size());
        if(is_reverse)
        {
          std::string rev(s.rbegin(), s.rend());
          return python_string_literal(rev);
        }
        else
        {
          int lo =
            lower_json.is_null()
              ? 0
              : static_cast<int>(
                  try_eval_double(convert_expression(lower_json)).value_or(0));
          int hi =
            upper_json.is_null()
              ? len
              : static_cast<int>(try_eval_double(convert_expression(upper_json))
                                   .value_or(len));
          if(lo < 0)
            lo += len;
          if(hi < 0)
            hi += len;
          if(lo < 0)
            lo = 0;
          if(hi > len)
            hi = len;
          if(lo >= hi)
            return python_string_literal("");
          return python_string_literal(s.substr(lo, hi - lo));
        }
      }
      // Non-constant string: return nondet
      return side_effect_expr_nondett{python_string_type(), source_locationt{}};
    }

    const auto &st = to_struct_type(value.type());
    const auto &data_type = to_array_type(st.components()[1].type());
    typet elem_type = data_type.element_type();
    member_exprt src_data{value, "data", data_type};

    exprt lower, upper;
    if(is_reverse)
    {
      lower = from_integer(0, signedbv_typet{64});
      upper = length;
    }
    else
    {
      lower = lower_json.is_null() ? from_integer(0, signedbv_typet{64})
                                   : convert_expression(lower_json);
      upper = upper_json.is_null() ? length : convert_expression(upper_json);
    }

    exprt new_length =
      is_reverse ? exprt{length} : exprt{minus_exprt{upper, lower}};

    // Determine result type (same as source: list or string)
    std::size_t max_len = is_python_string_type(value.type())
                            ? PYTHON_MAX_STRING_LENGTH
                            : PYTHON_MAX_LIST_LENGTH;
    array_typet result_data_type{
      elem_type, from_integer(max_len, signedbv_typet{64})};

    exprt::operandst result_elems;
    for(std::size_t i = 0; i < max_len; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt src_elem;
      if(is_reverse)
      {
        // result[i] = src[length - 1 - i]
        src_elem = index_exprt{
          src_data,
          minus_exprt{
            minus_exprt{length, from_integer(1, signedbv_typet{64})}, idx}};
      }
      else
      {
        src_elem = index_exprt{src_data, plus_exprt{lower, idx}};
      }
      // PLR §6.10.1: positions beyond new_length must be zero so
      // struct-equality with a list literal (whose trailing slots
      // are zeros) works. Without this, `[1,2,3,4,5][1:4] == [2,3,4]`
      // fails because the slice reads xs[4]=5 into result.data[3]
      // while the literal has 0 there.
      exprt in_slice = binary_relation_exprt{idx, ID_lt, new_length};
      result_elems.push_back(
        if_exprt{in_slice, src_elem, safe_zero(elem_type)});
    }

    array_exprt result_data{std::move(result_elems), result_data_type};
    return struct_exprt{{new_length, result_data}, st};
  }

  exprt slice = convert_expression(json_member(expr, "slice"));

  if(value.is_nil() || slice.is_nil())
    return nil_exprt{};

  // String indexing: s[i] → s.data[i] as a single-char string struct
  if(is_python_string_type(value.type()))
  {
    // slice should be an integer index. If it isn't, the user
    // wrote something like `s["x"]` (a runtime TypeError in
    // Python) or our type tracking lost precision elsewhere.
    // Either way we should not produce a malformed if_exprt
    // here: nondet a sound python_string and emit a TypeError
    // property if the slice's static type is concretely not
    // an integer.
    bool slice_is_int = slice.type().id() == ID_signedbv ||
                        slice.type().id() == ID_unsignedbv ||
                        slice.type().id() == ID_integer ||
                        slice.type().id() == ID_bool;
    if(!slice_is_int)
    {
      log_overapprox(
        "string subscript with non-integer index — emitting type-error "
        "property and returning nondet python_string");
      source_locationt tloc = get_location(expr);
      tloc.set_property_class("type-error");
      tloc.set_comment("string indices must be integers");
      code_assertt te{false_exprt{}};
      te.add_source_location() = tloc;
      code_blockt te_block;
      te_block.add(std::move(te));
      pending_checks.push_back(std::move(te_block));
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }
    member_exprt length{value, "length", python_int_type()};
    // PLR §6.3.3: negative indices count from the end
    exprt adjusted_idx = if_exprt{
      binary_relation_exprt{slice, ID_lt, from_integer(0, slice.type())},
      plus_exprt{length, slice},
      slice};
    // Path-sensitive elision: if we're inside an `if s:` body
    // (string_min_lengths records `s` has at least 1 char) and
    // the index is a non-negative constant smaller than that
    // bound, the IndexError check is trivially safe.
    bool elide_str_idx_check = false;
    if(
      slice.is_constant() && value.id() == ID_symbol &&
      string_min_lengths.count(to_symbol_expr(value).get_identifier()) > 0)
    {
      mp_integer idx_val;
      if(!to_integer(to_constant_expr(slice), idx_val) && idx_val >= 0)
      {
        const mp_integer &min_len =
          string_min_lengths.at(to_symbol_expr(value).get_identifier());
        if(idx_val < min_len)
          elide_str_idx_check = true;
      }
    }
    if(!elide_str_idx_check)
      add_check(
        and_exprt{
          binary_relation_exprt{adjusted_idx, ID_ge, safe_zero(slice.type())},
          binary_relation_exprt{adjusted_idx, ID_lt, length}},
        "index-out-of-bounds",
        "string index out of range",
        get_location(expr));
    // Constant-string optimization for indexing
    {
      auto sv = extract_string_value(value);
      if(!sv.has_value() && value.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(value).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
      {
        auto nv = try_eval_double(adjusted_idx);
        if(nv.has_value())
        {
          int i = static_cast<int>(nv.value());
          int len = static_cast<int>(sv.value().size());
          if(i >= 0 && i < len)
            return python_string_literal(std::string(1, sv.value()[i]));
        }
      }
    }
    // Non-constant indexing: read char via pointer arithmetic
    {
      member_exprt data_ptr(
        value, "data", pointer_typet(unsignedbv_typet{8}, 64));
      dereference_exprt ch(plus_exprt(data_ptr, adjusted_idx));
      // Build a single-char string from the character
      exprt::operandst chars;
      chars.push_back(ch);
      array_typet at(unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
      array_exprt arr(std::move(chars), at);
      exprt ptr = address_of_exprt(index_exprt(
        arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      exprt len = from_integer(1, signedbv_typet{64});
      return struct_exprt({len, ptr}, python_string_type());
    }
    typet str_type = python_string_type();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));

    member_exprt data{value, "data", data_type};
    index_exprt char_val{data, adjusted_idx};

    // Build a single-character string struct
    exprt::operandst chars;
    chars.push_back(char_val);
    while(chars.size() < PYTHON_MAX_STRING_LENGTH)
      chars.push_back(from_integer(0, unsignedbv_typet{8}));

    array_exprt data_expr{std::move(chars), data_type};
    exprt length_expr = from_integer(1, python_int_type());

    struct_exprt result{{length_expr, data_expr}, str_type};
    return std::move(result);
  }

  // Array/list indexing
  if(is_python_list_type(value.type()))
  {
    // PLR §6.3.2: Non-integer index → TypeError
    if(
      slice.type().id() != ID_signedbv && slice.type().id() != ID_unsignedbv &&
      slice.type().id() != ID_integer && slice.type().id() != ID_bool)
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    member_exprt length{value, "length", python_int_type()};

    // PLR §6.3.2: Subscriptions — negative indices count from the end.
    // Handle negative indices: lst[-1] → lst[len-1]
    exprt effective_idx = slice;
    if(slice.is_constant())
    {
      mp_integer idx_val;
      if(!to_integer(to_constant_expr(slice), idx_val) && idx_val < 0)
        effective_idx = plus_exprt{length, slice};
    }
    else
    {
      // Runtime: if idx < 0 then idx + length else idx
      effective_idx = if_exprt{
        binary_relation_exprt{slice, ID_lt, safe_zero(slice.type())},
        plus_exprt{length, slice},
        slice};
    }

    // Path-sensitive list-length idiom: when we're inside a
    // short-circuiting 'and' whose first operand asserts
    // len(L) >= N (or one of the equivalent forms recognised
    // in convert_bool_op), and the subscript index is a
    // non-negative constant < N, the IndexError check is
    // trivially safe and we elide it. Without this, the check
    // would fire unconditionally at conversion time, before
    // the and-test's path constraint reaches the solver.
    bool elide_idx_check = false;
    if(
      slice.is_constant() && value.id() == ID_symbol &&
      list_min_lengths.count(to_symbol_expr(value).get_identifier()) > 0)
    {
      mp_integer idx_val;
      if(!to_integer(to_constant_expr(slice), idx_val) && idx_val >= 0)
      {
        const mp_integer &min_len =
          list_min_lengths.at(to_symbol_expr(value).get_identifier());
        if(idx_val < min_len)
          elide_idx_check = true;
      }
    }

    if(!elide_idx_check)
      add_check(
        and_exprt{
          binary_relation_exprt{
            effective_idx, ID_ge, safe_zero(effective_idx.type())},
          binary_relation_exprt{effective_idx, ID_lt, length}},
        "index-out-of-bounds",
        "list index out of range",
        get_location(expr));

    const auto &st = to_struct_type(value.type());
    const auto &data_type = to_array_type(st.components()[1].type());
    member_exprt data{value, "data", data_type};
    return index_exprt{data, effective_idx};
  }

  // Tuple indexing with constant index
  if(is_python_tuple_type(value.type()))
  {
    if(slice.is_constant())
    {
      mp_integer idx;
      if(!to_integer(to_constant_expr(slice), idx))
      {
        const auto &st = to_struct_type(value.type());
        std::string field = "_" + integer2string(idx);
        if(st.has_component(field))
          return member_exprt{value, field, st.get_component(field).type()};
      }
    }
    log.error() << "Tuple indexing requires a constant index" << messaget::eom;
    // PLR §6.3.2: with a non-constant index, we can't pick a
    // specific tuple element statically. Returning nil_exprt
    // would propagate as a malformed argument into downstream
    // calls (CBMC SSA's equal_exprt, if_exprt invariants). Use
    // a sound over-approximation: nondet of python_value.
    log_overapprox(
      "tuple subscript with non-constant index — returning nondet "
      "python_value");
    return side_effect_expr_nondett{python_value_type(), get_location(expr)};
  }

  // Tagged union subscript: unwrap to list and index
  if(is_python_value_type(value.type()))
  {
    exprt list_val = python_value_list(value);
    if(is_python_list_type(list_val.type()))
    {
      const auto &list_st = to_struct_type(list_val.type());
      const auto &data_type = to_array_type(list_st.components()[1].type());
      member_exprt data{list_val, "data", data_type};
      return index_exprt{data, slice};
    }
  }

  log_overapprox("Subscript: unsupported operand type, using nondet");
  // Returning nil_exprt would propagate as a malformed argument
  // into downstream function calls (CBMC SSA's equal_exprt,
  // if_exprt invariants). Sound over-approximation: nondet of
  // python_value (the most general Python type).
  return side_effect_expr_nondett{python_value_type(), get_location(expr)};
}

// PLR §6.2.5: List, set and tuple displays
// "A tuple display yields a new tuple object."
exprt python_convertert::convert_tuple(const jsont &expr)
{
  const jsont &elts = json_member(expr, "elts");
  if(!elts.is_array())
    return nil_exprt{};

  exprt::operandst elements;
  std::vector<typet> element_types;
  for(const auto &elt : as_array(elts))
  {
    exprt e = convert_expression(elt);
    if(e.is_nil())
      return nil_exprt{};
    element_types.push_back(e.type());
    elements.push_back(e);
  }

  struct_typet tuple_type = python_tuple_type(element_types);
  return struct_exprt{std::move(elements), tuple_type};
}

// PLR §6.2.5: List displays
// "A list display yields a new list object, the contents being specified
// by either a list of expressions or a comprehension."
exprt python_convertert::convert_list(const jsont &expr)
{
  const jsont &elts = json_member(expr, "elts");
  if(!elts.is_array())
    return nil_exprt{};

  // Collect elements and determine element type from first element
  exprt::operandst elements;
  for(const auto &elt : as_array(elts))
  {
    // PEP 448: iterable unpacking — [*a, 3, 4]. If the
    // starred expression wraps a list literal whose length
    // is known at conversion time, splice its elements
    // directly. Otherwise over-approximate by skipping the
    // starred element (sound — the resulting list is a
    // subset of what runtime would produce).
    if(is_node_type(elt, "Starred"))
    {
      const jsont &inner = json_member(elt, "value");
      exprt inner_expr = convert_expression(inner);
      if(inner_expr.is_nil())
        return nil_exprt{};

      const exprt *lit = nullptr;
      if(inner_expr.id() == ID_struct && inner_expr.operands().size() >= 2)
        lit = &inner_expr;
      else if(inner_expr.id() == ID_symbol)
      {
        auto it =
          list_literals.find(to_symbol_expr(inner_expr).get_identifier());
        if(it != list_literals.end())
          lit = &it->second;
      }
      if(
        lit != nullptr && lit->operands().size() >= 2 &&
        lit->operands()[0].is_constant())
      {
        mp_integer len_val;
        if(!to_integer(to_constant_expr(lit->operands()[0]), len_val))
        {
          const exprt &data_arr = lit->operands()[1];
          std::size_t n = len_val.to_ulong();
          for(std::size_t i = 0; i < n && i < data_arr.operands().size(); i++)
            elements.push_back(data_arr.operands()[i]);
          continue;
        }
      }
      log_overapprox(
        "PEP 448 iterable unpacking of non-literal list — skipping");
      continue;
    }

    // PLR §3.1: if the element is a Name that resolves to an
    // escaped list/dict-typed symbol (i.e. a name that we promoted
    // to `list[python_value]` / `dict[str, python_value]` storage),
    // wrap it as `make_python_value(LIST_or_DICT, &symbol)` —
    // pointing to the original symbol's storage, NOT a struct copy.
    // This makes `outer = [inner]; outer[0][0] = 99` write through
    // to inner.data[0].
    if(is_node_type(elt, "Name"))
    {
      std::string en = json_string(json_member(elt, "id"));
      irep_idt eq{qualify_name(en)};
      if(escaped_mutables.count(eq) > 0)
      {
        const symbolt *esym = symbol_table.lookup(eq);
        if(esym != nullptr)
        {
          if(is_python_list_type(esym->type))
          {
            elements.push_back(make_python_value(
              python_type_tagt::LIST, address_of_exprt{esym->symbol_expr()}));
            continue;
          }
          if(is_python_dict_type(esym->type))
          {
            elements.push_back(make_python_value(
              python_type_tagt::DICT, address_of_exprt{esym->symbol_expr()}));
            continue;
          }
        }
      }
    }

    exprt e = convert_expression(elt);
    if(e.is_nil())
      return nil_exprt{};
    elements.push_back(e);
  }

  if(elements.empty())
  {
    // Empty list — default to int element type
    struct_typet list_type = python_list_type(python_int_type());
    exprt length = from_integer(0, python_int_type());
    array_typet data_type{
      python_int_type(),
      from_integer(PYTHON_MAX_LIST_LENGTH, python_int_type())};
    exprt::operandst zeros;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      zeros.push_back(from_integer(0, python_int_type()));
    array_exprt data{std::move(zeros), data_type};
    return struct_exprt{{length, data}, list_type};
  }

  typet elem_type = elements[0].type();
  // Check for mixed types — use tagged union for heterogeneous lists
  bool is_heterogeneous = false;
  for(const auto &e : elements)
  {
    if(e.type() != elem_type)
    {
      is_heterogeneous = true;
      elem_type = python_value_type();
      break;
    }
  }
  // The backing array has a fixed maximum size (PYTHON_MAX_LIST_LENGTH);
  // however, list literals in stdlib code can exceed it (e.g.
  // typing.py's 100+-entry '__all__'). Grow the array size to the
  // number of elements whenever that exceeds the default so the
  // resulting IR is self-consistent (operands count == array size).
  const std::size_t list_array_size =
    std::max<std::size_t>(PYTHON_MAX_LIST_LENGTH, elements.size());
  struct_typet list_type = python_list_type(elem_type);
  // Rebuild the struct's array-typed 'data' component to match the
  // actual literal size. python_list_type always returns the struct
  // with the default max length, so override the data component's
  // array type here.
  {
    auto &comps = list_type.components();
    if(comps.size() == 2)
      comps[1].type() = array_typet{
        elem_type, from_integer(list_array_size, python_int_type())};
  }
  array_typet data_type{
    elem_type, from_integer(list_array_size, python_int_type())};

  // Build data array: elements followed by zeros
  exprt::operandst data_elems;
  for(auto &e : elements)
  {
    if(is_heterogeneous)
      e = wrap_value(e);
    else if(e.type() != elem_type)
      e = typecast_exprt{e, elem_type};
    data_elems.push_back(e);
  }
  while(data_elems.size() < list_array_size)
    data_elems.push_back(safe_zero(elem_type));

  array_exprt data{std::move(data_elems), data_type};
  exprt length =
    from_integer(static_cast<long long>(elements.size()), python_int_type());

  return struct_exprt{{length, data}, list_type};
}

// PLR §6.3.1: Attribute references
// "An attribute reference is a primary followed by a period and a name."
exprt python_convertert::convert_attribute(const jsont &expr)
{
  std::string attr = json_string(json_member(expr, "attr"));

  // Math module constants: math.pi, math.e, etc.
  // Check BEFORE converting value (which would fail for module names)
  if(
    json_member(expr, "value").is_object() &&
    is_node_type(json_member(expr, "value"), "Name"))
  {
    std::string obj_name =
      json_string(json_member(json_member(expr, "value"), "id"));
    if(obj_name == "math")
    {
      if(attr == "pi")
        return double_to_floatbv(M_PI);
      if(attr == "e")
        return double_to_floatbv(M_E);
      if(attr == "tau")
        return double_to_floatbv(2.0 * M_PI);
      if(attr == "inf")
      {
        ieee_floatt v{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        v.make_plus_infinity();
        return v.to_expr();
      }
      if(attr == "nan")
      {
        ieee_floatt v{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        v.make_NaN();
        return v.to_expr();
      }
    }
  }

  exprt value = convert_expression(json_member(expr, "value"));

  if(value.is_nil())
    return nil_exprt{};

  // If value is a pointer (self in a method), dereference first
  if(value.type().id() == ID_pointer)
  {
    const auto &base = to_pointer_type(value.type()).base_type();
    if(base.id() == ID_struct)
    {
      const auto &st = to_struct_type(base);
      if(st.has_component(attr))
        return member_exprt{
          dereference_exprt{value}, attr, st.get_component(attr).type()};
    }
  }

  // For struct types (classes), access the member directly.
  // PLR §3.3.2: an @property-decorated method is accessed
  // like a field — emit the call with self as sole arg.
  if(value.type().id() == ID_struct)
  {
    const auto &st = to_struct_type(value.type());
    std::string stag = id2string(st.get_tag());
    if(stag.substr(0, 13) == "python_class_")
    {
      std::string cls = stag.substr(13);
      auto pit = class_property_methods.find(cls);
      if(pit != class_property_methods.end() && pit->second.count(attr))
      {
        irep_idt mid{"python::" + cls + "::" + attr};
        const symbolt *msym = symbol_table.lookup(mid);
        if(msym != nullptr && msym->type.id() == ID_code)
        {
          const code_typet &mty = to_code_type(msym->type);
          // Materialise into a tmp so the ASSIGN (via
          // pending_checks) is visible to subsequent
          // statements. A bare side_effect_expr_function_callt
          // return was getting dropped in some assignment
          // paths.
          static unsigned prop_ctr = 0;
          std::string tn = "__prop_" + std::to_string(prop_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, mty.return_type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            symbol_table.add(ts);
          }
          symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            tv,
            side_effect_expr_function_callt{
              msym->symbol_expr(),
              {address_of_exprt{value}},
              mty.return_type(),
              get_location(expr)}});
          return std::move(tv);
        }
      }
    }
    if(st.has_component(attr))
      return member_exprt{value, attr, st.get_component(attr).type()};
  }

  // Attribute accesses on an already-opaque tagged python_value
  // base are the common case for imported module symbols and
  // unannotated parameters. The only information we can return is
  // a fresh nondet, so log at debug level (or warning level if
  // --python-strict-warnings is set) rather than falling through
  // to the louder 'attribute ...' path.
  const bool base_is_module_value =
    (value.type().id() == ID_struct_tag &&
     id2string(to_struct_tag_type(value.type()).get_identifier()) ==
       std::string{PYTHON_VALUE_TAG});
  if(base_is_module_value)
  {
    // If any known user class has this attribute, dereference
    // __class_ptr through its struct type and read the field.
    // We pick the first matching class; when multiple classes
    // share the attribute name the result's type comes from
    // the first. Callers that need precision should narrow
    // with isinstance first.
    for(const auto &[cls_name, cls_type] : class_types)
    {
      if(!cls_type.has_component(attr))
        continue;
      const typet &field_type = cls_type.get_component(attr).type();
      exprt class_ptr = python_value_class_ptr(value);
      pointer_typet cls_ptr_type{cls_type, 64};
      dereference_exprt deref{
        typecast_exprt{class_ptr, cls_ptr_type}, cls_type};
      return member_exprt{std::move(deref), attr, field_type};
    }
    log_overapprox("attribute '" + attr + "': using nondet over-approximation");
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }

  log_overapprox("attribute '" + attr + "': using nondet over-approximation");
  return side_effect_expr_nondett{python_int_type(), source_locationt{}};
}

// PLR §6.2.7: Dictionary displays
// "A dictionary display yields a new dictionary object."
exprt python_convertert::convert_dict(const jsont &expr)
{
  const jsont &keys = json_member(expr, "keys");
  const jsont &values = json_member(expr, "values");

  if(!keys.is_array() || !values.is_array())
    return nil_exprt{};

  // Collect key-value pairs
  std::vector<std::pair<exprt, exprt>> pairs;
  auto key_it = as_array(keys).begin();
  auto val_it = as_array(values).begin();
  for(; key_it != as_array(keys).end(); ++key_it, ++val_it)
  {
    exprt k = convert_expression(*key_it);
    exprt v;
    // PLR §3.1: if the value is a Name resolving to an escaped
    // list/dict-typed symbol, wrap as `make_python_value(LIST/DICT,
    // &symbol)` — pointing to the original storage. Subsequent
    // mutations through this dict's value propagate to the symbol.
    bool wrapped_as_ref = false;
    if(is_node_type(*val_it, "Name"))
    {
      std::string vn = json_string(json_member(*val_it, "id"));
      irep_idt vq{qualify_name(vn)};
      if(escaped_mutables.count(vq) > 0)
      {
        const symbolt *vsym = symbol_table.lookup(vq);
        if(vsym != nullptr)
        {
          if(is_python_list_type(vsym->type))
          {
            v = make_python_value(
              python_type_tagt::LIST, address_of_exprt{vsym->symbol_expr()});
            wrapped_as_ref = true;
          }
          else if(is_python_dict_type(vsym->type))
          {
            v = make_python_value(
              python_type_tagt::DICT, address_of_exprt{vsym->symbol_expr()});
            wrapped_as_ref = true;
          }
        }
      }
    }
    if(!wrapped_as_ref)
      v = convert_expression(*val_it);
    if(k.is_nil() || v.is_nil())
      continue;
    pairs.emplace_back(k, v);
  }

  // Determine key/value types from first pair
  typet key_type = pairs.empty() ? python_string_type() : pairs[0].first.type();
  typet val_type = pairs.empty() ? python_int_type() : pairs[0].second.type();

  // Heterogeneous-value detection: if any later value's type
  // disagrees with val_type, promote val_type to the tagged
  // union (python_value_type) so each value can be wrapped via
  // wrap_value rather than typecast through a smaller struct.
  // The latter triggers CBMC's struct-narrowing typecast that
  // emits `((_ extract H L) <struct-value>)` — invalid SMT-LIB
  // under cvc5's use_datatypes.
  for(std::size_t i = 1; i < pairs.size(); i++)
  {
    if(pairs[i].second.type() != val_type)
    {
      val_type = python_value_type();
      break;
    }
  }
  // Same for keys.
  for(std::size_t i = 1; i < pairs.size(); i++)
  {
    if(pairs[i].first.type() != key_type)
    {
      key_type = python_value_type();
      break;
    }
  }

  struct_typet dict_type = python_dict_type(key_type, val_type);
  const auto &keys_arr_type = to_array_type(dict_type.components()[1].type());
  const auto &vals_arr_type = to_array_type(dict_type.components()[2].type());

  // Build keys array
  exprt::operandst key_elems;
  for(const auto &p : pairs)
  {
    exprt k = p.first;
    if(k.type() != key_type)
      k = is_python_value_type(key_type) ? wrap_value(k)
                                         : safe_typecast(k, key_type);
    key_elems.push_back(k);
  }
  while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
    key_elems.push_back(safe_zero(key_type));

  // Build values array
  exprt::operandst val_elems;
  for(const auto &p : pairs)
  {
    exprt v = p.second;
    if(v.type() != val_type)
      v = is_python_value_type(val_type) ? wrap_value(v)
                                         : safe_typecast(v, val_type);
    val_elems.push_back(v);
  }
  while(val_elems.size() < PYTHON_MAX_DICT_SIZE)
    val_elems.push_back(safe_zero(val_type));

  exprt length =
    from_integer(static_cast<long long>(pairs.size()), signedbv_typet{64});

  return struct_exprt{
    {length,
     array_exprt{std::move(key_elems), keys_arr_type},
     array_exprt{std::move(val_elems), vals_arr_type}},
    dict_type};
}
