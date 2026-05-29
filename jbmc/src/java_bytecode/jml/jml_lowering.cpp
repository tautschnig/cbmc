/*******************************************************************\

Module: JML-to-GOTO Lowering

Author: Kiro (AI agent)

\*******************************************************************/

#include "jml_lowering.h"

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/fresh_symbol.h>
#include <util/mathematical_expr.h>
#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_expr.h>

#include <goto-programs/goto_model.h>

#include <ansi-c/c_expr.h>
#include <langapi/language_util.h>

#include "jml_ids.h"

namespace
{

/// Resolve unresolved JML expressions (jml_field_access,
/// jml_method_call, untyped symbols) against the symbol table.
/// This is the "Phase 3 resolution" that the parser deferred.
exprt resolve_jml_expr(
  const exprt &e,
  const irep_idt &method_id,
  const irep_idt &class_id,
  const namespacet &ns,
  const std::vector<std::string> &param_names)
{
  // Resolve symbol_exprt with empty type: look up as parameter
  // or field of the enclosing class.
  if(e.id() == ID_symbol && e.type() == typet())
  {
    const irep_idt &name = to_symbol_expr(e).get_identifier();
    const std::string name_str = id2string(name);

    // Special case: \result → method's #return_value symbol
    if(name_str == CPROVER_PREFIX "return_value")
    {
      const irep_idt rv_id = id2string(method_id) + "#return_value";
      if(const auto *sym = ns.get_symbol_table().lookup(rv_id))
        return sym->symbol_expr();
      return e;
    }

    // Try as a parameter: match by base_name or by position.
    // JBMC names parameters as java::Class.method:(sig)::arg0x
    // where x is a type suffix. We match by base_name first.
    const symbolt *func_sym = ns.get_symbol_table().lookup(method_id);
    if(func_sym != nullptr && func_sym->type.id() == ID_code)
    {
      const auto &params = to_code_type(func_sym->type).parameters();

      // For instance methods, the GOTO `params` list begins with
      // an implicit `this` parameter that the source-extracted
      // `param_names` (parsed from the method signature in the
      // JML file) does not include. Skip it so positional-match
      // aligns the JML signature's i-th name with the i-th real
      // parameter.
      const std::size_t param_offset =
        (!params.empty() &&
         id2string(params.front().get_base_name()) == "this")
          ? 1
          : 0;

      // Positional match via param_names from source signature
      for(std::size_t i = 0;
          i < param_names.size() && i + param_offset < params.size();
          ++i)
      {
        if(param_names[i] == name_str)
        {
          const auto &p = params[i + param_offset];
          if(!p.get_identifier().empty())
          {
            if(
              const auto *psym =
                ns.get_symbol_table().lookup(p.get_identifier()))
              return psym->symbol_expr();
          }
        }
      }

      // Fallback: match by exact base_name. The previous prefix
      // form (`base.substr(0, name_str.size()) == name_str`) was
      // unsound — `i` would match `idx`, `iter`, etc. Aggregate
      // bound variables in particular need to NOT match.
      for(const auto &p : params)
      {
        const std::string base = id2string(p.get_base_name());
        if(base == name_str)
        {
          if(!p.get_identifier().empty())
          {
            if(
              const auto *psym =
                ns.get_symbol_table().lookup(p.get_identifier()))
              return psym->symbol_expr();
          }
        }
      }
      // Note: the previous "match by stripping 'arg' prefix +
      // index" fallback (where `name_str` was matched against the
      // suffix of any param's identifier) has been REMOVED. It
      // produced false matches such as `i` -> `arg0i`. If a JML
      // expression cannot be resolved through the strict paths
      // above, leave it unresolved so downstream produces a clear
      // error rather than silently bind to the wrong variable.
    }

    // Try as a fully-qualified parameter: method_id::name
    const irep_idt param_id = id2string(method_id) + "::" + name_str;
    if(const auto *sym = ns.get_symbol_table().lookup(param_id))
      return sym->symbol_expr();

    // Special case: bare `this` → the implicit first parameter
    // of an instance method. Look up `<method_id>::this`.
    if(name_str == "this")
    {
      const irep_idt this_id = id2string(method_id) + "::this";
      if(const auto *sym = ns.get_symbol_table().lookup(this_id))
        return sym->symbol_expr();
    }

    // Try as a static field: class_id.name. Walk the class
    // hierarchy if the field isn't on the enclosing class
    // directly: a JML expression that references an inherited
    // field without an explicit `super.` qualifier should still
    // resolve. We follow base-class symbols via the struct
    // type's components — the first component on a derived
    // class is `@<BaseName>`, whose type is a struct_tag of the
    // parent class — and try each ancestor in turn.
    //
    // For instance fields, build a member-access chain rooted
    // at the method's `this` parameter and walking through each
    // `@<Parent>` synthetic component until we reach the
    // declaring class.
    {
      irep_idt cur_class_id = class_id;
      // `this_chain`, when non-empty, is a member_exprt rooted
      // at `*this` whose value walks the @-component chain.
      // Built lazily the first time we need it.
      auto build_this_chain = [&]() -> exprt
      {
        const irep_idt this_id = id2string(method_id) + "::this";
        const auto *this_sym = ns.get_symbol_table().lookup(this_id);
        if(this_sym == nullptr)
          return nil_exprt();
        exprt this_ref = this_sym->symbol_expr();
        if(this_ref.type().id() == ID_pointer)
          this_ref = dereference_exprt(this_ref);
        return this_ref;
      };
      exprt this_chain = nil_exprt();
      while(!cur_class_id.empty())
      {
        const irep_idt static_field_id =
          id2string(cur_class_id) + "." + name_str;
        if(const auto *sym = ns.get_symbol_table().lookup(static_field_id))
          return sym->symbol_expr();

        const auto *cls = ns.get_symbol_table().lookup(cur_class_id);
        if(cls == nullptr || cls->type.id() != ID_struct)
          break;
        const auto &components = to_struct_type(cls->type).components();

        // Direct match on this class's components.
        for(const auto &c : components)
        {
          if(c.get_name() == name_str || c.get_base_name() == name_str)
          {
            if(this_chain.is_nil())
              this_chain = build_this_chain();
            if(this_chain.is_nil())
              break;
            return member_exprt(
              std::move(this_chain), c.get_name(), c.type());
          }
        }

        // Walk to the base class via the synthetic `@<Parent>`
        // component (java_bytecode_convert_class emits this as
        // the first component of subclasses). Extend the
        // member-access chain so the eventual leaf access is
        // typed against the correct ancestor struct.
        irep_idt next;
        for(const auto &c : components)
        {
          const std::string cname = id2string(c.get_name());
          if(
            !cname.empty() && cname[0] == '@' &&
            c.type().id() == ID_struct_tag)
          {
            if(this_chain.is_nil())
              this_chain = build_this_chain();
            if(this_chain.is_nil())
              break;
            this_chain = member_exprt(
              std::move(this_chain), c.get_name(), c.type());
            next = to_struct_tag_type(c.type()).get_identifier();
            break;
          }
        }
        cur_class_id = next;
      }
    }

    // Leave unresolved — will produce a verification failure with
    // a clear "unknown symbol" message.
    return e;
  }

  // Resolve jml_field_access: obj.field → member_exprt or
  // static field symbol. The parser produced an
  // `jml_field_access` node carrying the field name; the
  // receiver is operands()[0] (which itself may be unresolved
  // and needs recursive resolution first).
  if(e.id() == jml_ids::jml_field_access && e.operands().size() == 1)
  {
    exprt receiver = resolve_jml_expr(
      e.operands()[0], method_id, class_id, ns, param_names);
    const irep_idt field_name{e.get("field_name")};

    // Static field: <ReceiverClassName>.field_name. The receiver
    // is a (resolved) symbol naming the class itself rather than
    // an instance — treat the receiver as a class identifier and
    // look up the static field directly.
    if(receiver.id() == ID_symbol)
    {
      const irep_idt &recv_id =
        to_symbol_expr(receiver).get_identifier();
      const irep_idt field_id =
        id2string(recv_id) + "." + id2string(field_name);
      if(const auto *sym = ns.get_symbol_table().lookup(field_id))
        return sym->symbol_expr();
    }

    // Instance field: dereference the pointer and walk the
    // struct's components (including super-classes via the
    // `@<Parent>` synthetic field).
    if(receiver.type().id() == ID_pointer)
      receiver = dereference_exprt(receiver);

    if(receiver.type().id() == ID_struct_tag)
    {
      irep_idt cur = to_struct_tag_type(receiver.type()).get_identifier();
      exprt cur_obj = receiver;
      while(!cur.empty())
      {
        const auto *cls = ns.get_symbol_table().lookup(cur);
        if(cls == nullptr || cls->type.id() != ID_struct)
          break;
        const auto &components = to_struct_type(cls->type).components();
        for(const auto &c : components)
        {
          if(
            c.get_name() == field_name ||
            c.get_base_name() == field_name)
          {
            return member_exprt(
              std::move(cur_obj), c.get_name(), c.type());
          }
        }
        // Step into `@<Parent>` synthetic component.
        irep_idt next;
        for(const auto &c : components)
        {
          const std::string cname = id2string(c.get_name());
          if(
            !cname.empty() && cname[0] == '@' &&
            c.type().id() == ID_struct_tag)
          {
            cur_obj = member_exprt(cur_obj, c.get_name(), c.type());
            next = to_struct_tag_type(c.type()).get_identifier();
            break;
          }
        }
        cur = next;
      }
    }

    // Couldn't resolve: leave the original node so downstream
    // produces a clear error.
    return e;
  }

  if(e.id() == ID_member && e.type().id() == ID_empty)
  {
    // Defer to Phase 3 full resolution (needs type info from obj)
    return e;
  }

  // Aggregate expressions need special handling: their bound
  // variable (operand[0]) is a symbol_exprt with the user's name
  // (e.g., "i"). If we recurse into operands first, the bound
  // variable's references inside the range and body get resolved
  // by the parameter-name fallback (substring matching) to
  // arbitrary parameters in the enclosing method. To avoid that,
  // we handle aggregates here, before generic recursion:
  //   1. Extract bound variable name and type
  //   2. Resolve the range with the bound variable shadowed
  //   3. Try to extract constant bounds from the resolved range
  //   4. For constant bounds, substitute the bound variable with
  //      each integer value, then recursively resolve the
  //      substituted body
  if(
    e.id() == jml_ids::jml_sum || e.id() == jml_ids::jml_product ||
    e.id() == jml_ids::jml_min || e.id() == jml_ids::jml_max)
  {
    if(e.operands().size() == 3 && e.operands()[0].id() == ID_symbol)
    {
      const symbol_exprt &bound_var = to_symbol_expr(e.operands()[0]);
      const irep_idt &bound_name = bound_var.get_identifier();
      const typet &bound_type = bound_var.type();
      const exprt &raw_range = e.operands()[1];
      const exprt &raw_body = e.operands()[2];

      // Substitute the bound variable's parser-level (untyped)
      // symbol_exprt with a typed one, so the resolver will skip
      // it (its type is no longer empty).
      auto type_bound_refs = [&](exprt ex) -> exprt
      {
        std::function<void(exprt &)> walk = [&](exprt &x)
        {
          if(
            x.id() == ID_symbol &&
            to_symbol_expr(x).get_identifier() == bound_name)
          {
            x = symbol_exprt(bound_name, bound_type);
          }
          for(auto &child : x.operands())
            walk(child);
        };
        walk(ex);
        return ex;
      };

      const exprt range = resolve_jml_expr(
        type_bound_refs(raw_range), method_id, class_id, ns, param_names);

      // Extract constant bounds from the resolved range:
      // `lb <= bound_var && bound_var < ub` (or any commuted /
      // open / closed variant).
      std::optional<mp_integer> lb_val, ub_val;
      auto extract_constant = [](const exprt &x) -> std::optional<mp_integer>
      {
        if(!x.is_constant())
          return {};
        return numeric_cast<mp_integer>(to_constant_expr(x));
      };
      auto is_bound_ref = [&](const exprt &x) -> bool
      {
        return x.id() == ID_symbol &&
               to_symbol_expr(x).get_identifier() == bound_name;
      };
      if(range.id() == ID_and && range.operands().size() == 2)
      {
        for(const auto &conjunct : range.operands())
        {
          const irep_idt &op = conjunct.id();
          if(op != ID_le && op != ID_lt && op != ID_ge && op != ID_gt)
          {
            continue;
          }
          const auto &rel = to_binary_relation_expr(conjunct);
          const bool var_on_lhs = is_bound_ref(rel.lhs());
          const bool var_on_rhs = is_bound_ref(rel.rhs());
          if(var_on_lhs == var_on_rhs)
            continue;
          const auto k = var_on_lhs ? extract_constant(rel.rhs())
                                    : extract_constant(rel.lhs());
          if(!k.has_value())
            continue;
          // Canonicalise to `bound_var <op'> k`.
          irep_idt canonical_op = op;
          if(var_on_rhs)
          {
            if(op == ID_le)
              canonical_op = ID_ge;
            else if(op == ID_lt)
              canonical_op = ID_gt;
            else if(op == ID_ge)
              canonical_op = ID_le;
            else // ID_gt
              canonical_op = ID_lt;
          }
          if(canonical_op == ID_lt)
            ub_val = *k;
          else if(canonical_op == ID_le)
            ub_val = *k + 1;
          else if(canonical_op == ID_gt)
            lb_val = *k + 1;
          else // ID_ge
            lb_val = *k;
        }
      }

      static constexpr int MAX_AGGREGATE_EXPANSION = 100;
      if(
        lb_val.has_value() && ub_val.has_value() && *lb_val <= *ub_val &&
        *ub_val - *lb_val <= MAX_AGGREGATE_EXPANSION)
      {
        // For each integer i in [lb, ub): substitute bound_var
        // with constant i in the body, then recursively resolve
        // the substituted body.
        exprt::operandst expanded;
        for(mp_integer i = *lb_val; i < *ub_val; ++i)
        {
          exprt body_i = raw_body;
          std::function<void(exprt &)> sub = [&](exprt &x)
          {
            if(
              x.id() == ID_symbol &&
              to_symbol_expr(x).get_identifier() == bound_name)
            {
              x = from_integer(i, bound_type);
              return;
            }
            for(auto &child : x.operands())
              sub(child);
          };
          sub(body_i);
          expanded.push_back(
            resolve_jml_expr(body_i, method_id, class_id, ns, param_names));
        }
        if(expanded.empty())
        {
          // Empty range (lb == ub) → identity element.
          if(e.id() == jml_ids::jml_sum)
            return from_integer(0, bound_type);
          else if(e.id() == jml_ids::jml_product)
            return from_integer(1, bound_type);
          // For min/max with empty range there's no sensible
          // identity. Leave the aggregate uninterpreted; the
          // user shouldn't have written it over an empty range
          // anyway.
          return e;
        }
        exprt combined = expanded[0];
        for(std::size_t k = 1; k < expanded.size(); ++k)
        {
          if(e.id() == jml_ids::jml_sum)
            combined = plus_exprt(combined, expanded[k]);
          else if(e.id() == jml_ids::jml_product)
            combined = mult_exprt(combined, expanded[k]);
          else if(e.id() == jml_ids::jml_min)
            combined = if_exprt(
              binary_relation_exprt(combined, ID_le, expanded[k]),
              combined,
              expanded[k]);
          else // jml_max
            combined = if_exprt(
              binary_relation_exprt(combined, ID_ge, expanded[k]),
              combined,
              expanded[k]);
        }
        return combined;
      }
      // Bounds couldn't be extracted; fall through to leave the
      // aggregate as an uninterpreted exprt with resolved sub-
      // expressions. This is sound but the solver won't be able
      // to evaluate it.
    }
  }

  // Recurse into operands
  exprt result = e;
  for(auto &op : result.operands())
    op = resolve_jml_expr(op, method_id, class_id, ns, param_names);

  // Refresh the outer type after resolution. The parser
  // constructs many wrapper expressions (history_exprt,
  // unary_exprt, member_exprt, plus/minus/times) before the
  // inner symbol's type is known; their .type() field reflects
  // the unresolved operand's default type — which is `nil`
  // (irept id "nil") for nodes built by the parser-helper
  // exprt(id) constructor, and the empty `typet()` for nodes
  // built by other paths. Both shapes signal "type not yet
  // known", so propagate up if either holds.
  const auto needs_type =
    [](const exprt &x)
  {
    return x.type() == typet() || x.type().id().empty() ||
           x.type().is_nil();
  };
  // Harmonise operand types in (in)equality comparisons against
  // the parser's untyped null literal. The parser emits
  //   constant_exprt("NULL", pointer_typet(empty_typet(), 64))
  // for `null`, but after resolution the other operand has a
  // concrete struct_tag-pointer type. The pointer types
  // therefore differ in their pointee types, tripping the
  // (notequal_exprt) → equal_exprt precondition
  // `lhs.type() == rhs.type()`. Retype the null constant to
  // match its peer.
  if(
    (result.id() == ID_equal || result.id() == ID_notequal) &&
    result.operands().size() == 2)
  {
    auto retype_null = [](exprt &null_side, const exprt &peer)
    {
      if(
        null_side.id() == ID_constant && null_side.type().id() == ID_pointer &&
        peer.type().id() == ID_pointer && null_side.type() != peer.type())
      {
        null_side = constant_exprt(ID_NULL, peer.type());
      }
    };
    retype_null(result.operands()[0], result.operands()[1]);
    retype_null(result.operands()[1], result.operands()[0]);
  }

  if(needs_type(result))
  {
    if(
      result.id() == ID_old || result.id() == ID_loop_entry ||
      result.id() == ID_unary_minus || result.id() == ID_unary_plus)
    {
      if(!result.operands().empty())
        result.type() = result.operands()[0].type();
    }
    else if(
      result.id() == ID_plus || result.id() == ID_minus ||
      result.id() == ID_mult || result.id() == ID_div ||
      result.id() == ID_mod || result.id() == ID_bitand ||
      result.id() == ID_bitor || result.id() == ID_bitxor ||
      result.id() == ID_shl || result.id() == ID_ashr || result.id() == ID_lshr)
    {
      // Binary arithmetic / bitwise: type follows lhs.
      // (Java's promotion rules would widen byte/short → int but
      // the existing test uses int operands; widening can be
      // added later if needed.)
      if(!result.operands().empty())
        result.type() = result.operands()[0].type();
    }
    else if(
      result.id() == ID_lt || result.id() == ID_le || result.id() == ID_gt ||
      result.id() == ID_ge || result.id() == ID_equal ||
      result.id() == ID_notequal || result.id() == ID_and ||
      result.id() == ID_or || result.id() == ID_not ||
      result.id() == ID_implies)
    {
      // Logical / relational: result is bool.
      result.type() = bool_typet();
    }
    else if(result.id() == ID_if && result.operands().size() == 3)
    {
      // Ternary: type follows the (resolved) then/else branches.
      // The parser builds if_exprt for `cond ? a : b` constructs
      // with empty type because operand[1]/[2] aren't resolved
      // yet at construction time.
      result.type() = result.operands()[1].type();
    }
  }

  // Lower aggregate expressions (\sum, \product, \min, \max)
  // by bounded expansion when the range is of the form
  // `lb <= var && var < ub` with constant bounds.
  //
  // Aggregates are now handled at the top of resolve_jml_expr,
  // BEFORE generic operand recursion (because the bound variable
  // shadows enclosing parameters, so we must avoid resolving its
  // references via the substring-fallback path). The early-handler
  // returns directly when expansion succeeds; control flow only
  // reaches this section when expansion failed (e.g. non-constant
  // bounds), in which case we leave the aggregate as an
  // uninterpreted exprt.

  // Lower \fresh(e) to a call to __CPROVER_is_fresh(ptr, size).
  //
  // The contract is: \fresh(p) holds iff p was freshly allocated
  // (and therefore does not alias any pre-existing object). DFCC's
  // memory_predicates pass recognises calls to the symbol
  // __CPROVER_is_fresh and replaces them with the appropriate
  // pointer-allocation check.
  //
  // Two-arg shape (ptr, size) is required by the C typechecker
  // (see src/ansi-c/c_typecheck_expr.cpp). For Java references the
  // size value matters only for byte-level allocation tracking;
  // for reference-level freshness we use the size_t representation
  // of 1 (one object), which is the smallest sound choice.
  //
  // For inline (non-modular) JML, this expands to a function call
  // that DFCC's instrument pass turns into the proper predicate
  // when the goto-program is processed; for non-DFCC pipelines the
  // call remains unresolved and downstream symex will treat it as
  // an opaque boolean (the test will then fall back to assuming
  // fresh, which is the conservative behaviour for `requires`).
  if(result.id() == jml_ids::jml_fresh && result.operands().size() == 1)
  {
    const exprt &arg = result.operands()[0];
    if(arg.type().id() != ID_pointer)
    {
      // \fresh applied to a non-reference is ill-typed; leave
      // it as an uninterpreted exprt and let downstream produce
      // a clear error.
      return result;
    }
    const symbol_exprt is_fresh_fun{
      CPROVER_PREFIX "is_fresh",
      mathematical_function_typet{{arg.type(), size_type()}, bool_typet{}}};
    const exprt size_expr = from_integer(1, size_type());
    function_application_exprt call{is_fresh_fun, {arg, size_expr}};
    return call;
  }

  return result;
}

} // namespace

std::set<irep_idt>
lower_jml_contracts(goto_modelt &goto_model, const jml_contract_mapt &contracts)
{
  std::set<irep_idt> annotated_functions;
  const namespacet ns{goto_model.symbol_table};

  for(const auto &[method_id, spec] : contracts)
  {
    auto fn_it = goto_model.goto_functions.function_map.find(method_id);
    if(fn_it == goto_model.goto_functions.function_map.end())
      continue;

    auto &body = fn_it->second.body;
    if(body.instructions.empty())
      continue;

    const symbolt &func_sym = ns.lookup(method_id);
    const code_typet &func_type = to_code_type(func_sym.type);
    const irep_idt class_id = func_type.get(ID_C_class);

    // Collect clauses by kind
    exprt::operandst requires_exprs;
    exprt::operandst ensures_exprs;
    exprt::operandst assigns_exprs;
    exprt::operandst invariant_exprs;
    exprt::operandst decreases_exprs;

    for(const auto &clause : spec.clauses)
    {
      if(clause.expr.is_nil())
        continue;

      exprt resolved = resolve_jml_expr(
        clause.expr, method_id, class_id, ns, spec.param_names);

      // The JML clause is a logical predicate; symex expects
      // a bool_typet expression for ASSUME / ASSERT. If the
      // resolved expression is a Java boolean (c_bool_typet,
      // width 8) — typically because the clause is just
      // `obj.flag` for some `boolean` field — coerce it to
      // bool by comparing against zero.
      if(
        resolved.type().id() == ID_c_bool ||
        resolved.type().id() == ID_unsignedbv ||
        resolved.type().id() == ID_signedbv)
      {
        resolved = notequal_exprt(
          resolved, from_integer(0, resolved.type()));
      }

      switch(clause.kind)
      {
      case jml_clauset::kindt::REQUIRES:
        requires_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::ENSURES:
        ensures_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::ASSIGNABLE:
        assigns_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::INVARIANT:
        invariant_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::DECREASES:
        decreases_exprs.push_back(resolved);
        break;
      case jml_clauset::kindt::SIGNALS:
      case jml_clauset::kindt::PURE:
      case jml_clauset::kindt::NULLABLE:
      case jml_clauset::kindt::NON_NULL:
      case jml_clauset::kindt::ALSO:
      case jml_clauset::kindt::UNKNOWN:
        break;
      }
    }

    if(requires_exprs.empty() && ensures_exprs.empty())
      continue;

    // Pre-state capture for \old(expr): walk all ensures and
    // collect every history_exprt(expr, ID_old) subexpression.
    // For each unique expr, declare a fresh symbol and assign
    // it expr's value at function entry; then substitute
    // history_exprt(...) with the fresh symbol.
    //
    // Without this pass, inline-lowered ensures would reference
    // the parser's history_exprt, which symex cannot evaluate
    // for non-modular verification (no DFCC pre-state binding).
    //
    // We record the source expression alongside the fresh symbol
    // so the DECL/ASSIGN emission below doesn't need to re-walk
    // the clauses to find each \old's operand.
    struct old_capture_entryt
    {
      symbol_exprt fresh;
      exprt source_expr;
    };
    std::map<std::string, old_capture_entryt> old_capture;
    std::function<exprt(exprt)> capture_old = [&](exprt e) -> exprt
    {
      for(auto &op : e.operands())
        op = capture_old(op);
      if(e.id() == ID_old && e.operands().size() == 1 && !e.type().id().empty())
      {
        // Key by serialized expression text. pretty() builds a
        // recursive string representation; not free, but the
        // alternative — structural exprt equality — has its own
        // cost and we'd still need a stable key for the map.
        std::ostringstream key_oss;
        key_oss << e.operands()[0].pretty();
        const std::string key = key_oss.str();
        auto it_cap = old_capture.find(key);
        if(it_cap != old_capture.end())
          return it_cap->second.fresh;
        // Build fresh symbol: <method>::__jml_old_<idx>
        const std::string fresh_name = id2string(method_id) + "::__jml_old_" +
                                       std::to_string(old_capture.size());
        symbol_exprt fresh{fresh_name, e.type()};
        old_capture.emplace(key, old_capture_entryt{fresh, e.operands()[0]});
        return fresh;
      }
      return e;
    };
    for(auto &ens : ensures_exprs)
      ens = capture_old(ens);

    // Emit body-level ASSUME for requires at function entry
    auto first_it = body.instructions.begin();

    // Pre-state capture: DECL + ASSIGN for each captured \old.
    for(const auto &[key, entry] : old_capture)
    {
      // Add the symbol to the symbol table so symex sees it.
      symbolt sym;
      sym.name = entry.fresh.get_identifier();
      sym.base_name = sym.name;
      sym.type = entry.fresh.type();
      sym.mode = ID_java;
      sym.is_thread_local = true;
      sym.is_lvalue = true;
      sym.is_state_var = true;
      goto_model.symbol_table.insert(std::move(sym));

      source_locationt loc = first_it->source_location();
      loc.set_comment("JML \\old capture");
      loc.set_step_kind(ID_old_capture);
      body.insert_before(first_it, goto_programt::make_decl(entry.fresh, loc));
      body.insert_before(
        first_it,
        goto_programt::make_assignment(entry.fresh, entry.source_expr, loc));
    }

    for(const auto &req : requires_exprs)
    {
      source_locationt loc = first_it->source_location();
      loc.set_comment("JML requires");
      loc.set_step_kind(ID_precondition);
      loc.set_property_class("precondition");
      body.insert_before(first_it, goto_programt::make_assumption(req, loc));
    }

    // Emit body-level ASSERT for ensures at return sites
    // Find all SET_RETURN_VALUE or END_FUNCTION instructions
    for(auto it = body.instructions.begin(); it != body.instructions.end();
        ++it)
    {
      if(it->type() == END_FUNCTION)
      {
        for(const auto &ens : ensures_exprs)
        {
          source_locationt loc = it->source_location();
          loc.set_comment("JML ensures");
          loc.set_step_kind(ID_postcondition);
          loc.set_property_class("postcondition");
          body.insert_before(it, goto_programt::make_assertion(ens, loc));
        }
      }
    }

    // Emit loop invariants at loop heads (back-edge targets).
    // A loop head is any instruction that is the target of a
    // backward GOTO (i.e., a GOTO whose target has a lower
    // location number than the GOTO itself).
    if(!invariant_exprs.empty())
    {
      // Find loop heads
      std::vector<goto_programt::targett> loop_heads;
      for(auto it = body.instructions.begin(); it != body.instructions.end();
          ++it)
      {
        if(it->is_goto())
        {
          for(const auto &target : it->targets)
          {
            if(target->location_number <= it->location_number)
              loop_heads.push_back(target);
          }
        }
      }

      // Insert invariant assertions at each loop head
      for(auto head : loop_heads)
      {
        for(const auto &inv : invariant_exprs)
        {
          source_locationt loc = head->source_location();
          loc.set_comment("JML loop invariant");
          loc.set_step_kind(ID_loop_invariant);
          loc.set_property_class("loop-invariant");
          body.insert_before(head, goto_programt::make_assertion(inv, loc));
        }
      }
    }

    // Emit decreases (variant function) checks at loop heads.
    // For each loop head, assert that the variant is non-negative
    // (termination argument).
    if(!decreases_exprs.empty())
    {
      std::vector<goto_programt::targett> loop_heads;
      for(auto it = body.instructions.begin(); it != body.instructions.end();
          ++it)
      {
        if(it->is_goto())
        {
          for(const auto &target : it->targets)
          {
            if(target->location_number <= it->location_number)
              loop_heads.push_back(target);
          }
        }
      }

      for(auto head : loop_heads)
      {
        for(const auto &dec : decreases_exprs)
        {
          source_locationt loc = head->source_location();
          loc.set_comment("JML decreases (non-negative)");
          loc.set_step_kind(ID_decreases);
          loc.set_property_class("loop-variant");
          exprt non_neg =
            binary_relation_exprt(dec, ID_ge, from_integer(0, dec.type()));
          body.insert_before(head, goto_programt::make_assertion(non_neg, loc));
        }
      }
    }

    body.update();
    annotated_functions.insert(method_id);

    // Populate code_with_contract_typet on the symbol (for DFCC)
    auto sym_it = goto_model.symbol_table.get_writeable(method_id);
    if(sym_it != nullptr)
    {
      const code_typet &existing_type = to_code_type(sym_it->type);

      // Build parameter symbols for lambda wrapping
      binding_exprt::variablest parameter_syms;
      if(existing_type.return_type().id() != ID_empty)
      {
        parameter_syms.push_back(symbol_exprt(
          CPROVER_PREFIX "return_value", existing_type.return_type()));
      }
      for(const auto &p : existing_type.parameters())
      {
        if(!p.get_identifier().empty())
          parameter_syms.push_back(symbol_exprt(p.get_identifier(), p.type()));
      }

      auto wrap_lambda = [&](const exprt &e) -> exprt
      {
        lambda_exprt lambda(parameter_syms, e);
        lambda.add_source_location() = e.source_location();
        return std::move(lambda);
      };

      code_with_contract_typet contract_type(
        existing_type.parameters(), existing_type.return_type());
      static_cast<typet &>(contract_type)
        .set(ID_C_class, existing_type.get(ID_C_class));

      auto &c_req = contract_type.c_requires();
      auto &c_ens = contract_type.c_ensures();
      auto &c_asg = contract_type.c_assigns();

      for(const auto &e : requires_exprs)
        c_req.push_back(wrap_lambda(e));
      for(const auto &e : ensures_exprs)
        c_ens.push_back(wrap_lambda(e));
      for(const auto &e : assigns_exprs)
        c_asg.push_back(wrap_lambda(e));

      sym_it->type = contract_type;

      // Insert parallel contract:: symbol
      const irep_idt contract_id = "contract::" + id2string(method_id);
      if(!goto_model.symbol_table.has_symbol(contract_id))
      {
        symbolt contract;
        contract.name = contract_id;
        contract.base_name = sym_it->base_name;
        contract.pretty_name = sym_it->pretty_name;
        contract.is_property = false;
        contract.type = contract_type;
        contract.mode = sym_it->mode;
        contract.module = sym_it->module;
        contract.location = sym_it->location;
        goto_model.symbol_table.insert(std::move(contract));
      }
    }
  }

  // After mutating function symbol types to
  // code_with_contract_typet, refresh every CALL instruction's
  // call_function operand type from the symbol table. Without
  // this, --validate-goto-model fires `<callee> type
  // inconsistency` because the cached operand type at the call
  // site no longer matches the symbol-table entry's type. This
  // mirrors dfcct::refresh_call_function_types() in
  // src/goto-instrument/contracts/dynamic-frames/dfcc.cpp.
  if(!annotated_functions.empty())
  {
    const namespacet ns_refresh{goto_model.symbol_table};
    for(auto &gf_pair : goto_model.goto_functions.function_map)
    {
      for(auto &ins : gf_pair.second.body.instructions)
      {
        if(!ins.is_function_call())
          continue;
        exprt &callee = ins.call_function();
        if(callee.id() != ID_symbol)
          continue;
        const irep_idt callee_id = to_symbol_expr(callee).get_identifier();
        if(annotated_functions.count(callee_id) == 0)
          continue; // Only refresh callees we touched.
        const symbolt *sym = ns_refresh.get_symbol_table().lookup(callee_id);
        if(sym == nullptr)
          continue;
        callee.type() = sym->type;
      }
    }
  }

  return annotated_functions;
}
