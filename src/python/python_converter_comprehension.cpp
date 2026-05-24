/// Python to GOTO converter — ListComp (PLR §6.2.4) and
/// DictComp (PLR §6.2.6) implementation.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include "python_converter.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/json.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

// "A comprehension consists of a single expression followed by at least
// one for clause and zero or more for or if clauses."
exprt python_convertert::convert_list_comp(const jsont &expr)
{
  const jsont &elt = json_member(expr, "elt");
  const jsont &generators = json_member(expr, "generators");

  if(!generators.is_array() || as_array(generators).empty())
    return nil_exprt{};

  // Collect all generators (support nested for)
  struct gen_info
  {
    std::string var_name;
    std::vector<const jsont *> values;
    std::vector<exprt> const_values;
  };
  std::vector<gen_info> gens;

  for(const auto &gen : as_array(generators))
  {
    const jsont &gen_iter = json_member(gen, "iter");
    gen_info gi;
    gi.var_name = json_string(json_member(json_member(gen, "target"), "id"));
    // Literal-list form: [f(x) for x in [1, 2, 3]]
    if(is_node_type(gen_iter, "List"))
    {
      const jsont &elts = json_member(gen_iter, "elts");
      if(elts.is_array())
      {
        for(const auto &e : as_array(elts))
          gi.values.push_back(&e);
      }
    }
    // Name form: xs = [1, 2, 3]; [f(x) for x in xs]
    // Resolve via list_literals if the name points to a
    // tracked literal list.
    else if(is_node_type(gen_iter, "Name"))
    {
      std::string iter_name = json_string(json_member(gen_iter, "id"));
      auto it = list_literals.find(irep_idt{qualify_name(iter_name)});
      if(it != list_literals.end())
      {
        // The list_literal is a struct_exprt with [length,
        // data]. data is an array_exprt whose operands are
        // the values. But we need JSON pointers for the same
        // values — we can't plug non-JSON exprt values into
        // the gens structure.
        // Build synthetic JSON Constant nodes? Too complex.
        // Instead, just take the values directly into
        // a secondary 'const_values' stream.
        const exprt &list_struct = it->second;
        if(list_struct.operands().size() >= 2)
        {
          const exprt &length_expr = list_struct.operands()[0];
          if(length_expr.is_constant())
          {
            mp_integer len;
            if(!to_integer(to_constant_expr(length_expr), len))
            {
              const exprt &data_arr = list_struct.operands()[1];
              for(mp_integer i = 0; i < len; ++i)
              {
                auto idx = i.to_ulong();
                if(idx < data_arr.operands().size())
                  gi.const_values.push_back(data_arr.operands()[idx]);
              }
            }
          }
        }
      }
      if(gi.values.empty() && gi.const_values.empty())
      {
        log.warning() << "List comprehension iterable '" << iter_name
                      << "' is not a known literal list" << messaget::eom;
        return nil_exprt{};
      }
    }
    // Range form: [f(x) for x in range(N)] or range(a, b[, c])
    else if(
      is_node_type(gen_iter, "Call") &&
      is_node_type(json_member(gen_iter, "func"), "Name") &&
      json_string(json_member(json_member(gen_iter, "func"), "id")) == "range")
    {
      const jsont &args_n = json_member(gen_iter, "args");
      if(!args_n.is_array() || as_array(args_n).empty())
      {
        log.warning() << "range() in comprehension requires arguments"
                      << messaget::eom;
        return nil_exprt{};
      }
      std::vector<mp_integer> ints;
      bool ok = true;
      for(const auto &a : as_array(args_n))
      {
        exprt av = convert_expression(a);
        // Try constant
        if(av.is_constant() && av.type().id() == ID_signedbv)
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(av), v))
          {
            ints.push_back(v);
            continue;
          }
        }
        // Fall back to try_eval_double for tracked-symbol cases
        auto evd = try_eval_double(av);
        if(evd.has_value())
        {
          ints.push_back(mp_integer{static_cast<long long>(evd.value())});
          continue;
        }
        ok = false;
        break;
      }
      if(!ok || ints.size() < 1 || ints.size() > 3)
      {
        log.warning()
          << "range() in comprehension requires constant integer arguments"
          << messaget::eom;
        return nil_exprt{};
      }
      mp_integer start{0}, stop, step{1};
      if(ints.size() == 1)
        stop = ints[0];
      else
      {
        start = ints[0];
        stop = ints[1];
        if(ints.size() == 3)
          step = ints[2];
      }
      if(step == 0)
      {
        log.warning() << "range() step cannot be zero" << messaget::eom;
        return nil_exprt{};
      }
      typet i64 = signedbv_typet{64};
      if(step > 0)
      {
        for(mp_integer i = start; i < stop; i += step)
          gi.const_values.push_back(from_integer(i, i64));
      }
      else
      {
        for(mp_integer i = start; i > stop; i += step)
          gi.const_values.push_back(from_integer(i, i64));
      }
    }
    else
    {
      log.warning()
        << "List comprehension iterable shape not supported (only literal "
           "list, name-of-tracked-list, or range())"
        << messaget::eom;
      return nil_exprt{};
    }
    gens.push_back(std::move(gi));
  }

  if(gens.empty())
    return nil_exprt{};

  // Create symbols for all iteration variables
  for(auto &gi : gens)
  {
    std::string qname = qualify_name(gi.var_name);
    irep_idt sym_id{qname};
    if(symbol_table.lookup(sym_id) == nullptr)
    {
      symbolt sym{sym_id, python_int_type(), "python"};
      sym.base_name = gi.var_name;
      sym.is_lvalue = true;
      sym.is_state_var = true;
      symbol_table.add(sym);
    }
  }

  // Unroll all combinations
  // For single generator: iterate values
  // For nested: iterate cartesian product
  std::vector<std::vector<std::size_t>> combos;
  combos.push_back({});
  for(const auto &gi : gens)
  {
    std::vector<std::vector<std::size_t>> new_combos;
    // Use whichever source of values is populated.
    std::size_t n =
      gi.values.empty() ? gi.const_values.size() : gi.values.size();
    for(const auto &combo : combos)
    {
      for(std::size_t i = 0; i < n; i++)
      {
        auto new_combo = combo;
        new_combo.push_back(i);
        new_combos.push_back(std::move(new_combo));
      }
    }
    combos = std::move(new_combos);
  }

  // Evaluate elt for each combination
  exprt::operandst elements;
  for(const auto &combo : combos)
  {
    // Convert each iteration variable's value
    std::vector<std::pair<irep_idt, exprt>> bindings;
    for(std::size_t g = 0; g < gens.size(); g++)
    {
      exprt val;
      if(!gens[g].values.empty())
        val = convert_expression(*gens[g].values[combo[g]]);
      else
        val = gens[g].const_values[combo[g]];
      bindings.push_back({irep_idt{qualify_name(gens[g].var_name)}, val});
    }

    // Evaluate elt and substitute
    exprt elt_expr = convert_expression(elt);
    for(const auto &[sym_id, val] : bindings)
    {
      std::function<void(exprt &)> subst = [&](exprt &e)
      {
        if(e.id() == ID_symbol && to_symbol_expr(e).get_identifier() == sym_id)
          e = val;
        else
          for(auto &op : e.operands())
            subst(op);
      };
      subst(elt_expr);
    }
    // Check if conditions (filters) for this combination
    bool passes_filter = true;
    for(std::size_t g = 0; g < gens.size() && passes_filter; g++)
    {
      const jsont &gen = *std::next(as_array(generators).begin(), g);
      const jsont &ifs = json_member(gen, "ifs");
      if(ifs.is_array())
      {
        for(const auto &cond_json : as_array(ifs))
        {
          exprt cond_expr = convert_expression(cond_json);
          for(const auto &[sym_id, val] : bindings)
          {
            std::function<void(exprt &)> subst2 = [&](exprt &e)
            {
              if(
                e.id() == ID_symbol &&
                to_symbol_expr(e).get_identifier() == sym_id)
                e = val;
              else
                for(auto &op : e.operands())
                  subst2(op);
            };
            subst2(cond_expr);
          }
          // Try constant evaluation: check if all operands are constant
          // and the result can be computed
          // Try constant evaluation after substitution
          // Recursively simplify the expression
          std::function<exprt(const exprt &)> try_eval =
            [&](const exprt &e) -> exprt
          {
            if(e.is_constant())
              return e;
            // Simplify binary ops with constant operands
            if(e.operands().size() == 2)
            {
              exprt l = try_eval(e.operands()[0]);
              exprt r = try_eval(e.operands()[1]);
              if(
                l.is_constant() && r.is_constant() &&
                l.type().id() == ID_signedbv)
              {
                mp_integer a, b;
                if(
                  !to_integer(to_constant_expr(l), a) &&
                  !to_integer(to_constant_expr(r), b))
                {
                  if(e.id() == ID_mod && b != 0)
                    return from_integer(a % b, l.type());
                  if(e.id() == ID_plus)
                    return from_integer(a + b, l.type());
                  if(e.id() == ID_minus)
                    return from_integer(a - b, l.type());
                  if(e.id() == ID_mult)
                    return from_integer(a * b, l.type());
                  if(e.id() == ID_bitxor)
                  {
                    long long av = a.to_long(), bv = b.to_long();
                    return from_integer(av ^ bv, l.type());
                  }
                  if(e.id() == ID_div && b != 0)
                    return from_integer(a / b, l.type());
                  if(e.id() == ID_equal)
                    return a == b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_notequal)
                    return a != b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_lt)
                    return a < b ? static_cast<exprt>(true_exprt{})
                                 : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_gt)
                    return a > b ? static_cast<exprt>(true_exprt{})
                                 : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_le)
                    return a <= b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_ge)
                    return a >= b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                }
              }
            }
            // if_exprt: evaluate condition
            if(e.id() == ID_if && e.operands().size() == 3)
            {
              exprt c = try_eval(e.operands()[0]);
              if(c.is_true())
                return try_eval(e.operands()[1]);
              if(c.is_false())
                return try_eval(e.operands()[2]);
            }
            // and/or
            if(e.id() == ID_and && e.operands().size() == 2)
            {
              exprt l = try_eval(e.operands()[0]);
              exprt r = try_eval(e.operands()[1]);
              if(l.is_false() || r.is_false())
                return false_exprt{};
              if(l.is_true() && r.is_true())
                return true_exprt{};
            }
            if(e.id() == ID_not && e.operands().size() == 1)
            {
              exprt o = try_eval(e.operands()[0]);
              if(o.is_true())
                return false_exprt{};
              if(o.is_false())
                return true_exprt{};
            }
            return e;
          };
          exprt simplified = try_eval(cond_expr);
          if(simplified.is_false())
            passes_filter = false;
        }
      }
    }
    if(!passes_filter)
      continue;
    elements.push_back(elt_expr);
  }

  if(elements.empty())
    return nil_exprt{};

  // Build the result list
  typet elem_type = elements[0].type();
  struct_typet list_type = python_list_type(elem_type);
  array_typet data_type{
    elem_type, from_integer(PYTHON_MAX_LIST_LENGTH, python_int_type())};

  exprt::operandst data_elems;
  for(auto &e : elements)
  {
    if(e.type() != elem_type)
      e = typecast_exprt{e, elem_type};
    data_elems.push_back(e);
  }
  while(data_elems.size() < PYTHON_MAX_LIST_LENGTH)
    data_elems.push_back(safe_zero(elem_type));

  array_exprt data{std::move(data_elems), data_type};
  exprt length =
    from_integer(static_cast<long long>(elements.size()), python_int_type());

  return struct_exprt{{length, data}, list_type};
}

// PLR §6.2.7: dict comprehension '{k: v for x in xs}'. Shares the
// generator-unrolling logic with convert_list_comp; builds a
// python_dict struct (length, keys array, values array) at the end.
exprt python_convertert::convert_dict_comp(const jsont &expr)
{
  const jsont &key_expr_json = json_member(expr, "key");
  const jsont &val_expr_json = json_member(expr, "value");
  const jsont &generators = json_member(expr, "generators");

  if(!generators.is_array() || as_array(generators).empty())
    return nil_exprt{};

  // Collect generators: supported iterables are literal lists and
  // range() calls with constant-integer arguments. Anything else
  // falls back to a nondet dict over-approximation.
  struct gen_info
  {
    std::string var_name;
    // Either a list of jsont pointers from a literal [ ... ] or a
    // list of pre-computed integer values from a range().
    std::vector<const jsont *> json_values;
    std::vector<mp_integer> int_values;
    bool is_range = false;
  };
  std::vector<gen_info> gens;

  // Helper: try to evaluate range(a[, b[, c]]) with constant ints.
  auto try_range = [&](const jsont &call, std::vector<mp_integer> &out) -> bool
  {
    if(!is_node_type(call, "Call"))
      return false;
    const jsont &func = json_member(call, "func");
    if(!is_node_type(func, "Name"))
      return false;
    if(json_string(json_member(func, "id")) != "range")
      return false;
    const jsont &args_n = json_member(call, "args");
    if(!args_n.is_array() || as_array(args_n).empty())
      return false;
    std::vector<mp_integer> ints;
    for(const auto &a : as_array(args_n))
    {
      exprt av = convert_expression(a);
      if(!av.is_constant() || av.type().id() != ID_signedbv)
        return false;
      mp_integer v;
      if(to_integer(to_constant_expr(av), v))
        return false;
      ints.push_back(v);
    }
    mp_integer start{0}, stop, step{1};
    if(ints.size() == 1)
      stop = ints[0];
    else if(ints.size() == 2)
    {
      start = ints[0];
      stop = ints[1];
    }
    else if(ints.size() == 3)
    {
      start = ints[0];
      stop = ints[1];
      step = ints[2];
    }
    else
      return false;
    if(step == 0)
      return false;
    out.clear();
    if(step > 0)
    {
      for(mp_integer i = start; i < stop; i += step)
        out.push_back(i);
    }
    else
    {
      for(mp_integer i = start; i > stop; i += step)
        out.push_back(i);
    }
    return true;
  };

  for(const auto &gen : as_array(generators))
  {
    const jsont &gen_iter = json_member(gen, "iter");
    gen_info gi;
    gi.var_name = json_string(json_member(json_member(gen, "target"), "id"));
    std::vector<mp_integer> r_values;
    if(is_node_type(gen_iter, "List"))
    {
      const jsont &elts = json_member(gen_iter, "elts");
      if(elts.is_array())
      {
        for(const auto &e : as_array(elts))
          gi.json_values.push_back(&e);
      }
    }
    else if(try_range(gen_iter, r_values))
    {
      gi.is_range = true;
      gi.int_values = std::move(r_values);
    }
    else
    {
      log_overapprox(
        "dict comprehension with non-literal iterable: using nondet dict");
      return side_effect_expr_nondett{
        python_dict_type(python_value_type(), python_value_type()),
        source_locationt{}};
    }
    gens.push_back(std::move(gi));
  }

  // Register iteration-variable symbols.
  for(auto &gi : gens)
  {
    std::string qname = qualify_name(gi.var_name);
    irep_idt sym_id{qname};
    if(symbol_table.lookup(sym_id) == nullptr)
    {
      symbolt sym{sym_id, python_int_type(), "python"};
      sym.base_name = gi.var_name;
      sym.is_lvalue = true;
      sym.is_state_var = true;
      symbol_table.add(sym);
    }
  }

  // Cartesian-product unroll.
  std::vector<std::vector<std::size_t>> combos{{}};
  for(const auto &gi : gens)
  {
    const std::size_t n =
      gi.is_range ? gi.int_values.size() : gi.json_values.size();
    std::vector<std::vector<std::size_t>> next;
    for(const auto &combo : combos)
      for(std::size_t i = 0; i < n; i++)
      {
        auto c = combo;
        c.push_back(i);
        next.push_back(std::move(c));
      }
    combos = std::move(next);
  }

  // Evaluate (key, value) pairs for each combination, applying ifs.
  std::vector<std::pair<exprt, exprt>> pairs;
  for(const auto &combo : combos)
  {
    std::vector<std::pair<irep_idt, exprt>> bindings;
    for(std::size_t g = 0; g < gens.size(); g++)
    {
      exprt v;
      if(gens[g].is_range)
        v = from_integer(gens[g].int_values[combo[g]], python_int_type());
      else
        v = convert_expression(*gens[g].json_values[combo[g]]);
      bindings.push_back({irep_idt{qualify_name(gens[g].var_name)}, v});
    }

    std::function<void(exprt &)> subst = [&](exprt &e)
    {
      for(const auto &[sym_id, val] : bindings)
        if(e.id() == ID_symbol && to_symbol_expr(e).get_identifier() == sym_id)
        {
          e = val;
          return;
        }
      for(auto &op : e.operands())
        subst(op);
    };

    // Check filter 'if' clauses.
    bool passes = true;
    for(std::size_t g = 0; g < gens.size() && passes; g++)
    {
      const jsont &gen = *std::next(as_array(generators).begin(), g);
      const jsont &ifs = json_member(gen, "ifs");
      if(ifs.is_array())
      {
        for(const auto &cond_json : as_array(ifs))
        {
          exprt cond = convert_expression(cond_json);
          subst(cond);
          // Constant-fold obvious cases.
          if(cond.is_false())
          {
            passes = false;
            break;
          }
        }
      }
    }
    if(!passes)
      continue;

    exprt k = convert_expression(key_expr_json);
    exprt v = convert_expression(val_expr_json);
    subst(k);
    subst(v);
    // PLR §6.2.7: when a dict-comp key/value is a function call
    // whose only non-constant argument was the iteration variable,
    // the post-substitution expression is a side-effect (e.g.
    // {__string_len, __string_ptr} for str(i)) that no longer
    // refers to the constant. To recover the constant fold, when
    // the key was the AST `str(<iter-var>)` we re-convert it with
    // the iteration variable replaced by its concrete constant.
    auto try_constant_fold_call = [&](
                                    const jsont &call_ast,
                                    const std::vector<std::pair<irep_idt, exprt>>
                                      &binds) -> exprt {
      if(!is_node_type(call_ast, "Call"))
        return nil_exprt{};
      const jsont &func = json_member(call_ast, "func");
      if(!is_node_type(func, "Name"))
        return nil_exprt{};
      std::string fn_name = json_string(json_member(func, "id"));
      const jsont &args_n = json_member(call_ast, "args");
      if(!args_n.is_array() || as_array(args_n).size() != 1)
        return nil_exprt{};
      const jsont &arg = *as_array(args_n).begin();
      // Only support an argument that is the iteration variable
      // directly. (Compound expressions like str(i+1) would
      // require recursive AST evaluation.)
      if(!is_node_type(arg, "Name"))
        return nil_exprt{};
      std::string aname = json_string(json_member(arg, "id"));
      irep_idt qid{qualify_name(aname)};
      const exprt *bound = nullptr;
      for(const auto &[sid, val] : binds)
      {
        if(sid == qid)
        {
          bound = &val;
          break;
        }
      }
      if(bound == nullptr || !bound->is_constant())
        return nil_exprt{};
      mp_integer iv;
      if(
        bound->type().id() != ID_signedbv ||
        to_integer(to_constant_expr(*bound), iv))
        return nil_exprt{};
      // Fold known builtins.
      if(fn_name == "str")
        return python_string_literal(integer2string(iv));
      return nil_exprt{};
    };
    if(exprt folded_k = try_constant_fold_call(key_expr_json, bindings);
       folded_k.is_not_nil())
      k = std::move(folded_k);
    if(exprt folded_v = try_constant_fold_call(val_expr_json, bindings);
       folded_v.is_not_nil())
      v = std::move(folded_v);
    pairs.emplace_back(std::move(k), std::move(v));
  }

  // Choose key/value types from the first pair (fall back to generic).
  typet key_type =
    pairs.empty() ? python_string_type() : pairs.front().first.type();
  typet val_type =
    pairs.empty() ? python_int_type() : pairs.front().second.type();
  struct_typet dict_type = python_dict_type(key_type, val_type);
  const auto &keys_arr_type = to_array_type(dict_type.components()[1].type());
  const auto &vals_arr_type = to_array_type(dict_type.components()[2].type());

  exprt::operandst key_elems, val_elems;
  for(auto &p : pairs)
  {
    if(p.first.type() != key_type)
      p.first = safe_typecast(p.first, key_type);
    if(p.second.type() != val_type)
      p.second = safe_typecast(p.second, val_type);
    key_elems.push_back(p.first);
    val_elems.push_back(p.second);
  }
  while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
    key_elems.push_back(safe_zero(key_type));
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
