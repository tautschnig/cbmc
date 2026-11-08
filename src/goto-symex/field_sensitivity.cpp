/*******************************************************************\

Module: Field-sensitive SSA

Author: Michael Tautschnig

\*******************************************************************/

#include "field_sensitivity.h"

#include <util/arith_tools.h>
#include <util/byte_operators.h>
#include <util/c_types.h>
#include <util/pointer_offset_size.h>

#include "goto_symex_state.h"
#include "simplify_expr_with_value_set.h"
#include "symex_target.h"

#define ENABLE_ARRAY_FIELD_SENSITIVITY

/// Whether the object that \p ssa_expr is (part of) is kept whole (see
/// field_sensitivityt): an object of static lifetime whose declared type is
/// a const-qualified struct or an array of const-qualified elements. The
/// decision rests on the symbol table rather than on the type of
/// \p ssa_expr, as the latter may differ in its qualifiers between uses of
/// the same object (for example, in C++ an argument converted to a const
/// parameter type), while the decision must be the same for all of them.
static bool is_undivided_constant(const namespacet &ns, const ssa_exprt &expr)
{
  const symbolt *symbol;
  if(ns.lookup(expr.get_object_name(), symbol) || !symbol->is_static_lifetime)
    return false;

  const typet &type = symbol->type;
  if(type.id() == ID_array)
    return to_array_type(type).element_type().get_bool(ID_C_constant);
  return (type.id() == ID_struct || type.id() == ID_struct_tag) &&
         type.get_bool(ID_C_constant);
}

std::size_t
field_sensitivityt::fields_of(const namespacet &ns, const typet &type) const
{
  const std::size_t limit = max_divided_fields() + 1;
  if(
    type.id() == ID_struct || type.id() == ID_struct_tag ||
    type.id() == ID_union || type.id() == ID_union_tag)
  {
    const auto cached = fields_cache.find(type);
    if(cached != fields_cache.end())
      return cached->second;
    const struct_union_typet::componentst &components =
      (type.id() == ID_struct_tag || type.id() == ID_union_tag)
        ? ns.follow_tag(to_struct_or_union_tag_type(type)).components()
        : to_struct_union_type(type).components();
    std::size_t result = 0;
    for(const auto &component : components)
      result = std::min(limit, result + fields_of(ns, component.type()));
    result = std::max(result, std::size_t{1});
    fields_cache.emplace(type, result);
    return result;
  }
#ifdef ENABLE_ARRAY_FIELD_SENSITIVITY
  if(type.id() == ID_array && to_array_type(type).size().is_constant())
  {
    const auto cached = fields_cache.find(type);
    if(cached != fields_cache.end())
      return cached->second;
    const auto &array_type = to_array_type(type);
    const mp_integer size =
      numeric_cast_v<mp_integer>(to_constant_expr(array_type.size()));
    const std::size_t result =
      is_divisible_array(ns, size, array_type.element_type())
        ? numeric_cast_v<std::size_t>(
            size * fields_of(ns, array_type.element_type()))
        : 1;
    fields_cache.emplace(type, result);
    return result;
  }
#endif
  return 1;
}

bool field_sensitivityt::is_divisible_array(
  const namespacet &ns,
  const mp_integer &size,
  const typet &element_type) const
{
  return size >= 0 && size <= max_field_sensitivity_array_size &&
         size * fields_of(ns, element_type) <= max_divided_fields();
}

exprt field_sensitivityt::apply(
  const namespacet &ns,
  goto_symex_statet &state,
  ssa_exprt ssa_expr,
  bool write) const
{
  if(write)
    return std::move(ssa_expr);
  else
    return get_fields(ns, state, ssa_expr, true);
}

exprt field_sensitivityt::apply_byte_extract(
  const namespacet &ns,
  goto_symex_statet &state,
  const byte_extract_exprt &be,
  bool write) const
{
  if(be.op().type().id() != ID_union && be.op().type().id() != ID_union_tag)
    return be;

  // For non-constant offsets, decompose through the widest union member so
  // that field sensitivity can process the resulting struct/array expression.
  // This avoids materialising the full union as a flat bitvector in the SMT
  // encoding. The operand need not be an SSA expression (e.g., it may be an
  // index into an array of unions when array field sensitivity is disabled).
  if(!be.offset().is_constant())
  {
    const union_typet &union_type =
      be.op().type().id() == ID_union_tag
        ? ns.follow_tag(to_union_tag_type(be.op().type()))
        : to_union_type(be.op().type());

    const auto union_size = pointer_offset_bits(union_type, ns);
    const auto widest = union_type.find_widest_union_component(ns);
    if(
      widest.has_value() && union_size.has_value() &&
      widest->second == *union_size)
    {
      byte_extract_exprt new_be{
        be.id(),
        member_exprt{be.op(), widest->first.get_name(), widest->first.type()},
        be.offset(),
        be.get_bits_per_byte(),
        be.type()};
      return apply(ns, state, std::move(new_be), write);
    }

    return be;
  }

  if(!is_ssa_expr(be.op()))
    return be;

  const union_typet &type = be.op().type().id() == ID_union_tag
                              ? ns.follow_tag(to_union_tag_type(be.op().type()))
                              : to_union_type(be.op().type());
  for(const auto &comp : type.components())
  {
    ssa_exprt tmp = to_ssa_expr(be.op());
    bool was_l2 = !tmp.get_level_2().empty();
    tmp.remove_level_2();
    const member_exprt member{tmp.get_original_expr(), comp};
    auto recursive_member =
      get_subexpression_at_offset(member, be.offset(), be.type(), ns);
    if(!recursive_member.has_value())
      continue;

    // We need to inspect the access path as the resulting expression may
    // involve index expressions. When array field sensitivity is disabled
    // or the size of the array that is indexed into is larger than
    // max_field_sensitivity_array_size then only the expression up to (but
    // excluding) said index expression can be turned into an ssa_exprt.
    exprt full_exprt = *recursive_member;
    exprt *for_ssa = &full_exprt;
    exprt *parent = for_ssa;
    while(parent->id() == ID_typecast)
      parent = &to_typecast_expr(*parent).op();
    while(parent->id() == ID_member || parent->id() == ID_index)
    {
      if(parent->id() == ID_member)
      {
        parent = &to_member_expr(*parent).compound();
      }
      else
      {
        parent = &to_index_expr(*parent).array();
#ifdef ENABLE_ARRAY_FIELD_SENSITIVITY
        if(
          !to_array_type(parent->type()).size().is_constant() ||
          !is_divisible_array(
            ns,
            numeric_cast_v<mp_integer>(
              to_constant_expr(to_array_type(parent->type()).size())),
            to_array_type(parent->type()).element_type()))
        {
          for_ssa = parent;
        }
#else
        for_ssa = parent;
#endif // ENABLE_ARRAY_FIELD_SENSITIVITY
      }
    }

    if(for_ssa->id() == ID_index || for_ssa->id() == ID_member)
    {
      tmp.type() = for_ssa->type();
      tmp.set_expression(*for_ssa);
      if(was_l2)
      {
        *for_ssa =
          apply(ns, state, state.rename(std::move(tmp), ns).get(), write);
      }
      else
        *for_ssa = apply(ns, state, std::move(tmp), write);

      return full_exprt;
    }
    else if(for_ssa->id() == ID_typecast)
    {
      if(was_l2)
      {
        *for_ssa = apply(ns, state, state.rename(*for_ssa, ns).get(), write);
      }
      else
        *for_ssa = apply(ns, state, std::move(*for_ssa), write);

      return full_exprt;
    }
  }

  return be;
}

exprt field_sensitivityt::apply(
  const namespacet &ns,
  goto_symex_statet &state,
  exprt expr,
  bool write) const
{
  if(expr.id() != ID_address_of)
  {
    Forall_operands(it, expr)
      *it = apply(ns, state, std::move(*it), write);
  }

  if(!write && is_ssa_expr(expr))
  {
    return get_fields(ns, state, to_ssa_expr(expr), true);
  }
  else if(
    !write && expr.id() == ID_member &&
    to_member_expr(expr).struct_op().id() == ID_struct)
  {
    return simplify_opt(std::move(expr), state.value_set, ns);
  }
#ifdef ENABLE_ARRAY_FIELD_SENSITIVITY
  else if(
    !write && expr.id() == ID_index &&
    to_index_expr(expr).array().id() == ID_array)
  {
    return simplify_opt(std::move(expr), state.value_set, ns);
  }
#endif // ENABLE_ARRAY_FIELD_SENSITIVITY
  else if(expr.id() == ID_member)
  {
    // turn a member-of-an-SSA-expression into a single SSA expression, thus
    // encoding the member as an individual symbol rather than only the full
    // struct
    member_exprt &member = to_member_expr(expr);

    if(
      is_ssa_expr(member.struct_op()) &&
      !is_undivided_constant(ns, to_ssa_expr(member.struct_op())) &&
      (member.struct_op().type().id() == ID_struct ||
       member.struct_op().type().id() == ID_struct_tag ||
       member.struct_op().type().id() == ID_union ||
       member.struct_op().type().id() == ID_union_tag))
    {
      // place the entire member expression, not just the struct operand, in an
      // SSA expression
      ssa_exprt tmp = to_ssa_expr(member.struct_op());
      bool was_l2 = !tmp.get_level_2().empty();

      tmp.remove_level_2();
      member.struct_op() = tmp.get_original_expr();
      tmp.set_expression(member);
      if(was_l2)
        return apply(ns, state, state.rename(std::move(tmp), ns).get(), write);
      else
        return apply(ns, state, std::move(tmp), write);
    }
  }
  else if(
    !write && (expr.id() == ID_byte_extract_little_endian ||
               expr.id() == ID_byte_extract_big_endian))
  {
    return apply_byte_extract(ns, state, to_byte_extract_expr(expr), write);
  }
#ifdef ENABLE_ARRAY_FIELD_SENSITIVITY
  else if(expr.id() == ID_index)
  {
    // turn a index-of-an-SSA-expression into a single SSA expression, thus
    // encoding the index access into an array as an individual symbol rather
    // than only the full array
    index_exprt &index = to_index_expr(expr);
    if(should_simplify)
    {
      simplify_expr_with_value_sett{state.value_set, language_mode, ns}
        .simplify(index.index());
    }

    if(
      is_ssa_expr(index.array()) && index.array().type().id() == ID_array &&
      !is_undivided_constant(ns, to_ssa_expr(index.array())) &&
      index.index().is_constant())
    {
      // place the entire index expression, not just the array operand, in an
      // SSA expression
      ssa_exprt tmp = to_ssa_expr(index.array());
      auto l2_index = state.rename(index.index(), ns).get();
      if(should_simplify)
      {
        simplify_expr_with_value_sett{state.value_set, language_mode, ns}
          .simplify(l2_index);
      }
      bool was_l2 = !tmp.get_level_2().empty();
      exprt l2_size =
        state.rename(to_array_type(index.array().type()).size(), ns).get();
      if(l2_size.is_nil() && index.array().id() == ID_symbol)
      {
        // In case the array type was incomplete, attempt to retrieve it from
        // the symbol table.
        const symbolt *array_from_symbol_table = ns.get_symbol_table().lookup(
          to_symbol_expr(index.array()).get_identifier());
        if(array_from_symbol_table != nullptr)
          l2_size = to_array_type(array_from_symbol_table->type).size();
      }

      if(
        l2_size.is_constant() &&
        is_divisible_array(
          ns,
          numeric_cast_v<mp_integer>(to_constant_expr(l2_size)),
          to_array_type(index.array().type()).element_type()))
      {
        if(l2_index.is_constant())
        {
          // place the entire index expression, not just the array operand,
          // in an SSA expression
          ssa_exprt ssa_array = to_ssa_expr(index.array());
          ssa_array.remove_level_2();
          index.array() = ssa_array.get_original_expr();
          index.index() = l2_index;
          tmp.set_expression(index);
          if(was_l2)
          {
            return apply(
              ns, state, state.rename(std::move(tmp), ns).get(), write);
          }
          else
            return apply(ns, state, std::move(tmp), write);
        }
        else if(!write)
        {
          // Expand the array and return `{array[0]; array[1]; ...}[index]`
          exprt expanded_array =
            get_fields(ns, state, to_ssa_expr(index.array()), true);
          return index_exprt{std::move(expanded_array), index.index()};
        }
      }
    }
  }
#endif // ENABLE_ARRAY_FIELD_SENSITIVITY
  return expr;
}

exprt field_sensitivityt::get_fields(
  const namespacet &ns,
  goto_symex_statet &state,
  const ssa_exprt &ssa_expr,
  bool disjoined_fields_only) const
{
  if(is_undivided_constant(ns, ssa_expr))
    return ssa_expr;

  if(
    ssa_expr.type().id() == ID_struct ||
    ssa_expr.type().id() == ID_struct_tag ||
    (!disjoined_fields_only && (ssa_expr.type().id() == ID_union ||
                                ssa_expr.type().id() == ID_union_tag)))
  {
    const struct_union_typet::componentst &components =
      (ssa_expr.type().id() == ID_struct_tag ||
       ssa_expr.type().id() == ID_union_tag)
        ? ns.follow_tag(to_struct_or_union_tag_type(ssa_expr.type()))
            .components()
        : to_struct_union_type(ssa_expr.type()).components();

    exprt::operandst fields;
    fields.reserve(components.size());

    const exprt &compound_op = ssa_expr.get_original_expr();

    for(const auto &comp : components)
    {
      ssa_exprt tmp = ssa_expr;
      bool was_l2 = !tmp.get_level_2().empty();
      tmp.remove_level_2();
      tmp.set_expression(
        member_exprt{compound_op, comp.get_name(), comp.type()});
      exprt field = get_fields(ns, state, tmp, disjoined_fields_only);
      if(was_l2)
      {
        fields.emplace_back(state.rename(std::move(field), ns).get());
      }
      else
        fields.emplace_back(std::move(field));
    }

    if(
      disjoined_fields_only && (ssa_expr.type().id() == ID_struct ||
                                ssa_expr.type().id() == ID_struct_tag))
    {
      return struct_exprt{std::move(fields), ssa_expr.type()};
    }
    else
      return field_sensitive_ssa_exprt{ssa_expr, std::move(fields)};
  }
#ifdef ENABLE_ARRAY_FIELD_SENSITIVITY
  else if(
    ssa_expr.type().id() == ID_array &&
    to_array_type(ssa_expr.type()).size().is_constant())
  {
    const mp_integer mp_array_size = numeric_cast_v<mp_integer>(
      to_constant_expr(to_array_type(ssa_expr.type()).size()));
    if(!is_divisible_array(
         ns, mp_array_size, to_array_type(ssa_expr.type()).element_type()))
      return ssa_expr;

    const array_typet &type = to_array_type(ssa_expr.type());
    const std::size_t array_size = numeric_cast_v<std::size_t>(mp_array_size);

    array_exprt::operandst elements;
    elements.reserve(array_size);

    const exprt &array = ssa_expr.get_original_expr();

    for(std::size_t i = 0; i < array_size; ++i)
    {
      const index_exprt index(array, from_integer(i, type.index_type()));
      ssa_exprt tmp = ssa_expr;
      bool was_l2 = !tmp.get_level_2().empty();
      tmp.remove_level_2();
      tmp.set_expression(index);
      exprt element = get_fields(ns, state, tmp, disjoined_fields_only);
      if(was_l2)
      {
        elements.emplace_back(state.rename(std::move(element), ns).get());
      }
      else
        elements.emplace_back(std::move(element));
    }

    if(disjoined_fields_only)
      return array_exprt(std::move(elements), type);
    else
      return field_sensitive_ssa_exprt{ssa_expr, std::move(elements)};
  }
#endif // ENABLE_ARRAY_FIELD_SENSITIVITY
  else
    return ssa_expr;
}

void field_sensitivityt::field_assignments(
  const namespacet &ns,
  goto_symex_statet &state,
  const ssa_exprt &lhs,
  const exprt &rhs,
  symex_targett &target,
  bool allow_pointer_unsoundness,
  const std::optional<std::pair<mp_integer, mp_integer>> &updated_bytes) const
{
  const exprt lhs_fs = get_fields(ns, state, lhs, false);

  if(lhs != lhs_fs)
  {
    field_assignments_rec(
      ns,
      state,
      lhs_fs,
      rhs,
      target,
      allow_pointer_unsoundness,
      updated_bytes,
      mp_integer{0});
    // Erase the composite symbol from our working state. Note that we need to
    // have it in the propagation table and the value set while doing the field
    // assignments, thus we cannot skip putting it in there above.
    if(is_divisible(ns, lhs, true))
    {
      state.propagation.erase_if_exists(lhs.get_identifier());
      state.value_set.erase_symbol(lhs, ns);
    }
  }
}

/// Assign to the individual fields \p lhs_fs of a non-expanded symbol \p lhs.
/// This is required whenever prior steps have updated the full object rather
/// than individual fields, e.g., in case of assignments to an array at an
/// unknown index.
/// \param ns: a namespace to resolve type symbols/tag types
/// \param state: symbolic execution state
/// \param lhs_fs: expanded symbol
/// \param ssa_rhs: right-hand-side value to assign
/// \param target: symbolic execution equation store
/// \param allow_pointer_unsoundness: allow pointer unsoundness
void field_sensitivityt::field_assignments_rec(
  const namespacet &ns,
  goto_symex_statet &state,
  const exprt &lhs_fs,
  const exprt &ssa_rhs,
  symex_targett &target,
  bool allow_pointer_unsoundness,
  const std::optional<std::pair<mp_integer, mp_integer>> &updated_bytes,
  const std::optional<mp_integer> &offset) const
{
  // Fields outside the bytes that the assignment changes keep their values.
  if(updated_bytes.has_value() && offset.has_value())
  {
    const auto size = pointer_offset_size(lhs_fs.type(), ns);
    if(
      size.has_value() &&
      (*offset + *size <= updated_bytes->first ||
       *offset >= updated_bytes->first + updated_bytes->second))
    {
      return;
    }
  }

  if(is_ssa_expr(lhs_fs))
  {
    const ssa_exprt &l1_lhs = to_ssa_expr(lhs_fs);
    const ssa_exprt ssa_lhs =
      state
        .assignment(l1_lhs, ssa_rhs, ns, true, true, allow_pointer_unsoundness)
        .get();

    // do the assignment
    target.assignment(
      state.guard.as_expr(),
      ssa_lhs,
      ssa_lhs,
      ssa_lhs.get_original_expr(),
      ssa_rhs,
      state.source,
      symex_targett::assignment_typet::STATE);
    // Erase the composite symbol from our working state. Note that we need to
    // have it in the propagation table and the value set while doing the field
    // assignments, thus we cannot skip putting it in there above.
    if(is_divisible(ns, l1_lhs, true))
    {
      state.propagation.erase_if_exists(l1_lhs.get_identifier());
      state.value_set.erase_symbol(l1_lhs, ns);
    }
  }
  else if(
    ssa_rhs.type().id() == ID_struct || ssa_rhs.type().id() == ID_struct_tag)
  {
    const struct_typet &type =
      ssa_rhs.type().id() == ID_struct_tag
        ? ns.follow_tag(to_struct_tag_type(ssa_rhs.type()))
        : to_struct_type(ssa_rhs.type());
    const struct_union_typet::componentst &components = type.components();

    PRECONDITION(
      components.empty() ||
      (lhs_fs.has_operands() && lhs_fs.operands().size() == components.size()));

    exprt::operandst::const_iterator fs_it = lhs_fs.operands().begin();
    for(const auto &comp : components)
    {
      std::optional<mp_integer> member_offset_bytes;
      if(offset.has_value())
      {
        const auto m = member_offset(type, comp.get_name(), ns);
        if(m.has_value())
          member_offset_bytes = *offset + *m;
      }
      const exprt member_rhs = apply(
        ns,
        state,
        simplify_opt(
          member_exprt{ssa_rhs, comp.get_name(), comp.type()},
          state.value_set,
          ns),
        false);

      const exprt &member_lhs = *fs_it;
      if(
        auto fs_ssa =
          expr_try_dynamic_cast<field_sensitive_ssa_exprt>(member_lhs))
      {
        field_assignments_rec(
          ns,
          state,
          fs_ssa->get_object_ssa(),
          member_rhs,
          target,
          allow_pointer_unsoundness,
          updated_bytes,
          member_offset_bytes);
      }

      field_assignments_rec(
        ns,
        state,
        member_lhs,
        member_rhs,
        target,
        allow_pointer_unsoundness,
        updated_bytes,
        member_offset_bytes);
      ++fs_it;
    }
  }
  else if(
    ssa_rhs.type().id() == ID_union || ssa_rhs.type().id() == ID_union_tag)
  {
    const union_typet &type =
      ssa_rhs.type().id() == ID_union_tag
        ? ns.follow_tag(to_union_tag_type(ssa_rhs.type()))
        : to_union_type(ssa_rhs.type());
    const struct_union_typet::componentst &components = type.components();

    PRECONDITION(
      components.empty() ||
      (lhs_fs.has_operands() && lhs_fs.operands().size() == components.size()));

    exprt::operandst::const_iterator fs_it = lhs_fs.operands().begin();
    for(const auto &comp : components)
    {
      const exprt member_rhs = apply(
        ns,
        state,
        simplify_opt(
          make_byte_extract(
            ssa_rhs, from_integer(0, c_index_type()), comp.type()),
          state.value_set,
          ns),
        false);

      const exprt &member_lhs = *fs_it;
      if(
        auto fs_ssa =
          expr_try_dynamic_cast<field_sensitive_ssa_exprt>(member_lhs))
      {
        field_assignments_rec(
          ns,
          state,
          fs_ssa->get_object_ssa(),
          member_rhs,
          target,
          allow_pointer_unsoundness,
          updated_bytes,
          offset);
      }

      field_assignments_rec(
        ns,
        state,
        member_lhs,
        member_rhs,
        target,
        allow_pointer_unsoundness,
        updated_bytes,
        offset);
      ++fs_it;
    }
  }
#ifdef ENABLE_ARRAY_FIELD_SENSITIVITY
  else if(const auto &type = type_try_dynamic_cast<array_typet>(ssa_rhs.type()))
  {
    const std::size_t array_size =
      numeric_cast_v<std::size_t>(to_constant_expr(type->size()));
    PRECONDITION(lhs_fs.operands().size() == array_size);

    if(!is_divisible_array(ns, array_size, type->element_type()))
      return;

    const auto element_size = pointer_offset_size(type->element_type(), ns);
    exprt::operandst::const_iterator fs_it = lhs_fs.operands().begin();
    for(std::size_t i = 0; i < array_size; ++i)
    {
      std::optional<mp_integer> element_offset;
      if(offset.has_value() && element_size.has_value())
        element_offset = *offset + *element_size * i;
      const exprt index_rhs = apply(
        ns,
        state,
        simplify_opt(
          index_exprt{ssa_rhs, from_integer(i, type->index_type())},
          state.value_set,
          ns),
        false);

      const exprt &index_lhs = *fs_it;
      if(
        auto fs_ssa =
          expr_try_dynamic_cast<field_sensitive_ssa_exprt>(index_lhs))
      {
        field_assignments_rec(
          ns,
          state,
          fs_ssa->get_object_ssa(),
          index_rhs,
          target,
          allow_pointer_unsoundness,
          updated_bytes,
          element_offset);
      }

      field_assignments_rec(
        ns,
        state,
        index_lhs,
        index_rhs,
        target,
        allow_pointer_unsoundness,
        updated_bytes,
        element_offset);
      ++fs_it;
    }
  }
#endif // ENABLE_ARRAY_FIELD_SENSITIVITY
  else if(lhs_fs.has_operands())
  {
    PRECONDITION(
      ssa_rhs.has_operands() &&
      lhs_fs.operands().size() == ssa_rhs.operands().size());

    exprt::operandst::const_iterator fs_it = lhs_fs.operands().begin();
    for(const exprt &op : ssa_rhs.operands())
    {
      if(auto fs_ssa = expr_try_dynamic_cast<field_sensitive_ssa_exprt>(*fs_it))
      {
        field_assignments_rec(
          ns,
          state,
          fs_ssa->get_object_ssa(),
          op,
          target,
          allow_pointer_unsoundness,
          updated_bytes,
          std::nullopt);
      }

      field_assignments_rec(
        ns,
        state,
        *fs_it,
        op,
        target,
        allow_pointer_unsoundness,
        updated_bytes,
        std::nullopt);
      ++fs_it;
    }
  }
  else
  {
    UNREACHABLE;
  }
}

bool field_sensitivityt::is_divisible(
  const namespacet &ns,
  const ssa_exprt &expr,
  bool disjoined_fields_only) const
{
  if(is_undivided_constant(ns, expr))
    return false;

  if(expr.type().id() == ID_struct || expr.type().id() == ID_struct_tag)
    return true;

#ifdef ENABLE_ARRAY_FIELD_SENSITIVITY
  if(
    expr.type().id() == ID_array &&
    to_array_type(expr.type()).size().is_constant() &&
    is_divisible_array(
      ns,
      numeric_cast_v<mp_integer>(
        to_constant_expr(to_array_type(expr.type()).size())),
      to_array_type(expr.type()).element_type()))
  {
    return true;
  }
#endif

  if(
    !disjoined_fields_only &&
    (expr.type().id() == ID_union || expr.type().id() == ID_union_tag))
  {
    return true;
  }

  return false;
}

exprt field_sensitivityt::simplify_opt(
  exprt e,
  const value_sett &value_set,
  const namespacet &ns) const
{
  if(!should_simplify)
    return e;

  simplify_expr_with_value_sett{value_set, language_mode, ns}.simplify(e);
  return e;
}
