/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Symbolic Execution

#include "goto_symex.h"

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/format_expr.h>
#include <util/fresh_symbol.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/prefix.h>
#include <util/refined_string_type.h>
#include <util/simplify_utils.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/string_expr.h>
#include <util/string_utils.h>

#include "expr_skeleton.h"
#include "simplify_expr_with_value_set.h"
#include "symex_assign.h"

#include <climits>

void goto_symext::do_simplify(exprt &expr, const value_sett &value_set)
{
  if(symex_config.simplify_opt)
  {
    simplify_expr_with_value_sett{value_set, language_mode, ns}.simplify(expr);
  }
}

void goto_symext::symex_assign(
  statet &state,
  const exprt &o_lhs,
  const exprt &o_rhs)
{
  exprt lhs = clean_expr(o_lhs, state, true);
  exprt rhs = clean_expr(o_rhs, state, false);

  // Handle type mismatches from dynamically-typed languages (Python)
  // where variables can be assigned values of different types.
  if(lhs.type() != rhs.type())
  {
    rhs = typecast_exprt::conditional_cast(rhs, lhs.type());
  }

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    lhs.type() == rhs.type(),
    "assignments must be type consistent, got",
    lhs.type().pretty(),
    rhs.type().pretty(),
    state.source.pc->source_location());

  log.conditional_output(
    log.debug(),
    [this, &lhs](messaget::mstreamt &mstream)
    {
      mstream << "Assignment to " << format(lhs) << " ["
              << pointer_offset_bits(lhs.type(), ns).value_or(0) << " bits]"
              << messaget::eom;
    });

  // rvalues present within the lhs (for example, "some_array[this_rvalue]" or
  // "byte_extract <type> from an_lvalue offset this_rvalue") can affect whether
  // we use field-sensitive symbols or not, so L2-rename them up front:
  lhs = state.l2_rename_rvalues(lhs, ns);
  do_simplify(lhs, state.value_set);
  lhs = state.field_sensitivity.apply(ns, state, std::move(lhs), true);

  if(rhs.id() == ID_side_effect)
  {
    const side_effect_exprt &side_effect_expr = to_side_effect_expr(rhs);
    const irep_idt &statement = side_effect_expr.get_statement();

    if(
      statement == ID_cpp_new || statement == ID_cpp_new_array ||
      statement == ID_java_new_array_data)
    {
      symex_cpp_new(state, lhs, side_effect_expr);
    }
    else if(statement == ID_allocate)
      symex_allocate(state, lhs, side_effect_expr);
    else if(statement == ID_va_start)
      symex_va_start(state, lhs, side_effect_expr);
    else
      UNREACHABLE;
  }
  else
  {
    assignment_typet assignment_type = symex_targett::assignment_typet::STATE;

    // Let's hide return value assignments.
    if(
      lhs.id() == ID_symbol &&
      id2string(to_symbol_expr(lhs).identifier()).find("#return_value!") !=
        std::string::npos)
    {
      assignment_type = symex_targett::assignment_typet::HIDDEN;
    }

    // We hide if we are in a hidden function.
    if(state.call_stack().top().hidden_function)
      assignment_type = symex_targett::assignment_typet::HIDDEN;

    // We hide if we are executing a hidden instruction.
    if(state.source.pc->source_location().get_hide())
      assignment_type = symex_targett::assignment_typet::HIDDEN;

    symex_assignt symex_assign{
      shadow_memory,
      state,
      assignment_type,
      ns,
      symex_config,
      language_mode,
      target};

    // String backend (choice B): canonicalise cprover_string comparison
    // content pointers to their backing array object so the refinement
    // reasons over real, constrained content. Currently Python-specific: the
    // materialisation is only valid for symex-resolved leaf content (Python's
    // chr symbol arrays); it is harmful for refinement-constrained associated
    // content (e.g. JBMC's char[]), which has no clean symex-side condition,
    // so this stays gated while phase-2 backing (below) is language-agnostic.
    if(language_mode == "python" && rhs.id() == ID_function_application)
      resolve_python_string_content(to_function_application_expr(rhs), state);

    // Try to constant propagate potential side effects of the assignment, when
    // simplification is turned on and there is one thread only. Constant
    // propagation is only sound for sequential code as here we do not take into
    // account potential writes from other threads when propagating the values.
    if(symex_config.simplify_opt && state.threads.size() <= 1)
    {
      if(constant_propagate_assignment_with_side_effects(
           state, symex_assign, lhs, rhs))
        return;
    }

    // We may end up reading (and writing) all components of an object composed
    // of multiple fields. In such cases, we must do so atomically to avoid
    // overwriting components modified by another thread. Note that this also
    // implies multiple shared reads on the rhs being treated as atomic.
    const bool maybe_divisible =
      lhs.id() == ID_index ||
      (is_ssa_expr(lhs) &&
       state.field_sensitivity.is_divisible(to_ssa_expr(lhs), false));
    const bool need_atomic_section = maybe_divisible &&
                                     state.threads.size() > 1 &&
                                     state.atomic_section_id == 0;

    if(need_atomic_section)
      symex_atomic_begin(state);

    exprt::operandst lhs_if_then_else_conditions;
    symex_assignt{
      shadow_memory,
      state,
      assignment_type,
      ns,
      symex_config,
      language_mode,
      target}
      .assign_rec(lhs, expr_skeletont{}, rhs, lhs_if_then_else_conditions);

    if(need_atomic_section)
      symex_atomic_end(state);
  }
}

/// Maps the given array expression containing constant characters to a string
/// containing only alphanumeric characters
///
/// \param char_array: array_exprt containing characters represented by
///   expressions of kind constant_exprt and type unsignedbv_typet or
///   signedbv_typet
/// \return a string containing only alphanumeric characters
static std::string get_alnum_string(const array_exprt &char_array)
{
  const auto &ibv_type =
    to_integer_bitvector_type(to_array_type(char_array.type()).element_type());

  const std::size_t n_bits = ibv_type.get_width();
  CHECK_RETURN(n_bits % 8 == 0);

  static_assert(CHAR_BIT == 8, "bitwidth of char assumed to be 8");

  const std::size_t n_chars = n_bits / 8;

  INVARIANT(
    sizeof(std::size_t) >= n_chars,
    "size_t shall be large enough to represent a character");

  std::string result;

  for(const auto &c : char_array.operands())
  {
    const std::size_t c_val = numeric_cast_v<std::size_t>(to_constant_expr(c));

    for(std::size_t i = 0; i < n_chars; i++)
    {
      const char c_chunk = static_cast<char>((c_val >> (i * 8)) & 0xff);
      result.push_back(c_chunk);
    }
  }

  return escape_non_alnum(result);
}

/// Resolve a Python string content pointer to a literal char array of its
/// real content, sized to the backing array's actual byte length and pinned
/// to the current SSA version, so the string refinement's array_pool fast
/// path reasons over the constrained content with no front-end association.
/// Sizing to the true length (not a fixed bound) keeps chr cheap and
/// preserves multibyte content. \p app's refined-string content operands are
/// rewritten in place. Gated to Python by the caller; literal-array and
/// unresolved (e.g. refinement-produced) content are left untouched.
void goto_symext::resolve_python_string_content(
  function_application_exprt &app,
  statet &state)
{
  if(app.function().id() != ID_symbol)
    return;
  const irep_idt fn = to_symbol_expr(app.function()).get_identifier();
  // Any string intrinsic: the value-set guard below only rewrites content
  // that resolves to a single concrete array object, so refinement-produced
  // (unconstrained) output content is left untouched.
  if(id2string(fn).find("cprover_string") == std::string::npos)
    return;

  const typet index_type = signedbv_typet{64};

  for(auto &arg : app.arguments())
  {
    if(
      arg.id() != ID_struct || arg.operands().size() != 2 ||
      !is_refined_string_type(arg.type()))
      continue;
    const exprt content = arg.operands()[1];
    if(content.type().id() != ID_pointer)
      continue;

    // Determine the backing array object the content pointer denotes.
    exprt backing;
    if(
      content.id() == ID_address_of &&
      to_address_of_expr(content).object().id() == ID_index)
    {
      const exprt &a =
        to_index_expr(to_address_of_expr(content).object()).array();
      // A literal constant array already routes through the refinement's fast
      // path unchanged; leave it alone.
      if(a.id() == ID_array || a.type().id() != ID_array)
        continue;
      backing = a;
    }
    else
    {
      // Opaque pointer (e.g. a struct member read after the string was stored
      // in a variable). Query the value-set read-only first: only proceed if
      // it resolves to exactly one array object. This avoids calling
      // dereference() on an unconstrained/invalid pointer (e.g. a
      // refinement-produced concat result), whose failed-object side effect
      // would otherwise disturb the refinement.
      const std::vector<exprt> vs = state.value_set.get_value_set(content, ns);
      if(vs.size() != 1)
        continue;
      exprt obj = vs.front();
      if(obj.id() == ID_object_descriptor)
        obj = to_object_descriptor_expr(obj).object();
      backing = obj.id() == ID_index ? to_index_expr(obj).array() : obj;
    }

    // The byte length is the backing array's static size.
    if(backing.type().id() != ID_array)
      continue;
    const exprt &size_expr = to_array_type(backing.type()).size();
    if(!size_expr.is_constant())
      continue;
    mp_integer n;
    if(to_integer(to_constant_expr(size_expr), n) || n < 0 || n > 64)
      continue;
    const std::size_t len = numeric_cast_v<std::size_t>(n);

    // Materialise *(content + k) for each byte, pinned to its current SSA
    // version (address_of below would otherwise leave the symbol at its
    // unconstrained base), into a literal array the fast path resolves.
    const typet char_type = to_pointer_type(content.type()).base_type();
    exprt::operandst elems;
    elems.reserve(len);
    for(std::size_t k = 0; k < len; ++k)
    {
      exprt e = dereference_exprt{
        plus_exprt{content, from_integer(k, index_type)}, char_type};
      dereference(e, state, false);
      e = state.rename<L2>(std::move(e), ns).get();
      elems.push_back(std::move(e));
    }
    array_typet array_type{char_type, from_integer(len, index_type)};
    array_exprt materialised{std::move(elems), array_type};
    arg.operands()[1] = address_of_exprt{
      index_exprt{std::move(materialised), from_integer(0, index_type)}};
  }
}

bool goto_symext::constant_propagate_assignment_with_side_effects(
  statet &state,
  symex_assignt &symex_assign,
  const exprt &lhs,
  const exprt &rhs)
{
  if(rhs.id() == ID_function_application)
  {
    const function_application_exprt &f_l1 = to_function_application_expr(rhs);

    if(f_l1.function().id() == ID_symbol)
    {
      const irep_idt &func_id = to_symbol_expr(f_l1.function()).identifier();

      // String-producing intrinsics whose first two arguments are the output
      // (length, content). When constant propagation below succeeds it
      // installs real backing for the result; when it fails (symbolic
      // inputs) and the result is not already backed by a concrete array
      // (i.e. the front-end has not installed one -- JBMC does, the Python
      // front-end does not), install a fresh nondet backing array and
      // associate it so the produced result has real content memory the
      // refinement constrains. Language-agnostic: gated on whether backing
      // already exists, not on the source language.
      auto with_backing = [&](bool constant_folded) -> bool
      {
        if(!constant_folded && !string_result_already_backed(state, f_l1))
          setup_string_result_backing(state, symex_assign, f_l1);
        return constant_folded;
      };

      if(func_id == ID_cprover_string_concat_func)
      {
        return with_backing(
          constant_propagate_string_concat(state, symex_assign, f_l1));
      }
      else if(func_id == ID_cprover_string_empty_string_func)
      {
        // constant propagating the empty string always succeeds as it does
        // not depend on potentially non-constant inputs
        constant_propagate_empty_string(state, symex_assign, f_l1);
        return true;
      }
      else if(func_id == ID_cprover_string_substring_func)
      {
        return with_backing(
          constant_propagate_string_substring(state, symex_assign, f_l1));
      }
      else if(
        func_id == ID_cprover_string_of_int_func ||
        func_id == ID_cprover_string_of_long_func)
      {
        return with_backing(
          constant_propagate_integer_to_string(state, symex_assign, f_l1));
      }
      else if(func_id == ID_cprover_string_delete_char_at_func)
      {
        return with_backing(
          constant_propagate_delete_char_at(state, symex_assign, f_l1));
      }
      else if(func_id == ID_cprover_string_delete_func)
      {
        return with_backing(
          constant_propagate_delete(state, symex_assign, f_l1));
      }
      else if(func_id == ID_cprover_string_set_length_func)
      {
        return with_backing(
          constant_propagate_set_length(state, symex_assign, f_l1));
      }
      else if(func_id == ID_cprover_string_char_set_func)
      {
        return with_backing(
          constant_propagate_set_char_at(state, symex_assign, f_l1));
      }
      else if(func_id == ID_cprover_string_trim_func)
      {
        return with_backing(constant_propagate_trim(state, symex_assign, f_l1));
      }
      else if(func_id == ID_cprover_string_to_lower_case_func)
      {
        return with_backing(
          constant_propagate_case_change(state, symex_assign, f_l1, false));
      }
      else if(func_id == ID_cprover_string_to_upper_case_func)
      {
        return with_backing(
          constant_propagate_case_change(state, symex_assign, f_l1, true));
      }
      else if(func_id == ID_cprover_string_replace_func)
      {
        return with_backing(
          constant_propagate_replace(state, symex_assign, f_l1));
      }
    }
  }

  return false;
}

void goto_symext::assign_string_constant(
  statet &state,
  symex_assignt &symex_assign,
  const ssa_exprt &length,
  const constant_exprt &new_length,
  const ssa_exprt &char_array,
  const array_exprt &new_char_array)
{
  // We need to make sure that the length of the previous array isn't
  // unconstrained, otherwise it could be arbitrarily large which causes
  // invalid traces
  symex_assume(state, equal_exprt{length, from_integer(0, length.type())});

  // assign length of string
  symex_assign.assign_symbol(length, expr_skeletont{}, new_length, {});

  const std::string aux_symbol_name =
    get_alnum_string(new_char_array) + "_constant_char_array";

  const bool string_constant_exists =
    state.symbol_table.has_symbol(aux_symbol_name);

  const symbolt &aux_symbol =
    string_constant_exists
      ? state.symbol_table.lookup_ref(aux_symbol_name)
      : get_new_string_data_symbol(
          state, symex_assign, aux_symbol_name, char_array, new_char_array);

  INVARIANT(
    aux_symbol.value == new_char_array,
    "symbol shall have value derived from char array content");

  const address_of_exprt string_data(
    index_exprt(aux_symbol.symbol_expr(), from_integer(0, c_index_type())));

  symex_assign.assign_symbol(char_array, expr_skeletont{}, string_data, {});

  if(!string_constant_exists)
  {
    associate_array_to_pointer(
      state, symex_assign, new_char_array, string_data);
  }
}

const symbolt &goto_symext::get_new_string_data_symbol(
  statet &state,
  symex_assignt &symex_assign,
  const std::string &aux_symbol_name,
  const ssa_exprt &char_array,
  const array_exprt &new_char_array)
{
  array_typet new_char_array_type = new_char_array.type();
  new_char_array_type.set(ID_C_constant, true);

  symbolt &new_aux_symbol = get_fresh_aux_symbol(
    new_char_array_type,
    "",
    aux_symbol_name,
    source_locationt(),
    ns.lookup(to_symbol_expr(char_array.get_original_expr())).mode,
    ns,
    state.symbol_table);

  CHECK_RETURN(new_aux_symbol.is_state_var);
  CHECK_RETURN(new_aux_symbol.is_lvalue);

  new_aux_symbol.is_static_lifetime = true;
  new_aux_symbol.is_file_local = false;
  new_aux_symbol.is_thread_local = false;

  new_aux_symbol.value = new_char_array;

  const exprt arr = state.rename(new_aux_symbol.symbol_expr(), ns).get();

  symex_assign.assign_symbol(
    to_ssa_expr(arr).get_l1_object(), expr_skeletont{}, new_char_array, {});

  return new_aux_symbol;
}

void goto_symext::associate_array_to_pointer(
  statet &state,
  symex_assignt &symex_assign,
  const array_exprt &new_char_array,
  const address_of_exprt &string_data)
{
  const symbolt &function_symbol =
    ns.lookup(ID_cprover_associate_array_to_pointer_func);

  const function_application_exprt array_to_pointer_app{
    function_symbol.symbol_expr(), {new_char_array, string_data}};

  symbol_exprt return_symbol_expr =
    get_fresh_aux_symbol(
      to_mathematical_function_type(function_symbol.type).codomain(),
      "",
      "return_value",
      source_locationt(),
      function_symbol.mode,
      ns,
      state.symbol_table)
      .symbol_expr();

  const ssa_exprt ssa_expr(return_symbol_expr);

  symex_assign.assign_symbol(
    ssa_expr, expr_skeletont{}, array_to_pointer_app, {});
}

void goto_symext::setup_string_result_backing(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  if(f_l1.arguments().size() < 2)
    return;
  const exprt &length_arg = f_l1.arguments().at(0);
  const exprt &content_arg = f_l1.arguments().at(1);
  if(!is_ssa_expr(content_arg) || content_arg.type().id() != ID_pointer)
    return;
  const typet char_type = to_pointer_type(content_arg.type()).base_type();

  // Mode of the backing matches the result content's symbol (language of the
  // producing front-end), keeping this routine language-agnostic.
  irep_idt mode = ID_C;
  if(
    const symbolt *content_sym =
      state.symbol_table.lookup(to_ssa_expr(content_arg).get_object_name()))
    mode = content_sym->mode;

  // Fresh per-execution backing array (a distinct aux symbol per dynamic
  // call, mirroring the constant-string path, so loops never re-associate a
  // single pointer to two arrays).
  const array_typet backing_type{char_type, infinity_exprt(signedbv_typet{64})};
  symbolt &backing = get_fresh_aux_symbol(
    backing_type,
    "",
    "string_content_backing",
    source_locationt{},
    mode,
    ns,
    state.symbol_table);
  backing.is_state_var = true;
  backing.is_lvalue = true;
  const symbol_exprt backing_expr = backing.symbol_expr();

  const address_of_exprt string_data{
    index_exprt{backing_expr, from_integer(0, signedbv_typet{64})}};

  // result.content := &backing[0]
  symex_assign.assign_symbol(
    to_ssa_expr(content_arg), expr_skeletont{}, string_data, {});

  // Associate backing<->pointer and backing<->length with the string solver,
  // so find(content) resolves to `backing` (real memory the refinement's
  // axioms for this operation will constrain) with the result length.
  auto emit_associate = [&](const irep_idt &fn_id, const exprt &b)
  {
    if(!state.symbol_table.has_symbol(fn_id))
    {
      symbolt fs{
        fn_id,
        mathematical_function_typet(
          {backing_type, b.type()}, signedbv_typet{32}),
        mode};
      fs.base_name = id2string(fn_id);
      state.symbol_table.insert(std::move(fs));
    }
    const symbolt &fsym = ns.lookup(fn_id);
    function_application_exprt app{fsym.symbol_expr(), {backing_expr, b}};
    const symbol_exprt rc =
      get_fresh_aux_symbol(
        to_mathematical_function_type(fsym.type).codomain(),
        "",
        "return_value",
        source_locationt{},
        mode,
        ns,
        state.symbol_table)
        .symbol_expr();
    symex_assign.assign_symbol(ssa_exprt{rc}, expr_skeletont{}, app, {});
  };
  emit_associate(ID_cprover_associate_array_to_pointer_func, string_data);
  emit_associate(ID_cprover_associate_length_to_array_func, length_arg);
}

bool goto_symext::string_result_already_backed(
  statet &state,
  const function_application_exprt &f_l1)
{
  if(f_l1.arguments().size() < 2)
    return true;
  const exprt &content = f_l1.arguments().at(1);
  if(content.type().id() != ID_pointer)
    return true;
  // If the value-set resolves the content pointer to a concrete object, a
  // front-end (e.g. JBMC) has already installed and associated real backing
  // and we must not install a second one (double association is a crash). A
  // bare nondet result pointer (e.g. the Python front-end's __string_ptr)
  // resolves to nothing concrete -> not backed -> symex installs backing.
  const std::vector<exprt> vs = state.value_set.get_value_set(content, ns);
  for(const auto &v : vs)
  {
    const exprt &obj = v.id() == ID_object_descriptor
                         ? to_object_descriptor_expr(v).object()
                         : v;
    if(obj.id() == ID_unknown || obj.id() == ID_invalid)
      continue;
    if(
      obj.id() == ID_symbol &&
      has_prefix(
        id2string(to_symbol_expr(obj).get_identifier()), "symex::invalid"))
      continue;
    return true;
  }
  return false;
}

std::optional<std::reference_wrapper<const array_exprt>>
goto_symext::try_evaluate_constant_string(
  const statet &state,
  const exprt &content)
{
  if(content.id() != ID_symbol)
  {
    return {};
  }

  const auto s_pointer_opt =
    state.propagation.find(to_symbol_expr(content).identifier());

  if(!s_pointer_opt)
  {
    return {};
  }

  return try_get_string_data_array(s_pointer_opt->get(), ns);
}

std::optional<std::reference_wrapper<const constant_exprt>>
goto_symext::try_evaluate_constant(const statet &state, const exprt &expr)
{
  if(expr.id() != ID_symbol)
  {
    return {};
  }

  const auto constant_expr_opt =
    state.propagation.find(to_symbol_expr(expr).identifier());

  if(!constant_expr_opt || !constant_expr_opt->get().is_constant())
  {
    return {};
  }

  return std::optional<std::reference_wrapper<const constant_exprt>>(
    to_constant_expr(constant_expr_opt->get()));
}

void goto_symext::constant_propagate_empty_string(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const constant_exprt length = from_integer(0, length_type);

  const array_typet char_array_type(char_type, length);

  DATA_INVARIANT(
    f_l1.arguments().size() >= 2,
    "empty string primitive requires two output arguments");

  const array_exprt char_array({}, char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    length,
    to_ssa_expr(f_l1.arguments().at(1)),
    char_array);
}

bool goto_symext::constant_propagate_string_concat(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s1 = to_string_expr(f_l1.arguments().at(2));
  const auto s1_data_opt = try_evaluate_constant_string(state, s1.content());

  if(!s1_data_opt)
    return false;

  const array_exprt &s1_data = s1_data_opt->get();

  const refined_string_exprt &s2 = to_string_expr(f_l1.arguments().at(3));
  const auto s2_data_opt = try_evaluate_constant_string(state, s2.content());

  if(!s2_data_opt)
    return false;

  const array_exprt &s2_data = s2_data_opt->get();

  const std::size_t new_size =
    s1_data.operands().size() + s2_data.operands().size();

  const constant_exprt new_char_array_length =
    from_integer(new_size, length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);

  exprt::operandst operands(s1_data.operands());
  operands.insert(
    operands.end(), s2_data.operands().begin(), s2_data.operands().end());

  const array_exprt new_char_array(std::move(operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_string_substring(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  const std::size_t num_operands = f_l1.arguments().size();

  PRECONDITION(num_operands >= 4);
  PRECONDITION(num_operands <= 5);

  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
  const auto s_data_opt = try_evaluate_constant_string(state, s.content());

  if(!s_data_opt)
    return false;

  const array_exprt &s_data = s_data_opt->get();

  mp_integer end_index;

  if(num_operands == 5)
  {
    const auto end_index_expr_opt =
      try_evaluate_constant(state, f_l1.arguments().at(4));

    if(!end_index_expr_opt)
    {
      return false;
    }

    end_index =
      numeric_cast_v<mp_integer>(to_constant_expr(*end_index_expr_opt));

    if(end_index < 0 || end_index > s_data.operands().size())
    {
      return false;
    }
  }
  else
  {
    end_index = s_data.operands().size();
  }

  const auto start_index_expr_opt =
    try_evaluate_constant(state, f_l1.arguments().at(3));

  if(!start_index_expr_opt)
  {
    return false;
  }

  const mp_integer start_index =
    numeric_cast_v<mp_integer>(to_constant_expr(*start_index_expr_opt));

  if(start_index < 0 || start_index > end_index)
  {
    return false;
  }

  const constant_exprt new_char_array_length =
    from_integer(end_index - start_index, length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);

  exprt::operandst operands(
    std::next(
      s_data.operands().begin(), numeric_cast_v<std::size_t>(start_index)),
    std::next(
      s_data.operands().begin(), numeric_cast_v<std::size_t>(end_index)));

  const array_exprt new_char_array(std::move(operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_integer_to_string(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  // The function application expression f_l1 takes the following arguments:
  // - result string length (output parameter)
  // - result string data array (output parameter)
  // - integer to convert to a string
  // - radix (optional, default 10)
  const std::size_t num_operands = f_l1.arguments().size();

  PRECONDITION(num_operands >= 3);
  PRECONDITION(num_operands <= 4);

  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const auto &integer_opt =
    try_evaluate_constant(state, f_l1.arguments().at(2));

  if(!integer_opt)
  {
    return false;
  }

  const mp_integer integer = numeric_cast_v<mp_integer>(integer_opt->get());

  unsigned base = 10;

  if(num_operands == 4)
  {
    const auto &base_constant_opt =
      try_evaluate_constant(state, f_l1.arguments().at(3));

    if(!base_constant_opt)
    {
      return false;
    }

    const auto base_opt = numeric_cast<unsigned>(base_constant_opt->get());

    if(!base_opt)
    {
      return false;
    }

    base = *base_opt;
  }

  std::string s = integer2string(integer, base);

  const constant_exprt new_char_array_length =
    from_integer(s.length(), length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);

  exprt::operandst operands;

  std::transform(
    s.begin(),
    s.end(),
    std::back_inserter(operands),
    [&char_type](const char c) { return from_integer(tolower(c), char_type); });

  const array_exprt new_char_array(std::move(operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_delete_char_at(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  // The function application expression f_l1 takes the following arguments:
  // - result string length (output parameter)
  // - result string data array (output parameter)
  // - string to delete char from
  // - index of char to delete
  PRECONDITION(f_l1.arguments().size() == 4);

  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
  const auto s_data_opt = try_evaluate_constant_string(state, s.content());

  if(!s_data_opt)
  {
    return false;
  }

  const array_exprt &s_data = s_data_opt->get();

  const auto &index_opt = try_evaluate_constant(state, f_l1.arguments().at(3));

  if(!index_opt)
  {
    return false;
  }

  const mp_integer index = numeric_cast_v<mp_integer>(index_opt->get());

  if(index < 0 || index >= s_data.operands().size())
  {
    return false;
  }

  const constant_exprt new_char_array_length =
    from_integer(s_data.operands().size() - 1, length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);

  exprt::operandst operands;
  operands.reserve(s_data.operands().size() - 1);

  const std::size_t i = numeric_cast_v<std::size_t>(index);

  operands.insert(
    operands.end(),
    s_data.operands().begin(),
    std::next(s_data.operands().begin(), i));

  operands.insert(
    operands.end(),
    std::next(s_data.operands().begin(), i + 1),
    s_data.operands().end());

  const array_exprt new_char_array(std::move(operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_delete(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  // The function application expression f_l1 takes the following arguments:
  // - result string length (output parameter)
  // - result string data array (output parameter)
  // - string to delete substring from
  // - index of start of substring to delete (inclusive)
  // - index of end of substring to delete (exclusive)
  PRECONDITION(f_l1.arguments().size() == 5);

  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
  const auto s_data_opt = try_evaluate_constant_string(state, s.content());

  if(!s_data_opt)
  {
    return false;
  }

  const array_exprt &s_data = s_data_opt->get();

  const auto &start_opt = try_evaluate_constant(state, f_l1.arguments().at(3));

  if(!start_opt)
  {
    return false;
  }

  const mp_integer start = numeric_cast_v<mp_integer>(start_opt->get());

  if(start < 0 || start > s_data.operands().size())
  {
    return false;
  }

  const auto &end_opt = try_evaluate_constant(state, f_l1.arguments().at(4));

  if(!end_opt)
  {
    return false;
  }

  const mp_integer end = numeric_cast_v<mp_integer>(end_opt->get());

  if(start > end)
  {
    return false;
  }

  const std::size_t start_index = numeric_cast_v<std::size_t>(start);

  const std::size_t end_index =
    std::min(numeric_cast_v<std::size_t>(end), s_data.operands().size());

  const std::size_t new_size =
    s_data.operands().size() - end_index + start_index;

  const constant_exprt new_char_array_length =
    from_integer(new_size, length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);

  exprt::operandst operands;
  operands.reserve(new_size);

  operands.insert(
    operands.end(),
    s_data.operands().begin(),
    std::next(s_data.operands().begin(), start_index));

  operands.insert(
    operands.end(),
    std::next(s_data.operands().begin(), end_index),
    s_data.operands().end());

  const array_exprt new_char_array(std::move(operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_set_length(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  // The function application expression f_l1 takes the following arguments:
  // - result string length (output parameter)
  // - result string data array (output parameter)
  // - current string
  // - new length of the string
  PRECONDITION(f_l1.arguments().size() == 4);

  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const auto &new_length_opt =
    try_evaluate_constant(state, f_l1.arguments().at(3));

  if(!new_length_opt)
  {
    return false;
  }

  const mp_integer new_length =
    numeric_cast_v<mp_integer>(new_length_opt->get());

  if(new_length < 0)
  {
    return false;
  }

  const std::size_t new_size = numeric_cast_v<std::size_t>(new_length);

  const constant_exprt new_char_array_length =
    from_integer(new_size, length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);

  exprt::operandst operands;

  if(new_size != 0)
  {
    operands.reserve(new_size);

    const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
    const auto s_data_opt = try_evaluate_constant_string(state, s.content());

    if(!s_data_opt)
    {
      return false;
    }

    const array_exprt &s_data = s_data_opt->get();

    operands.insert(
      operands.end(),
      s_data.operands().begin(),
      std::next(
        s_data.operands().begin(),
        std::min(new_size, s_data.operands().size())));

    operands.insert(
      operands.end(),
      new_size - std::min(new_size, s_data.operands().size()),
      from_integer(0, char_type));
  }

  const array_exprt new_char_array(std::move(operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_set_char_at(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  // The function application expression f_l1 takes the following arguments:
  // - result string length (output parameter)
  // - result string data array (output parameter)
  // - current string
  // - index of char to set
  // - new char
  PRECONDITION(f_l1.arguments().size() == 5);

  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
  const auto s_data_opt = try_evaluate_constant_string(state, s.content());

  if(!s_data_opt)
  {
    return false;
  }

  array_exprt s_data = s_data_opt->get();

  const auto &index_opt = try_evaluate_constant(state, f_l1.arguments().at(3));

  if(!index_opt)
  {
    return false;
  }

  const mp_integer index = numeric_cast_v<mp_integer>(index_opt->get());

  if(index < 0 || index >= s_data.operands().size())
  {
    return false;
  }

  const auto &new_char_opt =
    try_evaluate_constant(state, f_l1.arguments().at(4));

  if(!new_char_opt)
  {
    return false;
  }

  const constant_exprt new_char_array_length =
    from_integer(s_data.operands().size(), length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);

  s_data.operands()[numeric_cast_v<std::size_t>(index)] = new_char_opt->get();

  const array_exprt new_char_array(
    std::move(s_data.operands()), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_case_change(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1,
  bool to_upper)
{
  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
  const auto s_data_opt = try_evaluate_constant_string(state, s.content());

  if(!s_data_opt)
    return false;

  array_exprt string_data = s_data_opt->get();

  auto &operands = string_data.operands();
  for(auto &operand : operands)
  {
    auto &constant_value = to_constant_expr(operand);
    auto character = numeric_cast_v<unsigned int>(constant_value);

    // Can't guarantee matches against non-ASCII characters.
    if(character >= 128)
      return false;

    if(isalpha(character))
    {
      if(to_upper)
      {
        if(islower(character))
          constant_value =
            from_integer(toupper(character), constant_value.type());
      }
      else
      {
        if(isupper(character))
          constant_value =
            from_integer(tolower(character), constant_value.type());
      }
    }
  }

  const constant_exprt new_char_array_length =
    from_integer(operands.size(), length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);
  const array_exprt new_char_array(std::move(operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_replace(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
  const auto s_data_opt = try_evaluate_constant_string(state, s.content());

  if(!s_data_opt)
    return false;

  auto &new_data = f_l1.arguments().at(4);
  auto &old_data = f_l1.arguments().at(3);

  array_exprt::operandst characters_to_find;
  array_exprt::operandst characters_to_replace;

  // Two main ways to perform a replace: characters or strings.
  bool is_single_character = new_data.type().id() == ID_unsignedbv &&
                             old_data.type().id() == ID_unsignedbv;
  if(is_single_character)
  {
    const auto new_char_pointer = try_evaluate_constant(state, new_data);
    const auto old_char_pointer = try_evaluate_constant(state, old_data);

    if(!new_char_pointer || !old_char_pointer)
    {
      return {};
    }

    characters_to_find.emplace_back(old_char_pointer->get());
    characters_to_replace.emplace_back(new_char_pointer->get());
  }
  else
  {
    auto &new_char_array = to_string_expr(new_data);
    auto &old_char_array = to_string_expr(old_data);

    const auto new_char_array_opt =
      try_evaluate_constant_string(state, new_char_array.content());

    const auto old_char_array_opt =
      try_evaluate_constant_string(state, old_char_array.content());

    if(!new_char_array_opt || !old_char_array_opt)
    {
      return {};
    }

    characters_to_find = old_char_array_opt->get().operands();
    characters_to_replace = new_char_array_opt->get().operands();
  }

  // Copy data, then do initial search for a replace sequence.
  array_exprt existing_data = s_data_opt->get();
  auto found_pattern = std::search(
    existing_data.operands().begin(),
    existing_data.operands().end(),
    characters_to_find.begin(),
    characters_to_find.end());

  // If we've found a match, proceed to perform a replace on all instances.
  while(found_pattern != existing_data.operands().end())
  {
    // Find the difference between our first/last match iterator.
    auto match_end = found_pattern + characters_to_find.size();

    // Erase them.
    found_pattern = existing_data.operands().erase(found_pattern, match_end);

    // Insert our replacement characters, then move the iterator to the end of
    // our new sequence.
    found_pattern = existing_data.operands().insert(
                      found_pattern,
                      characters_to_replace.begin(),
                      characters_to_replace.end()) +
                    characters_to_replace.size();

    // Then search from there for any additional matches.
    found_pattern = std::search(
      found_pattern,
      existing_data.operands().end(),
      characters_to_find.begin(),
      characters_to_find.end());
  }

  const constant_exprt new_char_array_length =
    from_integer(existing_data.operands().size(), length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);
  const array_exprt new_char_array(
    std::move(existing_data.operands()), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}

bool goto_symext::constant_propagate_trim(
  statet &state,
  symex_assignt &symex_assign,
  const function_application_exprt &f_l1)
{
  const auto &f_type = f_l1.function_type();
  const auto &length_type = f_type.domain().at(0);
  const auto &char_type = to_pointer_type(f_type.domain().at(1)).base_type();

  const refined_string_exprt &s = to_string_expr(f_l1.arguments().at(2));
  const auto s_data_opt = try_evaluate_constant_string(state, s.content());

  if(!s_data_opt)
    return false;

  auto is_not_whitespace = [](const exprt &expr)
  {
    auto character = numeric_cast_v<unsigned int>(to_constant_expr(expr));
    return character > ' ';
  };

  // Note the points where a trim would trim too.
  auto &operands = s_data_opt->get().operands();
  auto end_iter =
    std::find_if(operands.rbegin(), operands.rend(), is_not_whitespace);
  auto start_iter =
    std::find_if(operands.begin(), operands.end(), is_not_whitespace);

  // Then copy in the string with leading/trailing whitespace removed.
  // Note: if start_iter == operands.end it means the entire string is
  // whitespace, so we'll trim it to be empty anyway.
  exprt::operandst new_operands;
  if(start_iter != operands.end())
    new_operands = exprt::operandst{start_iter, end_iter.base()};

  const constant_exprt new_char_array_length =
    from_integer(new_operands.size(), length_type);

  const array_typet new_char_array_type(char_type, new_char_array_length);
  const array_exprt new_char_array(
    std::move(new_operands), new_char_array_type);

  assign_string_constant(
    state,
    symex_assign,
    to_ssa_expr(f_l1.arguments().at(0)),
    new_char_array_length,
    to_ssa_expr(f_l1.arguments().at(1)),
    new_char_array);

  return true;
}
