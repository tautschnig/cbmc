/// Python to GOTO converter — list-method dispatch
/// (PLR §6.4.6 list methods). Handles append / sort /
/// reverse / pop / copy / extend / remove / index /
/// __iter__ / __contains__ / count / clear, plus the
/// bytes-as-list[uint8] decode/encode methods. Extracted
/// from python_converter_call_method.cpp per
/// doc/python-frontend-architecture.md.
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
  // Extraction-then-mutate soundness: if `obj` aliases a container element
  // (`r = c[i]`) and this is an in-place mutator, havoc the source container.
  invalidate_extracted_source_on_mutation(obj, method_name);
  // Direct mutation of a non-int-keyed dict value (`d["k"].mutator(...)`): the
  // value is returned by copy, so the mutation is lost -- havoc the dict.
  invalidate_dict_value_on_mutation(
    json_member(json_member(expr, "func"), "value"), method_name);
  // PLR §3.3: a content-changing in-place mutator (append/insert/extend/remove/
  // pop/clear) alters the receiver's length/elements but does NOT update the
  // `list_literals` constant-fold snapshot. Leaving the stale snapshot lets
  // sorted()/min()/max()/index()/the mixed-orderable check fold against
  // PRE-mutation data -- a SOUNDNESS bug: `xs.append(-5); min(xs)` folded to the
  // old min 0 and false-proved `min(xs) == 0`. Erase the snapshot so those ops
  // read the runtime (mutated) list. sort()/reverse() maintain the snapshot
  // in-place themselves, so they are excluded.
  if(obj.id() == ID_symbol)
  {
    static const std::set<std::string> content_mutators = {
      "append", "insert", "extend", "remove", "pop", "clear"};
    if(content_mutators.count(method_name) > 0)
      list_literals.erase(to_symbol_expr(obj).get_identifier());
  }
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

  // PLR §6.2.9 / PEP 342: generator.send(value). A generator object starts
  // suspended *before* its first line; the first interaction must prime it
  // (next() / send(None)). send(non-None) on a just-started generator raises
  // TypeError ("can't send non-None value to a just-started generator").
  //
  // The eager list-with-cursor model already encodes priming state in the
  // cursor: allocate_generator_cursor inits it to 0 at `it = g()`, and next()
  // does cursor++ before returning data[cursor-1]. So `cursor == 0` *is* the
  // not-yet-started state -- no separate flag is needed (single source of
  // truth for generator progress). We resolve the receiver's cursor (a Name
  // bound to a `gen()` call); an opaque/aliased generator with no resolvable
  // cursor is not flagged (sound, no false positive). Faithful value-passing
  // into the pending `yield` needs real resumption and is deferred (plan §1
  // Phase 2); send() here resumes like next() and returns the next element.
  if(method_name == "send")
  {
    const jsont &recv = json_member(json_member(expr, "func"), "value");
    irep_idt cursor_id;
    if(is_node_type(recv, "Name"))
    {
      irep_idt sid{qualify_name(json_string(json_member(recv, "id")))};
      auto cit = generator_cursors.find(sid);
      if(cit != generator_cursors.end())
        cursor_id = cit->second;
    }
    if(
      !cursor_id.empty() && symbol_table.lookup(cursor_id) != nullptr &&
      args.is_array() && !as_array(args).empty())
    {
      symbol_exprt cursor = symbol_table.lookup_ref(cursor_id).symbol_expr();
      const jsont &arg0 = *as_array(args).begin();
      exprt arg_expr = convert_expression(arg0);

      // Is the sent value provably None? A None send always primes the
      // generator (valid) and is never flagged.
      bool provably_none = false;
      if(is_node_type(arg0, "Constant") && json_member(arg0, "value").is_null())
        provably_none = true;
      else if(
        is_node_type(arg0, "Name") &&
        json_string(json_member(arg0, "id")) == "None")
        provably_none = true;
      else if(is_python_none_constant(arg_expr))
        provably_none = true;

      if(!provably_none)
      {
        // Runtime "argument is not None": a concrete-typed value is never
        // None; a python_value may be None at runtime, so guard on its tag.
        exprt arg_not_none =
          is_python_value_type(arg_expr.type())
            ? static_cast<exprt>(
                not_exprt{python_value_is(arg_expr, python_type_tagt::NONE)})
            : static_cast<exprt>(true_exprt{});
        exprt not_started =
          equal_exprt{cursor, from_integer(0, signedbv_typet{64})};
        emit_conditional_exception(
          and_exprt{not_started, arg_not_none}, "TypeError");
      }

      // Resume: advance the cursor like next() and return the next yielded
      // value. StopIteration once the cursor reaches the eager-yield length.
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr && exc_type_sym != nullptr)
      {
        exprt cond = binary_relation_exprt{cursor, ID_ge, length};
        code_blockt then_block;
        then_block.add(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
        long h = exception_type_hash("StopIteration");
        then_block.add(code_frontend_assignt{
          exc_type_sym->symbol_expr(), from_integer(h, python_int_type())});
        code_blockt else_block;
        else_block.add(code_frontend_assignt{
          cursor, plus_exprt{cursor, from_integer(1, signedbv_typet{64})}});
        code_ifthenelset advance{
          cond, std::move(then_block), std::move(else_block)};
        advance.add_source_location() = get_location(expr);
        pending_checks.push_back(std::move(advance));
      }
      else
      {
        pending_checks.push_back(code_frontend_assignt{
          cursor, plus_exprt{cursor, from_integer(1, signedbv_typet{64})}});
      }
      exprt prev = minus_exprt{cursor, from_integer(1, signedbv_typet{64})};
      if_exprt safe_idx{
        binary_relation_exprt{prev, ID_lt, from_integer(0, signedbv_typet{64})},
        from_integer(0, signedbv_typet{64}),
        prev};
      return index_exprt{data, safe_idx};
    }
    // Unresolvable / opaque generator receiver: no model, no flag.
  }

  // PLR §6.2.9 / PEP 342: gen.close() finalises the generator, so a subsequent
  // next()/send() raises StopIteration. Model it by exhausting the consumption
  // cursor (cursor = length); the existing next()/send() StopIteration guard
  // (cursor >= length) then fires. Only a resolvable generator receiver is
  // handled; `[1,2].close()` on a real list falls through to the missing-method
  // path. Returns None.
  if(method_name == "close")
  {
    const jsont &recv = json_member(json_member(expr, "func"), "value");
    if(is_node_type(recv, "Name"))
    {
      irep_idt sid{qualify_name(json_string(json_member(recv, "id")))};
      auto cit = generator_cursors.find(sid);
      if(
        cit != generator_cursors.end() &&
        symbol_table.lookup(cit->second) != nullptr)
      {
        symbol_exprt cursor =
          symbol_table.lookup_ref(cit->second).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{cursor, length});
        return python_none_value();
      }
    }
    // not a resolvable generator -> fall through
  }

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
        // PLR §6.4.6 / §3.2.1: a list mixing int and float (numeric
        // subtypes) is ordered by numeric value across the tagged
        // union, exactly as `<` / `==` already promote. Keep a
        // value-keyed view that accepts any constant numeric element
        // so a mixed `[3, 1.5]` constant-folds instead of falling to
        // the bubble sort (whose element `>` does not promote tags).
        std::vector<std::pair<double, exprt>> num_pairs;
        bool all_const_int = true;
        bool all_const_str = true;
        bool all_const_numeric = true;
        for(mp_integer i = 0; i < lv; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= data_arr.operands().size())
          {
            all_const_int = false;
            all_const_str = false;
            all_const_numeric = false;
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
          if(all_const_numeric)
          {
            std::optional<double> nv = try_eval_double(e);
            if(
              !nv.has_value() && e.id() == ID_struct &&
              is_python_value_type(e.type()) && e.operands().size() >= 4)
            {
              // Mixed-numeric lists store each element as a python_value
              // tagged-union struct {tag,int,float,bool,...}; read the
              // active numeric field so the value-keyed fold applies
              // (a whole-struct `>` would otherwise order by tag, putting
              // every int before every float regardless of value).
              const exprt &tag_op = e.operands()[0];
              mp_integer t;
              if(
                tag_op.is_constant() && tag_op.type().id() == ID_signedbv &&
                !to_integer(to_constant_expr(tag_op), t))
              {
                if(t == mp_integer{static_cast<int>(python_type_tagt::INT)})
                  nv = try_eval_double(e.operands()[1]);
                else if(
                  t == mp_integer{static_cast<int>(python_type_tagt::FLOAT)})
                  nv = try_eval_double(e.operands()[2]);
                else if(
                  t == mp_integer{static_cast<int>(python_type_tagt::BOOL)})
                  nv = try_eval_double(e.operands()[3]);
              }
            }
            if(!nv.has_value())
              all_const_numeric = false;
            else
              num_pairs.emplace_back(nv.value(), e);
          }
        }
        if(
          (all_const_int && !int_pairs.empty()) ||
          (all_const_str && !str_pairs.empty()) ||
          (all_const_numeric && !num_pairs.empty()))
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
          else if(all_const_str)
          {
            std::sort(
              str_pairs.begin(),
              str_pairs.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
            for(const auto &p : str_pairs)
              sorted_elems.push_back(p.second);
          }
          else
          {
            // Mixed / all-float numeric: order by numeric value,
            // preserving each element's original (int- or
            // float-typed) expr — CPython keeps the objects, only
            // reordering them. stable_sort matches CPython's stable
            // sort so equal values (e.g. 2 and 2.0) keep input order.
            std::stable_sort(
              num_pairs.begin(),
              num_pairs.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
            for(const auto &p : num_pairs)
              sorted_elems.push_back(p.second);
          }
          while(sorted_elems.size() < PYTHON_MAX_LIST_LENGTH)
            sorted_elems.push_back(safe_zero(data_type.element_type()));
          // Write the sorted elements IN PLACE through `data` (a member of
          // obj) rather than reassigning the whole struct to obj: an
          // in-place write propagates when obj is a by-reference parameter
          // (e.g. `heapq.heapify(h)` sorting the caller's list), whereas a
          // whole-struct `obj = sorted` only updates a local alias. Mirrors
          // reverse() / the bubble-sort fallback. (sort does not change the
          // length.)
          for(std::size_t si = 0; si < sorted_elems.size(); ++si)
            pending_checks.push_back(code_frontend_assignt{
              index_exprt{data, from_integer(si, signedbv_typet{64})},
              sorted_elems[si]});
          if(!list_sym.empty())
            list_literals[list_sym] = struct_exprt{
              {lit->operands()[0],
               array_exprt{std::move(sorted_elems), data_type}},
              obj.type()};
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
        // The by-reference container (and any python_value list) wraps each
        // element as a tagged python_value struct; a raw `>` on the structs
        // does not compare the underlying value. Compare the int payload
        // (__int_val) when the element type is the tagged union, so a sort
        // of an int list passed by reference (e.g. heapq.heapify) orders it
        // correctly. (Float/mixed payloads remain a residual.)
        exprt lhs_e = index_exprt{data, idx};
        exprt rhs_e = index_exprt{data, next};
        if(is_python_value_type(data_type.element_type()))
        {
          lhs_e = python_value_int(lhs_e);
          rhs_e = python_value_int(rhs_e);
        }
        exprt should_swap =
          binary_relation_exprt{std::move(lhs_e), ID_gt, std::move(rhs_e)};
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
      // Set __exception_active for try/except catch; downstream
      // uncaught_exception fires for unhandled cases.
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt out_of_range = not_exprt{or_exprt{nonneg_ok, neg_ok}};
        pending_checks.push_back(code_frontend_assignt{
          exc_sym->symbol_expr(),
          or_exprt{exc_sym->symbol_expr(), out_of_range}});
        if(exc_type_sym != nullptr)
        {
          long h = exception_type_hash("IndexError");
          pending_checks.push_back(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            if_exprt{
              out_of_range,
              from_integer(h, exc_type_sym->type),
              exc_type_sym->symbol_expr()}});
        }
      }
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
        search = coerce_element(search, data_type.element_type());
      // Build if-then-else chain: check from end to start
      exprt result = from_integer(-1, python_int_type()); // not found
      for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt match = equal_exprt{index_exprt{data, idx}, search};
        result = if_exprt{and_exprt{in_range, match}, idx, result};
      }
      // PLR list.index: raise ValueError when the value is absent.
      emit_conditional_exception(
        equal_exprt{result, from_integer(-1, result.type())}, "ValueError");
      return result;
    }
  }

  // PLib stdtypes: list.count(value) -- number of occurrences. Was unmodelled
  // (nondet). Sum 1 for each in-range element equal to the search value; works
  // for constant AND symbolic lists.
  if(method_name == "count")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt search = convert_expression(*as_array(args).begin());
      if(search.type() != data_type.element_type())
        search = coerce_element(search, data_type.element_type());
      exprt count = from_integer(0, python_int_type());
      for(int i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt match = and_exprt{
          binary_relation_exprt{idx, ID_lt, length},
          equal_exprt{index_exprt{data, idx}, search}};
        count = plus_exprt{
          count,
          if_exprt{
            match,
            from_integer(1, python_int_type()),
            from_integer(0, python_int_type())}};
      }
      return count;
    }
  }

  // PLib stdtypes: list.extend(iterable)
  if(method_name == "extend")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      // MATERIALISE a non-symbol argument (PLR §6.2 evaluation-once +
      // whole-group perf): the per-slot copy loop below uses `arg` ~35
      // times (16 element reads + lengths + guard); embedding a CALL
      // expression executed the callee once PER USE -- semantically wrong
      // for a side-effecting argument and the root of a 486x loop-unwind
      // blowup on aws_untagged (`all_resources.extend(self.get_ec2_
      // instances())` emitted 144 calls).
      if(
        arg.id() != ID_symbol && arg.id() != ID_dereference && !arg.is_nil() &&
        // Only the LIST branch multiplies the arg; the string branch folds
        // a compile-time constant (a temp would hide it from
        // extract_string_value -- regressed list_extend5).
        is_python_list_type(arg.type()))
      {
        static unsigned ext_arg_ctr = 0;
        const std::string tn = "__extend_arg_" + std::to_string(ext_arg_ctr++);
        const irep_idt tid{qualify_name(tn)};
        if(symbol_table.lookup(tid) == nullptr)
        {
          symbolt ts{tid, arg.type(), "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt tsym = symbol_table.lookup_ref(tid).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{tsym, arg});
        arg = tsym;
      }
      if(is_python_list_type(arg.type()))
      {
        member_exprt arg_len{arg, "length", signedbv_typet{64}};
        const auto &arg_data_type =
          to_array_type(to_struct_type(arg.type()).components()[1].type());
        member_exprt arg_data{arg, "data", arg_data_type};
        // Report (python-model-bound) + cut before the copy loop if the
        // extended length would exceed capacity: otherwise the data[length+i]
        // stores run past the modelled array silently.
        emit_count_capacity_guard(
          pending_checks,
          plus_exprt{length, arg_len},
          PYTHON_MAX_LIST_LENGTH,
          get_location(expr));
        // Copy elements: obj.data[obj.length + i] = arg.data[i].
        // Coerce each source element to the destination element type: the two
        // lists need not share an element type (e.g. extending a list[int]
        // with a list[python_value] produced by a reference-list concat, or a
        // heterogeneous literal). A raw copy would bit-reinterpret the element
        // (storing a python_value struct into an int slot, or vice versa) and
        // corrupt the value on read-back. coerce_element unwraps/wraps as
        // needed (python_value <-> scalar) so the stored value is faithful.
        const typet &dst_elem = data_type.element_type();
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt dst = plus_exprt{length, idx};
          exprt src_el = index_exprt{arg_data, idx};
          if(src_el.type() != dst_elem)
            src_el = coerce_element(src_el, dst_elem);
          pending_checks.push_back(code_ifthenelset{
            binary_relation_exprt{idx, ID_lt, arg_len},
            code_frontend_assignt{index_exprt{data, dst}, src_el}});
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
          emit_count_capacity_guard(
            pending_checks,
            plus_exprt{
              length,
              from_integer(
                static_cast<long long>(sv.value().size()), signedbv_typet{64})},
            PYTHON_MAX_LIST_LENGTH,
            get_location(expr));
          // Constant string: add each char as a single-char string
          for(std::size_t i = 0; i < sv.value().size(); i++)
          {
            exprt dst = plus_exprt{length, from_integer(i, signedbv_typet{64})};
            exprt ch_str = python_string_literal(std::string(1, sv.value()[i]));
            if(ch_str.type() != data_type.element_type())
              ch_str = coerce_element(ch_str, data_type.element_type());
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
        val = coerce_element(val, data_type.element_type());
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
      // PLR list.remove: raise ValueError when the value is absent.
      emit_conditional_exception(not_exprt{found}, "ValueError");
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
