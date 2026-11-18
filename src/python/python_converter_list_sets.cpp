/// \file
/// Python to GOTO converter: the methods of the sets whose elements are not
/// small ints (PLR §6.4.6, PLib stdtypes "Set Types"), which are lists of
/// distinct elements (set displays, set(), set comprehensions and `set[str]`
/// annotations), and their iteration order, which CPython leaves to the
/// hashes of the elements (string hashes vary with PYTHONHASHSEED).

#include <util/arith_tools.h>
#include <util/c_types.h>
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

#include <optional>
#include <string>

bool python_convertert::may_be_list_backed_set(const exprt &e) const
{
  if(!is_python_list_type(e.type()))
    return false;
  if(e.get_bool("#python_set_semantic"))
    return true;
  // A list parameter is passed by reference.
  const exprt &s =
    e.id() == ID_dereference ? to_dereference_expr(e).pointer() : e;
  return s.id() == ID_symbol &&
         set_semantic_symbols.count(to_symbol_expr(s).get_identifier()) > 0;
}

bool python_convertert::may_hold_set(const exprt &e) const
{
  if(e.get_bool("#python_set_semantic"))
    return true;
  const exprt &s =
    e.id() == ID_dereference ? to_dereference_expr(e).pointer() : e;
  return s.id() == ID_symbol &&
         set_semantic_symbols.count(to_symbol_expr(s).get_identifier()) > 0;
}

std::string python_convertert::known_container_kind(const exprt &e) const
{
  if(e.get_bool("#python_set_semantic"))
    return "set";
  if(e.get_bool("#python_list_display"))
    return "list";
  if(e.id() == ID_symbol)
  {
    const auto it = container_kinds.find(to_symbol_expr(e).get_identifier());
    if(it != container_kinds.end())
      return it->second;
  }
  return {};
}

void python_convertert::mark_set_temporary(const symbol_exprt &set)
{
  set_semantic_symbols.insert(set.get_identifier());
  container_kinds[set.get_identifier()] = "set";
}

void python_convertert::track_container_kind(
  const irep_idt &id,
  const exprt &value)
{
  const std::string kind = known_container_kind(value);
  if(kind.empty())
    container_kinds.erase(id);
  else
    container_kinds[id] = kind;
}

bool python_convertert::annotation_is_set(const jsont &annotation) const
{
  const jsont &base = is_node_type(annotation, "Subscript")
                        ? json_member(annotation, "value")
                        : annotation;
  std::string name;
  if(is_node_type(base, "Name"))
    name = json_string(json_member(base, "id"));
  else if(is_node_type(base, "Attribute"))
    name = json_string(json_member(base, "attr"));
  return name == "set" || name == "frozenset" || name == "Set" ||
         name == "FrozenSet" || name == "AbstractSet" || name == "MutableSet";
}

std::size_t python_convertert::list_bound(const exprt &e) const
{
  const std::size_t cap = PYTHON_MAX_LIST_LENGTH;
  mp_integer n;
  if(
    e.id() == ID_struct && !e.operands().empty() &&
    e.operands()[0].is_constant() &&
    !to_integer(to_constant_expr(e.operands()[0]), n))
    return std::min<std::size_t>(numeric_cast_v<std::size_t>(n), cap);
  if(e.id() == ID_symbol)
  {
    const auto it = list_set_bounds.find(to_symbol_expr(e).get_identifier());
    if(it != list_set_bounds.end())
      return std::min(it->second, cap);
  }
  return cap;
}

void python_convertert::set_list_bound(const exprt &e, std::size_t bound)
{
  if(e.id() == ID_symbol)
    list_set_bounds[to_symbol_expr(e).get_identifier()] =
      std::min<std::size_t>(bound, PYTHON_MAX_LIST_LENGTH);
}

symbol_exprt python_convertert::fresh_list_set_symbol(
  const std::string &stem,
  const typet &type)
{
  static unsigned list_set_ctr = 0;
  const std::string name = stem + std::to_string(list_set_ctr++);
  const irep_idt id{qualify_name(name)};
  if(symbol_table.lookup(id) == nullptr)
  {
    symbolt s{id, type, "python"};
    s.base_name = name;
    s.is_lvalue = true;
    s.is_state_var = true;
    s.is_static_lifetime = current_function.empty();
    symbol_table.add(s);
  }
  return symbol_table.lookup_ref(id).symbol_expr();
}

exprt python_convertert::list_contains(const exprt &list, const exprt &elem)
{
  PRECONDITION(is_python_list_type(list.type()));
  const auto &data_type =
    to_array_type(to_struct_type(list.type()).components()[1].type());
  const signedbv_typet i64{64};
  const member_exprt data{list, "data", data_type};
  const member_exprt length{list, "length", i64};
  // Compare the slots below the length, up to the first equal one: a string
  // comparison is a function of the string solver, which should not see the
  // unused slots.
  const symbol_exprt found = fresh_list_set_symbol("__set_in_", bool_typet{});
  pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});
  const std::size_t bound = list_bound(list);
  for(std::size_t i = 0; i < bound; ++i)
  {
    const exprt idx = from_integer(i, i64);
    std::vector<codet> outer;
    std::swap(outer, pending_checks);
    const exprt equal = list_element_equal(index_exprt{data, idx}, elem);
    code_blockt compare;
    for(auto &c : pending_checks)
      compare.add(std::move(c));
    compare.add(code_frontend_assignt{found, equal});
    pending_checks = std::move(outer);
    pending_checks.push_back(code_ifthenelset{
      and_exprt{binary_relation_exprt{idx, ID_lt, length}, not_exprt{found}},
      std::move(compare)});
  }
  return found;
}

exprt python_convertert::list_set_equal(const exprt &left, const exprt &right)
{
  // Each operand once (its elements are compared many times).
  auto once = [this](const exprt &e) -> exprt
  {
    if(e.id() == ID_symbol)
      return e;
    const symbol_exprt t = fresh_list_set_symbol("__set_cmp_", e.type());
    pending_checks.push_back(code_frontend_assignt{t, e});
    return t;
  };
  const exprt l = once(left);
  const exprt r = once(right);
  const auto &data_type =
    to_array_type(to_struct_type(l.type()).components()[1].type());
  const signedbv_typet i64{64};
  const member_exprt length{l, "length", i64};
  const member_exprt data{l, "data", data_type};
  // The elements of a set are distinct: equal lengths and each element of
  // the left one in the right one.
  exprt::operandst all{equal_exprt{length, member_exprt{r, "length", i64}}};
  // A left length beyond the right bound differs from the right length.
  const std::size_t bound = std::min(list_bound(l), list_bound(r));
  for(std::size_t i = 0; i < bound; ++i)
  {
    const exprt idx = from_integer(i, i64);
    const symbol_exprt e =
      fresh_list_set_symbol("__set_cmp_elem_", data_type.element_type());
    pending_checks.push_back(code_frontend_assignt{e, index_exprt{data, idx}});
    std::vector<codet> outer;
    std::swap(outer, pending_checks);
    const exprt in_right = list_contains(r, e);
    code_blockt check;
    for(auto &c : pending_checks)
      check.add(std::move(c));
    pending_checks = std::move(outer);
    const symbol_exprt holds =
      fresh_list_set_symbol("__set_cmp_in_", bool_typet{});
    pending_checks.push_back(code_frontend_assignt{holds, true_exprt{}});
    check.add(code_frontend_assignt{holds, in_right});
    pending_checks.push_back(code_ifthenelset{
      binary_relation_exprt{idx, ID_lt, length}, std::move(check)});
    all.push_back(holds);
  }
  return conjunction(std::move(all));
}

void python_convertert::permute_list_set(
  const exprt &set,
  const source_locationt &loc,
  std::size_t max_length)
{
  PRECONDITION(is_python_list_type(set.type()));
  const auto &data_type =
    to_array_type(to_struct_type(set.type()).components()[1].type());
  const typet &element_type = data_type.element_type();
  const signedbv_typet i64{64};
  const member_exprt length{set, "length", i64};
  const member_exprt data{set, "data", data_type};
  const std::size_t slots = std::min(max_length, list_bound(set));
  if(slots < 2)
    return;
  // Slot i takes original slot p_i, for distinct p_i below the length. Each
  // slot is read and written at a constant index, and its value is one of
  // the originals (not a chain of swaps): the refined string solver
  // mis-handles strings read at a symbolic index, and simplifies the
  // arguments of its functions as trees.
  std::vector<symbol_exprt> originals;
  for(std::size_t k = 0; k < slots; ++k)
  {
    const symbol_exprt o =
      fresh_list_set_symbol("__set_order_slot_", element_type);
    pending_checks.push_back(
      code_frontend_assignt{o, index_exprt{data, from_integer(k, i64)}});
    originals.push_back(o);
  }
  std::vector<symbol_exprt> picks;
  for(std::size_t i = 0; i < slots; ++i)
  {
    const exprt idx = from_integer(i, i64);
    const symbol_exprt p = fresh_list_set_symbol("__set_order_pick_", i64);
    pending_checks.push_back(
      code_frontend_assignt{p, side_effect_expr_nondett{i64, loc}});
    const exprt in_set = binary_relation_exprt{idx, ID_lt, length};
    exprt::operandst valid{
      binary_relation_exprt{p, ID_ge, from_integer(0, i64)},
      binary_relation_exprt{p, ID_lt, length}};
    for(const symbol_exprt &q : picks)
      valid.push_back(notequal_exprt{p, q});
    pending_checks.push_back(
      code_assumet{implies_exprt{in_set, conjunction(std::move(valid))}});
    for(std::size_t k = 0; k < slots; ++k)
      pending_checks.push_back(code_ifthenelset{
        and_exprt{in_set, equal_exprt{p, from_integer(k, i64)}},
        code_frontend_assignt{index_exprt{data, idx}, originals[k]}});
    picks.push_back(p);
  }
}

std::optional<exprt> python_convertert::try_list_backed_set_method(
  const jsont &expr,
  const exprt &obj,
  const std::string &method_name,
  const jsont &args,
  const std::vector<exprt> *converted)
{
  PRECONDITION(is_python_list_type(obj.type()));
  // Methods that only sets (and frozensets) have; the others apply to a list
  // that is known to be a set.
  static const std::set<std::string> set_only = {
    "add",
    "discard",
    "union",
    "intersection",
    "difference",
    "symmetric_difference",
    "update",
    "intersection_update",
    "difference_update",
    "symmetric_difference_update",
    "issubset",
    "issuperset",
    "isdisjoint"};
  static const std::set<std::string> shared = {"remove", "pop", "copy"};
  const std::string kind = known_container_kind(obj);
  // A list has no set-only method.
  if(kind == "list" && set_only.count(method_name) != 0 && converted == nullptr)
  {
    emit_conditional_exception(true_exprt{}, "AttributeError");
    return side_effect_expr_nondett{python_value_type(), get_location(expr)};
  }
  const bool known_set =
    kind == "set" || (kind.empty() && may_be_list_backed_set(obj));
  if(
    set_only.count(method_name) == 0 &&
    !(known_set && shared.count(method_name)))
    return std::nullopt;
  // A list that is not known to be a set may be a list, which does not have
  // the method (unknown when its value comes from elsewhere).
  if(!known_set && set_only.count(method_name) != 0 && converted == nullptr)
    emit_may_raise("AttributeError");

  static const std::map<std::string, std::pair<std::size_t, std::size_t>>
    arity = {
      {"add", {1, 1}},
      {"discard", {1, 1}},
      {"remove", {1, 1}},
      {"pop", {0, 0}},
      {"copy", {0, 0}},
      {"symmetric_difference", {1, 1}},
      {"symmetric_difference_update", {1, 1}},
      {"issubset", {1, 1}},
      {"issuperset", {1, 1}},
      {"isdisjoint", {1, 1}}};
  if(converted == nullptr)
    check_method_arity(arity, method_name, expr, args);

  const source_locationt loc = get_location(expr);
  const typet &set_type = obj.type();
  const auto &data_type =
    to_array_type(to_struct_type(set_type).components()[1].type());
  const typet &element_type = data_type.element_type();
  const signedbv_typet i64{64};
  const exprt none = from_integer(0, python_int_type());
  auto argument_nodes = [this](const jsont &a)
  {
    std::vector<const jsont *> result;
    if(a.is_array())
      for(const auto &x : as_array(a))
        result.push_back(&x);
    return result;
  };

  // The receiver as an lvalue: a display is copied (its changes are lost
  // with it).
  exprt set = obj;
  if(
    set.id() != ID_symbol && set.id() != ID_dereference &&
    set.id() != ID_member)
  {
    const symbol_exprt tmp = fresh_list_set_symbol("__set_recv_", set_type);
    pending_checks.push_back(code_frontend_assignt{tmp, set});
    set = tmp;
  }
  if(set.id() == ID_symbol)
    list_literals.erase(to_symbol_expr(set).get_identifier());
  const member_exprt length{set, "length", i64};
  const member_exprt data{set, "data", data_type};

  // The set as an unknown set of its type (an argument that is not a list:
  // a string, a dict, a set of ints, an iterator).
  auto havoc = [&](const exprt &target)
  {
    if(target.id() == ID_symbol)
      list_set_bounds.erase(to_symbol_expr(target).get_identifier());
    pending_checks.push_back(
      code_frontend_assignt{target, side_effect_expr_nondett{set_type, loc}});
    const member_exprt l{target, "length", i64};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{l, ID_ge, from_integer(0, i64)},
      binary_relation_exprt{
        l, ID_le, from_integer(PYTHON_MAX_LIST_LENGTH, i64)}}});
  };
  // Each argument, converted once, as a list (nullopt when it is not one).
  auto list_arguments = [&]() -> std::optional<std::vector<exprt>>
  {
    std::vector<exprt> result;
    bool all_lists = true;
    std::vector<exprt> values;
    if(converted != nullptr)
      values = *converted;
    else
      for(const jsont *a : argument_nodes(args))
      {
        if(is_node_type(*a, "Starred"))
          return std::nullopt;
        values.push_back(convert_expression(*a));
      }
    for(exprt value : values)
    {
      if(!is_python_list_type(value.type()))
      {
        all_lists = false;
        continue;
      }
      if(value.id() != ID_symbol)
      {
        const symbol_exprt tmp =
          fresh_list_set_symbol("__set_arg_", value.type());
        pending_checks.push_back(code_frontend_assignt{tmp, value});
        set_list_bound(tmp, list_bound(value));
        value = tmp;
      }
      emit_scan_bound_guard(
        member_exprt{value, "length", i64}, loc, -1, &pending_checks);
      result.push_back(value);
    }
    if(!all_lists)
      return std::nullopt;
    return result;
  };
  // Add `elem` to `target` unless it is there (or, when `distinct`, without
  // looking: the element is known not to be there).
  auto add_to = [&](const exprt &target, const exprt &elem, bool distinct)
  {
    const member_exprt l{target, "length", i64};
    const member_exprt d{target, "data", data_type};
    code_blockt add;
    emit_count_capacity_guard(
      add.statements(),
      plus_exprt{l, from_integer(1, i64)},
      PYTHON_MAX_LIST_LENGTH,
      loc);
    add.add(code_frontend_assignt{
      index_exprt{d, l}, coerce_element(elem, element_type)});
    add.add(code_frontend_assignt{l, plus_exprt{l, from_integer(1, i64)}});
    if(distinct)
      pending_checks.push_back(std::move(add));
    else
      pending_checks.push_back(code_ifthenelset{
        not_exprt{list_contains(target, elem)}, std::move(add)});
  };
  // Add the elements of `source` to `target` that satisfy `keep` (`distinct`:
  // none of them is in `target`, and they are distinct).
  auto add_all = [&](
                   const exprt &target,
                   const exprt &source,
                   const std::function<exprt(const exprt &)> &keep,
                   bool distinct)
  {
    const auto &source_data_type =
      to_array_type(to_struct_type(source.type()).components()[1].type());
    const member_exprt source_length{source, "length", i64};
    const member_exprt source_data{source, "data", source_data_type};
    const std::size_t bound = list_bound(source);
    for(std::size_t i = 0; i < bound; ++i)
    {
      const exprt idx = from_integer(i, i64);
      exprt elem = index_exprt{source_data, idx};
      const symbol_exprt e = fresh_list_set_symbol("__set_elem_", elem.type());
      code_blockt body;
      body.add(code_frontend_assignt{e, elem});
      // The code that keep and add_to emit runs only for an element of the
      // source: keep's before its condition, add_to's where it holds.
      std::vector<codet> outer;
      std::swap(outer, pending_checks);
      const exprt kept = keep(e);
      for(auto &c : pending_checks)
        body.add(std::move(c));
      pending_checks.clear();
      add_to(target, e, distinct);
      code_blockt add;
      for(auto &c : pending_checks)
        add.add(std::move(c));
      pending_checks = std::move(outer);
      body.add(code_ifthenelset{kept, std::move(add)});
      pending_checks.push_back(code_ifthenelset{
        binary_relation_exprt{idx, ID_lt, source_length}, std::move(body)});
    }
  };
  auto everywhere = [](const exprt &) -> exprt { return true_exprt{}; };
  // Whether `pred` holds for each element of `source`.
  auto for_all = [&](
                   const exprt &source,
                   const std::function<exprt(const exprt &)> &pred) -> exprt
  {
    const auto &source_data_type =
      to_array_type(to_struct_type(source.type()).components()[1].type());
    const member_exprt source_length{source, "length", i64};
    const member_exprt source_data{source, "data", source_data_type};
    exprt::operandst all;
    const std::size_t bound = list_bound(source);
    for(std::size_t i = 0; i < bound; ++i)
    {
      const exprt idx = from_integer(i, i64);
      all.push_back(implies_exprt{
        binary_relation_exprt{idx, ID_lt, source_length},
        pred(index_exprt{source_data, idx})});
    }
    return conjunction(std::move(all));
  };
  auto new_set = [&]() -> symbol_exprt
  {
    const symbol_exprt result = fresh_list_set_symbol("__set_", set_type);
    pending_checks.push_back(
      code_frontend_assignt{result, side_effect_expr_nondett{set_type, loc}});
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{result, "length", i64}, from_integer(0, i64)});
    mark_set_temporary(result);
    return result;
  };

  if(method_name == "add")
  {
    const auto nodes = argument_nodes(args);
    if(nodes.size() != 1)
      return none;
    const exprt value = convert_expression(*nodes.front());
    // PLR §3.2: a list, dict or set element is unhashable.
    if(is_unhashable_type(value.type()))
    {
      emit_conditional_exception(true_exprt{}, "TypeError");
      return none;
    }
    const symbol_exprt e = fresh_list_set_symbol("__set_elem_", value.type());
    pending_checks.push_back(code_frontend_assignt{e, value});
    const std::size_t bound = list_bound(set);
    add_to(set, e, false);
    if(
      set.id() == ID_symbol &&
      list_set_bounds.count(to_symbol_expr(set).get_identifier()))
      set_list_bound(set, bound + 1);
    permute_list_set(set, loc);
    return none;
  }

  if(method_name == "discard" || method_name == "remove")
  {
    const auto nodes = argument_nodes(args);
    if(nodes.size() != 1)
      return none;
    const exprt value = convert_expression(*nodes.front());
    const symbol_exprt e = fresh_list_set_symbol("__set_elem_", value.type());
    pending_checks.push_back(code_frontend_assignt{e, value});
    const symbol_exprt found =
      fresh_list_set_symbol("__set_found_", bool_typet{});
    pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});
    // The elements are distinct: remove the one equal to e, moving the
    // later ones down.
    const std::size_t bound = list_bound(set);
    for(std::size_t i = 0; i < bound; ++i)
    {
      const exprt idx = from_integer(i, i64);
      const exprt in_set = binary_relation_exprt{idx, ID_lt, length};
      pending_checks.push_back(code_ifthenelset{
        and_exprt{in_set, list_element_equal(index_exprt{data, idx}, e)},
        code_frontend_assignt{found, true_exprt{}}});
      if(i + 1 < bound)
        pending_checks.push_back(code_ifthenelset{
          and_exprt{in_set, found},
          code_frontend_assignt{
            index_exprt{data, idx},
            index_exprt{data, from_integer(i + 1, i64)}}});
    }
    pending_checks.push_back(code_ifthenelset{
      found,
      code_frontend_assignt{
        length, minus_exprt{length, from_integer(1, i64)}}});
    if(method_name == "remove")
      emit_conditional_exception(not_exprt{found}, "KeyError");
    return none;
  }

  if(method_name == "pop")
  {
    // An arbitrary element: the last one in any order.
    emit_conditional_exception(
      equal_exprt{length, from_integer(0, i64)}, "KeyError");
    permute_list_set(set, loc);
    const symbol_exprt result =
      fresh_list_set_symbol("__set_popped_", element_type);
    const std::size_t bound = list_bound(set);
    for(std::size_t i = 0; i < bound; ++i)
      pending_checks.push_back(code_ifthenelset{
        equal_exprt{length, from_integer(i + 1, i64)},
        code_frontend_assignt{
          result, index_exprt{data, from_integer(i, i64)}}});
    pending_checks.push_back(code_ifthenelset{
      binary_relation_exprt{length, ID_gt, from_integer(0, i64)},
      code_frontend_assignt{
        length, minus_exprt{length, from_integer(1, i64)}}});
    if(is_python_string_handle_type(result.type()))
      return string_handle_to_string(result);
    return result;
  }

  if(method_name == "copy")
  {
    const symbol_exprt result = fresh_list_set_symbol("__set_copy_", set_type);
    pending_checks.push_back(code_frontend_assignt{result, set});
    mark_set_temporary(result);
    set_list_bound(result, list_bound(set));
    return result;
  }

  const auto others = list_arguments();
  const bool in_place = method_name == "update" ||
                        method_name == "intersection_update" ||
                        method_name == "difference_update" ||
                        method_name == "symmetric_difference_update";
  if(!others.has_value())
  {
    // An argument this model does not read: any set (or truth value).
    log_overapprox(
      "set." + method_name +
      " of an argument that is not a list-backed set: "
      "nondet over-approximation");
    if(
      method_name == "issubset" || method_name == "issuperset" ||
      method_name == "isdisjoint")
      return side_effect_expr_nondett{bool_typet{}, loc};
    if(in_place)
    {
      havoc(set);
      return none;
    }
    const symbol_exprt result = new_set();
    havoc(result);
    return result;
  }

  if(method_name == "issubset")
    return for_all(
      set, [&](const exprt &e) { return list_contains(others->front(), e); });
  if(method_name == "issuperset")
    return for_all(
      others->front(), [&](const exprt &e) { return list_contains(set, e); });
  if(method_name == "isdisjoint")
    return for_all(
      set,
      [&](const exprt &e)
      { return not_exprt{list_contains(others->front(), e)}; });

  // union / update: the receiver's elements and those of the others;
  // intersection(_update): those in each other; difference(_update): those in
  // no other; symmetric_difference(_update): those in exactly one.
  const symbol_exprt result = new_set();
  // The receiver's elements are distinct, and so are those it keeps.
  if(method_name == "union" || method_name == "update")
  {
    pending_checks.push_back(code_frontend_assignt{result, set});
    for(const exprt &other : *others)
      add_all(result, other, everywhere, false);
  }
  else if(method_name == "intersection" || method_name == "intersection_update")
  {
    add_all(
      result,
      set,
      [&](const exprt &e)
      {
        exprt::operandst in_each;
        for(const exprt &other : *others)
          in_each.push_back(list_contains(other, e));
        return conjunction(std::move(in_each));
      },
      true);
  }
  else if(method_name == "difference" || method_name == "difference_update")
  {
    add_all(
      result,
      set,
      [&](const exprt &e)
      {
        exprt::operandst in_none;
        for(const exprt &other : *others)
          in_none.push_back(not_exprt{list_contains(other, e)});
        return conjunction(std::move(in_none));
      },
      true);
  }
  else
  {
    const exprt &other = others->front();
    add_all(
      result,
      set,
      [&](const exprt &e) { return not_exprt{list_contains(other, e)}; },
      true);
    // The other's elements not in the receiver, which may repeat (a list).
    add_all(
      result,
      other,
      [&](const exprt &e) { return not_exprt{list_contains(set, e)}; },
      false);
  }
  // The result is no longer than the receiver (with the others for union
  // and symmetric_difference).
  std::size_t bound = list_bound(set);
  if(
    method_name == "union" || method_name == "update" ||
    method_name == "symmetric_difference" ||
    method_name == "symmetric_difference_update")
    for(const exprt &other : *others)
      bound += list_bound(other);
  set_list_bound(result, bound);
  permute_list_set(result, loc);
  if(in_place)
  {
    pending_checks.push_back(code_frontend_assignt{set, result});
    if(
      set.id() == ID_symbol &&
      list_set_bounds.count(to_symbol_expr(set).get_identifier()))
      set_list_bound(set, bound);
    return none;
  }
  return result;
}

exprt python_convertert::list_set_binary_operation(
  const jsont &expr,
  const std::string &op,
  const exprt &left,
  const exprt &right)
{
  static const std::map<std::string, std::string> methods = {
    {"BitOr", "union"},
    {"BitAnd", "intersection"},
    {"Sub", "difference"},
    {"BitXor", "symmetric_difference"}};
  const auto method = methods.find(op);
  if(method == methods.end())
    return nil_exprt{};
  const bool l_list = is_python_list_type(left.type());
  const bool r_list = is_python_list_type(right.type());
  const bool l_bitmap = is_python_set_type(left.type());
  const bool r_bitmap = is_python_set_type(right.type());
  if(
    !(l_list && may_be_list_backed_set(left) && (r_list || r_bitmap)) &&
    !(r_list && may_be_list_backed_set(right) && (l_list || l_bitmap)))
    return nil_exprt{};
  // A list display is a list, for which these operators raise TypeError.
  auto list_display = [this](const exprt &e)
  { return known_container_kind(e) == "list"; };
  if(list_display(left) || list_display(right))
  {
    emit_conditional_exception(true_exprt{}, "TypeError");
    return side_effect_expr_nondett{left.type(), get_location(expr)};
  }
  const source_locationt loc = get_location(expr);
  if(l_list && r_list)
  {
    const std::vector<exprt> others{right};
    const auto result = try_list_backed_set_method(
      expr, left, method->second, json_arrayt{}, &others);
    return result.has_value() ? *result : exprt{nil_exprt{}};
  }
  // A set held in a list and a set of small ints (a bitmap; `set()` is
  // one). Strings are no ints: the difference and the intersection are
  // exact; a union with ints (or a set whose elements may be ints) is not
  // modelled, and is reported.
  const exprt &list = l_list ? left : right;
  const exprt &bitmap = l_list ? right : left;
  const member_exprt bits{bitmap, "bitmap", unsignedbv_typet{64}};
  const exprt empty = equal_exprt{bits, from_integer(0, bits.type())};
  const auto &data_type =
    to_array_type(to_struct_type(list.type()).components()[1].type());
  const bool strings = is_python_string_type(data_type.element_type()) ||
                       is_python_string_handle_type(data_type.element_type());
  auto empty_list_set = [&]()
  {
    const symbol_exprt r = fresh_list_set_symbol("__set_", list.type());
    pending_checks.push_back(
      code_frontend_assignt{r, side_effect_expr_nondett{list.type(), loc}});
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{r, "length", signedbv_typet{64}},
      from_integer(0, signedbv_typet{64})});
    mark_set_temporary(r);
    set_list_bound(r, 0);
    return r;
  };
  auto copy = [&](const exprt &s)
  {
    const symbol_exprt r = fresh_list_set_symbol("__set_", s.type());
    pending_checks.push_back(code_frontend_assignt{r, s});
    if(is_python_list_type(s.type()))
    {
      mark_set_temporary(r);
      set_list_bound(r, list_bound(s));
    }
    return exprt{r};
  };
  auto not_modelled = [&]()
  {
    source_locationt aloc = loc;
    aloc.set_property_class("python-model-bound");
    aloc.set_comment(
      "a set of strings and small ints is not modelled (verifier model "
      "bound)");
    code_assertt check{empty};
    check.add_source_location() = aloc;
    pending_checks.push_back(std::move(check));
    pending_checks.push_back(code_assumet{empty});
  };
  if(op == "Sub")
  {
    // list - ints: the list; ints - list: the ints.
    if(!strings)
      not_modelled();
    return copy(l_list ? list : bitmap);
  }
  if(op == "BitAnd")
  {
    if(!strings)
      not_modelled();
    return l_list ? exprt{empty_list_set()}
                  : struct_exprt{
                      {from_integer(0, unsignedbv_typet{64}),
                       from_integer(0, signedbv_typet{64})},
                      python_set_type()};
  }
  // | and ^ with an empty set of ints: the list.
  not_modelled();
  return copy(list);
}
