/*******************************************************************\

Module: Symex Shadow Memory Instrumentation

Author: Peter Schrammel

\*******************************************************************/

/// \file
/// Symex Shadow Memory Instrumentation

#ifndef CPROVER_GOTO_SYMEX_SHADOW_MEMORY_H
#define CPROVER_GOTO_SYMEX_SHADOW_MEMORY_H

#include <util/cprover_prefix.h>
#include <util/expr.h>
#include <util/message.h>
#include <util/symbol.h>

#include "shadow_memory_field_definitions.h"
#include "shadow_memory_state.h"

#include <map>
#include <vector>

#define SHADOW_MEMORY_PREFIX "SM__"
#define SHADOW_MEMORY_FIELD_DECL "field_decl"
#define SHADOW_MEMORY_GLOBAL_SCOPE "_global"
#define SHADOW_MEMORY_LOCAL_SCOPE "_local"
#define SHADOW_MEMORY_GET_FIELD "get_field"
#define SHADOW_MEMORY_SET_FIELD "set_field"
#define SHADOW_MEMORY_SYMBOL_PREFIX "__SM"
/// A Boolean variable that, if the program declares it, set_field and
/// get_field set when they cannot resolve the shadow memory of some object
/// the pointer may point to (the value set is unknown or contains an
/// object without shadow memory): a client can assert that it stays false
/// rather than miss a lost field value.
#define SHADOW_MEMORY_INCOMPLETE CPROVER_PREFIX "shadow_memory_incomplete"

class code_function_callt;
class abstract_goto_modelt;
class goto_symex_statet;
class side_effect_exprt;
class ssa_exprt;
class symbol_exprt;

/// \brief The shadow memory instrumentation performed during symbolic execution
class shadow_memoryt
{
public:
  shadow_memoryt(
    const std::function<void(goto_symex_statet &, const exprt &, const exprt &)>
      symex_assign,
    const namespacet &ns,
    message_handlert &message_handler)
    : symex_assign(symex_assign), ns(ns), log(message_handler)
  {
  }

  /// Gathers the available shadow memory field definitions
  /// (__CPROVER_field_decl calls) from the goto model.
  /// \param goto_model The goto model
  /// \param message_handler For logging
  /// \return The field definitions
  static shadow_memory_field_definitionst gather_field_declarations(
    const abstract_goto_modelt &goto_model,
    message_handlert &message_handler);

  /// Initialize global-scope shadow memory for global/static variables.
  /// \param state The symex state
  /// \param lhs The LHS expression of the initializer assignment
  void symex_field_static_init(goto_symex_statet &state, const ssa_exprt &lhs);

  /// Initialize global-scope shadow memory for string constants.
  /// \param state The symex state
  /// \param expr The defined symbol expression
  /// \param rhs The RHS expression of the initializer assignment
  void symex_field_static_init_string_constant(
    goto_symex_statet &state,
    const ssa_exprt &expr,
    const exprt &rhs);

  /// Initialize local-scope shadow memory for local variables and parameters.
  /// \param state The symex state
  /// \param expr The declared symbol expression
  void symex_field_local_init(goto_symex_statet &state, const ssa_exprt &expr);

  /// Initialize global-scope shadow memory for dynamically allocated memory.
  /// \param state The symex state
  /// \param expr The dynamic object symbol expression
  /// \param code The allocation side effect code
  void symex_field_dynamic_init(
    goto_symex_statet &state,
    const exprt &expr,
    const side_effect_exprt &code);

  /// Symbolically executes a __CPROVER_get_field call
  /// \param state The symex state
  /// \param lhs The LHS of the call
  /// \param arguments The call arguments
  void symex_get_field(
    goto_symex_statet &state,
    const exprt &lhs,
    const exprt::operandst &arguments);

  /// A new run of symbolic execution starts: the registrations of all
  /// threads and paths of the previous run become those that a pointer may
  /// refer to before the object's shadow memory is registered in this run
  /// (by a thread that symbolic execution executes later); names agree
  /// across runs (see goto_symext::fresh_object_symbol), and add_field
  /// reuses their shadow symbols.
  void begin_run()
  {
    previous_addresses = std::move(all_addresses);
    all_addresses.clear();
  }

  /// Symbolically executes a __CPROVER_set_field call
  /// \param state The symex state
  /// \param arguments The call arguments
  void
  symex_set_field(goto_symex_statet &state, const exprt::operandst &arguments);

private:
  /// Converts a field declaration
  /// \param code_function_call The __CPROVER_field_decl_* call
  /// \param fields The field declaration to be extended
  /// \param is_global True if the declaration is global
  /// \param message_handler For logging
  static void convert_field_declaration(
    const code_function_callt &code_function_call,
    shadow_memory_field_definitionst::field_definitiont &fields,
    bool is_global,
    message_handlert &message_handler);

  /// Allocates and initializes a shadow memory field for the given original
  /// memory.
  /// \param state The symex state
  /// \param expr The expression for which shadow memory should be allocated
  /// \param fields The field definition to be used
  void initialize_shadow_memory(
    goto_symex_statet &state,
    exprt expr,
    const shadow_memory_field_definitionst::field_definitiont &fields);

  /// Registers a shadow memory field for the given original memory
  /// \param state The symex state
  /// \param expr The expression for which shadow memory should be allocated
  /// \param field_name The field name
  /// \param field_type The field type
  /// \return The resulting shadow memory symbol expression
  const symbol_exprt &add_field(
    goto_symex_statet &state,
    const exprt &expr,
    const irep_idt &field_name,
    const typet &field_type);

  /// Records in SHADOW_MEMORY_INCOMPLETE, if declared, that a field could
  /// not be resolved.
  void mark_incomplete(goto_symex_statet &state);

  /// The objects with shadow memory for field \p field_name: those that
  /// \p state registered, then those of other threads and paths
  /// (all_addresses), which a pointer that another thread wrote may point
  /// to.
  std::vector<shadow_memory_statet::shadowed_addresst>
  addresses_of(goto_symex_statet &state, const irep_idt &field_name);

private:
  const std::function<void(goto_symex_statet &, const exprt &, const exprt)>
    symex_assign;
  const namespacet &ns;
  messaget log;
  /// Every registration of shadow memory, of any thread and path: the
  /// registry of a state (shadow_memory_statet::address_fields) has those
  /// of its path only, as each thread starts with a copy of the spawning
  /// thread's.
  std::map<
    irep_idt,
    std::vector<std::pair<shadow_memory_statet::shadowed_addresst, symbolt>>>
    all_addresses;
  /// all_addresses of the previous run.
  std::map<
    irep_idt,
    std::vector<std::pair<shadow_memory_statet::shadowed_addresst, symbolt>>>
    previous_addresses;
};

#endif // CPROVER_GOTO_SYMEX_SHADOW_MEMORY_H
