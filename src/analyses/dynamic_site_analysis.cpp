/*******************************************************************\

Module: Analysis of the values at dynamic sites

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Analysis of the values at dynamic sites

#include "dynamic_site_analysis.h"

#include <util/find_symbols.h>
#include <util/mathematical_expr.h>
#include <util/namespace.h>
#include <util/pointer_expr.h>
#include <util/replace_symbol.h>
#include <util/sharing_map.h>
#include <util/simplify_expr.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include <goto-programs/goto_model.h>

#include <linking/static_lifetime_init.h>

#include "ai.h"

#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace
{
/// A set of strings, or nothing for any string.
using valuet = std::optional<std::set<std::string>>;

/// The empty set: no value (unreached, or not assigned).
valuet no_value()
{
  return std::set<std::string>{};
}

/// The symbols the analysis tracks, and how values are combined.
class trackingt
{
public:
  trackingt(const goto_modelt &goto_model, std::size_t bound)
    : ns(goto_model.symbol_table), bound(bound)
  {
  }

  const namespacet ns;
  const std::size_t bound;
  /// Symbols whose values may reach a site.
  std::unordered_set<irep_idt> relevant;
  /// Symbols whose address is taken: they may change through pointers.
  std::unordered_set<irep_idt> address_taken;

  valuet join(const valuet &a, const valuet &b) const
  {
    if(!a.has_value() || !b.has_value())
      return {};
    std::set<std::string> result = *a;
    result.insert(b->begin(), b->end());
    if(result.size() > bound)
      return {};
    return result;
  }

  valuet concat(const valuet &a, const valuet &b) const
  {
    if(!a.has_value() || !b.has_value())
      return {};
    std::set<std::string> result;
    for(const auto &x : *a)
    {
      for(const auto &y : *b)
      {
        result.insert(x + y);
        if(result.size() > bound)
          return {};
      }
    }
    return result;
  }

  /// The component of type string of the struct type \p type, if it has
  /// exactly one.
  std::optional<irep_idt> string_component(const typet &type) const
  {
    if(type.id() != ID_struct_tag)
      return {};
    std::optional<irep_idt> result;
    for(const auto &component :
        ns.follow_tag(to_struct_tag_type(type)).components())
    {
      if(component.type().id() != ID_string)
        continue;
      if(result.has_value())
        return {};
      result = component.get_name();
    }
    return result;
  }

  /// Whether symbols of type \p type hold a string, or an array of
  /// strings, whose elements are joined (smashed).
  bool is_string_like(const typet &type) const
  {
    if(type.id() == ID_array)
      return is_string_like(to_array_type(type).element_type());
    return type.id() == ID_string || string_component(type).has_value();
  }

  /// The symbol that \p lhs writes the string of, if it is tracked; with
  /// \p weak set if it writes one element of an array.
  std::optional<irep_idt> written(const exprt &lhs, bool &weak) const
  {
    weak = false;
    if(lhs.id() == ID_index)
    {
      const exprt &array = to_index_expr(lhs).array();
      if(array.id() == ID_symbol && is_string_like(array.type()))
      {
        weak = true;
        return tracked(to_symbol_expr(array).identifier());
      }
      return {};
    }
    if(lhs.id() == ID_symbol && is_string_like(lhs.type()))
      return tracked(to_symbol_expr(lhs).identifier());
    if(lhs.id() == ID_member)
    {
      const auto &member = to_member_expr(lhs);
      const auto component = string_component(member.compound().type());
      if(component.has_value() && member.get_component_name() == *component)
      {
        if(member.compound().id() == ID_symbol)
          return tracked(to_symbol_expr(member.compound()).identifier());
        // The string of an element.
        bool element = false;
        const auto array = written(member.compound(), element);
        weak = element;
        return element ? array : std::nullopt;
      }
    }
    return {};
  }

  /// Functions with a body.
  std::unordered_set<irep_idt> bodies;

  /// Local variables t whose only assignments are t := x, for one symbol
  /// x of the same type: saved copies of x. x := t then restores the value
  /// of x where t was assigned, and whether x was assigned there.
  std::unordered_map<irep_idt, irep_idt> copy_of;

  /// The symbol whose saved copy \p rhs restores into \p lhs, if it is
  /// a restore x := t of a copy t of x.
  bool is_restore(const exprt &lhs, const exprt &rhs) const
  {
    if(lhs.id() != ID_symbol || rhs.id() != ID_symbol)
      return false;
    const auto found = copy_of.find(to_symbol_expr(rhs).identifier());
    return found != copy_of.end() &&
           found->second == to_symbol_expr(lhs).identifier();
  }

  bool has_body(const irep_idt &id) const
  {
    return bodies.count(id) != 0;
  }

  /// Whether \p id is a local variable (neither static nor a parameter).
  bool is_local(const irep_idt &id) const
  {
    const symbolt *symbol = nullptr;
    return !ns.lookup(id, symbol) && !symbol->is_static_lifetime &&
           !symbol->is_parameter;
  }

  std::optional<irep_idt> tracked(const irep_idt &id) const
  {
    if(relevant.count(id) == 0 || address_taken.count(id) != 0)
      return {};
    return id;
  }

  /// Scalar symbols whose constant values may decide branches: those in
  /// conditions, and those assigned to them.
  std::unordered_set<irep_idt> scalars;
  /// The scalar symbols of static lifetime that each function, or its
  /// callees, may write.
  std::unordered_map<irep_idt, std::unordered_set<irep_idt>> writes;
  /// Whether there are calls through function pointers, which may write
  /// any symbol.
  bool indirect = false;

  /// Whether \p id is of static lifetime.
  bool is_global(const irep_idt &id) const
  {
    const symbolt *symbol = nullptr;
    return !ns.lookup(id, symbol) && symbol->is_static_lifetime;
  }

  bool is_scalar(const typet &type) const
  {
    return type.id() == ID_bool || type.id() == ID_c_bool ||
           type.id() == ID_signedbv || type.id() == ID_unsignedbv;
  }
};

/// The root symbol of \p lhs through members, if any.
const symbol_exprt *root_symbol(const exprt &lhs)
{
  const exprt *e = &lhs;
  while(e->id() == ID_member)
    e = &to_member_expr(*e).compound();
  return e->id() == ID_symbol ? &to_symbol_expr(*e) : nullptr;
}

/// The root symbol of \p lhs through members and indices, if any.
const symbol_exprt *base_symbol(const exprt &lhs)
{
  const exprt *e = &lhs;
  while(e->id() == ID_member || e->id() == ID_index)
  {
    e = e->id() == ID_member ? &to_member_expr(*e).compound()
                             : &to_index_expr(*e).array();
  }
  return e->id() == ID_symbol ? &to_symbol_expr(*e) : nullptr;
}

bool is_marker(const goto_programt::instructiont &instruction)
{
  return instruction.is_function_call() &&
         instruction.call_function().id() == ID_symbol &&
         id2string(to_symbol_expr(instruction.call_function()).identifier()) ==
           DYNAMIC_SITE_FUNCTION &&
         instruction.call_arguments().size() == 2;
}

/// Constant values of scalar symbols (see trackingt::scalars).
/// Shared, as each instruction has its own copy.
using constantst = sharing_mapt<irep_idt, exprt>;

/// Remove from \p ours the constants that \p theirs lacks or differs on.
/// \return whether any were removed.
bool meet_constants(constantst &ours, const constantst &theirs)
{
  std::vector<irep_idt> lost;
  for(const auto &item : ours.get_delta_view(theirs, false))
  {
    if(!item.is_in_both_maps() || item.m != item.get_other_map_value())
      lost.push_back(item.k);
  }
  for(const auto &id : lost)
    ours.erase(id);
  return !lost.empty();
}

bool equal_constants(const constantst &a, const constantst &b)
{
  if(a.size() != b.size())
    return false;
  for(const auto &item : a.get_delta_view(b, false))
  {
    if(!item.is_in_both_maps() || item.m != item.get_other_map_value())
      return false;
  }
  return true;
}

/// What holds where a function starts (at all its calls), or where it
/// returns (for its callers): definitely assigned relevant symbols, and
/// constant scalars.
struct factst
{
  std::set<irep_idt> assigned;
  constantst constants;

  bool operator==(const factst &other) const
  {
    return assigned == other.assigned &&
           equal_constants(constants, other.constants);
  }
  bool operator!=(const factst &other) const
  {
    return !(*this == other);
  }

  /// Keep what also holds in \p other.
  void meet(const factst &other)
  {
    for(auto it = assigned.begin(); it != assigned.end();)
      it = other.assigned.count(*it) == 0 ? assigned.erase(it) : std::next(it);
    meet_constants(constants, other.constants);
  }
};

/// What each function definitely assigns, and the constants where it
/// returns, for its callers: absent while the function is not known to
/// return.
using summariest = std::map<irep_idt, factst>;

/// The relevant symbols that are definitely assigned (outside
/// __CPROVER_initialize) within one function, starting from the symbols
/// assigned at all its calls; a call adds what the callee definitely
/// assigns. Also the constant values of scalars, which prune branches
/// whose condition they decide. Bottom where the function cannot be.
class assigned_domaint : public ai_domain_baset
{
public:
  assigned_domaint(
    const trackingt &tracking,
    const summariest &summaries,
    const factst &entry)
    : tracking(tracking), summaries(summaries), entry(entry)
  {
  }

  bool bottom = true;
  std::set<irep_idt> assigned;
  /// Copies t of x (see trackingt::copy_of) made where x was assigned.
  std::set<irep_idt> saved_assigned;
  constantst constants;

  /// \p expr with the constants substituted, simplified.
  exprt evaluate(exprt expr) const
  {
    replace_symbolt replace;
    for(const auto &id : find_symbol_identifiers(expr))
    {
      const auto found = constants.find(id);
      if(found.has_value())
        replace.insert(symbol_exprt{id, found->get().type()}, found->get());
    }
    replace(expr);
    return simplify_expr(std::move(expr), tracking.ns);
  }

  /// Track the value that \p lhs gets from \p rhs, if it is a scalar.
  void assign_constant(const exprt &lhs, const exprt &rhs)
  {
    const symbol_exprt *symbol = base_symbol(lhs);
    if(symbol == nullptr)
      return;
    const irep_idt &id = symbol->identifier();
    if(&lhs == symbol && rhs.is_not_nil() && tracking.scalars.count(id) != 0)
    {
      exprt value = evaluate(rhs);
      if(value.is_constant())
      {
        constants.insert_or_replace(id, std::move(value));
        return;
      }
    }
    constants.erase_if_exists(id);
  }

  void transform(
    const irep_idt &function_from,
    trace_ptrt from,
    const irep_idt &,
    trace_ptrt to,
    ai_baset &,
    const namespacet &) override
  {
    const auto &instruction = *from->current_location();
    const symbol_exprt *symbol = nullptr;
    if(instruction.is_goto())
    {
      // A branch that the constants decide against cannot be taken; with
      // the target the next instruction, both edges are the same.
      const auto location = from->current_location();
      const auto target = instruction.get_target();
      if(target != std::next(location))
      {
        const bool taken = to->current_location() == target;
        const exprt condition = evaluate(instruction.condition());
        if(taken ? condition.is_false() : condition.is_true())
          make_bottom();
      }
      return;
    }
    if(instruction.is_assume())
    {
      if(evaluate(instruction.condition()).is_false())
        make_bottom();
      return;
    }
    if(instruction.is_other())
    {
      // It may write the symbols it mentions.
      for(const auto &id : find_symbol_identifiers(instruction.code()))
        constants.erase_if_exists(id);
      return;
    }
    if(instruction.is_assign())
    {
      const exprt &lhs = instruction.assign_lhs();
      const exprt &rhs = instruction.assign_rhs();
      assign_constant(lhs, rhs);
      if(tracking.is_restore(lhs, rhs))
      {
        // x is as assigned as it was where it was saved.
        const irep_idt &x = to_symbol_expr(lhs).identifier();
        if(saved_assigned.count(to_symbol_expr(rhs).identifier()) != 0)
          assigned.insert(x);
        else
          assigned.erase(x);
        return;
      }
      if(
        lhs.id() == ID_symbol &&
        tracking.copy_of.count(to_symbol_expr(lhs).identifier()) != 0)
      {
        const irep_idt &t = to_symbol_expr(lhs).identifier();
        if(assigned.count(tracking.copy_of.at(t)) != 0)
          saved_assigned.insert(t);
        else
          saved_assigned.erase(t);
      }
      symbol = root_symbol(lhs);
    }
    else if(instruction.is_function_call())
    {
      // The callee returns, having assigned at least its summary;
      // without a summary it does not return (yet).
      const exprt &function = instruction.call_function();
      if(instruction.call_lhs().is_not_nil())
        assign_constant(instruction.call_lhs(), nil_exprt{});
      if(function.id() != ID_symbol)
      {
        // The callee may write any symbol of static lifetime.
        std::vector<irep_idt> globals;
        for(const auto &constant : constants.get_view())
        {
          if(tracking.is_global(constant.first))
            globals.push_back(constant.first);
        }
        for(const auto &id : globals)
          constants.erase(id);
      }
      if(function.id() == ID_symbol)
      {
        const irep_idt &callee = to_symbol_expr(function).identifier();
        const auto summary = summaries.find(callee);
        if(summary != summaries.end())
        {
          assigned.insert(
            summary->second.assigned.begin(), summary->second.assigned.end());
          const auto writes = tracking.writes.find(callee);
          if(writes != tracking.writes.end())
          {
            for(const auto &id : writes->second)
              constants.erase_if_exists(id);
          }
          for(const auto &constant : summary->second.constants.get_view())
            constants.insert_or_replace(constant.first, constant.second);
        }
        else if(tracking.has_body(callee))
        {
          make_bottom();
          return;
        }
      }
      if(instruction.call_lhs().is_not_nil())
        symbol = root_symbol(instruction.call_lhs());
    }
    else if(instruction.is_decl())
    {
      assigned.erase(instruction.decl_symbol().identifier());
      saved_assigned.erase(instruction.decl_symbol().identifier());
      constants.erase_if_exists(instruction.decl_symbol().identifier());
    }
    else if(instruction.is_dead())
    {
      assigned.erase(instruction.dead_symbol().identifier());
      saved_assigned.erase(instruction.dead_symbol().identifier());
      constants.erase_if_exists(instruction.dead_symbol().identifier());
    }
    // The values that __CPROVER_initialize assigns are joined at reads
    // that the program did not assign.
    if(
      symbol != nullptr && function_from != INITIALIZE_FUNCTION &&
      tracking.relevant.count(symbol->identifier()) != 0)
    {
      assigned.insert(symbol->identifier());
    }
  }

  void make_bottom() override
  {
    bottom = true;
    assigned.clear();
    saved_assigned.clear();
    constants.clear();
  }
  void make_top() override
  {
    bottom = false;
    assigned.clear();
    saved_assigned.clear();
    constants.clear();
  }
  void make_entry() override
  {
    bottom = false;
    assigned = entry.assigned;
    saved_assigned.clear();
    constants = entry.constants;
  }
  bool is_bottom() const override
  {
    return bottom;
  }
  bool is_top() const override
  {
    return !bottom && assigned.empty() && constants.empty();
  }

  bool merge(const assigned_domaint &other, trace_ptrt, trace_ptrt)
  {
    if(other.bottom)
      return false;
    if(bottom)
    {
      bottom = false;
      assigned = other.assigned;
      saved_assigned = other.saved_assigned;
      constants = other.constants;
      return true;
    }
    bool changed = false;
    for(auto *pair : {&assigned, &saved_assigned})
    {
      const auto &theirs =
        pair == &assigned ? other.assigned : other.saved_assigned;
      for(auto it = pair->begin(); it != pair->end();)
      {
        if(theirs.count(*it) == 0)
        {
          it = pair->erase(it);
          changed = true;
        }
        else
          ++it;
      }
    }
    changed |= meet_constants(constants, other.constants);
    return changed;
  }

  /// The facts of non-local symbols, for callees and callers.
  factst facts() const
  {
    factst result;
    for(const auto &id : assigned)
    {
      if(!tracking.is_local(id))
        result.assigned.insert(id);
    }
    result.constants = constants;
    for(const auto &constant : constants.get_view())
    {
      if(!tracking.is_global(constant.first))
        result.constants.erase(constant.first);
    }
    return result;
  }

private:
  const trackingt &tracking;
  const summariest &summaries;
  const factst &entry;
};

class assigned_domain_factoryt : public ai_domain_factoryt<assigned_domaint>
{
public:
  assigned_domain_factoryt(
    const trackingt &tracking,
    const summariest &summaries,
    const factst &entry)
    : tracking(tracking), summaries(summaries), entry(entry)
  {
  }

  std::unique_ptr<statet> make(locationt) const override
  {
    return std::make_unique<assigned_domaint>(tracking, summaries, entry);
  }

private:
  const trackingt &tracking;
  const summariest &summaries;
  const factst &entry;
};

/// Must-assigned symbols and constants at each instruction of each
/// function, by summaries: a function is reached if a reachable call
/// reaches it, and starts with the facts that hold at all its reachable
/// calls.
class must_assignedt
{
public:
  must_assignedt(const goto_modelt &goto_model, const trackingt &tracking)
    : goto_model(goto_model), tracking(tracking)
  {
  }

  void operator()()
  {
    const auto &functions = goto_model.goto_functions.function_map;
    // The callers of each function, to analyse again when its summary
    // changes.
    std::map<irep_idt, std::set<irep_idt>> callers, callees;
    for(const auto &entry : functions)
    {
      for(const auto &instruction : entry.second.body.instructions)
      {
        if(
          instruction.is_function_call() &&
          instruction.call_function().id() == ID_symbol)
        {
          const irep_idt &callee =
            to_symbol_expr(instruction.call_function()).identifier();
          callers[callee].insert(entry.first);
          callees[entry.first].insert(callee);
        }
      }
    }
    // Roots start with no facts: the entry point, or with calls through
    // function pointers, which may reach any function, all functions.
    std::set<irep_idt> roots;
    if(tracking.indirect)
    {
      for(const auto &entry : functions)
      {
        if(entry.second.body_available())
          roots.insert(entry.first);
      }
    }
    else
      roots.insert(goto_functionst::entry_point());
    for(const auto &root : roots)
      entry_sets[root];
    // Analyse callees before their callers (in post-order of the call
    // graph), so that a caller sees the changes of several callees' summaries
    // at once.
    std::map<irep_idt, std::size_t> order;
    std::set<irep_idt> visited;
    for(const auto &root : roots)
    {
      std::vector<std::pair<irep_idt, bool>> stack{{root, false}};
      while(!stack.empty())
      {
        const auto top = stack.back();
        stack.pop_back();
        if(top.second)
        {
          order.emplace(top.first, order.size());
          continue;
        }
        if(!visited.insert(top.first).second)
          continue;
        stack.emplace_back(top.first, true);
        for(const auto &callee : callees[top.first])
        {
          if(visited.count(callee) == 0)
            stack.emplace_back(callee, false);
        }
      }
    }
    const auto rank = [&order](const irep_idt &name)
    {
      const auto found = order.find(name);
      return found == order.end() ? order.size() : found->second;
    };
    std::set<std::pair<std::size_t, irep_idt>> worklist;
    const auto schedule = [&worklist, &rank](const irep_idt &name)
    { worklist.emplace(rank(name), name); };
    for(const auto &root : roots)
      schedule(root);
    // The facts at the reachable calls from each caller to each callee.
    std::map<irep_idt, std::map<irep_idt, factst>> call_facts;
    // Entries, call facts and summaries only lose facts once they exist,
    // so that the iteration terminates; when it does, each entry holds at
    // all reachable calls, and each summary where the function returns.
    while(!worklist.empty())
    {
      const irep_idt name = worklist.begin()->second;
      worklist.erase(worklist.begin());
      const auto function = functions.find(name);
      if(function == functions.end() || !function->second.body_available())
        continue;
      // Not reached (yet).
      const auto entry_facts = entry_sets.find(name);
      if(entry_facts == entry_sets.end())
        continue;
      auto analysis = std::make_unique<ait<assigned_domaint>>(
        std::make_unique<assigned_domain_factoryt>(
          tracking, summaries, entry_facts->second));
      (*analysis)(name, function->second, tracking.ns);
      std::map<irep_idt, factst> at_calls;
      const auto &body = function->second.body;
      for(auto it = body.instructions.begin(); it != body.instructions.end();
          ++it)
      {
        const auto state_ptr = get(*analysis, it);
        const assigned_domaint &state = *state_ptr;
        if(state.bottom)
          continue;
        if(it->is_end_function())
        {
          // Locals are dead at the end. Constants that the function does
          // not write are the callers' own.
          factst summary = state.facts();
          const auto &writes = tracking.writes.at(name);
          std::vector<irep_idt> own;
          for(const auto &constant : summary.constants.get_view())
          {
            if(writes.count(constant.first) == 0)
              own.push_back(constant.first);
          }
          for(const auto &id : own)
            summary.constants.erase(id);
          const auto old = summaries.find(name);
          if(old != summaries.end())
            summary.meet(old->second);
          if(old == summaries.end() || old->second != summary)
          {
            summaries[name] = std::move(summary);
            for(const auto &caller : callers[name])
              schedule(caller);
          }
        }
        if(!it->is_function_call() || it->call_function().id() != ID_symbol)
          continue;
        const irep_idt &callee =
          to_symbol_expr(it->call_function()).identifier();
        const auto found = at_calls.find(callee);
        if(found == at_calls.end())
          at_calls.emplace(callee, state.facts());
        else
          found->second.meet(state.facts());
      }
      analyses[name] = std::move(analysis);
      for(auto &call : at_calls)
      {
        const irep_idt &callee = call.first;
        auto &facts = call_facts[name];
        const auto old = facts.find(callee);
        if(old != facts.end())
        {
          call.second.meet(old->second);
          if(call.second == old->second)
            continue;
        }
        facts[callee] = call.second;
        if(roots.count(callee) != 0)
          continue;
        // The entry of the callee: the facts at all its reachable calls.
        std::optional<factst> entry;
        for(const auto &caller : callers[callee])
        {
          const auto from = call_facts.find(caller);
          if(from == call_facts.end())
            continue;
          const auto at = from->second.find(callee);
          if(at == from->second.end())
            continue;
          if(entry.has_value())
            entry->meet(at->second);
          else
            entry = at->second;
        }
        const auto current = entry_sets.find(callee);
        if(current == entry_sets.end() || current->second != *entry)
        {
          entry_sets[callee] = std::move(*entry);
          schedule(callee);
        }
      }
    }
  }

  /// The symbols definitely assigned before \p it in \p function, or
  /// nothing where the program cannot be.
  std::optional<std::set<irep_idt>>
  before(const irep_idt &function, goto_programt::const_targett it) const
  {
    const auto analysis = analyses.find(function);
    if(analysis == analyses.end())
      return {};
    const auto state_ptr = get(*analysis->second, it);
    const assigned_domaint &state = *state_ptr;
    if(state.bottom)
      return {};
    return state.assigned;
  }

private:
  const goto_modelt &goto_model;
  const trackingt &tracking;
  summariest summaries;
  std::map<irep_idt, factst> entry_sets;
  std::map<irep_idt, std::unique_ptr<ait<assigned_domaint>>> analyses;

  /// The state before \p it; held by the pointer, as the state of an
  /// instruction that was not reached is made on demand.
  static std::shared_ptr<const assigned_domaint>
  get(const ait<assigned_domaint> &analysis, goto_programt::const_targett it)
  {
    return std::static_pointer_cast<const assigned_domaint>(
      analysis.abstract_state_before(it));
  }
};

/// Flow-insensitive values of the tracked symbols.
class valuest
{
public:
  valuest(const trackingt &tracking) : tracking(tracking)
  {
  }

  /// Values assigned outside __CPROVER_initialize, and in it.
  std::map<irep_idt, valuet> program, initial;

  /// The value of symbol \p id where the symbols in \p assigned are
  /// definitely assigned; \p in_initialize: in __CPROVER_initialize.
  valuet read(
    const irep_idt &id,
    const std::set<irep_idt> &assigned,
    bool in_initialize) const
  {
    if(!tracking.tracked(id).has_value())
      return {};
    const auto lookup =
      [](const std::map<irep_idt, valuet> &map, const irep_idt &id)
    {
      const auto found = map.find(id);
      return found == map.end() ? no_value() : found->second;
    };
    if(in_initialize)
    {
      // A symbol it does not assign, such as a nondeterministic
      // temporary, may be any value.
      const auto found = initial.find(id);
      return found == initial.end() ? valuet{} : found->second;
    }
    valuet result = lookup(program, id);
    if(assigned.count(id) != 0)
      return result;
    // Not assigned by the program on some path: the initial value, or for
    // a variable without one (such as an uninitialised local) any value.
    // Parameters get their values from the arguments (see bind).
    const symbolt *symbol = nullptr;
    if(!tracking.ns.lookup(id, symbol) && symbol->is_parameter)
      return result;
    const auto initial_value = initial.find(id);
    if(initial_value == initial.end())
      return {};
    return tracking.join(result, initial_value->second);
  }

  valuet eval(
    const exprt &expr,
    const std::set<irep_idt> &assigned,
    bool in_initialize) const
  {
    if(expr.is_constant() && expr.type().id() == ID_string)
      return std::set<std::string>{
        id2string(to_constant_expr(expr).get_value())};
    if(expr.id() == ID_symbol)
    {
      if(!tracking.is_string_like(expr.type()))
        return {};
      return read(to_symbol_expr(expr).identifier(), assigned, in_initialize);
    }
    if(expr.id() == ID_struct)
    {
      const auto component = tracking.string_component(expr.type());
      if(!component.has_value())
        return {};
      const auto &type =
        tracking.ns.follow_tag(to_struct_tag_type(expr.type()));
      return eval(
        expr.operands()[type.component_number(*component)],
        assigned,
        in_initialize);
    }
    if(expr.id() == ID_member)
    {
      const auto &member = to_member_expr(expr);
      const auto component =
        tracking.string_component(member.compound().type());
      if(!component.has_value() || member.get_component_name() != *component)
        return {};
      return eval(member.compound(), assigned, in_initialize);
    }
    if(expr.id() == ID_index)
    {
      // Any element: the joined elements of the array.
      const exprt &array = to_index_expr(expr).array();
      if(!tracking.is_string_like(array.type()))
        return {};
      return eval(array, assigned, in_initialize);
    }
    if(expr.id() == ID_array_of)
      return eval(to_array_of_expr(expr).what(), assigned, in_initialize);
    if(expr.id() == ID_array)
    {
      valuet result = no_value();
      for(const auto &element : expr.operands())
        result = tracking.join(result, eval(element, assigned, in_initialize));
      return result;
    }
    if(expr.id() == ID_if)
    {
      const auto &choice = to_if_expr(expr);
      return tracking.join(
        eval(choice.true_case(), assigned, in_initialize),
        eval(choice.false_case(), assigned, in_initialize));
    }
    if(expr.id() == ID_function_application)
    {
      const auto &application = to_function_application_expr(expr);
      if(
        application.function().id() == ID_symbol &&
        to_symbol_expr(application.function()).identifier() ==
          ID_cprover_string_concat_func &&
        application.arguments().size() == 2)
      {
        return tracking.concat(
          eval(application.arguments()[0], assigned, in_initialize),
          eval(application.arguments()[1], assigned, in_initialize));
      }
    }
    return {};
  }

  /// Join \p value into the values of \p id; \return whether they grew.
  bool add(bool in_initialize, const irep_idt &id, const valuet &value)
  {
    auto &map = in_initialize ? initial : program;
    const auto found = map.find(id);
    const valuet old = found == map.end() ? no_value() : found->second;
    const valuet joined = tracking.join(old, value);
    if(found != map.end() && joined == old)
      return false;
    map[id] = joined;
    return true;
  }

private:
  const trackingt &tracking;
};

/// Symbols whose values may reach a site, through assignments and
/// arguments, and symbols whose address is taken.
void collect_relevant(const goto_modelt &goto_model, trackingt &tracking)
{
  for(const auto &entry : goto_model.goto_functions.function_map)
  {
    for(const auto &instruction : entry.second.body.instructions)
    {
      instruction.apply(
        [&tracking](const exprt &e)
        {
          e.visit_pre(
            [&tracking](const exprt &sub)
            {
              if(
                sub.id() == ID_address_of &&
                to_address_of_expr(sub).object().id() == ID_symbol)
              {
                tracking.address_taken.insert(
                  to_symbol_expr(to_address_of_expr(sub).object())
                    .identifier());
              }
            });
        });
      if(is_marker(instruction))
      {
        for(const auto &id :
            find_symbol_identifiers(instruction.call_arguments()[1]))
        {
          tracking.relevant.insert(id);
        }
      }
    }
  }
  bool grew = true;
  while(grew)
  {
    grew = false;
    for(const auto &entry : goto_model.goto_functions.function_map)
    {
      for(const auto &instruction : entry.second.body.instructions)
      {
        if(instruction.is_assign())
        {
          const symbol_exprt *symbol = base_symbol(instruction.assign_lhs());
          if(
            symbol == nullptr ||
            tracking.relevant.count(symbol->identifier()) == 0)
          {
            continue;
          }
          for(const auto &id :
              find_symbol_identifiers(instruction.assign_rhs()))
            grew |= tracking.relevant.insert(id).second;
        }
        else if(
          instruction.is_function_call() &&
          instruction.call_function().id() == ID_symbol)
        {
          const symbolt *callee = nullptr;
          if(tracking.ns.lookup(
               to_symbol_expr(instruction.call_function()).identifier(),
               callee))
          {
            continue;
          }
          const auto &parameters = to_code_type(callee->type).parameters();
          const auto &arguments = instruction.call_arguments();
          for(std::size_t i = 0; i < parameters.size() && i < arguments.size();
              ++i)
          {
            if(tracking.relevant.count(parameters[i].get_identifier()) == 0)
              continue;
            for(const auto &id : find_symbol_identifiers(arguments[i]))
              grew |= tracking.relevant.insert(id).second;
          }
        }
      }
    }
  }
}

/// Local variables whose only assignments copy one symbol of the same
/// type (see trackingt::copy_of).
void collect_copies(const goto_modelt &goto_model, trackingt &tracking)
{
  std::unordered_map<irep_idt, std::optional<irep_idt>> sources;
  for(const auto &entry : goto_model.goto_functions.function_map)
  {
    for(const auto &instruction : entry.second.body.instructions)
    {
      const symbol_exprt *lhs = nullptr;
      if(instruction.is_assign())
        lhs = root_symbol(instruction.assign_lhs());
      else if(
        instruction.is_function_call() && instruction.call_lhs().is_not_nil())
      {
        lhs = root_symbol(instruction.call_lhs());
      }
      if(lhs == nullptr || !tracking.is_local(lhs->identifier()))
        continue;
      std::optional<irep_idt> source;
      if(
        instruction.is_assign() && lhs == &instruction.assign_lhs() &&
        instruction.assign_rhs().id() == ID_symbol &&
        instruction.assign_rhs().type() == lhs->type())
      {
        source = to_symbol_expr(instruction.assign_rhs()).identifier();
      }
      const auto found = sources.find(lhs->identifier());
      if(found == sources.end())
        sources.emplace(lhs->identifier(), source);
      else if(found->second != source)
        found->second.reset();
    }
  }
  for(const auto &entry : sources)
  {
    if(
      entry.second.has_value() && *entry.second != entry.first &&
      tracking.address_taken.count(entry.first) == 0)
    {
      tracking.copy_of.emplace(entry.first, *entry.second);
    }
  }
}

/// Scalars that decide conditions (see trackingt::scalars), and what each
/// function may write of them (see trackingt::writes).
void collect_scalars(const goto_modelt &goto_model, trackingt &tracking)
{
  const auto &functions = goto_model.goto_functions.function_map;
  const auto add = [&tracking](const exprt &expr)
  {
    bool grew = false;
    for(const auto &id : find_symbol_identifiers(expr))
    {
      const symbolt *symbol = nullptr;
      if(
        tracking.address_taken.count(id) == 0 &&
        !tracking.ns.lookup(id, symbol) && tracking.is_scalar(symbol->type))
      {
        grew |= tracking.scalars.insert(id).second;
      }
    }
    return grew;
  };
  for(const auto &entry : functions)
  {
    for(const auto &instruction : entry.second.body.instructions)
    {
      if(instruction.is_goto() || instruction.is_assume())
        add(instruction.condition());
      tracking.indirect |= instruction.is_function_call() &&
                           instruction.call_function().id() != ID_symbol;
    }
  }
  bool grew = true;
  while(grew)
  {
    grew = false;
    for(const auto &entry : functions)
    {
      for(const auto &instruction : entry.second.body.instructions)
      {
        if(
          instruction.is_assign() &&
          instruction.assign_lhs().id() == ID_symbol &&
          tracking.scalars.count(
            to_symbol_expr(instruction.assign_lhs()).identifier()) != 0)
        {
          grew |= add(instruction.assign_rhs());
        }
      }
    }
  }

  // The scalars of static lifetime that each function writes itself, and
  // the functions it calls.
  std::unordered_set<irep_idt> globals;
  for(const auto &id : tracking.scalars)
  {
    if(tracking.is_global(id))
      globals.insert(id);
  }
  std::unordered_map<irep_idt, std::unordered_set<irep_idt>> callees;
  for(const auto &entry : functions)
  {
    auto &writes = tracking.writes[entry.first];
    const auto write = [&](const exprt &lhs)
    {
      for(const auto &id : find_symbol_identifiers(lhs))
      {
        if(globals.count(id) != 0)
          writes.insert(id);
      }
    };
    for(const auto &instruction : entry.second.body.instructions)
    {
      if(instruction.is_assign())
        write(instruction.assign_lhs());
      else if(instruction.is_other())
        write(instruction.code());
      else if(instruction.is_function_call())
      {
        write(instruction.call_lhs());
        if(instruction.call_function().id() == ID_symbol)
        {
          callees[entry.first].insert(
            to_symbol_expr(instruction.call_function()).identifier());
        }
        else
          writes = globals;
      }
    }
  }
  bool more = true;
  while(more)
  {
    more = false;
    for(const auto &entry : callees)
    {
      auto &writes = tracking.writes[entry.first];
      for(const auto &callee : entry.second)
      {
        const auto found = tracking.writes.find(callee);
        if(found == tracking.writes.end() || found->first == entry.first)
          continue;
        for(const auto &id : found->second)
          more |= writes.insert(id).second;
      }
    }
  }
}

/// Give DYNAMIC_SITE_FUNCTION an empty body and its parameters, which the
/// abstract interpreter needs to analyse calls of it.
void prepare_marker_function(goto_modelt &goto_model)
{
  for(const auto &entry : goto_model.symbol_table.symbols)
  {
    if(id2string(entry.first) != DYNAMIC_SITE_FUNCTION)
      continue;
    auto &function = goto_model.goto_functions.function_map[entry.first];
    if(function.body.instructions.empty())
      function.body.add(goto_programt::make_end_function());
    function.set_parameter_identifiers(to_code_type(entry.second.type));
    return;
  }
}
} // namespace

dynamic_resolutionst analyse_dynamic_sites(
  goto_modelt &goto_model,
  std::size_t bound,
  dynamic_site_statisticst &statistics)
{
  statistics = dynamic_site_statisticst{};
  prepare_marker_function(goto_model);
  trackingt tracking{goto_model, bound};
  collect_relevant(goto_model, tracking);
  if(tracking.relevant.empty())
    return {};

  for(const auto &entry : goto_model.goto_functions.function_map)
  {
    if(entry.second.body_available())
      tracking.bodies.insert(entry.first);
  }
  collect_copies(goto_model, tracking);
  collect_scalars(goto_model, tracking);
  // Reachability and must-assigned symbols at each instruction.
  must_assignedt must_assigned{goto_model, tracking};
  must_assigned();
  const auto assigned_before =
    [&must_assigned](const irep_idt &function, goto_programt::const_targett it)
  { return must_assigned.before(function, it); };

  // Flow-insensitive values, to a fixpoint.
  valuest values{tracking};
  bool changed = true;
  while(changed)
  {
    changed = false;
    for(const auto &entry : goto_model.goto_functions.function_map)
    {
      const bool in_initialize = entry.first == INITIALIZE_FUNCTION;
      const auto &body = entry.second.body;
      for(auto it = body.instructions.begin(); it != body.instructions.end();
          ++it)
      {
        const auto before = assigned_before(entry.first, it);
        if(!before.has_value())
          continue;
        const std::set<irep_idt> &assigned_here = *before;
        if(it->is_assign())
        {
          // A restore puts back a value that x had.
          if(tracking.is_restore(it->assign_lhs(), it->assign_rhs()))
            continue;
          bool weak = false;
          const auto id = tracking.written(it->assign_lhs(), weak);
          if(id.has_value())
          {
            // Element writes join into the array's elements; the array's
            // other elements keep their values, which a whole write of
            // the array also joined in. Both are joins here.
            changed |= values.add(
              in_initialize,
              *id,
              values.eval(it->assign_rhs(), assigned_here, in_initialize));
          }
          else if(const auto *symbol = base_symbol(it->assign_lhs()))
          {
            // A write of a tracked symbol in another way: any value,
            // unless it writes only other components of a struct.
            const exprt &lhs = it->assign_lhs();
            bool other_component = false;
            if(lhs.id() == ID_member)
            {
              const auto component = tracking.string_component(
                to_member_expr(lhs).compound().type());
              other_component =
                component.has_value() &&
                to_member_expr(lhs).get_component_name() != *component;
            }
            if(
              !other_component &&
              tracking.tracked(symbol->identifier()).has_value())
            {
              changed |= values.add(in_initialize, symbol->identifier(), {});
            }
          }
        }
        else if(it->is_function_call())
        {
          if(it->call_lhs().is_not_nil())
          {
            if(const auto *symbol = root_symbol(it->call_lhs()))
            {
              if(tracking.tracked(symbol->identifier()).has_value())
                changed |= values.add(in_initialize, symbol->identifier(), {});
            }
          }
          // Parameters get the values of the arguments.
          const symbolt *callee = nullptr;
          if(
            it->call_function().id() != ID_symbol ||
            tracking.ns.lookup(
              to_symbol_expr(it->call_function()).identifier(), callee))
          {
            continue;
          }
          const auto &parameters = to_code_type(callee->type).parameters();
          const auto &arguments = it->call_arguments();
          for(std::size_t i = 0; i < parameters.size(); ++i)
          {
            const auto id = tracking.tracked(parameters[i].get_identifier());
            if(!id.has_value())
              continue;
            changed |= values.add(
              false,
              *id,
              i < arguments.size()
                ? values.eval(arguments[i], assigned_here, in_initialize)
                : valuet{});
          }
        }
      }
    }
  }

  // The values at the sites, joined over the copies of each.
  std::map<irep_idt, valuet> sites;
  for(const auto &entry : goto_model.goto_functions.function_map)
  {
    const auto &body = entry.second.body;
    for(auto it = body.instructions.begin(); it != body.instructions.end();
        ++it)
    {
      if(!is_marker(*it) || !it->call_arguments()[0].is_constant())
        continue;
      const irep_idt site =
        to_constant_expr(it->call_arguments()[0]).get_value();
      const auto before = assigned_before(entry.first, it);
      const valuet value = !before.has_value()
                             ? no_value()
                             : values.eval(
                                 it->call_arguments()[1],
                                 *before,
                                 entry.first == INITIALIZE_FUNCTION);
      const auto found = sites.find(site);
      if(found == sites.end())
        sites.emplace(site, value);
      else
        found->second = tracking.join(found->second, value);
    }
  }

  dynamic_resolutionst resolutions;
  for(const auto &site : sites)
  {
    if(!site.second.has_value())
      statistics.unresolved.insert(site.first);
    else
    {
      ++(site.second->empty() ? statistics.unreached : statistics.finite);
      resolutions.emplace(site.first, *site.second);
    }
  }
  return resolutions;
}
