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
#include <util/std_expr.h>
#include <util/symbol.h>

#include <goto-programs/goto_model.h>

#include <linking/static_lifetime_init.h>

#include "ai.h"

#include <map>
#include <optional>
#include <set>
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

  /// Whether symbols of type \p type hold a string.
  bool is_string_like(const typet &type) const
  {
    return type.id() == ID_string || string_component(type).has_value();
  }

  /// The symbol that \p lhs writes the string of, if it is tracked.
  std::optional<irep_idt> written(const exprt &lhs) const
  {
    if(lhs.id() == ID_symbol && is_string_like(lhs.type()))
      return tracked(to_symbol_expr(lhs).identifier());
    if(lhs.id() == ID_member)
    {
      const auto &member = to_member_expr(lhs);
      const auto component = string_component(member.compound().type());
      if(
        component.has_value() && member.get_component_name() == *component &&
        member.compound().id() == ID_symbol)
      {
        return tracked(to_symbol_expr(member.compound()).identifier());
      }
    }
    return {};
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
};

/// The root symbol of \p lhs through members, if any.
const symbol_exprt *root_symbol(const exprt &lhs)
{
  const exprt *e = &lhs;
  while(e->id() == ID_member)
    e = &to_member_expr(*e).compound();
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

/// The symbols that are definitely assigned (outside __CPROVER_initialize),
/// among the relevant ones: static-lifetime ones, interprocedurally, where
/// the domain is bottom where the program cannot be; or local ones,
/// within one function, across whose calls they do not change.
class assigned_domaint : public ai_domain_baset
{
public:
  assigned_domaint(const trackingt &tracking, bool locals)
    : tracking(tracking), locals(locals)
  {
  }

  bool bottom = true;
  std::set<irep_idt> assigned;

  void transform(
    const irep_idt &function_from,
    trace_ptrt from,
    const irep_idt &,
    trace_ptrt,
    ai_baset &,
    const namespacet &) override
  {
    const auto &instruction = *from->current_location();
    const symbol_exprt *symbol = nullptr;
    if(instruction.is_assign())
      symbol = root_symbol(instruction.assign_lhs());
    else if(
      instruction.is_function_call() && instruction.call_lhs().is_not_nil())
    {
      symbol = root_symbol(instruction.call_lhs());
    }
    else if(instruction.is_decl())
      assigned.erase(instruction.decl_symbol().identifier());
    else if(instruction.is_dead())
      assigned.erase(instruction.dead_symbol().identifier());
    // The values that __CPROVER_initialize assigns are joined at reads
    // that the program did not assign.
    if(
      symbol != nullptr && function_from != INITIALIZE_FUNCTION &&
      tracking.relevant.count(symbol->identifier()) != 0 &&
      tracking.is_local(symbol->identifier()) == locals)
    {
      assigned.insert(symbol->identifier());
    }
  }

  void make_bottom() override
  {
    bottom = true;
    assigned.clear();
  }
  void make_top() override
  {
    bottom = false;
    assigned.clear();
  }
  void make_entry() override
  {
    make_top();
  }
  bool is_bottom() const override
  {
    return bottom;
  }
  bool is_top() const override
  {
    return !bottom && assigned.empty();
  }

  bool merge(const assigned_domaint &other, trace_ptrt, trace_ptrt)
  {
    if(other.bottom)
      return false;
    if(bottom)
    {
      bottom = false;
      assigned = other.assigned;
      return true;
    }
    bool changed = false;
    for(auto it = assigned.begin(); it != assigned.end();)
    {
      if(other.assigned.count(*it) == 0)
      {
        it = assigned.erase(it);
        changed = true;
      }
      else
        ++it;
    }
    return changed;
  }

private:
  const trackingt &tracking;
  const bool locals;
};

class assigned_domain_factoryt : public ai_domain_factoryt<assigned_domaint>
{
public:
  assigned_domain_factoryt(const trackingt &tracking, bool locals)
    : tracking(tracking), locals(locals)
  {
  }

  std::unique_ptr<statet> make(locationt) const override
  {
    return std::make_unique<assigned_domaint>(tracking, locals);
  }

private:
  const trackingt &tracking;
  const bool locals;
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
          const symbol_exprt *symbol = root_symbol(instruction.assign_lhs());
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

  // Reachability and must-assigned symbols at each instruction: global
  // ones over the whole program, and the locals of each function within
  // it.
  ait<assigned_domaint> assigned{
    std::make_unique<assigned_domain_factoryt>(tracking, false)};
  assigned(goto_model);
  std::map<irep_idt, std::unique_ptr<ait<assigned_domaint>>> local_assigned;
  for(const auto &entry : goto_model.goto_functions.function_map)
  {
    if(!entry.second.body_available())
      continue;
    auto analysis = std::make_unique<ait<assigned_domaint>>(
      std::make_unique<assigned_domain_factoryt>(tracking, true));
    (*analysis)(entry.first, entry.second, tracking.ns);
    local_assigned.emplace(entry.first, std::move(analysis));
  }
  // The symbols definitely assigned before \p it in \p function, or
  // nothing where the program cannot be.
  const auto assigned_before =
    [&](
      const irep_idt &function,
      goto_programt::const_targett it) -> std::optional<std::set<irep_idt>>
  {
    const auto &global = static_cast<const assigned_domaint &>(
      *assigned.abstract_state_before(it));
    if(global.bottom)
      return {};
    std::set<irep_idt> result = global.assigned;
    const auto local = local_assigned.find(function);
    if(local != local_assigned.end())
    {
      const auto &state = static_cast<const assigned_domaint &>(
        *local->second->abstract_state_before(it));
      result.insert(state.assigned.begin(), state.assigned.end());
    }
    return result;
  };

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
          const auto id = tracking.written(it->assign_lhs());
          if(id.has_value())
          {
            changed |= values.add(
              in_initialize,
              *id,
              values.eval(it->assign_rhs(), assigned_here, in_initialize));
          }
          else if(const auto *symbol = root_symbol(it->assign_lhs()))
          {
            // A write of the whole of a tracked symbol of another shape.
            if(
              symbol == &it->assign_lhs() &&
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
