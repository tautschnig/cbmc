/// Python to GOTO converter — dict-method dispatch
/// (PLR §6.4.6 dict methods). Handles get / setdefault /
/// pop / popitem / update / clear / keys / values / items /
/// fromkeys / copy / __contains__ on the array-based dict
/// model. Extracted from python_converter_call_method.cpp
/// per doc/python-frontend-architecture.md.
/// Pure source-split — semantics are preserved.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/floatbv_expr.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

std::optional<exprt> python_convertert::try_dict_method(
  const jsont &expr,
  const exprt &obj,
  const typet &obj_base_type,
  const std::string &method_name,
  const jsont &args)
{
  if(method_name == "get")
  {
    // d.get(key, default) — scan keys array
    if(args.is_array() && !as_array(args).empty())
    {
      auto arg_it = as_array(args).begin();
      exprt key_expr = convert_expression(*arg_it);
      const auto &dict_st = to_struct_type(obj_base_type);
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      member_exprt length{obj, "length", signedbv_typet{64}};
      member_exprt keys{obj, "keys", keys_type};
      member_exprt vals{obj, "values", vals_type};

      if(key_expr.type() != keys_type.element_type())
        key_expr = coerce_element(key_expr, keys_type.element_type());

      // Default value: None if not specified, else second arg.
      // The Python-int "None sentinel" is only representable in an
      // integer-typed value column; for bool/float/string/etc. dicts
      // there is no in-band None and we fall back to safe_zero of
      // the element type (e.g. False for bool, 0.0 for float).
      // Without this guard, from_integer aborts on the precondition
      // when the value type cannot hold the sentinel — see
      // regression test cbmc/python-dict-get-bool.
      exprt default_val;
      {
        const typet &elem_t = vals_type.element_type();
        if(
          elem_t.id() == ID_signedbv || elem_t.id() == ID_unsignedbv ||
          elem_t.id() == ID_integer || elem_t.id() == ID_natural)
          default_val =
            from_integer(python_none_sentinel_int(), elem_t);
        else
          default_val = safe_zero(elem_t);
      }
      ++arg_it;
      exprt raw_default = default_val; // default in its ACTUAL type (uncoerced)
      if(arg_it != as_array(args).end())
      {
        raw_default = convert_expression(*arg_it);
        default_val = coerce_element(raw_default, vals_type.element_type());
      }
      // PLR §6.4.6: get(k, default) returns d[k] if k is present, else `default`
      // -- so the result type is value_type | type(default). When the default's
      // type DIFFERS from the dict's value-element type, coercing it to the
      // value type (e.g. "s" -> int) loses the default's real type and
      // false-proves a later use (005: `{}.get(k, "s") + 1` missed the str
      // TypeError). Keep the default's actual type in that case.
      const bool default_differs =
        !raw_default.is_nil() &&
        raw_default.type() != vals_type.element_type() &&
        !is_python_value_type(vals_type.element_type());

      // Constant-key fast path: same logic as subscript read.
      // When the dict value is a literal struct or a tracked
      // dict_literal symbol, and the key is a string constant,
      // resolve at conversion time.
      auto key_str = extract_string_value(key_expr);
      if(key_str.has_value())
      {
        const exprt *dict_val = nullptr;
        if(obj.id() == ID_struct)
          dict_val = &obj;
        else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
        {
          auto it = dict_literals.find(
            obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                                  : irep_idt{});
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
            // Key not present in the literal: the result is exactly the
            // default, in its own type (PLR §6.4.6) -- not coerced to the
            // dict's value type.
            return raw_default;
          }
        }
      }

      // Symbolic scan: result = (key present ? d[key] : default). When the
      // default's type differs from the value type, the result is the union
      // value_type | type(default): model it as a python_value whose tag is the
      // matched value's when present and the default's when absent, so a later
      // type-restricting use observes the absent-key (default) possibility
      // (PLR-sound -- the absent case is real for a non-constant key).
      if(default_differs)
      {
        exprt result = wrap_value(raw_default);
        for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          result = if_exprt{
            dict_slot_match(
              keys, length, static_cast<std::size_t>(i), key_expr),
            wrap_value(index_exprt{vals, idx}),
            result};
        }
        return result;
      }
      exprt result = default_val;
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        result = if_exprt{
          dict_slot_match(keys, length, static_cast<std::size_t>(i), key_expr),
          index_exprt{vals, idx},
          result};
      }
      return result;
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  if(method_name == "keys")
  {
    // Constant fast path: when obj is a literal struct
    // or a tracked dict_literal symbol, build the keys
    // list directly from the stored operands — no
    // member_exprt indirection.
    const exprt *dict_val = nullptr;
    if(obj.id() == ID_struct)
      dict_val = &obj;
    else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      auto it = dict_literals.find(
        obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                              : irep_idt{});
      if(it != dict_literals.end())
        dict_val = &it->second;
    }
    const auto &dict_st = to_struct_type(obj_base_type);
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    typet key_type = python_dict_logical_key_type(keys_type.element_type());
    struct_typet list_type = python_list_type(key_type);
    const auto &list_data_type =
      to_array_type(list_type.components()[1].type());
    if(
      dict_val != nullptr && dict_val->operands().size() >= 3 &&
      dict_val->operands()[0].is_constant())
    {
      exprt::operandst elems;
      for(const auto &k : dict_val->operands()[1].operands())
        elems.push_back(python_dict_unbox_key(k));
      while(elems.size() < PYTHON_MAX_LIST_LENGTH)
        elems.push_back(safe_zero(key_type));
      return struct_exprt{
        {dict_val->operands()[0],
         array_exprt{std::move(elems), list_data_type}},
        list_type};
    }
    // d.keys() → list of d.keys[0..d.length-1]
    member_exprt length{obj, "length", signedbv_typet{64}};
    member_exprt keys{obj, "keys", keys_type};
    exprt::operandst elems;
    for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
      elems.push_back(python_dict_unbox_key(
        index_exprt{keys, from_integer(i, signedbv_typet{64})}));
    while(elems.size() < PYTHON_MAX_LIST_LENGTH)
      elems.push_back(safe_zero(key_type));
    return struct_exprt{
      {length, array_exprt{std::move(elems), list_data_type}}, list_type};
  }
  if(method_name == "values")
  {
    const exprt *dict_val = nullptr;
    if(obj.id() == ID_struct)
      dict_val = &obj;
    else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      auto it = dict_literals.find(
        obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                              : irep_idt{});
      if(it != dict_literals.end())
        dict_val = &it->second;
    }
    const auto &dict_st = to_struct_type(obj_base_type);
    const auto &vals_type = to_array_type(dict_st.components()[2].type());
    typet val_type = vals_type.element_type();
    struct_typet list_type = python_list_type(val_type);
    const auto &list_data_type =
      to_array_type(list_type.components()[1].type());
    if(
      dict_val != nullptr && dict_val->operands().size() >= 3 &&
      dict_val->operands()[0].is_constant())
    {
      exprt::operandst elems;
      for(const auto &v : dict_val->operands()[2].operands())
        elems.push_back(v);
      while(elems.size() < PYTHON_MAX_LIST_LENGTH)
        elems.push_back(safe_zero(val_type));
      return struct_exprt{
        {dict_val->operands()[0],
         array_exprt{std::move(elems), list_data_type}},
        list_type};
    }
    member_exprt length{obj, "length", signedbv_typet{64}};
    member_exprt vals{obj, "values", vals_type};
    exprt::operandst elems;
    for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
      elems.push_back(index_exprt{vals, from_integer(i, signedbv_typet{64})});
    while(elems.size() < PYTHON_MAX_LIST_LENGTH)
      elems.push_back(safe_zero(val_type));
    return struct_exprt{
      {length, array_exprt{std::move(elems), list_data_type}}, list_type};
  }
  if(method_name == "items")
  {
    // PLR dict.items(): view of (key, value) pairs. We
    // materialise as a python_list of python_tuple(key,
    // value) — precise for dict literals, falls through
    // to nondet otherwise.
    const exprt *dict_val = nullptr;
    if(obj.id() == ID_struct)
      dict_val = &obj;
    else if((obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      auto it = dict_literals.find(
        obj.id() == ID_symbol ? to_symbol_expr(obj).get_identifier()
                              : irep_idt{});
      if(it != dict_literals.end())
        dict_val = &it->second;
    }
    if(
      dict_val != nullptr && dict_val->operands().size() >= 3 &&
      dict_val->operands()[0].is_constant())
    {
      mp_integer lv;
      if(!to_integer(to_constant_expr(dict_val->operands()[0]), lv))
      {
        const auto &dict_st = to_struct_type(obj_base_type);
        const auto &keys_type = to_array_type(dict_st.components()[1].type());
        const auto &vals_type = to_array_type(dict_st.components()[2].type());
        const typet key_t = keys_type.element_type();
        const typet val_t = vals_type.element_type();
        struct_typet tuple_t = python_tuple_type({key_t, val_t});
        tuple_t.set_tag("python_tuple");
        struct_typet list_t = python_list_type(tuple_t);
        const auto &list_data_type =
          to_array_type(list_t.components()[1].type());
        const exprt &src_keys = dict_val->operands()[1];
        const exprt &src_vals = dict_val->operands()[2];
        exprt::operandst elems;
        for(mp_integer i = 0; i < lv; ++i)
        {
          auto idx = i.to_ulong();
          if(
            idx >= src_keys.operands().size() ||
            idx >= src_vals.operands().size())
            break;
          elems.push_back(struct_exprt{
            {src_keys.operands()[idx], src_vals.operands()[idx]}, tuple_t});
        }
        while(elems.size() < PYTHON_MAX_LIST_LENGTH)
          elems.push_back(safe_zero(tuple_t));
        return struct_exprt{
          {dict_val->operands()[0],
           array_exprt{std::move(elems), list_data_type}},
          list_t};
      }
    }
    // Returns list of tuples — for non-literal dicts,
    // construct a list whose i-th element is the tuple
    // (obj.keys[i], obj.values[i]). The for-loop walks
    // the resulting list using its own counter, so
    // unwinds naturally bound by the dict's actual length.
    {
      const auto &dict_st = to_struct_type(obj_base_type);
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      const typet key_t =
        python_dict_logical_key_type(keys_type.element_type());
      const typet val_t = vals_type.element_type();
      struct_typet tuple_t = python_tuple_type({key_t, val_t});
      tuple_t.set_tag("python_tuple");
      struct_typet list_t = python_list_type(tuple_t);
      const auto &list_data_type = to_array_type(list_t.components()[1].type());

      member_exprt obj_keys{obj, "keys", keys_type};
      member_exprt obj_vals{obj, "values", vals_type};
      member_exprt obj_len{obj, "length", signedbv_typet{64}};

      exprt::operandst elems;
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; ++i)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        elems.push_back(struct_exprt{
          {python_dict_unbox_key(index_exprt{obj_keys, idx}),
           index_exprt{obj_vals, idx}},
          tuple_t});
      }
      return struct_exprt{
        {obj_len, array_exprt{std::move(elems), list_data_type}}, list_t};
    }
  }
  if(method_name == "clear")
  {
    // d.clear() → set d.length = 0
    if((obj.id() == ID_symbol || obj.id() == ID_dereference))
    {
      member_exprt length{obj, "length", signedbv_typet{64}};
      pending_checks.push_back(
        code_frontend_assignt{length, from_integer(0, signedbv_typet{64})});
    }
    return from_integer(0, python_int_type()); // returns None
  }
  if(method_name == "copy")
  {
    return obj; // shallow copy = same struct
  }
  if(method_name == "update")
  {
    // PLR dict.update(other): merge entries from other
    // into d. If other is a dict literal (struct_exprt
    // with constant length + keys + values), emit one
    // dict-subscript-assign statement per entry to
    // preserve dict_literals tracking on both sides.
    // Non-literal 'other' or non-dict — fall through to
    // returning None (no mutation) as an
    // over-approximation.
    if(args.is_array() && !as_array(args).empty())
    {
      exprt other = convert_expression(*as_array(args).begin());
      const exprt *olit = nullptr;
      if(other.id() == ID_struct)
        olit = &other;
      else if(other.id() == ID_symbol)
      {
        auto it = dict_literals.find(to_symbol_expr(other).get_identifier());
        if(it != dict_literals.end())
          olit = &it->second;
      }
      if(
        olit != nullptr && olit->operands().size() >= 3 &&
        olit->operands()[0].is_constant() && is_python_dict_type(obj.type()))
      {
        mp_integer olen;
        if(!to_integer(to_constant_expr(olit->operands()[0]), olen))
        {
          // Invalidate dict_literals tracking on obj so
          // subsequent d["key"] lookups read from the
          // mutated struct, not the stale literal.
          if((obj.id() == ID_symbol || obj.id() == ID_dereference))
            if(obj.id() == ID_symbol)
              dict_literals.erase(to_symbol_expr(obj).get_identifier());
          const auto &dst_st = to_struct_type(obj.type());
          const auto &keys_type = to_array_type(dst_st.components()[1].type());
          const auto &vals_type = to_array_type(dst_st.components()[2].type());
          member_exprt dst_len{obj, "length", signedbv_typet{64}};
          member_exprt dst_keys{obj, "keys", keys_type};
          member_exprt dst_vals{obj, "values", vals_type};
          const exprt &src_keys = olit->operands()[1];
          const exprt &src_vals = olit->operands()[2];
          for(mp_integer i = 0; i < olen; ++i)
          {
            auto idx = i.to_ulong();
            if(
              idx >= src_keys.operands().size() ||
              idx >= src_vals.operands().size())
              break;
            exprt k = src_keys.operands()[idx];
            exprt v = src_vals.operands()[idx];
            if(k.type() != keys_type.element_type())
              k = coerce_element(k, keys_type.element_type());
            if(v.type() != vals_type.element_type())
              v = coerce_element(v, vals_type.element_type());
            // Scan existing keys, replace or append.
            // Matches the Assign-to-subscript handler
            // pattern used for d[k] = v statements.
            static unsigned upd_ctr = 0;
            std::string fn = "__upd_found_" + std::to_string(upd_ctr++);
            std::string fq = qualify_name(fn);
            irep_idt fi{fq};
            if(symbol_table.lookup(fi) == nullptr)
            {
              symbolt fs{fi, bool_typet{}, "python"};
              fs.base_name = fn;
              fs.is_lvalue = true;
              fs.is_state_var = true;
              symbol_table.add(fs);
            }
            symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
            pending_checks.push_back(
              code_frontend_assignt{found, false_exprt{}});
            for(std::size_t si = 0; si < PYTHON_MAX_DICT_SIZE; si++)
            {
              exprt sidx = from_integer(si, signedbv_typet{64});
              code_blockt upd;
              upd.add(code_frontend_assignt{index_exprt{dst_vals, sidx}, v});
              upd.add(code_frontend_assignt{found, true_exprt{}});
              pending_checks.push_back(code_ifthenelset{
                dict_slot_match(dst_keys, dst_len, si, k), std::move(upd)});
            }
            code_blockt append;
            append.add(code_frontend_assignt{
              index_exprt{dst_keys, dst_len},
              coerce_element(
                k, to_array_type(dst_keys.type()).element_type())});
            append.add(
              code_frontend_assignt{index_exprt{dst_vals, dst_len}, v});
            append.add(code_frontend_assignt{
              dst_len,
              plus_exprt{dst_len, from_integer(1, signedbv_typet{64})}});
            pending_checks.push_back(
              code_ifthenelset{not_exprt{found}, std::move(append)});
          }
        }
      }
    }
    return from_integer(0, python_int_type());
  }
  if(
    method_name == "setdefault" &&
    (obj.id() == ID_symbol || obj.id() == ID_dereference))
  {
    // PLR dict.setdefault(k, default=None): if k is in d
    // return d[k]; otherwise insert (k, default) and return
    // default. We model this with a side-effecting dict
    // store on the missing-key path so a later `k in d`
    // check correctly reports True.
    const auto &dict_st = to_struct_type(obj_base_type);
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    const auto &vals_type = to_array_type(dict_st.components()[2].type());
    member_exprt length{obj, "length", signedbv_typet{64}};
    member_exprt keys_arr{obj, "keys", keys_type};
    member_exprt vals_arr{obj, "values", vals_type};

    // Resolve key (1st arg) and default (2nd arg or
    // safe_zero of the value-type — which loses the
    // None-ness when value-type can't hold the sentinel,
    // but matches what dict.get already does).
    if(!args.is_array() || as_array(args).empty())
      return side_effect_expr_nondett{
        vals_type.element_type(), get_location(expr)};
    auto arg_it = as_array(args).begin();
    exprt key_expr = convert_expression(*arg_it);
    if(key_expr.type() != keys_type.element_type())
      key_expr = coerce_element(key_expr, keys_type.element_type());
    exprt default_val;
    {
      const typet &elem_t = vals_type.element_type();
      if(
        elem_t.id() == ID_signedbv || elem_t.id() == ID_unsignedbv ||
        elem_t.id() == ID_integer || elem_t.id() == ID_natural)
        default_val = from_integer(python_none_sentinel_int(), elem_t);
      else
        default_val = safe_zero(elem_t);
    }
    ++arg_it;
    if(arg_it != as_array(args).end())
    {
      default_val = convert_expression(*arg_it);
      if(default_val.type() != vals_type.element_type())
        default_val = coerce_element(default_val, vals_type.element_type());
    }

    // Allocate found flag + result temp.
    static unsigned sd_ctr = 0;
    std::string fn = "__sd_found_" + std::to_string(sd_ctr++);
    std::string fq = qualify_name(fn);
    irep_idt fi{fq};
    if(symbol_table.lookup(fi) == nullptr)
    {
      symbolt fs{fi, bool_typet{}, "python"};
      fs.base_name = fn;
      fs.is_lvalue = true;
      fs.is_state_var = true;
      symbol_table.add(fs);
    }
    symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
    std::string rn = "__sd_result_" + std::to_string(sd_ctr - 1);
    std::string rq = qualify_name(rn);
    irep_idt ri{rq};
    if(symbol_table.lookup(ri) == nullptr)
    {
      symbolt rs{ri, vals_type.element_type(), "python"};
      rs.base_name = rn;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    symbol_exprt result = symbol_table.lookup_ref(ri).symbol_expr();

    // Slot index (found key's index, or the insert position) — used to
    // return the value as an lvalue SLOT for mutable-container values
    // (dict-value-by-reference, Option 2) so setdefault(k, []).append(...)
    // mutates the stored list in place.
    std::string xn = "__sd_idx_" + std::to_string(sd_ctr - 1);
    irep_idt xi{qualify_name(xn)};
    if(symbol_table.lookup(xi) == nullptr)
    {
      symbolt xs{xi, signedbv_typet{64}, "python"};
      xs.base_name = xn;
      xs.is_lvalue = true;
      xs.is_state_var = true;
      symbol_table.add(xs);
    }
    symbol_exprt slot_idx = symbol_table.lookup_ref(xi).symbol_expr();

    pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});
    pending_checks.push_back(code_frontend_assignt{result, default_val});
    pending_checks.push_back(code_frontend_assignt{slot_idx, length});
    for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt match = dict_slot_match(keys_arr, length, i, key_expr);
      code_blockt update;
      update.add(code_frontend_assignt{found, true_exprt{}});
      update.add(code_frontend_assignt{result, index_exprt{vals_arr, idx}});
      update.add(code_frontend_assignt{slot_idx, idx});
      pending_checks.push_back(code_ifthenelset{match, std::move(update)});
    }
    // If not found, append (key, default) and set length+=1.
    code_blockt append;
    append.add(code_frontend_assignt{index_exprt{keys_arr, length}, key_expr});
    append.add(
      code_frontend_assignt{index_exprt{vals_arr, length}, default_val});
    append.add(code_frontend_assignt{
      length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
    pending_checks.push_back(
      code_ifthenelset{not_exprt{found}, std::move(append)});

    // Invalidate the compile-time dict-literal tracking for
    // this symbol. dict_literals captures the literal value of
    // the dict at construction time and is used by the `key in
    // dict` constant-fold path; without invalidation the fold
    // would report the pre-setdefault state and miss the key
    // we just inserted.
    if(obj.id() == ID_symbol)
      dict_literals.erase(to_symbol_expr(obj).get_identifier());

    // Dict-value-by-reference (Option 2): for a mutable-container value,
    // return the lvalue SLOT values[slot_idx] so an in-place mutation of
    // the returned default (setdefault(k, []).append(...)) propagates.
    {
      const typet &svet = vals_type.element_type();
      const typet &sket = keys_type.element_type();
      if(
        (is_python_list_type(svet) || is_python_dict_type(svet)) &&
        !is_python_string_type(sket) && !is_python_value_type(sket))
        return index_exprt{vals_arr, slot_idx, svet};
    }
    return result;
  }
  if(
    method_name == "pop" &&
    (obj.id() == ID_symbol || obj.id() == ID_dereference))
  {
    // PLR dict.pop(key, default=...): if key in d, remove
    // and return its value. If key not in d and a default
    // is supplied, return default. If key not in d and no
    // default is supplied, raise KeyError.
    const auto &dict_st = to_struct_type(obj_base_type);
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    const auto &vals_type = to_array_type(dict_st.components()[2].type());
    member_exprt length{obj, "length", signedbv_typet{64}};
    member_exprt keys_arr{obj, "keys", keys_type};
    member_exprt vals_arr{obj, "values", vals_type};

    if(!args.is_array() || as_array(args).empty())
      return side_effect_expr_nondett{
        vals_type.element_type(), get_location(expr)};
    auto arg_it = as_array(args).begin();
    exprt key_expr = convert_expression(*arg_it);
    if(key_expr.type() != keys_type.element_type())
      key_expr = coerce_element(key_expr, keys_type.element_type());
    bool has_default = false;
    exprt default_val = safe_zero(vals_type.element_type());
    exprt raw_default = default_val;
    ++arg_it;
    if(arg_it != as_array(args).end())
    {
      has_default = true;
      raw_default = convert_expression(*arg_it);
      default_val = raw_default;
      if(default_val.type() != vals_type.element_type())
        default_val = coerce_element(default_val, vals_type.element_type());
    }
    // PLR §6.4.6: pop(k, default) returns d[k] if present else `default` -- the
    // result type is value_type | type(default). When the default's type
    // differs, model the result as a python_value (the matched value's tag when
    // present, the default's when absent) so a later type-restricting use sees
    // the default possibility (a coerced default lost its type -- the get/005
    // bug, shared by pop).
    const bool default_differs =
      has_default && raw_default.type() != vals_type.element_type() &&
      !is_python_value_type(vals_type.element_type());
    const typet result_type =
      default_differs ? python_value_type() : vals_type.element_type();

    static unsigned pop_ctr = 0;
    std::string fn = "__pop_found_" + std::to_string(pop_ctr++);
    irep_idt fi{qualify_name(fn)};
    if(symbol_table.lookup(fi) == nullptr)
    {
      symbolt fs{fi, bool_typet{}, "python"};
      fs.base_name = fn;
      fs.is_lvalue = true;
      fs.is_state_var = true;
      symbol_table.add(fs);
    }
    symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();

    std::string rn = "__pop_result_" + std::to_string(pop_ctr - 1);
    irep_idt ri{qualify_name(rn)};
    if(symbol_table.lookup(ri) == nullptr)
    {
      symbolt rs{ri, result_type, "python"};
      rs.base_name = rn;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    symbol_exprt result = symbol_table.lookup_ref(ri).symbol_expr();

    pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});
    pending_checks.push_back(code_frontend_assignt{
      result, default_differs ? wrap_value(raw_default) : default_val});
    // Find and remove (compact by shifting).
    for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt match = dict_slot_match(keys_arr, length, i, key_expr);
      code_blockt update;
      update.add(code_frontend_assignt{found, true_exprt{}});
      update.add(code_frontend_assignt{
        result,
        default_differs ? wrap_value(index_exprt{vals_arr, idx})
                        : static_cast<exprt>(index_exprt{vals_arr, idx})});
      // Shift remaining entries down to compact.
      for(std::size_t j = i; j + 1 < PYTHON_MAX_DICT_SIZE; j++)
      {
        exprt jdx = from_integer(j, signedbv_typet{64});
        exprt jdx1 = from_integer(j + 1, signedbv_typet{64});
        update.add(code_frontend_assignt{
          index_exprt{keys_arr, jdx}, index_exprt{keys_arr, jdx1}});
        update.add(code_frontend_assignt{
          index_exprt{vals_arr, jdx}, index_exprt{vals_arr, jdx1}});
      }
      update.add(code_frontend_assignt{
        length, minus_exprt{length, from_integer(1, signedbv_typet{64})}});
      pending_checks.push_back(code_ifthenelset{match, std::move(update)});
    }
    // KeyError when not found and no default given.
    if(!has_default)
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt missing = not_exprt{found};
        pending_checks.push_back(code_frontend_assignt{
          exc_sym->symbol_expr(), or_exprt{exc_sym->symbol_expr(), missing}});
        if(exc_type_sym != nullptr)
        {
          long h = exception_type_hash("KeyError");
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            if_exprt{
              missing,
              from_integer(h, exc_type_sym->type),
              exc_type_sym->symbol_expr()}});
        }
      }
    }
    if(obj.id() == ID_symbol)
      dict_literals.erase(to_symbol_expr(obj).get_identifier());
    return result;
  }
  if(
    method_name == "popitem" &&
    (obj.id() == ID_symbol || obj.id() == ID_dereference))
  {
    // PLR dict.popitem(): remove and return an arbitrary
    // (key, value) pair. Raises KeyError on empty dict.
    const auto &dict_st = to_struct_type(obj_base_type);
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    const auto &vals_type = to_array_type(dict_st.components()[2].type());
    member_exprt length{obj, "length", signedbv_typet{64}};
    // Empty-dict KeyError check via __exception_active flag.
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt empty = binary_relation_exprt{
          length, ID_le, from_integer(0, signedbv_typet{64})};
        pending_checks.push_back(code_frontend_assignt{
          exc_sym->symbol_expr(), or_exprt{exc_sym->symbol_expr(), empty}});
        if(exc_type_sym != nullptr)
        {
          long h = exception_type_hash("KeyError");
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            if_exprt{
              empty,
              from_integer(h, exc_type_sym->type),
              exc_type_sym->symbol_expr()}});
        }
      }
    }
    // Snapshot the pre-popitem length so the (key, value) we
    // return doesn't depend on the post-decrement length.
    // pending_checks are emitted BEFORE the surrounding
    // statement; without this snapshot the decrement would
    // race ahead of the read and we'd return keys[length-2].
    static unsigned pi_ctr = 0;
    std::string ln = "__popitem_len_" + std::to_string(pi_ctr++);
    irep_idt li{qualify_name(ln)};
    if(symbol_table.lookup(li) == nullptr)
    {
      symbolt ls{li, signedbv_typet{64}, "python"};
      ls.base_name = ln;
      ls.is_lvalue = true;
      ls.is_state_var = true;
      symbol_table.add(ls);
    }
    symbol_exprt last_idx_sym = symbol_table.lookup_ref(li).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      last_idx_sym, minus_exprt{length, from_integer(1, signedbv_typet{64})}});
    // Return tuple (keys[snapshot], values[snapshot]) since
    // CPython pops in LIFO order.
    member_exprt keys_arr{obj, "keys", keys_type};
    member_exprt vals_arr{obj, "values", vals_type};
    exprt key_at = python_dict_unbox_key(index_exprt{keys_arr, last_idx_sym});
    exprt val_at = index_exprt{vals_arr, last_idx_sym};
    struct_typet::componentst tcomps;
    tcomps.push_back(struct_typet::componentt{"_0", key_at.type()});
    tcomps.push_back(struct_typet::componentt{"_1", val_at.type()});
    struct_typet tuple_t{tcomps};
    tuple_t.set_tag("python_tuple");
    // Decrement length AFTER recording the snapshot.
    pending_checks.push_back(code_frontend_assignt{length, last_idx_sym});
    if(obj.id() == ID_symbol)
      dict_literals.erase(to_symbol_expr(obj).get_identifier());
    return struct_exprt{{key_at, val_at}, tuple_t};
  }
  if(
    method_name == "setdefault" || method_name == "pop" ||
    method_name == "popitem")
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};

  return std::nullopt;
}
