/// Python to GOTO converter — list-method dispatch
/// (PLR §6.4.6 list methods). Handles append / sort /
/// reverse / pop / copy / extend / remove / index /
/// __iter__ / __contains__ / count / clear, plus the
/// bytes-as-list[uint8] decode/encode methods. Extracted
/// from python_converter_call_method.cpp per
/// doc/python-frontend-call-refactor-plan.md (Phase 3.3).
/// Pure source-split — semantics are preserved.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/json.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/string_constant.h>
#include <util/symbol.h>

#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cstdint>
#include <optional>
#include <string>

std::optional<exprt> python_convertert::try_list_method(
  const jsont &expr,
  const exprt &obj,
  const typet &obj_base_type,
  const std::string &method_name,
  const jsont &args)
{
  // PLR §3.3.1: list.__iter__() returns the list itself,
  // which is sufficient for our list-as-iterator model.
  // The for-loop iter path expects the same shape and
  // walks .data[0..length-1] via its own counter.
  if(method_name == "__iter__")
    return obj;
  // PLR §4.6: bytes are modelled as list[uint8]; expose
  // bytes.decode(encoding) by repackaging the bytes' data
  // pointer as a python_string struct. We don't translate
  // multibyte encodings — for ascii / latin-1 / utf-8 of
  // ASCII-only content the byte pattern is the same; for
  // other content the verifier sees the raw bytes which
  // are still sound for length and indexing checks.
  if(method_name == "decode")
  {
    const auto &list_st = to_struct_type(obj_base_type);
    const auto &data_type = to_array_type(list_st.components()[1].type());
    if(
      data_type.element_type().id() == ID_unsignedbv &&
      to_unsignedbv_type(data_type.element_type()).get_width() == 8)
    {
      // PLR §4.6: if the bytes value is a compile-time
      // constant (struct_exprt with constant length and
      // data array), recover the content and return a
      // python_string literal so subsequent string-solver
      // operations have a known content. This covers
      // 'b"hello".decode("utf-8")' and 'BS.decode(...)'
      // when BS = b"...".
      const exprt *src = nullptr;
      if(
        obj.id() == ID_struct && obj.operands().size() >= 2 &&
        obj.operands()[0].is_constant() && obj.operands()[1].id() == ID_array)
        src = &obj;
      else if(obj.id() == ID_symbol)
      {
        auto it = list_literals.find(to_symbol_expr(obj).get_identifier());
        if(it != list_literals.end())
          src = &it->second;
      }
      if(
        src != nullptr && src->operands().size() >= 2 &&
        src->operands()[0].is_constant() && src->operands()[1].id() == ID_array)
      {
        mp_integer blen;
        if(!to_integer(to_constant_expr(src->operands()[0]), blen))
        {
          std::string content;
          const exprt &data_arr = src->operands()[1];
          std::size_t n =
            std::min<std::size_t>(blen.to_ulong(), data_arr.operands().size());
          for(std::size_t i = 0; i < n; i++)
          {
            mp_integer bv;
            if(!to_integer(to_constant_expr(data_arr.operands()[i]), bv))
              content.push_back(static_cast<char>(bv.to_ulong()));
          }
          return python_string_literal(content);
        }
      }
      // Runtime bytes: repackage data pointer as a
      // python_string. Sound for length and indexing
      // queries; the string solver may still produce
      // nondet content because there's no explicit
      // array-to-pointer association.
      member_exprt blen{obj, "length", signedbv_typet{64}};
      member_exprt bdata{obj, "data", data_type};
      exprt data_ptr = address_of_exprt{
        index_exprt{bdata, from_integer(0, signedbv_typet{64})}};
      return struct_exprt{{blen, data_ptr}, python_string_type()};
    }
  }
  const auto &list_st = to_struct_type(obj_base_type);
  const auto &data_type = to_array_type(list_st.components()[1].type());
  member_exprt length{obj, "length", signedbv_typet{64}};
  member_exprt data{obj, "data", data_type};

  if(method_name == "reverse")
  {
    // Reverse in place: swap data[i] with data[len-1-i]
    code_blockt block;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH / 2; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt mirror = minus_exprt{
        minus_exprt{length, from_integer(1, signedbv_typet{64})}, idx};
      exprt cond = binary_relation_exprt{idx, ID_lt, mirror};
      // Swap via temp
      static unsigned rev_counter = 0;
      std::string tmp_name = "__rev_tmp_" + std::to_string(rev_counter++);
      std::string tmp_qname = qualify_name(tmp_name);
      irep_idt tmp_id{tmp_qname};
      if(symbol_table.lookup(tmp_id) == nullptr)
      {
        symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
        tmp_sym.base_name = tmp_name;
        tmp_sym.is_lvalue = true;
        tmp_sym.is_state_var = true;
        symbol_table.add(tmp_sym);
      }
      symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
      code_blockt swap;
      swap.add(code_frontend_assignt{tmp, index_exprt{data, idx}});
      swap.add(code_frontend_assignt{
        index_exprt{data, idx}, index_exprt{data, mirror}});
      swap.add(code_frontend_assignt{index_exprt{data, mirror}, tmp});
      pending_checks.push_back(code_ifthenelset{cond, std::move(swap)});
    }
    return from_integer(0, python_int_type()); // None
  }

  if(method_name == "sort")
  {
    // PLR §6.10: when the list is a known literal whose
    // elements are all constant ints or constant strings,
    // sort at conversion time and rewrite the list. Avoids
    // the O(n²) bubble-sort below which would emit one
    // string-solver comparison per pass per pair and time
    // out on string lists.
    const exprt *lit = nullptr;
    irep_idt list_sym;
    if(obj.id() == ID_symbol)
    {
      list_sym = to_symbol_expr(obj).get_identifier();
      auto it = list_literals.find(list_sym);
      if(it != list_literals.end())
        lit = &it->second;
    }
    if(
      lit != nullptr && lit->operands().size() >= 2 &&
      lit->operands()[0].is_constant())
    {
      mp_integer lv;
      if(!to_integer(to_constant_expr(lit->operands()[0]), lv))
      {
        const exprt &data_arr = lit->operands()[1];
        std::vector<std::pair<mp_integer, exprt>> int_pairs;
        std::vector<std::pair<std::string, exprt>> str_pairs;
        bool all_const_int = true;
        bool all_const_str = true;
        for(mp_integer i = 0; i < lv; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data_arr.operands().size())
          {
            all_const_int = false;
            all_const_str = false;
            break;
          }
          const exprt &e = data_arr.operands()[idx];
          if(all_const_int)
          {
            if(!e.is_constant() || e.type().id() != ID_signedbv)
              all_const_int = false;
            else
            {
              mp_integer val;
              if(to_integer(to_constant_expr(e), val))
                all_const_int = false;
              else
                int_pairs.emplace_back(val, e);
            }
          }
          if(all_const_str)
          {
            auto sv = extract_string_value(e);
            if(!sv.has_value())
              all_const_str = false;
            else
              str_pairs.emplace_back(sv.value(), e);
          }
        }
        if(
          (all_const_int && !int_pairs.empty()) ||
          (all_const_str && !str_pairs.empty()))
        {
          exprt::operandst sorted_elems;
          if(all_const_int)
          {
            std::sort(
              int_pairs.begin(),
              int_pairs.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
            for(const auto &p : int_pairs)
              sorted_elems.push_back(p.second);
          }
          else
          {
            std::sort(
              str_pairs.begin(),
              str_pairs.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
            for(const auto &p : str_pairs)
              sorted_elems.push_back(p.second);
          }
          while(sorted_elems.size() < PYTHON_MAX_LIST_LENGTH)
            sorted_elems.push_back(safe_zero(data_type.element_type()));
          exprt sorted_struct = struct_exprt{
            {lit->operands()[0],
             array_exprt{std::move(sorted_elems), data_type}},
            obj.type()};
          pending_checks.push_back(code_frontend_assignt{obj, sorted_struct});
          if(!list_sym.empty())
            list_literals[list_sym] = sorted_struct;
          return from_integer(0, python_int_type()); // None
        }
      }
    }
    // Bubble sort via pending_checks (correct for bounded lists)
    for(std::size_t pass = 0; pass < PYTHON_MAX_LIST_LENGTH; pass++)
    {
      for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt next = from_integer(i + 1, signedbv_typet{64});
        exprt in_bounds = binary_relation_exprt{next, ID_lt, length};
        exprt should_swap = binary_relation_exprt{
          index_exprt{data, idx}, ID_gt, index_exprt{data, next}};
        // Conditional swap
        static unsigned sort_counter = 0;
        std::string tmp_name = "__sort_tmp_" + std::to_string(sort_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        code_blockt swap;
        swap.add(code_frontend_assignt{tmp, index_exprt{data, idx}});
        swap.add(code_frontend_assignt{
          index_exprt{data, idx}, index_exprt{data, next}});
        swap.add(code_frontend_assignt{index_exprt{data, next}, tmp});
        pending_checks.push_back(
          code_ifthenelset{and_exprt{in_bounds, should_swap}, std::move(swap)});
      }
    }
    return from_integer(0, python_int_type()); // None
  }

  if(method_name == "pop")
  {
    // PLib stdtypes: pop(i) or pop() — remove and return element
    exprt pop_idx;
    if(args.is_array() && !as_array(args).empty())
      pop_idx = safe_typecast(
        convert_expression(*as_array(args).begin()), signedbv_typet{64});
    else
      pop_idx = minus_exprt{length, from_integer(1, signedbv_typet{64})};

    // PLR list.pop: raises IndexError when the list is empty
    // (default pop() with length == 0) or when the supplied
    // index is out of range. Python also accepts negative
    // indices that wrap from the end (-1 == last); the valid
    // range after wrapping is [-length, length).
    {
      exprt zero64 = from_integer(0, signedbv_typet{64});
      // If pop_idx is non-negative: pop_idx < length.
      // If pop_idx is negative: pop_idx >= -length, i.e.
      //   pop_idx + length >= 0.
      exprt nonneg_ok = and_exprt{
        binary_relation_exprt{pop_idx, ID_ge, zero64},
        binary_relation_exprt{pop_idx, ID_lt, length}};
      exprt neg_ok = and_exprt{
        binary_relation_exprt{pop_idx, ID_lt, zero64},
        binary_relation_exprt{plus_exprt{pop_idx, length}, ID_ge, zero64}};
      add_check(
        or_exprt{nonneg_ok, neg_ok},
        "exception",
        "IndexError: pop index out of range",
        get_location(expr));
    }

    // Normalise negative index for the actual extraction.
    exprt zero64 = from_integer(0, signedbv_typet{64});
    pop_idx = if_exprt{
      binary_relation_exprt{pop_idx, ID_lt, zero64},
      plus_exprt{pop_idx, length},
      pop_idx};

    static unsigned pop_counter = 0;
    std::string tmp_name = "__pop_tmp_" + std::to_string(pop_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};
    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
    // Save element at index
    pending_checks.push_back(
      code_frontend_assignt{tmp, index_exprt{data, pop_idx}});
    // Shift elements left from pop_idx
    for(std::size_t j = 0; j + 1 < PYTHON_MAX_LIST_LENGTH; j++)
    {
      exprt jexpr = from_integer(j, signedbv_typet{64});
      exprt guard = and_exprt{
        binary_relation_exprt{jexpr, ID_ge, pop_idx},
        binary_relation_exprt{
          jexpr,
          ID_lt,
          minus_exprt{length, from_integer(1, signedbv_typet{64})}}};
      pending_checks.push_back(code_ifthenelset{
        guard,
        code_frontend_assignt{
          index_exprt{data, jexpr},
          index_exprt{
            data, plus_exprt{jexpr, from_integer(1, signedbv_typet{64})}}}});
    }
    // Decrement length
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{obj, "length", signedbv_typet{64}},
      minus_exprt{length, from_integer(1, signedbv_typet{64})}});
    return std::move(tmp);
  }

  // PLib stdtypes: list.index(value) — return index of first occurrence
  if(method_name == "index")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt search = convert_expression(*as_array(args).begin());
      if(search.type() != data_type.element_type())
        search = safe_typecast(search, data_type.element_type());
      // Build if-then-else chain: check from end to start
      exprt result = from_integer(-1, python_int_type()); // not found
      for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt match = equal_exprt{index_exprt{data, idx}, search};
        result = if_exprt{and_exprt{in_range, match}, idx, result};
      }
      return result;
    }
  }

  // PLib stdtypes: list.extend(iterable)
  if(method_name == "extend")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
      {
        member_exprt arg_len{arg, "length", signedbv_typet{64}};
        const auto &arg_data_type =
          to_array_type(to_struct_type(arg.type()).components()[1].type());
        member_exprt arg_data{arg, "data", arg_data_type};
        // Copy elements: obj.data[obj.length + i] = arg.data[i]
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt dst = plus_exprt{length, idx};
          pending_checks.push_back(code_ifthenelset{
            binary_relation_exprt{idx, ID_lt, arg_len},
            code_frontend_assignt{
              index_exprt{data, dst}, index_exprt{arg_data, idx}}});
        }
        pending_checks.push_back(code_frontend_assignt{
          member_exprt{obj, "length", signedbv_typet{64}},
          plus_exprt{length, arg_len}});
      }
      else if(is_python_string_type(arg.type()))
      {
        // extend with string: iterate characters
        auto sv = extract_string_value(arg);
        if(!sv.has_value() && arg.id() == ID_symbol)
        {
          auto it = string_constants.find(to_symbol_expr(arg).get_identifier());
          if(it != string_constants.end())
            sv = it->second;
        }
        if(sv.has_value())
        {
          // Constant string: add each char as a single-char string
          for(std::size_t i = 0; i < sv.value().size(); i++)
          {
            exprt dst = plus_exprt{length, from_integer(i, signedbv_typet{64})};
            exprt ch_str = python_string_literal(std::string(1, sv.value()[i]));
            if(ch_str.type() != data_type.element_type())
              ch_str = safe_typecast(ch_str, data_type.element_type());
            pending_checks.push_back(
              code_frontend_assignt{index_exprt{data, dst}, ch_str});
          }
          pending_checks.push_back(code_frontend_assignt{
            member_exprt{obj, "length", signedbv_typet{64}},
            plus_exprt{
              length,
              from_integer(
                static_cast<long long>(sv.value().size()),
                signedbv_typet{64})}});
        }
      }
    }
    return from_integer(0, python_int_type());
  }

  // PLib stdtypes: list.remove(value)
  if(method_name == "remove")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt val = convert_expression(*as_array(args).begin());
      if(val.type() != data_type.element_type())
        val = safe_typecast(val, data_type.element_type());
      // Find first occurrence and shift left
      // Use a found flag to track if we've found the element
      static unsigned rm_counter = 0;
      std::string flag_name = "__rm_found_" + std::to_string(rm_counter++);
      std::string flag_qname = qualify_name(flag_name);
      irep_idt flag_id{flag_qname};
      if(symbol_table.lookup(flag_id) == nullptr)
      {
        symbolt flag_sym{flag_id, bool_typet{}, "python"};
        flag_sym.base_name = flag_name;
        flag_sym.is_lvalue = true;
        flag_sym.is_state_var = true;
        symbol_table.add(flag_sym);
      }
      symbol_exprt found = symbol_table.lookup_ref(flag_id).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});
      for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt next = from_integer(i + 1, signedbv_typet{64});
        exprt in_bounds = binary_relation_exprt{idx, ID_lt, length};
        exprt is_match = equal_exprt{index_exprt{data, idx}, val};
        // If not found yet and matches, set found
        code_blockt on_match;
        on_match.add(code_frontend_assignt{found, true_exprt{}});
        pending_checks.push_back(code_ifthenelset{
          and_exprt{in_bounds, and_exprt{not_exprt{found}, is_match}},
          std::move(on_match)});
        // If found, shift left
        pending_checks.push_back(code_ifthenelset{
          and_exprt{in_bounds, found},
          code_frontend_assignt{
            index_exprt{data, idx}, index_exprt{data, next}}});
      }
      pending_checks.push_back(code_ifthenelset{
        found,
        code_frontend_assignt{
          member_exprt{obj, "length", signedbv_typet{64}},
          minus_exprt{length, from_integer(1, signedbv_typet{64})}}});
    }
    return from_integer(0, python_int_type());
  }

  // PLib stdtypes: list.copy().
  // PLR §6.10.4: must produce a fresh list. Returning
  // `obj` directly aliases — subsequent mutations on the
  // chained result (e.g. `x.copy().append(99)`) would
  // mutate the original `x`. Allocate a temp symbol,
  // assign a struct-copy of obj to it, and return the
  // temp's symbol_expr. The caller chain then targets
  // the temp's storage.
  if(method_name == "copy")
  {
    static unsigned copy_ctr = 0;
    std::string tn = "__list_copy_" + std::to_string(copy_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, obj.type(), "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      ts.is_static_lifetime = current_function.empty();
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{tmp, obj});
    return tmp;
  }

  if(method_name == "clear")
  {
    // PLib stdtypes: list.clear() — empty the list in place.
    // Reset length to 0; data slots are left as-is (their
    // values become undefined, but indexing them is then
    // out-of-bounds anyway).
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{obj, "length", signedbv_typet{64}},
      from_integer(0, signedbv_typet{64})});
    return obj;
  }

  return std::nullopt;
}
