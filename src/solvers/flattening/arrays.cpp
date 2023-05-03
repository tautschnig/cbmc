/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "arrays.h"

#include <util/arith_tools.h>
#include <util/json.h>
#include <util/simplify_expr.h>
#include <util/std_expr.h>

#include <solvers/prop/literal_expr.h>
#include <solvers/prop/prop.h>

#define DEBUG_ARRAYST
#ifdef DEBUG_ARRAYST
#  include <util/format_expr.h>
#  include <util/string_utils.h>

#  include <iostream>
#endif

arrayst::arrayst(
  const namespacet &_ns,
  propt &_prop,
  message_handlert &_message_handler,
  bool _get_array_constraints)
  : equalityt(_prop, _message_handler),
    ns(_ns),
    message_handler(_message_handler)
{
  // get_array_constraints is true when --show-array-constraints is used
  get_array_constraints = _get_array_constraints;
}

void arrayst::record_array_index(const index_exprt &index)
{
  collect_indices(index);
}

literalt arrayst::record_array_equality(
  const equal_exprt &equality)
{
  const exprt &op0=equality.op0();
  const exprt &op1=equality.op1();

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    op0.type() == op1.type(),
    "record_array_equality got equality without matching types",
    irep_pretty_diagnosticst{equality});

  DATA_INVARIANT(
    op0.type().id() == ID_array,
    "record_array_equality parameter should be array-typed");

  literalt l = SUB::equality(op0, op1);

#ifdef DEBUG_ARRAYST
  std::cout << "LIT " << l.get() << " == " << format(equality) << '\n';
#endif

  const wegt::node_indext a1 = collect_arrays(op0);
  const wegt::node_indext a2 = collect_arrays(op1);

  // There may be trivial equalities, such as those generated by symex decls
  // (for the sole purpose of letting the solver know about some symbols).  We
  // do not need to add any edge.
  if(a1 == a2)
  {
    CHECK_RETURN(l.is_true());
    return l;
  }

  // one undirected edge for each array equality
  // TODO: we shouldn't later clean this up but instead use union-find from the
  // start
	if((op0.id() == ID_array || op0.id() == ID_array_comprehension || op0.id() == ID_array_of || op0.id() == ID_with) && op1.id() == ID_symbol)
  {
    weg.add_edge(a2, a1);
    weg[a1].in[a2] = literal_exprt{l};
    weg[a2].out[a1] = literal_exprt{l};
  }
  else if((op1.id() == ID_array || op1.id() == ID_array_comprehension || op1.id() == ID_array_of || op1.id() == ID_with) && op0.id() == ID_symbol)
  {
    weg.add_edge(a1, a2);
    weg[a2].in[a1] = literal_exprt{l};
    weg[a1].out[a2] = literal_exprt{l};
  }
  else
  {
    add_weg_edge(a1, a2, literal_exprt{l});
  }

  return l;
}

void arrayst::collect_indices(const exprt &expr)
{
  if(expr.id()!=ID_index)
  {
    if(expr.id() == ID_array_comprehension)
      array_comprehension_args.insert(
        to_array_comprehension_expr(expr).arg().get_identifier());

    for(const auto &op : expr.operands())
      collect_indices(op);
  }
  else
  {
    const index_exprt &e = to_index_expr(expr);

    if(
      e.index().id() == ID_symbol &&
      array_comprehension_args.count(
        to_symbol_expr(e.index()).get_identifier()) != 0)
    {
      return;
    }

    collect_indices(e.index());
    collect_indices(e.array());

    const typet &array_op_type = e.array().type();

    if(array_op_type.id()==ID_array)
    {
      const array_typet &array_type=
        to_array_type(array_op_type);

      if(is_unbounded_array(array_type))
      {
#ifdef DEBUG_ARRAYST
        std::cout << "RAI: " << format(e) << '\n';
#endif
        wegt::node_indext number = collect_arrays(e.array());
        index_map[number].insert(e.index());
      }
    }
  }
}

arrayst::wegt::node_indext arrayst::collect_arrays(const exprt &a)
{
  const wegt::node_indext a_index = arrays.number(a);
  // The following invariant ensures that we can use graph node indices and
  // array indices interchangeably.
  CHECK_RETURN(arrays.at(a_index) == a);
  expand_weg(a_index);

  const array_typet &array_type = to_array_type(a.type());

  if(a.id()==ID_with)
  {
    const with_exprt &with_expr=to_with_expr(a);

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == with_expr.old().type(),
      "collect_arrays got 'with' without matching types",
      irep_pretty_diagnosticst{a});

    for(std::size_t i = 1; i < with_expr.operands().size(); i += 2)
    {
      collect_indices(with_expr.operands()[i]);     // where
      if(with_expr.operands()[i + 1].type().id() == ID_array)
        collect_arrays(with_expr.operands()[i + 1]); // new value
      else
        collect_indices(with_expr.operands()[i + 1]); // new value
    }

    // record 'old'
    wegt::node_indext expr_old_index = collect_arrays(with_expr.old());

    // directed edge for each array update
    weg.add_edge(a_index, expr_old_index);
    weg[expr_old_index].in[a_index] = true_exprt{}; // with_expr;
    weg[a_index].out[expr_old_index] = true_exprt{}; // with_expr;
  }
  else if(a.id()==ID_update)
  {
    const update_exprt &update_expr=to_update_expr(a);

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == update_expr.old().type(),
      "collect_arrays got 'update' without matching types",
      irep_pretty_diagnosticst{a});

    UNIMPLEMENTED;
  }
  else if(a.id()==ID_if)
  {
    const if_exprt &if_expr=to_if_expr(a);

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == if_expr.true_case().type(),
      "collect_arrays got if without matching types",
      irep_pretty_diagnosticst{a});

    DATA_INVARIANT_WITH_DIAGNOSTICS(
      array_type == if_expr.false_case().type(),
      "collect_arrays got if without matching types",
      irep_pretty_diagnosticst{a});

    const wegt::node_indext expr_true_index =
      collect_arrays(if_expr.true_case());
    const wegt::node_indext expr_false_index =
      collect_arrays(if_expr.false_case());

    // add two directed edges
    weg.add_edge(a_index, expr_true_index);
    weg[expr_true_index].in[a_index] = if_expr.cond();
    weg[a_index].out[expr_true_index] = if_expr.cond();
    weg.add_edge(a_index, expr_false_index);
    weg[expr_false_index].in[a_index] = not_exprt{if_expr.cond()};
    weg[a_index].out[expr_false_index] = not_exprt{if_expr.cond()};
  }
  else if(a.id()==ID_symbol)
  {
  }
  else if(a.id()==ID_nondet_symbol)
  {
  }
  else if(a.id()==ID_member)
  {
    const auto &struct_op = to_member_expr(a).struct_op();

    DATA_INVARIANT(
      struct_op.id() == ID_symbol || struct_op.id() == ID_nondet_symbol,
      "unexpected array expression: member with '" + struct_op.id_string() +
        "'");
  }
  else if(a.id() == ID_array || a.id() == ID_string_constant)
  {
  }
  else if(a.id()==ID_array_of)
  {
  }
  else if(a.id()==ID_typecast)
  {
    const auto &typecast_op = to_typecast_expr(a).op();

    // cast between array types?
    DATA_INVARIANT(
      typecast_op.type().id() == ID_array,
      "unexpected array type cast from " + typecast_op.type().id_string());

    const wegt::node_indext op_index = collect_arrays(typecast_op);

    // TODO: not clear what the semantics should be for element types could be
    // different
    assert(false);
    add_weg_edge(a_index, op_index, literal_exprt{const_literal(true)});
  }
  else if(a.id()==ID_index)
  {
    // nested unbounded arrays
    collect_indices(a);
  }
  else if(a.id() == ID_array_comprehension)
  {
    const array_comprehension_exprt &array_comprehension =
      to_array_comprehension_expr(a);
    collect_indices(array_comprehension.body());
  }
  else
  {
    DATA_INVARIANT_WITH_DIAGNOSTICS(
      false,
      "unexpected array expression (collect_arrays): '",
      a.id_string(),
      "'");
  }

  return a_index;
}

exprt arrayst::weg_path_condition(
  const weg_patht &path,
  const exprt &index_a) const
{
  exprt::operandst cond;
  cond.reserve(path.size());

  for(const auto &pn : path)
  {
    if(!pn.edge.has_value())
      break;

#ifdef DEBUG_ARRAYST
    std::cout << "edge: " << pn.n << "->" << (*pn.edge)->first << " "
              << format(arrays[pn.n]) << "\n";
#endif

    const exprt &weg_edge = (*pn.edge)->second;

    // TODO: we should filter out trivially-false conjunctions when literals and
    // their negations or equalities and their notequal counterparts are
    // included
    if(weg_edge.id() == ID_with)
    {
      const auto &with_expr = to_with_expr(weg_edge);
      for(std::size_t i = 1; i + 1 < with_expr.operands().size(); i += 2)
      {
        notequal_exprt inequality(with_expr.operands()[i], index_a);
        cond.push_back(std::move(inequality));
#ifdef DEBUG_ARRAYST
        std::cout << "COND entry with[" << format(with_expr.operands()[i]) << "]: " <<
          format(cond.back()) << std::endl;
#endif
      }
    }
    else
    {
      DATA_INVARIANT_WITH_DIAGNOSTICS(weg_edge.is_boolean(), "path condition component should be ID_with or Boolean, found", weg_edge.id());
      cond.push_back(weg_edge);
#ifdef DEBUG_ARRAYST
        std::cout << "COND entry: " <<
          format(cond.back()) << std::endl;
#endif
    }
  }

  return simplify_expr(conjunction(cond), ns);
}

void arrayst::process_weg_path(const weg_patht &path)
{
  PRECONDITION(path.size() >= 2);
  const wegt::node_indext a = path.front().n;
  const wegt::node_indext b = path.back().n;

#ifdef DEBUG_ARRAYST
  std::cout << "PATH: ";
  for(const auto &n : path)
    std::cout << n.n << ",";
  std::cout << '\n';
#endif

#if 0
  // extensional array encoding -- do this first as it may add to the index sets
  exprt::operandst cond;
  cond.reserve(path.size());
  // collect all indices in with expressions along this path
  // record the first and last with expression
  // compute the path condition from a to first and last to b
  //weg_path_condition(path, nil_exprt{}, cond);

  implies_exprt implication(conjunction(cond), equal_exprt(arrays[a], arrays[b]));
#  ifdef DEBUG_ARRAYST
  std::cout << "E1: " << format(implication) << '\n';
#  endif
  set_to_true(implication);
#endif

  // do constraints
  const index_sett &index_set_a = index_map[a];
  const index_sett &index_set_b = index_map[b];

#ifdef DEBUG_ARRAYST
  std::cout << "b is: " << format(arrays[b]) << '\n';
#endif

  for(const auto &index_a : index_set_a)
  {
    // lazily instantiate read-over-write
    if(arrays[b].id() == ID_with)
    {
#if 0
      // we got x=(y with [i:=v, j:=w, ...])
      const auto &expr_b = to_with_expr(arrays[b]);
      for(std::size_t i = 1; i + 1 < expr_b.operands().size(); i += 2)
      {
        const exprt &index_b = expr_b.operands()[i];
        const exprt &value_b = expr_b.operands()[i + 1];

        // 1. we got a[i]...b[j], given as edges on the stack
        // 2. add (i=j AND path_cond)=>a[i]=b[j]
        // 3. The path condition requires the update indices
        //    on the stack to be different from i.
        exprt::operandst cond;
        cond.reserve(path.size() + 1);
        cond.push_back(equal_exprt(index_a, index_b));
        weg_path_condition(path, index_a, cond);

        // a_i=b_i
        index_exprt a_i(arrays[a], index_a);
        // cond => a_i=b_i
        implies_exprt implication(conjunction(cond), equal_exprt(a_i, value_b));
#  ifdef DEBUG_ARRAYST
        std::cout << "C2a: " << format(implication) << '\n';
#  endif
        set_to_true(implication);
      }
#endif
    }
    else if(arrays[b].id() == ID_array_of)
    {
#if 0
      const auto &expr_b = to_array_of_expr(arrays[b]);
      const exprt &value_b = expr_b.what();

      // 1. we got a[i]...b[j], given as edges on the stack
      // 2. add (i=j AND path_cond)=>a[i]=b[j]
      // 3. The path condition requires the update indices
      //    on the stack to be different from i.
      exprt::operandst cond;
      cond.reserve(path.size());
      weg_path_condition(path, index_a, cond);

      // a_i=value
      index_exprt a_i(arrays[a], index_a);
      // cond => a_i=b_i
      implies_exprt implication(conjunction(cond), equal_exprt(a_i, value_b));
#  ifdef DEBUG_ARRAYST
      std::cout << "C2b: " << format(implication) << '\n';
#  endif
      set_to_true(implication);
#endif
    }
    else if(arrays[b].id() == ID_array)
    {
#if 0
      // we got x=[a, b, c, ...]
      const auto &array_expr = to_array_expr(arrays[b]);
      for(std::size_t i = 0; i < array_expr.operands().size(); ++i)
      {
        const exprt index_b = from_integer(i, index_a.type());
        const exprt &value_b = array_expr.operands()[i];

        // 1. we got a[i]...b[j], given as edges on the stack
        // 2. add (i=j AND path_cond)=>a[i]=b[j]
        // 3. The path condition requires the update indices
        //    on the stack to be different from i.
        exprt::operandst cond;
        cond.reserve(path.size() + 1);
        cond.push_back(equal_exprt(index_a, index_b));
        weg_path_condition(path, index_a, cond);

        // a_i=b_i
        index_exprt a_i(arrays[a], index_a);
        // cond => a_i=b_i
        implies_exprt implication(conjunction(cond), equal_exprt(a_i, value_b));
#  ifdef DEBUG_ARRAYST
        std::cout << "C2c: " << format(implication) << '\n';
#  endif
        set_to_true(implication);
      }
#endif
    }
    else if(arrays[b].id() == ID_array_comprehension)
    {
#if 0
      const auto &expr_b = to_array_comprehension_expr(arrays[b]);
      exprt value_b = expr_b.instantiate({index_a});

      // 1. we got a[i]...b[j], given as edges on the stack
      // 2. add (i=j AND path_cond)=>a[i]=b[j]
      // 3. The path condition requires the update indices
      //    on the stack to be different from i.
      exprt::operandst cond;
      cond.reserve(path.size());
      weg_path_condition(path, index_a, cond);

      // a_i=value
      index_exprt a_i(arrays[a], index_a);
      // cond => a_i=b_i
      implies_exprt implication(true_exprt{}, /*conjunction(cond),*/ equal_exprt(a_i, value_b));
#  ifdef DEBUG_ARRAYST
      std::cout << "C2d: " << format(implication) << '\n';
#  endif
      set_to_true(implication);
#endif
    }
    else
    {
      DATA_INVARIANT_WITH_DIAGNOSTICS(
        arrays[b].id() == ID_nondet_symbol || arrays[b].id() == ID_symbol ||
          arrays[b].id() == ID_if || arrays[b].id() == ID_index,
        "expected symbol, if, or index; got ",
        arrays[b].pretty());
    }

    exprt cond = weg_path_condition(path, index_a);
#ifdef DEBUG_ARRAYST
    std::cout << "COND for " << format(index_a) << ": " << format(cond) << std::endl;
#endif
    if(cond.is_false())
      return;
    const exprt negated_path_cond = not_exprt{cond};

    // we handle same-array-different index in add_array_Ackermann_constraints
    const index_exprt a_i{arrays[a], index_a};
    for(const auto &index_b : index_set_b)
    {
      // skip over comparisons that are trivially false
      if(index_a.is_constant() && index_b.is_constant() && index_a != index_b)
      {
        continue;
      }

      // 1. we got a[i]...b[j], given as edges on the stack
      // 2. add (path_cond AND i=j)=>a[i]=b[j]
      // 3. The path condition requires the update indices
      //    on the stack to be different from i.

      // path_cond AND i=j => a_i=b_i
      if(index_a.is_constant() && index_b.is_constant() && index_a == index_b)
      {
        or_exprt implication{
          negated_path_cond,
          equal_exprt{a_i, index_exprt{arrays[b], index_b}}};
#ifdef DEBUG_ARRAYST
        std::cout << "C3: " << format(implication) << '\n';
#endif
        set_to_true(implication);
      }
      else
      {
        or_exprt implication{
          negated_path_cond,
          notequal_exprt{index_a, index_b},
          equal_exprt{a_i, index_exprt{arrays[b], index_b}}};
#ifdef DEBUG_ARRAYST
        std::cout << "C3: " << format(implication) << '\n';
#endif
        set_to_true(implication);
      }
    }
  }
}

void arrayst::process_weg_path(
  wegt::node_indext a,
  wegt::node_indext b,
  const std::unordered_map<exprt, exprt, irep_hash> &path_conditions
  )
{
  // 1. we got a[i]...b[j], given as edges on the stack
  // 2. add (i=j AND path_cond)=>a[i]=b[j]
  // 3. The path condition requires the update indices
  //    on the stack to be different from i.
  for(const auto &pc_entry : path_conditions)
  {
    const exprt &index_a = pc_entry.first;
    const exprt &cond = pc_entry.second;
#ifdef DEBUG_ARRAYST
      std::cout << "COND for index " << format(index_a) << " from " << a << " to " << b << ": " << format(cond) << std::endl;
#endif
    if(cond.is_false())
      continue;

    const index_exprt a_i{arrays[a], index_a};
    const exprt negated_path_cond = simplify_expr(not_exprt{cond}, ns);

    if(arrays[b].id() == ID_array)
    {
      // we got b=[x, y, z, ...]
      const auto &array_expr = to_array_expr(arrays[b]);

      // For a constant index_a just pick the value or leave it unconstrained if
      // the access would be out-of-bounds.
      if(index_a.is_constant())
      {
        mp_integer index = numeric_cast_v<mp_integer>(to_constant_expr(index_a));
        if(index >= 0 && index < array_expr.operands().size())
        {
          std::size_t index_int = numeric_cast_v<std::size_t>(index);
          or_exprt implication{
            negated_path_cond, equal_exprt{a_i, array_expr.operands()[index_int]}};
#  ifdef DEBUG_ARRAYST
          std::cout << "C2a: " << format(implication) << '\n';
#  endif
          set_to_true(implication);
        }

        continue;
      }

      // not a constant, compare to all indices in b
      for(std::size_t i = 0; i < array_expr.operands().size(); ++i)
      {
        const exprt index_b = from_integer(i, index_a.type());
        const exprt &value_b = array_expr.operands()[i];

        or_exprt implication{
          negated_path_cond,
          notequal_exprt{index_a, index_b},
          equal_exprt{a_i, value_b}};
#  ifdef DEBUG_ARRAYST
        std::cout << "C2b: " << format(implication) << '\n';
#  endif
        set_to_true(implication);
      }
    }
    else if(arrays[b].id() == ID_array_comprehension)
    {
      const auto &array_comprehension = to_array_comprehension_expr(arrays[b]);
      exprt value_b = array_comprehension.instantiate({index_a});

      or_exprt implication{
        negated_path_cond,
        equal_exprt{a_i, value_b}};
#  ifdef DEBUG_ARRAYST
      std::cout << "C2c: " << format(implication) << '\n';
#  endif
      set_to_true(implication);
    }
    else if(arrays[b].id() == ID_array_of)
    {
      const auto &array_of_expr = to_array_of_expr(arrays[b]);
      const auto &value_b = array_of_expr.what();

      or_exprt implication{
        negated_path_cond,
        equal_exprt{a_i, value_b}};
#  ifdef DEBUG_ARRAYST
      std::cout << "C2d: " << format(implication) << '\n';
#  endif
      set_to_true(implication);
    }
    else if(arrays[b].id() == ID_with)
    {
      // we got b=(x with [i:=v, j:=w, ...])
      // TODO: ensure set_to_true will not create previously unknown arrays, and must also not create new edges
        const with_exprt &with_expr = to_with_expr(arrays[b]);

      // first try to see whether we have an exact match for the index_a
      bool match_found = false;
      for(std::size_t i = 1; !match_found && i < with_expr.operands().size(); i += 2)
      {
        const exprt &index_b = with_expr.operands()[i];
        if(index_a == index_b)
        {
          match_found = true;

          const exprt &value_b = with_expr.operands()[i + 1];
          or_exprt implication{
            negated_path_cond,
            equal_exprt{a_i, value_b}};
#  ifdef DEBUG_ARRAYST
          std::cout << "C2e: " << format(implication) << '\n';
#  endif
          set_to_true(implication);
        }
      }

      // compare all updates if no exact match was found
      for(std::size_t i = 1; !match_found && i < with_expr.operands().size(); i += 2)
      {
        const exprt &index_b = with_expr.operands()[i];
        const exprt &value_b = with_expr.operands()[i + 1];

        or_exprt implication{
          negated_path_cond,
          notequal_exprt{index_a, index_b},
          equal_exprt{a_i, value_b}};
#  ifdef DEBUG_ARRAYST
        std::cout << "C2f: " << format(implication) << '\n';
#  endif
        set_to_true(implication);
      }
    }
    else if(a != b)
    {
      DATA_INVARIANT_WITH_DIAGNOSTICS(
        arrays[b].id() == ID_nondet_symbol || arrays[b].id() == ID_symbol ||
        arrays[b].id() == ID_if || arrays[b].id() == ID_index,
        "expected symbol, if, or index; got ",
        arrays[b].pretty());

#if 0
      const exprt &array_b_size = to_array_type(arrays[b].type()).size();
#endif

      // TODO: we handle same-array-different index in add_array_Ackermann_constraints
      // TODO: should we really do index_map[b] here, or just all of
      // index_map[a]?!
      for(const auto &index_b : index_map[b])
      {
#if 0
        // TODO: not sure whether this is needed
        exprt index_b_out_of_bounds = simplify_expr(binary_predicate_exprt{
                                                    typecast_exprt::conditional_cast(index_b, array_b_size.type()),
                                                    ID_ge,
                                                    array_b_size}, ns);
        if(index_b_out_of_bounds.is_true())
          continue;
#endif

        // skip over comparisons that are trivially false
        if(index_a.is_constant() && index_b.is_constant() && index_a != index_b)
        {
          continue;
        }

        or_exprt implication{
          negated_path_cond,
#if 0
            index_b_out_of_bounds,
#endif
            notequal_exprt{index_a, index_b},
            equal_exprt{a_i, index_exprt{arrays[b], index_b}}};
#ifdef DEBUG_ARRAYST
        std::cout << "C2g: " << format(implication) << '\n';
#endif
        set_to_true(implication);
        // candidate_for_index_not_found.erase(index_a);
      }
    }
  }
}

void arrayst::add_array_constraints()
{
  messaget log{message_handler};
  log.status() << "Array term preprocessing" << messaget::eom;
  // preprocessing
  for(std::size_t i = 0; i < arrays.size(); ++i)
  {
    if(arrays[i].id() == ID_array)
    {
#if 0
      const auto &array_expr = to_array_expr(arrays[i]);

      // auto &index_set_i = index_map[i];
      for(std::size_t j = 0; j < array_expr.operands().size(); ++j)
      {
        const exprt index_constant =
          from_integer(j, to_array_type(array_expr.type()).index_type());

        const exprt &value = array_expr.operands()[j];

        equal_exprt eq{index_exprt{array_expr, index_constant}, value};
#ifdef DEBUG_ARRAYST
        std::cout << "PREPROC: " << format(eq) << '\n';
#endif
        set_to_true(eq);
      }
#endif
    }
    else if(arrays[i].id() == ID_array_comprehension)
    {
#if 0
      const auto &array_comprehension = to_array_comprehension_expr(arrays[i]);
      auto &index_set_i = index_map[i];
      for(const auto r : weg.get_reachable(i, false))
        index_set_i.insert(index_map[r].begin(), index_map[r].end());

      for(const auto &index : index_set_i)
      {
        equal_exprt eq{
          index_exprt{array_comprehension, index},
          array_comprehension.instantiate({index})};
#ifdef DEBUG_ARRAYST
        std::cout << "PREPROC: " << format(eq) << '\n';
#endif
        set_to_true(eq);
      }
#else
      index_sett maybe_index_set;
      for(const auto r : weg.get_reachable(i, false))
        maybe_index_set.insert(index_map[r].begin(), index_map[r].end());

      const auto &array_comprehension = to_array_comprehension_expr(arrays[i]);
      for(const auto &index : maybe_index_set)
        collect_indices(array_comprehension.instantiate({index}));
#endif
    }
    else if(arrays[i].id() == ID_array_of)
    {
#if 0
      const auto &array_of_expr = to_array_of_expr(arrays[i]);
      const auto &value = array_of_expr.what();

      for(const auto &index : index_map[i])
      {
        equal_exprt eq{index_exprt{array_of_expr, index}, value};
#ifdef DEBUG_ARRAYST
        std::cout << "PREPROC: " << format(eq) << '\n';
#endif
        set_to_true(eq);
      }
#endif
    }
    else if(arrays[i].id() == ID_with)
    {
#if 0
      // create a copy as set_to_true may alter arrays
      const with_exprt with_expr = to_with_expr(arrays[i]);

      for(std::size_t j = 1; j < with_expr.operands().size(); j += 2)
      {
        equal_exprt eq{
          index_exprt{with_expr, with_expr.operands()[j]},
          with_expr.operands()[j + 1]};
#ifdef DEBUG_ARRAYST
        std::cout << "PREPROC: " << format(eq) << '\n';
#endif
        set_to_true(eq);
      }
#endif
    }
  }

  // TODO: we actually need a fixed point here
  for(std::size_t i = 0; i < arrays.size(); ++i)
  {
    if(arrays[i].id() == ID_array_comprehension)
    {
      index_sett maybe_index_set;
      for(const auto r : weg.get_reachable(i, false))
        maybe_index_set.insert(index_map[r].begin(), index_map[r].end());

      const auto &array_comprehension = to_array_comprehension_expr(arrays[i]);
      for(const auto &index : maybe_index_set)
        collect_indices(array_comprehension.instantiate({index}));
    }
  }

#ifdef DEBUG_ARRAYST
  std::cout << "digraph G {\n";
  for(wegt::node_indext i = 0; i < weg.size(); ++i)
  {
    std::cout << i
      << " [label=\"" << i << ": " << escape(format_to_string(arrays[i]));
    if(!index_map[i].empty())
    {
      std::cout << " with index set ";
    for(const auto &ind : index_map[i])
      std::cout << escape(format_to_string(ind)) << ", ";
    }
    std::cout << "\"];\n";
    for(const auto &edge : weg[i].out)
    {
      auto entry_it = weg[i].in.find(edge.first);
      std::string dir_annotation = "";
      if(entry_it != weg[i].in.end() && entry_it->second == edge.second)
      {
        if(edge.first < i)
          continue;
        else
          dir_annotation="dir=none, ";
      }
      std::cout << i << " -> " << edge.first << " [" << dir_annotation;
      if(!edge.second.is_true())
      {
        std::cout << "label=\"" << escape(format_to_string(edge.second)) << "\"";
      }
      std::cout << "];\n";
    }
  }
  std::cout << "}" << std::endl;
#endif

#if 0
  // disconnect non-store nodes with empty index sets
  for(wegt::node_indext a = 0; a < arrays.size(); ++a)
  {
    if(!index_map[a].empty() || weg[a].in.empty() || weg[a].out.empty())
      continue;
    if(arrays[a].id() == ID_array || arrays[a].id() == ID_array_comprehension ||
       arrays[a].id() == ID_array_of || arrays[a].id() == ID_with)
    {
      continue;
    }

    bool non_trival_loop = false;
    std::set<std::pair<wegt::node_indext, wegt::node_indext>> undirected_pairs_done;
    for(const auto &in_edge : weg[a].in)
    {
      if(non_trival_loop)
        break;

      const bool directed_in_edge = weg[a].out.find(in_edge.first) == weg[a].out.end();

      for(const auto &out_edge : weg[a].out)
      {
        if(in_edge.first == out_edge.first)
        {
          if(in_edge.second != out_edge.second)
          {
            non_trival_loop = true;
            break;
          }

#if 0
          DATA_INVARIANT_WITH_DIAGNOSTICS(in_edge.second == out_edge.second,
                         "no directed back&forth edge expected", in_edge.second.pretty(), out_edge.second.pretty());
#endif
          continue;
        }

        const bool directed_edge = directed_in_edge || weg[a].in.find(out_edge.first) == weg[a].in.end();
        if(!directed_edge && undirected_pairs_done.find({in_edge.first, out_edge.first}) != undirected_pairs_done.end())
        {
          continue;
        }

        const and_exprt cond{in_edge.second, out_edge.second};

        auto entry_out = weg[in_edge.first].out.insert({out_edge.first, cond});
        if(!entry_out.second)
        {
          or_exprt updated_cond{entry_out.first->second, cond};
          entry_out.first->second = updated_cond;
          weg[out_edge.first].in[in_edge.first] = updated_cond;
        }
        else
        {
          weg[out_edge.first].in.insert({in_edge.first, cond});
        }

        if(!directed_edge)
        {
          undirected_pairs_done.insert({in_edge.first, out_edge.first});
          undirected_pairs_done.insert({out_edge.first, in_edge.first});
          auto entry_reverse = weg[out_edge.first].out.insert({in_edge.first, cond});
          if(!entry_reverse.second)
          {
            or_exprt updated_cond{entry_reverse.first->second, cond};
            entry_reverse.first->second = updated_cond;
            weg[in_edge.first].in[out_edge.first] = updated_cond;
          }
          else
          {
            weg[in_edge.first].in.insert({out_edge.first, cond});
          }
        }
      }
    }

    if(non_trival_loop)
      continue;

    // disconnect the node
    for(const auto &in_edge : weg[a].in)
      weg[in_edge.first].erase_out(a);
    for(const auto &out_edge : weg[a].out)
      weg[out_edge.first].erase_in(a);
    weg[a].in.clear();
    weg[a].out.clear();
  }

#ifdef DEBUG_ARRAYST
  std::cout << "digraph G2 {\n";
  for(wegt::node_indext i = 0; i < weg.size(); ++i)
  {
    std::cout << i
      << " [label=\"" << i << ": " << escape(format_to_string(arrays[i]));
    if(!index_map[i].empty())
    {
      std::cout << " with index set ";
    for(const auto &ind : index_map[i])
      std::cout << escape(format_to_string(ind)) << ", ";
    }
    std::cout << "\"];\n";
    for(const auto &edge : weg[i].out)
    {
      auto entry_it = weg[i].in.find(edge.first);
      std::string dir_annotation = "";
      if(entry_it != weg[i].in.end() && entry_it->second == edge.second)
      {
        if(edge.first < i)
          continue;
        else
          dir_annotation="dir=none, ";
      }
      std::cout << i << " -> " << edge.first << " [" << dir_annotation;
      if(!edge.second.is_true())
      {
        std::cout << "label=\"" << escape(format_to_string(edge.second)) << "\"";
      }
      std::cout << "];\n";
    }
  }
  std::cout << "}" << std::endl;
#endif
#endif

  // Implement Algorithms 7.4.1 and 7.4.2 by collecting path conditions for all
  // simple paths instead of iterating over all pairs of arrays and indices.
  // Path conditions are computed via a breadth-first search from each node.

#if 0
  // Avoid redundant constraints (path from a to b yields the same constraints
  // as a path from b to a, if there is such a path).
  std::map<wegt::node_indext, std::set<wegt::node_indext>> paths_done;
#endif

  log.status() << "Traversing weak equivalence graph" << messaget::eom;
  for(wegt::node_indext a = 0; a < arrays.size(); ++a)
  {
#ifdef DEBUG_ARRAYST
    std::cout << "a is: " << format(arrays[a]) << '\n';
#endif

    const auto &index_set_a = index_map[a];

    // BFS from a to anything reachable in 'weg'
    std::vector<bool> visited(weg.size(), false);
    visited[a] = true;
    std::vector<wegt::node_indext> bfs_queue(1, a);
    bfs_queue.reserve(weg.size());

    using pc_mapt = std::unordered_map<exprt, exprt, irep_hash>;
    std::vector<pc_mapt> path_conditions(weg.size(), pc_mapt{});
#if 0
    const exprt &array_a_size = to_array_type(arrays[a].type()).size();
#endif
    for(const auto &index : index_set_a)
    {
#if 0
      // TODO: would true_exprt{} instead of the bounds-checked version do the
      // job?
      path_conditions[a][index] =
        simplify_expr(binary_predicate_exprt{
                      typecast_exprt::conditional_cast(index, array_a_size.type()),
                      ID_lt,
                      array_a_size}, ns);
#else
      path_conditions[a][index] = true_exprt{};
#endif
    }

    // if a has a non-empty index set and is an array constructor, we need to
    // add constraints that might otherwise be created by preprocessing
    process_weg_path(a, a, path_conditions[a]);

    while(!bfs_queue.empty())
    {
      wegt::node_indext current_node = bfs_queue.back();
      bfs_queue.pop_back();
      for(const auto &edge : weg[current_node].out)
      {
        DATA_INVARIANT(
          edge.first != current_node,
          "found node-local loop in weak equivalence graph");

        if(visited[edge.first])
        {
          // we may be returning via an undirected edge to a node that we have
          // already completed, no need for a further update of path conditions
          if(weg[current_node].in.find(edge.first) != weg[current_node].in.end())
            continue;
        }
        else
        {
          visited[edge.first] = true;
          bfs_queue.push_back(edge.first);
          for(const auto &index : index_set_a)
            path_conditions[edge.first][index] = false_exprt{};
        }

        for(const auto &index_a : index_set_a)
        {
          auto current_pc_entry = path_conditions[current_node].find(index_a);
          CHECK_RETURN(current_pc_entry != path_conditions[current_node].end());
          const exprt &current_path_condition = current_pc_entry->second;

          auto target_pc_entry = path_conditions[edge.first].find(index_a);
          CHECK_RETURN(target_pc_entry != path_conditions[edge.first].end());
          exprt &target_path_condition = target_pc_entry->second;

          if(current_path_condition.is_false())
          {
            target_path_condition = current_path_condition;
            continue;
          }

          exprt::operandst conjuncts(1, current_path_condition);

          if(arrays[current_node].id() == ID_with)
          {
            const auto &with_expr = to_with_expr(arrays[current_node]);
            const auto &operands = with_expr.operands();
            conjuncts.reserve(operands.size() / 2 + 1);

            for(std::size_t i = 1; i + 1 < operands.size(); i += 2)
              conjuncts.push_back(notequal_exprt{operands[i], index_a});
          }
          else
          {
            DATA_INVARIANT_WITH_DIAGNOSTICS(edge.second.is_boolean(), "path condition component should be ID_with or Boolean, found", edge.second.id());
            conjuncts.push_back(edge.second);
          }

          target_path_condition = simplify_expr(or_exprt{target_path_condition, conjunction(conjuncts)}, ns);
        }
      }
    }

    // TODO: this isn't sufficient, we need to handle this symbolically
    std::set<exprt> candidate_for_index_not_found = index_set_a;
    for(wegt::node_indext b = 0; b < arrays.size(); ++b)
    {
      if(a == b || !visited[b])
        continue;

      process_weg_path(a, b, path_conditions[b]);
    }

    if(!candidate_for_index_not_found.empty())
    {
      needs_Ackermann_constraints.insert(a);
    }
  }

  // add the Ackermann constraints
  log.status() << "Adding Ackermann constraints" << messaget::eom;
  add_array_Ackermann_constraints();

  log.status() << "Completed array post-processing" << messaget::eom;
}

#if 1
void arrayst::add_array_Ackermann_constraints()
{
  // this is quadratic!

#  ifdef DEBUG_ARRAYST
  std::cout << "arrays.size(): " << arrays.size() << '\n';
#  endif

  // iterate over arrays
  // We need these constraints whenever two indices may be the same but there
  // was no recorded write at that index. This also happens with array literals.
  // for(std::size_t i = 0; i < arrays.size(); i++)
  for(auto i : needs_Ackermann_constraints)
  {
    const index_sett &index_set = index_map[i];

#  ifdef DEBUG_ARRAYST
    std::cout << "index_set.size() of " << i << '/' << arrays.size() - 1 << ": " << format(arrays[i]) << ": " << index_set.size() << '\n';
#  endif

    // iterate over indices, 2x!
    for(index_sett::const_iterator i1 = index_set.begin();
        i1 != index_set.end();
        i1++)
    {
      index_sett::const_iterator next = i1;
      next++;
      for(index_sett::const_iterator i2 = next; i2 != index_set.end(); i2++)
      {
        if(i1->is_constant() && i2->is_constant())
          continue;

        // index equality
        equal_exprt indices_equal(*i1, *i2);

        if(indices_equal.op0().type() != indices_equal.op1().type())
        {
          indices_equal.op1() =
            typecast_exprt(indices_equal.op1(), indices_equal.op0().type());
        }

        literalt indices_equal_lit = convert(indices_equal);

        if(indices_equal_lit != const_literal(false))
        {
          const typet &subtype = to_array_type(arrays[i].type()).element_type();
          index_exprt index_expr1(arrays[i], *i1, subtype);

          index_exprt index_expr2 = index_expr1;
          index_expr2.index() = *i2;

          equal_exprt values_equal(index_expr1, index_expr2);
          prop.lcnf(!indices_equal_lit, convert(values_equal));

#  ifdef DEBUG_ARRAYST
          std::cout << "C4: " << format(indices_equal) << " => "
                    << format(values_equal) << '\n';
#  endif
        }
      }
    }
  }
}
#endif

std::string arrayst::enum_to_string(constraint_typet type)
{
  switch(type)
  {
  case constraint_typet::ARRAY_ACKERMANN:
    return "arrayAckermann";
  case constraint_typet::ARRAY_WITH:
    return "arrayWith";
  case constraint_typet::ARRAY_IF:
    return "arrayIf";
  case constraint_typet::ARRAY_OF:
    return "arrayOf";
  case constraint_typet::ARRAY_TYPECAST:
    return "arrayTypecast";
  case constraint_typet::ARRAY_CONSTANT:
    return "arrayConstant";
  case constraint_typet::ARRAY_COMPREHENSION:
    return "arrayComprehension";
  case constraint_typet::ARRAY_EQUALITY:
    return "arrayEquality";
  default:
    UNREACHABLE;
  }
}

void arrayst::display_array_constraint_count()
{
  json_objectt json_result;
  json_objectt &json_array_theory =
    json_result["arrayConstraints"].make_object();

  size_t num_constraints = 0;

  array_constraint_countt::iterator it = array_constraint_count.begin();
  while(it != array_constraint_count.end())
  {
    std::string contraint_type_string = enum_to_string(it->first);
    json_array_theory[contraint_type_string] =
      json_numbert(std::to_string(it->second));

    num_constraints += it->second;
    it++;
  }

  json_result["numOfConstraints"] =
    json_numbert(std::to_string(num_constraints));
  log.status() << ",\n" << json_result;
}
