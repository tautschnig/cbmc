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
#include <util/prefix.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/suffix.h>

#include <goto-programs/goto_model.h>

#include <ansi-c/c_expr.h>
#include <langapi/language_util.h>

#include "jml_ids.h"

#include <iostream>

#include "../java_bytecode_axiomatic.h"
#include "../java_types.h"
#include "../remove_exceptions.h"

namespace
{

/// Resolve unresolved JML expressions (jml_field_access,
/// jml_method_call, untyped symbols) against the symbol table.
/// This is the "Phase 3 resolution" that the parser deferred.
/// Recognised result for a trivial bean-style getter
/// (`int getX() { return this.x; }`). When the JML resolver
/// encounters a method call to such a getter, it rewrites the
/// call to a direct member access — the same shape that
/// `obj.x` would have produced — keeping JML's purity rule
/// honoured by inspection of the bytecode.
struct trivial_gettert
{
  irep_idt field_name;
  typet field_type;
  /// True if the original return expression had a final
  /// typecast (e.g. signedbv32 → c_bool8 for boolean
  /// getters). The caller may need to wrap the result with a
  /// matching typecast to preserve type-equality with the
  /// original `obj.getX()` site.
  bool returns_cast = false;
  typet return_type;
};

using trivial_getter_mapt = std::map<irep_idt, trivial_gettert>;

/// Strip outer typecast layers; return the innermost expression.
const exprt &strip_typecasts(const exprt &e)
{
  const exprt *cur = &e;
  while(cur->id() == ID_typecast && cur->operands().size() == 1)
    cur = &to_typecast_expr(*cur).op();
  return *cur;
}

/// Decide whether a goto-program body realises the trivial
/// `return this.field` pattern. Returns the field name + type
/// if so, std::nullopt otherwise.
std::optional<trivial_gettert>
recognise_trivial_getter(const irep_idt &method_id, const goto_programt &body)
{
  const std::string rv_id = id2string(method_id) + "#return_value";
  std::optional<trivial_gettert> found;

  for(const auto &ins : body.instructions)
  {
    // Skip harness-injected guards and bookkeeping.
    if(
      ins.is_dead() || ins.is_decl() || ins.is_skip() ||
      ins.is_atomic_begin() || ins.is_atomic_end() || ins.is_start_thread() ||
      ins.is_end_thread() || ins.is_end_function() || ins.is_location())
      continue;
    if(ins.is_assert() || ins.is_assume())
    {
      // Only ignore null-pointer / similar boilerplate.
      const auto &pc = ins.source_location().get_property_class();
      if(
        pc == "null-pointer-exception" || pc == "pointer-out-of-bounds" ||
        pc == "Null pointer check" || pc.empty())
        continue;
      return std::nullopt;
    }
    if(!ins.is_assign())
      return std::nullopt;
    if(found.has_value())
      return std::nullopt; // more than one meaningful assignment

    const auto &lhs = ins.assign_lhs();
    if(lhs.id() != ID_symbol)
      return std::nullopt;
    if(id2string(to_symbol_expr(lhs).get_identifier()) != rv_id)
      return std::nullopt;

    // Strip outer casts; expect member_exprt(deref(this), field).
    const exprt &raw_rhs = ins.assign_rhs();
    const exprt &core = strip_typecasts(raw_rhs);
    if(core.id() != ID_member)
      return std::nullopt;
    const auto &mem = to_member_expr(core);
    const exprt *root = &mem.compound();
    while(root->id() == ID_member)
      root = &to_member_expr(*root).compound();
    if(root->id() != ID_dereference)
      return std::nullopt;
    const exprt &deref_op = to_dereference_expr(*root).op();
    if(deref_op.id() != ID_symbol)
      return std::nullopt;
    const std::string this_id = id2string(method_id) + "::this";
    if(id2string(to_symbol_expr(deref_op).get_identifier()) != this_id)
      return std::nullopt;

    trivial_gettert g;
    g.field_name = mem.get_component_name();
    g.field_type = mem.type();
    g.returns_cast = (raw_rhs.id() == ID_typecast);
    g.return_type = lhs.type();
    found = g;
  }

  return found;
}

/// Build the map of trivial getters across all currently
/// loaded goto functions.
trivial_getter_mapt build_trivial_getters(const goto_functionst &fns)
{
  trivial_getter_mapt out;
  for(const auto &[id, fn] : fns.function_map)
  {
    if(fn.body.instructions.empty())
      continue;
    if(auto g = recognise_trivial_getter(id, fn.body))
      out[id] = *g;
  }
  return out;
}

/// Resolve a JML-supplied Java exception type name to a list of
/// fully-qualified `java::...` class identifiers, including the
/// transitive subclass closure.
///
/// The resolution rules are:
///   1. If the name already starts with `java::`, take it
///      verbatim (one match).
///   2. Else if `java::<name>` exists as a class symbol, use it.
///   3. Else treat the name as a simple class name and match
///      every `java::<pkg>.<name>` symbol — covers java.lang
///      import-style names and lets the user write `Foo`,
///      `pkg.Foo`, or `java::pkg.Foo` interchangeably.
///
/// After base resolution we walk every class symbol; if its
/// `@<Parent>` synthetic parent component refers to one of the
/// already-found classes, we add it. Iterates to saturation.
/// This is what lets `signals_only Throwable` cover every Java
/// exception type.
static std::vector<std::string>
resolve_jml_type_name(const namespacet &ns, const std::string &raw)
{
  std::string name = raw;
  std::vector<std::string> bases;
  if(name.rfind("java::", 0) == 0)
  {
    bases.push_back(name);
  }
  else if(ns.get_symbol_table().lookup("java::" + name) != nullptr)
  {
    bases.push_back("java::" + name);
  }
  else
  {
    const std::string suffix = "." + name;
    for(const auto &entry : ns.get_symbol_table().symbols)
    {
      const std::string id = id2string(entry.first);
      if(id.rfind("java::", 0) != 0)
        continue;
      // Class symbols have type ID_struct.
      if(entry.second.type.id() != ID_struct)
        continue;
      const std::string short_id = id.substr(6);
      if(
        short_id == name ||
        (short_id.size() > suffix.size() &&
         short_id.compare(
           short_id.size() - suffix.size(), suffix.size(), suffix) == 0))
      {
        bases.push_back(id);
      }
    }
  }
  std::set<std::string> all(bases.begin(), bases.end());
  bool changed = true;
  while(changed)
  {
    changed = false;
    for(const auto &entry : ns.get_symbol_table().symbols)
    {
      const std::string id = id2string(entry.first);
      if(id.rfind("java::", 0) != 0)
        continue;
      if(entry.second.type.id() != ID_struct)
        continue;
      if(all.count(id))
        continue;
      const auto &cs = to_struct_type(entry.second.type).components();
      for(const auto &c : cs)
      {
        const std::string cn = id2string(c.get_name());
        if(!cn.empty() && cn[0] == '@' && c.type().id() == ID_struct_tag)
        {
          const std::string parent =
            id2string(to_struct_tag_type(c.type()).get_identifier());
          if(all.count(parent))
          {
            all.insert(id);
            changed = true;
          }
          break;
        }
      }
    }
  }
  return std::vector<std::string>(all.begin(), all.end());
}

exprt resolve_jml_expr(
  const exprt &e,
  const irep_idt &method_id,
  const irep_idt &class_id,
  const namespacet &ns,
  const std::vector<std::string> &param_names,
  const trivial_getter_mapt &trivial_getters)
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
        (!params.empty() && id2string(params.front().get_base_name()) == "this")
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
            return member_exprt(std::move(this_chain), c.get_name(), c.type());
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
            !cname.empty() && cname[0] == '@' && c.type().id() == ID_struct_tag)
          {
            if(this_chain.is_nil())
              this_chain = build_this_chain();
            if(this_chain.is_nil())
              break;
            this_chain =
              member_exprt(std::move(this_chain), c.get_name(), c.type());
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
      e.operands()[0], method_id, class_id, ns, param_names, trivial_getters);
    const irep_idt field_name{e.get("field_name")};

    // Static field: <ReceiverClassName>.field_name. The receiver
    // is a (resolved) symbol naming the class itself rather than
    // an instance — treat the receiver as a class identifier and
    // look up the static field directly.
    if(receiver.id() == ID_symbol)
    {
      const irep_idt &recv_id = to_symbol_expr(receiver).get_identifier();
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
          if(c.get_name() == field_name || c.get_base_name() == field_name)
          {
            return member_exprt(std::move(cur_obj), c.get_name(), c.type());
          }
        }
        // Step into `@<Parent>` synthetic component.
        irep_idt next;
        for(const auto &c : components)
        {
          const std::string cname = id2string(c.get_name());
          if(
            !cname.empty() && cname[0] == '@' && c.type().id() == ID_struct_tag)
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

  // Resolve jml_method_call: obj.foo(args). JML's purity rule
  // requires methods called inside specifications to be free
  // of visible side effects. We honour the rule by inspection:
  // a JML method-call site is rewritten to a member access
  // only when the called method is a TRIVIAL bean-style getter
  // (`return this.field;`), which is provably pure by virtue
  // of its bytecode shape. Any other call leaves the node
  // unresolved and downstream produces a clear "non-trivial
  // method call in JML expression" error rather than silently
  // accepting a potentially-impure call.
  if(e.id() == jml_ids::jml_method_call && e.operands().size() >= 1)
  {
    const irep_idt method_name{e.get("method_name")};
    exprt receiver = resolve_jml_expr(
      e.operands()[0], method_id, class_id, ns, param_names, trivial_getters);

    // Trivial getters take no arguments. If the parser captured
    // arguments, this is not a candidate.
    if(e.operands().size() == 1)
    {
      // Resolve receiver class through its struct-tag type.
      exprt recv = receiver;
      if(recv.type().id() == ID_pointer)
        recv = dereference_exprt(recv);
      if(recv.type().id() == ID_struct_tag)
      {
        const irep_idt class_qid =
          to_struct_tag_type(recv.type()).get_identifier();
        // Walk the class hierarchy looking for a method
        // <ClassName>.<methodName>:... that is a known trivial
        // getter.
        irep_idt cur = class_qid;
        while(!cur.empty())
        {
          const std::string method_prefix =
            id2string(cur) + "." + id2string(method_name) + ":";
          // Find any symbol whose id starts with this prefix
          // and which corresponds to a trivial getter.
          for(const auto &[gid, info] : trivial_getters)
          {
            const std::string gid_str = id2string(gid);
            if(gid_str.rfind(method_prefix, 0) == 0)
            {
              // Build the equivalent member access.
              const auto *cls = ns.get_symbol_table().lookup(cur);
              if(cls != nullptr && cls->type.id() == ID_struct)
              {
                exprt cur_obj = recv;
                irep_idt walk = cur;
                while(!walk.empty())
                {
                  const auto *wcls = ns.get_symbol_table().lookup(walk);
                  if(wcls == nullptr || wcls->type.id() != ID_struct)
                    break;
                  const auto &comps = to_struct_type(wcls->type).components();
                  for(const auto &c : comps)
                  {
                    if(c.get_name() == info.field_name)
                    {
                      exprt mem = member_exprt(
                        std::move(cur_obj), info.field_name, info.field_type);
                      // Restore the original return-type cast
                      // if the bytecode getter applied one.
                      if(info.returns_cast)
                        mem = typecast_exprt(mem, info.return_type);
                      return mem;
                    }
                  }
                  irep_idt next;
                  for(const auto &c : comps)
                  {
                    const std::string cn = id2string(c.get_name());
                    if(
                      !cn.empty() && cn[0] == '@' &&
                      c.type().id() == ID_struct_tag)
                    {
                      cur_obj = member_exprt(cur_obj, c.get_name(), c.type());
                      next = to_struct_tag_type(c.type()).get_identifier();
                      break;
                    }
                  }
                  walk = next;
                }
              }
            }
          }
          // Walk to base class via @<Parent>.
          const auto *cls = ns.get_symbol_table().lookup(cur);
          if(cls == nullptr || cls->type.id() != ID_struct)
            break;
          const auto &cs = to_struct_type(cls->type).components();
          irep_idt next;
          for(const auto &c : cs)
          {
            const std::string cn = id2string(c.get_name());
            if(!cn.empty() && cn[0] == '@' && c.type().id() == ID_struct_tag)
            {
              next = to_struct_tag_type(c.type()).get_identifier();
              break;
            }
          }
          cur = next;
        }
      }
    }

    // Not a recognised trivial getter; emit a clear warning
    // and leave the node intact so downstream produces an
    // error site that points at the call. JML's purity rule
    // forbids arbitrary method invocations in specification
    // expressions, and JBMC only recognises trivial bean-
    // style getters today.
    std::cerr << "warning: JML method call '" << id2string(method_name)
              << "' in a specification expression is not a trivial bean "
              << "getter; only `return this.<field>` shapes are accepted "
              << "today. Either rewrite the spec to access the underlying "
              << "field directly, or wait for JBMC to grow @pure-method "
              << "support.\n";
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
        type_bound_refs(raw_range),
        method_id,
        class_id,
        ns,
        param_names,
        trivial_getters);

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
          expanded.push_back(resolve_jml_expr(
            body_i, method_id, class_id, ns, param_names, trivial_getters));
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
    op = resolve_jml_expr(
      op, method_id, class_id, ns, param_names, trivial_getters);

  // Refresh the outer type after resolution. The parser
  // constructs many wrapper expressions (history_exprt,
  // unary_exprt, member_exprt, plus/minus/times) before the
  // inner symbol's type is known; their .type() field reflects
  // the unresolved operand's default type — which is `nil`
  // (irept id "nil") for nodes built by the parser-helper
  // exprt(id) constructor, and the empty `typet()` for nodes
  // built by other paths. Both shapes signal "type not yet
  // known", so propagate up if either holds.
  const auto needs_type = [](const exprt &x)
  { return x.type() == typet() || x.type().id().empty() || x.type().is_nil(); };
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

  // Boolean-context operand coercion. When the parser
  // constructed `not_exprt`, `and_exprt`, `or_exprt`, or
  // similar boolean operators, the operand types were not
  // yet known. After resolution they may be Java booleans
  // (c_bool, width 8) or integer-typed (signedbv) — both
  // would trip the bool-operand invariants of the
  // surrounding nodes on a later structural visit. Coerce
  // each non-bool operand to a Boolean expression by
  // `!= 0`. This runs unconditionally (not just when the
  // outer node "needs a type") because the parser-built
  // nodes already have bool result types but operand types
  // that get refined here.
  if(
    result.id() == ID_and || result.id() == ID_or || result.id() == ID_not ||
    result.id() == ID_implies)
  {
    for(auto &op : result.operands())
    {
      if(
        op.type().id() == ID_c_bool || op.type().id() == ID_unsignedbv ||
        op.type().id() == ID_signedbv)
      {
        op = notequal_exprt(op, from_integer(0, op.type()));
      }
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

  // Build the table of trivial bean-style getters once across
  // the whole goto model. JML method-call expressions can then
  // be rewritten inline to direct member accesses without
  // running symex on the getter body. See
  // recognise_trivial_getter for the bytecode pattern.
  const trivial_getter_mapt trivial_getters =
    build_trivial_getters(goto_model.goto_functions);

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
    // SIGNALS_ONLY: list of allowed Java exception type names
    // accumulated from all signals_only clauses on the method.
    // Empty list means no signals_only clause was supplied.
    std::vector<std::string> signals_only_types;
    bool has_signals_only = false;

    // SIGNALS (typed): one entry per `signals (T e) p;` clause.
    // The captured fields are sufficient to build the
    // postcondition  (\inflight != null && classid \in T*) ==> p
    // at function exit.
    struct signals_typed_clauset
    {
      std::string type_name;
      std::string var_name;
      exprt predicate;
    };
    std::vector<signals_typed_clauset> signals_typed_clauses;

    for(const auto &clause : spec.clauses)
    {
      if(clause.expr.is_nil())
        continue;

      // Pre-resolve the typed-signals bound variable. For
      // `signals (T e) p;` the parser leaves `e` as an
      // unresolved symbol_exprt. We substitute every
      // occurrence of `e` with `(T*) @inflight_exception`
      // (typed) BEFORE handing the predicate to
      // resolve_jml_expr, so that `e.getFoo()` calls inside p
      // see a properly-typed receiver and the trivial-getter
      // rewrite can fire.
      exprt prepared_expr = clause.expr;
      if(
        clause.kind == jml_clauset::kindt::SIGNALS &&
        !clause.signal_type.empty() && !clause.signal_var.empty())
      {
        const auto *infl_sym =
          ns.get_symbol_table().lookup(INFLIGHT_EXCEPTION_VARIABLE_NAME);
        if(infl_sym != nullptr)
        {
          const exprt infl = infl_sym->symbol_expr();
          pointer_typet target_ptr =
            pointer_type(struct_tag_typet("java::java.lang.Object"));
          for(const auto &fq : resolve_jml_type_name(ns, clause.signal_type))
          {
            const auto *t_sym = ns.get_symbol_table().lookup(fq);
            if(t_sym != nullptr && t_sym->type.id() == ID_struct)
            {
              target_ptr = pointer_type(struct_tag_typet(fq));
              break;
            }
          }
          const exprt e_value = typecast_exprt(infl, target_ptr);
          std::function<void(exprt &)> subst = [&](exprt &e_walk)
          {
            if(
              e_walk.id() == ID_symbol &&
              id2string(to_symbol_expr(e_walk).get_identifier()) ==
                clause.signal_var)
            {
              e_walk = e_value;
              return;
            }
            for(auto &op : e_walk.operands())
              subst(op);
          };
          subst(prepared_expr);
        }
      }

      exprt resolved = resolve_jml_expr(
        prepared_expr,
        method_id,
        class_id,
        ns,
        spec.param_names,
        trivial_getters);

      // The JML clause is a logical predicate; symex expects
      // a bool_typet expression for ASSUME / ASSERT. If the
      // resolved expression is a Java boolean (c_bool_typet,
      // width 8) — typically because the clause is just
      // `obj.flag` for some `boolean` field — coerce it to
      // bool by comparing against zero.
      //
      // Skip the coercion for ASSIGNABLE clauses: those
      // expressions are l-values that name memory locations,
      // not predicates, and `obj.field != 0` would not be a
      // legitimate target.
      if(
        clause.kind != jml_clauset::kindt::ASSIGNABLE &&
        (resolved.type().id() == ID_c_bool ||
         resolved.type().id() == ID_unsignedbv ||
         resolved.type().id() == ID_signedbv))
      {
        resolved = notequal_exprt(resolved, from_integer(0, resolved.type()));
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
      case jml_clauset::kindt::SIGNALS_ONLY:
        has_signals_only = true;
        for(const auto &t : clause.signal_types)
          signals_only_types.push_back(t);
        break;
      case jml_clauset::kindt::SIGNALS:
        // signals (T e) p — typed exception postcondition. We
        // accept clauses where both type and variable are set;
        // untyped `signals expr;` is parsed but treated as a
        // no-op until the parser distinguishes the two forms.
        if(!clause.signal_type.empty() && !clause.signal_var.empty())
        {
          signals_typed_clauset c;
          c.type_name = clause.signal_type;
          c.var_name = clause.signal_var;
          c.predicate = resolved;
          signals_typed_clauses.push_back(std::move(c));
        }
        break;
      case jml_clauset::kindt::PURE:
      case jml_clauset::kindt::NULLABLE:
      case jml_clauset::kindt::NON_NULL:
      case jml_clauset::kindt::ALSO:
      case jml_clauset::kindt::UNKNOWN:
        break;
      }
    }

    if(
      requires_exprs.empty() && ensures_exprs.empty() && !has_signals_only &&
      signals_typed_clauses.empty() && assigns_exprs.empty() &&
      invariant_exprs.empty())
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
        // Insert all the exit-time checks BEFORE this
        // END_FUNCTION instruction, then redirect any GOTO
        // that targets END_FUNCTION (e.g. exception-throwing
        // paths emitted by remove_exceptions) to the first
        // inserted instruction. Without that redirection the
        // GOTOs jump straight at END_FUNCTION and skip the
        // ensures / signals_only checks entirely.
        const auto end_fn_it = it;
        // Track the first inserted instruction so we can
        // retarget gotos. Initially nil: if no checks are
        // inserted, no retargeting is needed.
        std::optional<goto_programt::targett> new_first;
        auto record_first = [&](goto_programt::targett inserted)
        {
          if(!new_first.has_value())
            new_first = inserted;
        };

        // ensures: per JML semantics, the postcondition only
        // applies on normal return, not on exceptional exit. We
        // emit `inflight == null ==> ens` so that an exception-
        // throwing path doesn't trigger the assertion.
        std::optional<exprt> normal_exit_guard;
        {
          const auto *infl_sym =
            ns.get_symbol_table().lookup(INFLIGHT_EXCEPTION_VARIABLE_NAME);
          if(infl_sym != nullptr)
          {
            const exprt infl = infl_sym->symbol_expr();
            normal_exit_guard = equal_exprt(
              infl, null_pointer_exprt(to_pointer_type(infl.type())));
          }
        }

        for(const auto &ens : ensures_exprs)
        {
          source_locationt loc = end_fn_it->source_location();
          loc.set_comment("JML ensures");
          loc.set_step_kind(ID_postcondition);
          loc.set_property_class("postcondition");
          exprt body_expr = ens;
          if(normal_exit_guard.has_value())
            body_expr = or_exprt(not_exprt(*normal_exit_guard), body_expr);
          auto inserted = body.insert_before(
            end_fn_it, goto_programt::make_assertion(body_expr, loc));
          record_first(inserted);
        }

        // Typed signals: signals (T e) p; — at the function exit,
        // assert that whenever an exception of (a subtype of) T
        // is in flight, the predicate p holds (with `e` bound to
        // the inflight exception cast to T).
        //
        // Emit BEFORE the signals_only block, because that block
        // assigns inflight to null at the end to suppress the
        // harness's default uncaught-exception check; if our
        // assertion ran afterwards it would always see inflight
        // == null and be vacuously true.
        if(!signals_typed_clauses.empty())
        {
          const auto *infl_sym =
            ns.get_symbol_table().lookup(INFLIGHT_EXCEPTION_VARIABLE_NAME);
          if(infl_sym != nullptr)
          {
            const exprt infl = infl_sym->symbol_expr();
            const auto *jlo_sym =
              ns.get_symbol_table().lookup("java::java.lang.Object");
            irep_idt classid_field;
            typet classid_type;
            if(jlo_sym != nullptr && jlo_sym->type.id() == ID_struct)
            {
              const auto &components =
                to_struct_type(jlo_sym->type).components();
              for(const auto &c : components)
              {
                if(
                  c.get_name() == "@class_identifier" ||
                  c.get_base_name() == "@class_identifier")
                {
                  classid_field = c.get_name();
                  classid_type = c.type();
                  break;
                }
              }
            }
            if(!classid_field.empty())
            {
              for(const auto &sc : signals_typed_clauses)
              {
                pointer_typet jlo_ptr =
                  pointer_type(struct_tag_typet("java::java.lang.Object"));
                const exprt jlo_obj =
                  dereference_exprt(typecast_exprt(infl, jlo_ptr));
                const exprt classid =
                  member_exprt(jlo_obj, classid_field, classid_type);

                // Build the type-class disjunction over T and its
                // subclasses.
                exprt class_match = false_exprt();
                for(const auto &fq : resolve_jml_type_name(ns, sc.type_name))
                {
                  const exprt eq =
                    equal_exprt(classid, constant_exprt(fq, classid_type));
                  class_match =
                    class_match.is_false() ? eq : or_exprt(class_match, eq);
                }

                // The predicate has already had `e` substituted
                // with `(T*) inflight` by the pre-resolve pass
                // and been run through resolve_jml_expr (so any
                // e.getFoo() calls have been rewritten via the
                // trivial-getter machinery).
                exprt pred = sc.predicate;

                // Coerce booleans (c_bool / Java boolean) to bool.
                if(
                  pred.type().id() == ID_c_bool ||
                  pred.type().id() == ID_unsignedbv ||
                  pred.type().id() == ID_signedbv)
                {
                  pred = notequal_exprt(pred, from_integer(0, pred.type()));
                }

                // (inflight != null && classid in T*) ==> pred
                const exprt is_nonnull = notequal_exprt(
                  infl, null_pointer_exprt(to_pointer_type(infl.type())));
                const exprt guard = and_exprt(is_nonnull, class_match);
                const exprt assertion = or_exprt(not_exprt(guard), pred);

                source_locationt sloc = end_fn_it->source_location();
                sloc.set_comment(
                  "JML signals (" + sc.type_name + " " + sc.var_name + ")");
                sloc.set_step_kind(ID_postcondition);
                sloc.set_property_class("signals");
                auto inserted = body.insert_before(
                  end_fn_it, goto_programt::make_assertion(assertion, sloc));
                record_first(inserted);
              }
            }
          }
        }

        // signals_only T1, T2, ...;
        // At the function exit, assert that the inflight
        // exception is null OR is one of the listed types,
        // then ASSUME the inflight exception is null so the
        // harness's downstream uncaught-exception check
        // passes regardless. The ASSUME is necessary because
        // a method with `signals_only T` is allowed to throw
        // T; without the ASSUME, the harness's default
        // "no uncaught exception" assertion would fire on
        // every successful T-throwing path.
        if(has_signals_only)
        {
          const auto *infl_sym =
            ns.get_symbol_table().lookup(INFLIGHT_EXCEPTION_VARIABLE_NAME);
          if(infl_sym != nullptr)
          {
            const exprt infl = infl_sym->symbol_expr();
            const exprt is_null = equal_exprt(
              infl, null_pointer_exprt(to_pointer_type(infl.type())));
            // Build an OR of class-identifier comparisons.
            // remove_instanceof has already run by the time we
            // get here, so use the post-removal form directly:
            //   ((java.lang.Object*)inflight)->@class_identifier == "java::T"
            // The exception is laid out as a Java object, so
            // its first component is `@java.lang.Object` whose
            // first component is `@class_identifier`. Cast to
            // a `struct java.lang.Object *`, dereference, and
            // read the field.
            const auto *jlo_sym =
              ns.get_symbol_table().lookup("java::java.lang.Object");
            exprt type_check = is_null;
            if(jlo_sym != nullptr && jlo_sym->type.id() == ID_struct)
            {
              const auto &components =
                to_struct_type(jlo_sym->type).components();
              irep_idt classid_field;
              typet classid_type;
              for(const auto &c : components)
              {
                if(
                  c.get_name() == "@class_identifier" ||
                  c.get_base_name() == "@class_identifier")
                {
                  classid_field = c.get_name();
                  classid_type = c.type();
                  break;
                }
              }
              if(!classid_field.empty())
              {
                pointer_typet jlo_ptr =
                  pointer_type(struct_tag_typet("java::java.lang.Object"));
                const exprt jlo_obj =
                  dereference_exprt(typecast_exprt(infl, jlo_ptr));
                const exprt classid =
                  member_exprt(jlo_obj, classid_field, classid_type);
                // Resolve a user-supplied type name to a list of
                // fully-qualified Java class identifiers. Strategy:
                //   1. If the user wrote a `java::` prefix, take it
                //      verbatim (one match).
                //   2. If `java::<name>` is a class symbol, use it.
                //   3. Otherwise, treat the name as a simple class
                //      name and match every `java::<pkg>.<name>`
                //      symbol — covers java.lang import-style names
                //      and lets `signals_only Foo` work whether the
                //      user wrote `Foo`, `pkg.Foo`, or
                //      `java::pkg.Foo`.
                //   4. Always include subclasses transitively, so
                //      `signals_only Throwable` covers every
                //      Java exception type.
                // resolve_jml_type_name (free helper above) does
                // the resolution and transitive subclass closure
                // for us.
                for(const auto &ty : signals_only_types)
                {
                  for(const auto &fq : resolve_jml_type_name(ns, ty))
                  {
                    const exprt eq =
                      equal_exprt(classid, constant_exprt(fq, classid_type));
                    type_check = or_exprt(type_check, eq);
                  }
                }
              }
            }
            source_locationt loc = end_fn_it->source_location();
            loc.set_comment("JML signals_only");
            loc.set_step_kind(ID_postcondition);
            loc.set_property_class("signals_only");
            auto a1 = body.insert_before(
              end_fn_it, goto_programt::make_assertion(type_check, loc));
            record_first(a1);
            // Suppress propagation to the harness's default
            // uncaught-exception check.
            body.insert_before(
              end_fn_it,
              goto_programt::make_assignment(
                infl, null_pointer_exprt(to_pointer_type(infl.type())), loc));
          }
        }
        // Retarget any GOTOs whose target was END_FUNCTION
        // (e.g. exception-throwing paths) to the first
        // inserted check, so they no longer skip the JML
        // exit-time checks.
        if(new_first.has_value() && *new_first != end_fn_it)
        {
          for(auto &ins : body.instructions)
          {
            for(auto &target : ins.targets)
            {
              if(target == end_fn_it)
                target = *new_first;
            }
          }
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

    // assignable / modifiable / modifies frame condition,
    // checked inline (non-modular). For each ASSIGN whose LHS
    // is rooted in a parameter (or `this`), assert that the
    // location is one of the allowed locations:
    //
    //   * \nothing  → no parameter-rooted writes allowed.
    //   * \everything → no constraint emitted.
    //   * obj.field, this.field → the listed location matches
    //     a structurally-equal write.
    //
    // Local-only writes (LHS rooted in a non-parameter local
    // symbol) and the harness-injected `<method>#return_value`
    // / `@inflight_exception` writes are unconditionally
    // allowed — they don't reach observable state.
    //
    // What this MVP does NOT yet handle:
    //   * fresh allocations producing pointers used as writable
    //     bases (constructors, factory-style helpers);
    //   * array indices and chained-member writes
    //     (this.foo.bar.baz = ...);
    //   * `\old(expr)` inside the assignable list;
    //   * commaseparated lists in a single clause (the parser
    //     currently keeps only the first target — users can
    //     work around this by writing one //@ assignable
    //     clause per location).
    if(!assigns_exprs.empty())
    {
      // Check whether \everything appears anywhere in the list.
      // If so, the constraint is vacuously satisfied and we
      // skip emission.
      bool any_everything = false;
      // Filter out \nothing markers to leave just the
      // explicit-location targets.
      exprt::operandst assign_targets;
      for(const auto &e : assigns_exprs)
      {
        if(e.id() == "jml_everything")
        {
          any_everything = true;
          break;
        }
        if(e.id() == "jml_nothing")
          continue;
        assign_targets.push_back(e);
      }

      if(!any_everything)
      {
        // Compute the set of method-parameter symbol names.
        // ASSIGNs whose LHS is rooted at one of these are
        // observable state writes that must satisfy the
        // assignable list. Other ASSIGNs (locals, harness
        // injectables) bypass the check.
        std::set<irep_idt> param_symbols;
        const auto sym_lookup = goto_model.symbol_table.lookup(method_id);
        if(sym_lookup != nullptr && sym_lookup->type.id() == ID_code)
        {
          const code_typet &ct = to_code_type(sym_lookup->type);
          for(const auto &p : ct.parameters())
          {
            if(!p.get_identifier().empty())
              param_symbols.insert(p.get_identifier());
          }
        }

        // Walk the LHS to find the underlying root symbol.
        // member_exprt(parent, name) and dereference_exprt(p)
        // and index_exprt(arr, i) all descend; ID_symbol is
        // the leaf.
        std::function<irep_idt(const exprt &)> root_symbol_of =
          [&](const exprt &e) -> irep_idt
        {
          if(e.id() == ID_symbol)
            return to_symbol_expr(e).get_identifier();
          if(e.has_operands())
            return root_symbol_of(e.operands().front());
          return irep_idt{};
        };

        // Collect (insertion-point, assertion-condition) pairs
        // first, then insert in a second pass to avoid iterator
        // invalidation while walking.
        std::vector<std::pair<goto_programt::targett, exprt>> to_insert;
        for(auto it = body.instructions.begin(); it != body.instructions.end();
            ++it)
        {
          if(!it->is_assign())
            continue;
          const exprt &lhs = it->assign_lhs();
          const irep_idt root = root_symbol_of(lhs);
          if(root.empty())
            continue;
          const std::string root_str = id2string(root);

          // Skip harness-injected writes.
          if(
            has_suffix(root_str, "#return_value") ||
            root_str == "java::@inflight_exception" ||
            has_prefix(root_str, "__CPROVER") || has_prefix(root_str, "tag-") ||
            has_prefix(root_str, "anonlocal::") ||
            root_str.find("::tmp_") != std::string::npos)
          {
            continue;
          }

          // Skip pure-local writes: root is a method-local
          // (declared inside method_id) but is NOT one of the
          // method's parameters.
          const std::string method_prefix = id2string(method_id) + "::";
          if(has_prefix(root_str, method_prefix) && !param_symbols.count(root))
          {
            continue;
          }

          // The LHS reaches observable state. Build the
          // disjunction of address-equalities against each
          // listed target. If none match, the disjunction is
          // false_exprt() and the assertion will fire.
          exprt matches = false_exprt();
          for(const auto &tgt : assign_targets)
          {
            if(lhs.type() != tgt.type())
              continue;
            address_of_exprt lhs_addr{lhs};
            address_of_exprt tgt_addr{tgt};
            if(lhs_addr.type() != tgt_addr.type())
              continue;
            const exprt eq = equal_exprt(lhs_addr, tgt_addr);
            matches = matches.is_false() ? eq : or_exprt(matches, eq);
          }
          to_insert.emplace_back(it, matches);
        }

        for(const auto &[where, cond] : to_insert)
        {
          source_locationt sloc = where->source_location();
          sloc.set_comment("JML assignable");
          sloc.set_property_class("assigns");
          body.insert_before(where, goto_programt::make_assertion(cond, sloc));
        }
        if(!to_insert.empty())
          body.update();
      }
    }

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
