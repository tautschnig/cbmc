/// Python to GOTO converter — assignment statement
/// handlers: AnnAssign (PLR §7.2.1), Assign (§7.2),
/// AugAssign (§7.2.2).
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
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

#include <cmath>

// PLR §7.2.1: Annotated assignment statements
// "Annotation assignment is the combination, in a single statement, of a
// variable or attribute annotation and an optional assignment statement."
codet python_convertert::convert_ann_assign(const jsont &stmt)
{
  // PLR §7.2.1: Annotated assignment
  const jsont &target = json_member(stmt, "target");
  const jsont &annotation = json_member(stmt, "annotation");
  const jsont &value = json_member(stmt, "value");
  source_locationt loc = get_location(stmt);

  // Handle self.attr: Type = value (attribute target)
  if(is_node_type(target, "Attribute"))
  {
    if(value.is_null())
      return code_skipt{};
    exprt obj = convert_expression(json_member(target, "value"));
    std::string attr = json_string(json_member(target, "attr"));
    exprt rhs = convert_expression(value);
    if(obj.is_nil() || rhs.is_nil())
      return code_skipt{};

    // enum-typed-field tracking (PLR §8.13): `self.<attr>: SomeEnum` records
    // the field as enum-typed for the current class, so a later
    // `obj.<attr>.value` resolves to the stored member value (the field
    // analogue of enum_member_vars).
    {
      const jsont &ann = json_member(stmt, "annotation");
      if(
        !current_class.empty() && is_node_type(ann, "Name") &&
        enum_members.count(json_string(json_member(ann, "id"))) > 0)
        enum_typed_fields[current_class].insert(attr);
    }

    typet obj_type = obj.type();
    if(obj_type.id() == ID_pointer)
    {
      const auto &base = to_pointer_type(obj_type).base_type();
      if(base.id() == ID_struct)
      {
        const auto &st = to_struct_type(base);
        if(st.has_component(attr))
        {
          dereference_exprt deref{obj};
          member_exprt lhs{deref, attr, st.get_component(attr).type()};
          // PLR §3.2: empty-dict rebuild for AnnAssign-on-
          // attribute. Mirrors the Name-target empty-dict path
          // below: when the LHS is a typed dict[K, V] and the
          // RHS is the empty dict literal {} (which convert_dict
          // builds as dict[str, int] by default), the type
          // mismatch otherwise falls through to safe_typecast
          // which only relabels the struct, leaving the
          // string-shaped keys in place.
          if(
            is_python_dict_type(lhs.type()) &&
            is_python_dict_type(rhs.type()) && rhs.type() != lhs.type() &&
            rhs.id() == ID_struct && !rhs.operands().empty() &&
            rhs.operands()[0].is_constant())
          {
            mp_integer rhs_len;
            if(
              !to_integer(to_constant_expr(rhs.operands()[0]), rhs_len) &&
              rhs_len == 0)
              rhs = safe_zero(lhs.type());
          }
          rhs = coerce_assign_rhs(rhs, lhs.type());
          code_blockt result;
          code_frontend_assignt assign{lhs, rhs};
          assign.add_source_location() = loc;
          result.add(std::move(assign));
          if(auto shadow = maybe_shadow_assign(deref, attr))
          {
            shadow->add_source_location() = loc;
            result.add(std::move(*shadow));
          }
          if(result.statements().size() == 1)
            return std::move(result.statements().front());
          return std::move(result);
        }
      }
    }
    return code_skipt{};
  }

  std::string var_name = json_string(json_member(target, "id"));
  typet var_type = convert_type_annotation(annotation);

  std::string qualified_name = qualify_name(var_name);
  irep_idt symbol_id{qualified_name};

  // Name-shadowing of imported functions. When a user's local
  // variable name happens to collide with a function the import
  // pipeline added at the same qualified scope (e.g. `match: ... =
  // re.match(...)` reusing `python::match` because re's module
  // contents are flattened into the top-level namespace), reusing
  // the existing code-typed symbol would have CBMC's symex abort
  // with "assignment to 'symbol' not handled". Instead, rename the
  // new variable to a fresh `__shadow_<name>__vN` symbol and
  // register the redirection in variable_versions so later reads
  // of `var_name` resolve to the local variable rather than the
  // imported function.
  {
    const symbolt *existing = symbol_table.lookup(symbol_id);
    if(
      existing != nullptr && existing->type.id() == ID_code &&
      var_type.id() != ID_code)
    {
      unsigned &ver = version_counters[qualified_name];
      ver++;
      std::string versioned_name =
        qualified_name + "__shadow__v" + std::to_string(ver);
      irep_idt versioned_id{versioned_name};
      if(symbol_table.lookup(versioned_id) == nullptr)
      {
        symbolt new_symbol{versioned_id, var_type, "python"};
        new_symbol.base_name = var_name + "__shadow__v" + std::to_string(ver);
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        new_symbol.is_static_lifetime = current_function.empty();
        symbol_table.add(new_symbol);
      }
      variable_versions[qualified_name] = versioned_id;
      symbol_id = versioned_id;
      qualified_name = versioned_name;
    }
  }

  // Create symbol if it doesn't exist
  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt new_symbol{symbol_id, var_type, "python"};
    new_symbol.base_name = var_name;
    new_symbol.location = loc;
    new_symbol.is_lvalue = true;
    new_symbol.is_state_var = true;
    new_symbol.is_static_lifetime = current_function.empty();
    symbol_table.add(new_symbol);
  }
  // Track the original annotation for later
  // --python-check-annotations checks on plain Assigns to the
  // same variable. Without this, the assign's widening overwrites
  // the symbol's type and the mismatch goes undetected.
  variable_annotations[symbol_id] = var_type;

  if(value.is_null())
    return code_skipt{};

  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  // Extraction-then-mutate soundness (mirror convert_assign): an ANNOTATED
  // single-Name binding to a mutable element extracted from a container
  // (`r: list = c[i]`) must record the source alias too, so a later in-place
  // mutation of `r` havocs it. Without this the AnnAssign path bypassed the
  // guard and cbmc proved the stale container (annotation-extract-mutate-bypass).
  note_mutable_extraction(symbol_id, rhs, value);

  // enum-member-variable tracking (mirror convert_assign): an annotated
  // `s: E = E.M` (or any `s = E.M`) records s so `s.value` resolves precisely.
  {
    const bool rhs_is_enum_member =
      is_node_type(value, "Attribute") &&
      is_node_type(json_member(value, "value"), "Name") &&
      enum_members.count(
        json_string(json_member(json_member(value, "value"), "id"))) > 0;
    if(rhs_is_enum_member)
      enum_member_vars.insert(symbol_id);
    else
      enum_member_vars.erase(symbol_id);
  }

  // PLR §3.2: a parameterised annotation (x: list[T] = []) is authoritative
  // for the empty list's element type. convert_list defaults an empty literal
  // to an int element type, so re-type the rhs from the annotation. Without
  // this the symbol is list[T] but the value is list[int]; a later
  // x.append(<T value>) then emits a T->int element coercion (e.g.
  // smt_string->signedbv) that aborts in the SMT back-end. This is the
  // architectural fix for the empty-container element-type gap; the append-
  // inference block below only covers the bare `list` annotation.
  if(
    is_node_type(value, "List") && json_member(value, "elts").is_array() &&
    as_array(json_member(value, "elts")).empty() &&
    is_python_list_type(rhs.type()) && is_python_list_type(var_type))
  {
    const typet ann_elem =
      to_array_type(to_struct_type(var_type).components()[1].type())
        .element_type();
    const typet rhs_elem =
      to_array_type(to_struct_type(rhs.type()).components()[1].type())
        .element_type();
    if(ann_elem.id() != ID_empty && ann_elem != rhs_elem)
    {
      struct_typet new_list_type = python_list_type(ann_elem);
      const auto &new_data_type =
        to_array_type(new_list_type.components()[1].type());
      exprt::operandst zeros;
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        zeros.push_back(safe_zero(ann_elem));
      array_exprt new_data{std::move(zeros), new_data_type};
      rhs = struct_exprt{
        {from_integer(0, signedbv_typet{64}), new_data}, new_list_type};
    }
  }

  // PLR §3.2: empty-list element-type inference. Same as in
  // convert_assign, but applies when the user wrote
  // 'name: list = []' (bare 'list' annotation, no parameter).
  // Parameterised forms (list[T]) already lock in T via the
  // annotation conversion above.
  if(
    is_node_type(value, "List") && json_member(value, "elts").is_array() &&
    as_array(json_member(value, "elts")).empty() &&
    is_python_list_type(rhs.type()))
  {
    auto inf_it = empty_list_inferred_types.find(symbol_id);
    if(inf_it != empty_list_inferred_types.end())
    {
      typet new_elem_t = inf_it->second;
      // Only override when annotation was bare list (or list
      // with default int element). Skip when annotation
      // already specified the element type.
      const auto &cur_st = to_struct_type(rhs.type());
      const auto &cur_data_t = to_array_type(cur_st.components()[1].type());
      if(
        cur_data_t.element_type() == python_int_type() ||
        cur_data_t.element_type().id() == ID_empty)
      {
        struct_typet new_list_type = python_list_type(new_elem_t);
        const auto &new_data_type =
          to_array_type(new_list_type.components()[1].type());
        exprt::operandst zeros;
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
          zeros.push_back(safe_zero(new_elem_t));
        array_exprt new_data{std::move(zeros), new_data_type};
        rhs = struct_exprt{
          {from_integer(0, signedbv_typet{64}), new_data}, new_list_type};
        // Refresh the symbol's type to match the rebuilt list.
        symbol_table.get_writeable_ref(symbol_id).type = new_list_type;
      }
    }
  }
  // PLR §3.2: empty-dict key/value-type inference. When the user
  // wrote 'name: dict[K, V] = {}' the annotation specifies the
  // dict's key and value types but convert_dict for the empty
  // literal {} defaults to dict[str, int] (no key/value pairs to
  // infer from). The resulting type mismatch leaves the symbol
  // typed as dict[K, V] but the assigned value structured as
  // dict[str, int] — subsequent d[k] = v writes then mismatch
  // on the key type at the keys[i] = k store, leaving the
  // stored key unconstrained (visible as a `nondet` in the
  // goto). Rebuild the empty dict literal as a zero-init
  // struct of var_type so the keys/values arrays carry the
  // annotation-specified element types.
  if(
    is_node_type(value, "Dict") && json_member(value, "keys").is_array() &&
    as_array(json_member(value, "keys")).empty() &&
    is_python_dict_type(rhs.type()) && is_python_dict_type(var_type) &&
    rhs.type() != var_type)
  {
    rhs = safe_zero(var_type);
  }
  // PLR §3.1: storage promotion for escaped mutables.
  // If this name's qualified form is in `escaped_mutables` (i.e. it
  // appears as a Name element of some List/Dict literal elsewhere)
  // and the RHS is a list with a non-python_value element type,
  // rebuild the RHS as `list[python_value]`. This makes the deref-
  // cast in `python_value_list` (which assumes list[python_value])
  // type-correct when this storage is later referenced via
  // `make_python_value(LIST, address_of(this_symbol))`.
  if(escaped_mutables.count(symbol_id) > 0 && is_python_list_type(rhs.type()))
  {
    const auto &src_st = to_struct_type(rhs.type());
    const auto &src_data_type = to_array_type(src_st.components()[1].type());
    if(!is_python_value_type(src_data_type.element_type()))
    {
      rhs = rebuild_list_as_pv(rhs);
      var_type = python_list_type(python_value_type());
      // Update the symbol's type to match.
      symbolt &existing = symbol_table.get_writeable_ref(symbol_id);
      existing.type = var_type;
    }
  }

  // Function alias: g: Callable = double → record alias
  if(rhs.id() == ID_symbol && rhs.type().id() == ID_code)
  {
    function_aliases[qualified_name] = to_symbol_expr(rhs).get_identifier();
    return code_skipt{};
  }

  // PLR §3.2: AnnAssign with rhs a call returning a closure.
  // For 'inner: int = outer(5)' where outer is a lambda-returning
  // function, the int annotation is a Python type-hint that must
  // not erase the actual closure value. Mirror the Assign-path
  // detection that records the alias and binds the closure
  // captures from the call's arguments. Without this, the value
  // is collapsed to nondet int and 'inner(10)' fails with
  // 'no body for callee'.
  if(rhs.id() == ID_side_effect)
  {
    const auto &se = to_side_effect_expr(rhs);
    if(se.get_statement() == ID_function_call && !se.operands().empty())
    {
      const exprt &func_op = se.operands()[0];
      if(func_op.id() == ID_symbol)
      {
        std::string called =
          id2string(to_symbol_expr(func_op).get_identifier());
        if(called.substr(0, 8) == "python::")
          called = called.substr(8);
        auto lr_it = lambda_returning_functions.find(called);
        if(lr_it != lambda_returning_functions.end())
        {
          code_blockt lam_block;
          // Per-closure-variable capture binding (PLR §4.2.2); see the
          // convert_assign path for the rationale. Snapshot each capture
          // into a fresh per-call temp recorded against the annotated
          // target so distinct factory invocations stay independent.
          std::map<std::string, irep_idt> this_call_caps;
          irep_idt fid{"python::" + called};
          const symbolt *fsym = symbol_table.lookup(fid);
          auto ci = closure_captures.find(id2string(lr_it->second));
          if(
            fsym != nullptr && fsym->type.id() == ID_code &&
            ci != closure_captures.end())
          {
            const auto &fp = to_code_type(fsym->type).parameters();
            std::set<std::string> fparam_ids;
            for(const auto &p : fp)
              fparam_ids.insert(id2string(p.get_identifier()));
            const jsont &call_args = json_member(value, "args");
            std::map<std::string, exprt> param_arg;
            if(call_args.is_array())
            {
              auto ai = as_array(call_args).begin();
              for(std::size_t i = 0;
                  i < fp.size() && ai != as_array(call_args).end();
                  i++, ++ai)
                param_arg[id2string(fp[i].get_identifier())] =
                  convert_expression(*ai);
            }
            bool has_local = false;
            for(const auto &cap : ci->second)
              if(
                fparam_ids.count(std::get<0>(cap)) == 0 &&
                symbol_table.lookup(irep_idt{std::get<0>(cap)}) != nullptr)
                has_local = true;
            if(has_local)
            {
              exprt::operandst fargs;
              if(call_args.is_array())
                for(const auto &a : as_array(call_args))
                  fargs.push_back(convert_expression(a));
              side_effect_expr_function_callt fcall{
                fsym->symbol_expr(),
                std::move(fargs),
                to_code_type(fsym->type).return_type(),
                loc};
              lam_block.add(code_expressiont{std::move(fcall)});
            }
            static unsigned ann_lb = 0;
            for(const auto &cap : ci->second)
            {
              const std::string &outer_id = std::get<0>(cap);
              const std::string &capname = std::get<1>(cap);
              exprt src;
              if(fparam_ids.count(outer_id))
              {
                auto pa = param_arg.find(outer_id);
                if(pa == param_arg.end())
                  continue;
                src = pa->second;
              }
              else
              {
                const symbolt *ls = symbol_table.lookup(irep_idt{outer_id});
                if(ls == nullptr)
                  continue;
                src = ls->symbol_expr();
              }
              // See convert_assign: skip code-typed captures (cannot be
              // snapshotted into a temp), avoiding a symex abort.
              if(src.type().id() == ID_code)
                continue;
              std::string tn = "__ann_lam_bind_" + std::to_string(ann_lb++);
              irep_idt ti{qualify_name(tn)};
              if(symbol_table.lookup(ti) == nullptr)
              {
                symbolt ts{ti, src.type(), "python"};
                ts.base_name = tn;
                ts.is_lvalue = true;
                ts.is_state_var = true;
                ts.is_static_lifetime = true;
                symbol_table.add(ts);
              }
              lam_block.add(code_frontend_assignt{
                symbol_table.lookup_ref(ti).symbol_expr(), src});
              this_call_caps[capname] = ti;
            }
          }
          // Record the alias so subsequent calls 'name(args)'
          // dispatch to the inner lambda symbol.
          function_aliases[qualified_name] = lr_it->second;
          if(!this_call_caps.empty())
            closure_var_captures[qualified_name] = this_call_caps;
          if(lam_block.statements().empty())
            return code_skipt{};
          return std::move(lam_block);
        }
      }
    }
  }

  // PLR §3.1 — list / dict aliasing. When the RHS is another list/dict
  // variable (or a chain of dereferences ending in one), the LHS must
  // bind to the SAME object, not a fresh copy. Without this, code like
  //
  //     a: list = [1, 2, 3]
  //     b: list = a
  //     b[0] = 99
  //     assert a[0] == 1   # FAILS in Python
  //
  // would currently struct-copy `a` into `b` and silently report
  // VERIFICATION SUCCESSFUL — a soundness gap.
  //
  // Fix: promote the LHS symbol to pointer-to-struct, bind it to
  // `address_of(rhs_target)`, and record the alias in
  // `alias_targets`. `convert_name` auto-dereferences the LHS at
  // every use, so subscript reads/writes and member access through
  // the LHS go through the same memory the RHS occupies.
  //
  // CRITICAL: we gate this on the *AST shape* of the RHS, not on the
  // converted exprt. A direct alias `b = a` has value AST shape
  // Name("a"); a method call `b = a.copy()` has shape
  // Call(func=Attribute(Name("a"), "copy"), ...). Both convert to the
  // same symbol_exprt at the converted-exprt level (because
  // .copy()'s handler currently returns obj rather than synthesising a
  // new struct), but only the first should promote — `.copy()` MUST
  // produce a fresh container, not an alias.
  bool rhs_is_direct_name = is_node_type(value, "Name");
  // reference-semantics-for-instances: a local instance is a python_class_*
  // struct; an annotated alias `r: C = o` (Name RHS) must pointer-promote and
  // share identity exactly like list/dict, so a mutation through `r` (or
  // through a callee that receives `r`) is visible via `o`. Without this the
  // annotated alias fell through to a struct copy -- a false proof
  // (a2_narrowing_alias). A fresh `r: C = C()` has a Call RHS, so
  // rhs_is_direct_name is false and it stays a distinct object.
  const bool rhs_is_instance_struct =
    (rhs.type().id() == ID_struct &&
     id2string(to_struct_type(rhs.type()).get_tag()).find("python_class_") !=
       std::string::npos) ||
    (rhs.type().id() == ID_struct_tag &&
     id2string(to_struct_tag_type(rhs.type()).get_identifier())
         .find("python_class_") != std::string::npos);
  if(
    rhs_is_direct_name && rhs.id() == ID_symbol &&
    (is_python_list_type(rhs.type()) || is_python_dict_type(rhs.type()) ||
     rhs_is_instance_struct))
  {
    irep_idt rhs_id = to_symbol_expr(rhs).get_identifier();
    auto chain = alias_targets.find(rhs_id);
    irep_idt target_id =
      (chain != alias_targets.end()) ? chain->second : rhs_id;
    const symbolt *target_sym = symbol_table.lookup(target_id);
    if(target_sym != nullptr)
    {
      pointer_typet ptr_type{target_sym->type, 64};
      symbol_table.get_writeable_ref(symbol_id).type = ptr_type;
      alias_targets[qualified_name] = target_id;
      // PLR §3.1 / §6.2.9: a generator aliased by `it2 = it` refers to the SAME
      // object and shares its consumption cursor. Propagate the cursor mapping
      // so next()/for/list on `it2` advance the same cursor (the alias is a
      // pointer to `it`'s storage, and reads auto-dereference, so the cursor
      // path sees the shared list).
      {
        auto gc = generator_cursors.find(target_id);
        if(gc != generator_cursors.end())
          generator_cursors[symbol_id] = gc->second;
      }
      // Aliasing makes the target's contents reachable through a
      // second name; subsequent constant-fold lookups via
      // list_literals / dict_literals would mis-fold reads against
      // the snapshot taken at the original assignment, missing any
      // mutation through the alias. Drop the cached literal so the
      // converter falls back to the runtime read path.
      list_literals.erase(target_id);
      dict_literals.erase(target_id);
      tuple_literals.erase(target_id);
      string_constants.erase(target_id);
      code_frontend_assignt assign{
        symbol_table.lookup_ref(symbol_id).symbol_expr(),
        address_of_exprt{target_sym->symbol_expr()}};
      assign.add_source_location() = loc;
      return std::move(assign);
    }
  }
  if(
    rhs_is_direct_name && rhs.id() == ID_dereference &&
    rhs.operands().size() == 1 && rhs.operands()[0].id() == ID_symbol &&
    rhs.operands()[0].type().id() == ID_pointer &&
    (is_python_list_type(
       to_pointer_type(rhs.operands()[0].type()).base_type()) ||
     is_python_dict_type(
       to_pointer_type(rhs.operands()[0].type()).base_type())))
  {
    // Pointer-copy from another already-promoted symbol or parameter.
    const exprt &inner_sym = rhs.operands()[0];
    symbol_table.get_writeable_ref(symbol_id).type = inner_sym.type();
    irep_idt inner_id = to_symbol_expr(inner_sym).get_identifier();
    auto chain = alias_targets.find(inner_id);
    irep_idt target_id =
      (chain != alias_targets.end()) ? chain->second : inner_id;
    alias_targets[qualified_name] = target_id;
    list_literals.erase(target_id);
    dict_literals.erase(target_id);
    tuple_literals.erase(target_id);
    string_constants.erase(target_id);
    code_frontend_assignt assign{
      symbol_table.lookup_ref(symbol_id).symbol_expr(), inner_sym};
    assign.add_source_location() = loc;
    return std::move(assign);
  }

  // PLR §3.1: if the RHS is pointer-to-list/dict (from a function
  // returning its parameter pointer), promote the LHS to pointer type
  // and bind directly. This covers `b: list = identity(a)` where
  // identity returns its pointer-typed parameter.
  if(
    rhs.type().id() == ID_pointer &&
    (is_python_list_type(to_pointer_type(rhs.type()).base_type()) ||
     is_python_dict_type(to_pointer_type(rhs.type()).base_type()) ||
     is_instance_pointer(rhs.type())))
  {
    symbol_table.get_writeable_ref(symbol_id).type = rhs.type();
    code_frontend_assignt assign{
      symbol_table.lookup_ref(symbol_id).symbol_expr(), rhs};
    assign.add_source_location() = loc;
    return std::move(assign);
  }

  // PLR §3.1: ternary alias — `c = a if cond else b` where both
  // branches are list/dict Names. The ternary should produce a
  // pointer-level if-then-else so mutations through `c` propagate
  // to the chosen branch's storage.
  if(is_node_type(value, "IfExp"))
  {
    const jsont &body_node = json_member(value, "body");
    const jsont &orelse_node = json_member(value, "orelse");
    if(is_node_type(body_node, "Name") && is_node_type(orelse_node, "Name"))
    {
      std::string body_name = json_string(json_member(body_node, "id"));
      std::string orelse_name = json_string(json_member(orelse_node, "id"));
      std::string body_qname = qualify_name(body_name);
      std::string orelse_qname = qualify_name(orelse_name);
      // Resolve through alias_targets / variable_versions
      auto resolve = [&](const std::string &qn) -> const symbolt *
      {
        auto vit = variable_versions.find(qn);
        irep_idt lid =
          vit != variable_versions.end() ? vit->second : irep_idt{qn};
        auto ait = alias_targets.find(lid);
        if(ait != alias_targets.end())
          lid = ait->second;
        return symbol_table.lookup(lid);
      };
      const symbolt *body_sym = resolve(body_qname);
      const symbolt *orelse_sym = resolve(orelse_qname);
      if(
        body_sym != nullptr && orelse_sym != nullptr &&
        (is_python_list_type(body_sym->type) ||
         is_python_dict_type(body_sym->type)) &&
        (is_python_list_type(orelse_sym->type) ||
         is_python_dict_type(orelse_sym->type)))
      {
        // Build pointer-level ternary: cond ? &body : &orelse
        exprt cond = convert_expression(json_member(value, "test"));
        if(cond.type().id() != ID_bool)
          cond = safe_typecast(cond, bool_typet{});
        pointer_typet ptr_type{body_sym->type, 64};
        exprt ptr_body = address_of_exprt{body_sym->symbol_expr()};
        exprt ptr_orelse = address_of_exprt{orelse_sym->symbol_expr()};
        if(ptr_orelse.type() != ptr_type)
          ptr_orelse = typecast_exprt{ptr_orelse, ptr_type};
        exprt ternary = if_exprt{cond, ptr_body, ptr_orelse};
        symbol_table.get_writeable_ref(symbol_id).type = ptr_type;
        code_frontend_assignt assign{
          symbol_table.lookup_ref(symbol_id).symbol_expr(), ternary};
        assign.add_source_location() = loc;
        return std::move(assign);
      }
    }
  }

  // PLR §3.1, §3.2: annotations are documentation, not enforcement.
  // When the RHS has a concrete type that differs from the declared
  // annotation, the variable binds to the RHS type — Python's
  // annotations don't coerce. Without this, `x: int = greet()` where
  // greet returns str would safe_typecast the string to nondet int,
  // losing the actual value.
  //
  // The pre-existing branch handles ID_struct rhs types (class
  // instances, lists, dicts when those use struct_typet); this
  // version also accepts ID_struct_tag (Python's refined-string
  // representation, class tags, named tuple types) so that `x: int
  // = "Hi"` and similar patterns also widen the symbol to the
  // string type rather than coercing.
  //
  // Gated on `!python_check_annotations`: when the user has
  // explicitly opted in to annotation-mismatch detection (via
  // --python-check-annotations), we keep the previous behaviour so
  // the mismatch property fires.
  const symbolt &sym = symbol_table.lookup_ref(symbol_id);
  // Reassignment annotation check (for `x: int = 10; x =
  // "wrong"`-shaped bugs): if x has a recorded annotation
  // and rhs's type is incompatible with that annotation,
  // emit a property BEFORE the type-widening logic below
  // overwrites the symbol's tracked type.
  if(python_check_annotations)
  {
    auto va = variable_annotations.find(symbol_id);
    if(
      va != variable_annotations.end() &&
      annotation_types_incompatible(va->second, rhs.type()))
    {
      add_check(
        false_exprt{},
        "annotation-mismatch",
        "assigned value's type does not match declared annotation of '" +
          var_name + "'",
        loc);
    }
  }
  bool rhs_struct_like =
    rhs.type().id() == ID_struct || rhs.type().id() == ID_struct_tag;
  // P3: bare 'list' annotation (= list[Any], with python_value
  // elements) — when the rhs is a typed list[T] with T !=
  // python_value, wrap each element via wrap_value() so the
  // symbol carries python_value-tagged elements rather than
  // the raw T values. Without this, the type-widening below
  // would override the symbol's declared list[Any] type back
  // to list[T], and `for x in name` / `name[i]` reads would
  // produce typed-T values that miss isinstance() dispatch.
  if(
    sym.type != rhs.type() && is_python_list_type(sym.type) &&
    is_python_list_type(rhs.type()) &&
    is_python_value_type(
      to_array_type(to_struct_type(sym.type).components()[1].type())
        .element_type()) &&
    !is_python_value_type(
      to_array_type(to_struct_type(rhs.type()).components()[1].type())
        .element_type()))
  {
    const auto &sym_st = to_struct_type(sym.type);
    const auto &dst_data_t = to_array_type(sym_st.components()[1].type());
    const auto &src_st = to_struct_type(rhs.type());
    const auto &src_data_t = to_array_type(src_st.components()[1].type());
    // For struct literals from `[a, b, c]`, peel directly.
    if(
      rhs.id() == ID_struct && rhs.operands().size() >= 2 &&
      rhs.operands()[1].id() == ID_array)
    {
      exprt::operandst promoted;
      const auto &src_arr = rhs.operands()[1];
      std::size_t max_len = static_cast<std::size_t>(PYTHON_MAX_LIST_LENGTH);
      for(std::size_t k = 0; k < max_len; k++)
      {
        if(k < src_arr.operands().size())
          promoted.push_back(wrap_value(src_arr.operands()[k]));
        else
          promoted.push_back(safe_zero(dst_data_t.element_type()));
      }
      rhs = struct_exprt{
        {rhs.operands()[0], array_exprt{std::move(promoted), dst_data_t}},
        sym.type};
    }
    else
    {
      // Materialise rhs into a temp and read elements via index.
      static unsigned src_mat_ctr2 = 0;
      std::string tn = "__list_assign_src_" + std::to_string(src_mat_ctr2++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, rhs.type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        ts.is_static_lifetime = current_function.empty();
        symbol_table.add(ts);
      }
      symbol_exprt s_sym = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{s_sym, rhs});
      member_exprt src_len{s_sym, "length", signedbv_typet{64}};
      member_exprt src_data{s_sym, "data", src_data_t};
      exprt::operandst promoted;
      std::size_t max_len = static_cast<std::size_t>(PYTHON_MAX_LIST_LENGTH);
      for(std::size_t k = 0; k < max_len; k++)
      {
        exprt idx = from_integer(k, signedbv_typet{64});
        promoted.push_back(wrap_value(index_exprt{src_data, idx}));
      }
      rhs = struct_exprt{
        {src_len, array_exprt{std::move(promoted), dst_data_t}}, sym.type};
    }
  }
  if(
    !python_check_annotations && sym.type != rhs.type() && rhs_struct_like &&
    !is_python_value_type(rhs.type()) &&
    (sym.type == python_int_type() || sym.type.id() != ID_struct ||
     (is_python_list_type(sym.type) && is_python_list_type(rhs.type())) ||
     (is_python_dict_type(sym.type) && is_python_dict_type(rhs.type()))))
  {
    symbol_table.get_writeable_ref(symbol_id).type = rhs.type();
  }
  // Pre-existing branch retained for the --python-check-annotations
  // path: when annotation enforcement is on, widen only the
  // ID_struct cases as before.
  else if(
    sym.type != rhs.type() && rhs.type().id() == ID_struct &&
    (sym.type == python_int_type() || sym.type.id() != ID_struct ||
     (is_python_list_type(sym.type) && is_python_list_type(rhs.type())) ||
     (is_python_dict_type(sym.type) && is_python_dict_type(rhs.type()))))
  {
    symbol_table.get_writeable_ref(symbol_id).type = rhs.type();
  }
  // PLR §3.2: a type annotation is documentation, not a runtime
  // coercion — `x: int = <value>` binds x to the value as-is, it
  // does NOT convert the value to int. When the RHS is a
  // tagged-union (python_value) whose runtime tag is not known to
  // be the scalar annotation, keep the symbol as python_value so
  // the actual tag is preserved. Otherwise coerce_assign_rhs ->
  // unwrap_value would read e.g. __int_val unconditionally,
  // discarding a str/float tag and making a later
  // `isinstance(x, int)` trivially (and unsoundly) true. Gated on
  // !python_check_annotations so the explicit annotation-mismatch
  // property still fires when that mode is enabled.
  else if(
    !python_check_annotations && is_python_value_type(rhs.type()) &&
    !is_python_value_type(sym.type) &&
    (sym.type.id() == ID_signedbv || sym.type.id() == ID_floatbv ||
     sym.type.id() == ID_bool || is_python_string_type(sym.type)))
  {
    symbol_table.get_writeable_ref(symbol_id).type = python_value_type();
  }

  const symbolt &sym2 = symbol_table.lookup_ref(symbol_id);

  // Type cast if needed
  if(rhs.type() != sym2.type)
  {
    // PLR soundness: if the assigned value's type is obviously
    // incompatible with the declared annotation (e.g.
    // 'x: int = "hello"'), emit an annotation-mismatch property.
    // Gated by --python-check-annotations.
    if(
      python_check_annotations &&
      annotation_types_incompatible(sym2.type, rhs.type()))
    {
      add_check(
        false_exprt{},
        "annotation-mismatch",
        "assigned value's type does not match declared type of '" + var_name +
          "'",
        loc);
    }
    // PLR §3.2: a variable annotation is a hint — it does not coerce the
    // runtime value. When the declared annotation and the RHS are both
    // simple value types (numeric or string) but in incompatible
    // categories (e.g. `s: str = get_num()` where get_num returns int),
    // keep the RHS value and retype the binding to its actual type,
    // rather than coercing through an incompatible slot (which yields a
    // value-losing nondet). Restricted to scalar/string on both sides so
    // container/class/unsupported-op results still coerce as before.
    auto simple_value_type = [this](const typet &t)
    {
      return t.id() == ID_signedbv || t.id() == ID_unsignedbv ||
             t.id() == ID_integer || t.id() == ID_floatbv ||
             t.id() == ID_bool || is_python_string_type(t);
    };
    if(
      annotation_types_incompatible(sym2.type, rhs.type()) &&
      simple_value_type(sym2.type) && simple_value_type(rhs.type()))
      symbol_table.get_writeable_ref(symbol_id).type = rhs.type();
    else
      rhs = coerce_assign_rhs(rhs, sym2.type);
  }

  code_frontend_assignt assign{sym2.symbol_expr(), rhs};
  assign.add_source_location() = loc;
  // Track constant string values
  if(is_python_string_type(rhs.type()))
  {
    auto sv = extract_string_value(rhs);
    if(!sv.has_value() && rhs.id() == ID_symbol)
    {
      auto it = string_constants.find(to_symbol_expr(rhs).get_identifier());
      if(it != string_constants.end())
        sv = it->second;
    }
    if(sv.has_value())
      string_constants[symbol_id] = sv.value();
    else
      string_constants.erase(symbol_id);
  }
  if(is_python_dict_type(rhs.type()))
  {
    // PLR §8.5: pick up the defaultdict-factory hint stashed
    // by convert_call. If non-empty, the RHS came from
    // 'defaultdict(F)' or 'Counter()' and the LHS dict should
    // return F()'s zero on missing-key reads.
    bool is_defaultdict_assign = !pending_defaultdict_factory.empty();
    if(is_defaultdict_assign)
    {
      defaultdict_factories[symbol_id] = pending_defaultdict_factory;
      pending_defaultdict_factory.clear();
    }
    else
    {
      // Plain-dict assignment: clear any prior defaultdict
      // tracking so reusing a name as a regular dict reverts
      // to KeyError-on-miss semantics.
      defaultdict_factories.erase(symbol_id);
      pending_defaultdict_factory.clear();
    }
    if(rhs.id() == ID_struct && !is_defaultdict_assign)
    {
      // PLR §3.1: do NOT cache escaped mutables — another reference
      // may mutate them, invalidating the snapshot.
      if(escaped_mutables.count(symbol_id) == 0)
        dict_literals[symbol_id] = rhs;
      // Populate per-key static category from the original
      // AST. The struct exprt's value array has had all
      // values typecast to a single uniform type via
      // safe_typecast, which loses the original category.
      // The AST is the source of truth.
      if(is_node_type(value, "Dict"))
      {
        const jsont &dkeys = json_member(value, "keys");
        const jsont &dvals = json_member(value, "values");
        if(dkeys.is_array() && dvals.is_array())
        {
          std::map<std::string, std::string> cats;
          std::map<std::string, std::string> str_consts;
          auto kit = as_array(dkeys).begin();
          auto vit = as_array(dvals).begin();
          for(; kit != as_array(dkeys).end() && vit != as_array(dvals).end();
              ++kit, ++vit)
          {
            if(!is_node_type(*kit, "Constant"))
              continue;
            const jsont &kv = json_member(*kit, "value");
            if(!kv.is_string())
              continue;
            std::string cat = ast_value_category(*vit);
            if(!cat.empty())
              cats[kv.value] = cat;
            // If the value AST is itself a Constant(str), record
            // the constant string for downstream regex-precision
            // checks (Stage 1 of the re-precision plan).
            if(is_node_type(*vit, "Constant"))
            {
              const jsont &cval = json_member(*vit, "value");
              if(cval.is_string())
                str_consts[kv.value] = cval.value;
            }
          }
          if(!cats.empty())
            dict_literal_value_categories[symbol_id] = std::move(cats);
          if(!str_consts.empty())
            dict_literal_value_string_consts[symbol_id] = std::move(str_consts);
        }
      }
    }
    else if(rhs.id() == ID_side_effect)
    {
      // Inter-procedural dict-literal propagation: if the
      // RHS is a call to a function whose return-statement
      // we recorded as a dict literal, synthesize a
      // dict-struct with the known keys so the caller's
      // subscript lookups can prove the key exists.
      const auto &se = to_side_effect_expr(rhs);
      if(
        se.get_statement() == ID_function_call && !se.operands().empty() &&
        se.operands()[0].id() == ID_symbol)
      {
        std::string callee =
          id2string(to_symbol_expr(se.operands()[0]).get_identifier());
        std::string prefix = "python::";
        if(callee.substr(0, prefix.size()) == prefix)
          callee = callee.substr(prefix.size());
        // PLR: if the callee has a single return statement
        // returning a constant dict literal, use the *full*
        // literal (keys and values). This generalises the
        // keys-only sentinel below and lets dict subscripts
        // recover the value at call sites.
        bool used_full_literal = false;
        auto fl_it = function_returned_literal.find(callee);
        auto rc_it = function_return_count.find(callee);
        if(
          fl_it != function_returned_literal.end() &&
          rc_it != function_return_count.end() && rc_it->second == 1 &&
          fl_it->second.id() == ID_struct &&
          is_python_dict_type(fl_it->second.type()) &&
          fl_it->second.type() == rhs.type())
        {
          dict_literals[symbol_id] = fl_it->second;
          used_full_literal = true;
        }
        if(used_full_literal)
        {
          // already populated dict_literals; nothing else to do
        }
        else
        {
          auto ki = function_returned_dict_keys.find(callee);
          auto rc2_it = function_return_count.find(callee);
          if(
            ki != function_returned_dict_keys.end() && !ki->second.empty() &&
            rc2_it != function_return_count.end() && rc2_it->second == 1)
          {
            // Build a sentinel dict-struct with the known keys
            // (values nondet). Match rhs.type() so dict_literals
            // entry stays consistent with the assigned var's
            // type.
            const auto &dt = to_struct_type(rhs.type());
            const auto &keys_type = to_array_type(dt.components()[1].type());
            const auto &vals_type = to_array_type(dt.components()[2].type());
            exprt::operandst key_elems, val_elems;
            for(const auto &k : ki->second)
            {
              key_elems.push_back(python_string_literal(k));
              val_elems.push_back(safe_zero(vals_type.element_type()));
            }
            while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
            {
              key_elems.push_back(safe_zero(keys_type.element_type()));
              val_elems.push_back(safe_zero(vals_type.element_type()));
            }
            exprt length = from_integer(
              static_cast<long long>(ki->second.size()), signedbv_typet{64});
            dict_literals[symbol_id] = struct_exprt{
              {length,
               array_exprt{std::move(key_elems), keys_type},
               array_exprt{std::move(val_elems), vals_type}},
              rhs.type()};
          }
          else
            dict_literals.erase(symbol_id);
        }
      }
      else
        dict_literals.erase(symbol_id);
    }
    else
      dict_literals.erase(symbol_id);
  }
  if(is_python_list_type(rhs.type()))
  {
    if(rhs.id() == ID_struct && escaped_mutables.count(symbol_id) == 0)
      list_literals[symbol_id] = rhs;
    else if(rhs.id() == ID_side_effect)
    {
      // PLR: inter-procedural list-literal propagation. If the
      // RHS is a call to a single-return function whose return
      // statement is a constant list literal, propagate the
      // literal so callers can constant-fold subscripts/len.
      const auto &se = to_side_effect_expr(rhs);
      if(
        se.get_statement() == ID_function_call && !se.operands().empty() &&
        se.operands()[0].id() == ID_symbol &&
        escaped_mutables.count(symbol_id) == 0)
      {
        std::string callee =
          id2string(to_symbol_expr(se.operands()[0]).get_identifier());
        std::string prefix = "python::";
        if(callee.substr(0, prefix.size()) == prefix)
          callee = callee.substr(prefix.size());
        auto fl_it = function_returned_literal.find(callee);
        auto rc_it = function_return_count.find(callee);
        if(
          fl_it != function_returned_literal.end() &&
          rc_it != function_return_count.end() && rc_it->second == 1 &&
          fl_it->second.id() == ID_struct &&
          is_python_list_type(fl_it->second.type()) &&
          fl_it->second.type() == rhs.type())
        {
          list_literals[symbol_id] = fl_it->second;
        }
        else
          list_literals.erase(symbol_id);
      }
      else
        list_literals.erase(symbol_id);
    }
    else
      list_literals.erase(symbol_id);
  }
  if(is_python_tuple_type(rhs.type()))
  {
    if(rhs.id() == ID_struct)
      tuple_literals[symbol_id] = rhs;
    else if(rhs.id() == ID_side_effect)
    {
      // PLR: inter-procedural tuple-literal propagation.
      const auto &se = to_side_effect_expr(rhs);
      if(
        se.get_statement() == ID_function_call && !se.operands().empty() &&
        se.operands()[0].id() == ID_symbol)
      {
        std::string callee =
          id2string(to_symbol_expr(se.operands()[0]).get_identifier());
        std::string prefix = "python::";
        if(callee.substr(0, prefix.size()) == prefix)
          callee = callee.substr(prefix.size());
        auto fl_it = function_returned_literal.find(callee);
        auto rc_it = function_return_count.find(callee);
        if(
          fl_it != function_returned_literal.end() &&
          rc_it != function_return_count.end() && rc_it->second == 1 &&
          fl_it->second.id() == ID_struct &&
          is_python_tuple_type(fl_it->second.type()) &&
          fl_it->second.type() == rhs.type())
        {
          tuple_literals[symbol_id] = fl_it->second;
        }
        else
          tuple_literals.erase(symbol_id);
      }
      else
        tuple_literals.erase(symbol_id);
    }
    else
      tuple_literals.erase(symbol_id);
  }
  // PLR §6.5: track python_complex literals (structs with tag
  // 'python_complex') so 'z = complex(a, b)' can be recovered
  // by the constant-fold path of '**'.
  if(
    rhs.type().id() == ID_struct &&
    to_struct_type(rhs.type()).get_tag() == "python_complex")
  {
    if(rhs.id() == ID_struct)
      complex_literals[symbol_id] = rhs;
    else
      complex_literals.erase(symbol_id);
  }
  // Track numeric constants (including expressions)
  {
    auto ev = try_eval_double(rhs);
    if(ev.has_value())
      float_constants[symbol_id] = ev.value();
    else
      float_constants.erase(symbol_id);
  }
  return std::move(assign);
}

// PLR §7.2: Assignment statements
// "Assignment statements are used to (re)bind names to values and to
// modify attributes or items of mutable objects."
codet python_convertert::convert_assign(const jsont &stmt)
{
  // x = expr  OR  a = b = expr (multiple targets)
  const jsont &targets = json_member(stmt, "targets");
  const jsont &value = json_member(stmt, "value");

  if(!targets.is_array() || as_array(targets).empty())
    return code_skipt{};

  source_locationt loc = get_location(stmt);

  // enum-member-variable tracking (PLR §8.13): `s = SomeEnum.MEMBER` records the
  // target so a later `s.value` resolves to the member's stored value instead
  // of a nondet attribute read. Any other RHS clears the record (reassignment
  // to a non-enum value).
  {
    const bool rhs_is_enum_member =
      is_node_type(value, "Attribute") &&
      is_node_type(json_member(value, "value"), "Name") &&
      enum_members.count(
        json_string(json_member(json_member(value, "value"), "id"))) > 0;
    for(const auto &t : as_array(targets))
      if(is_node_type(t, "Name"))
      {
        irep_idt sid{qualify_name(json_string(json_member(t, "id")))};
        if(rhs_is_enum_member)
          enum_member_vars.insert(sid);
        else
          enum_member_vars.erase(sid);
      }
  }

  // Track unannotated `d = {}` so the first `d[k] = v` can rebuild the
  // dict with the real key/value types (the empty literal defaults to
  // dict[str,int]; int/float keys would otherwise be lossily coerced
  // to str, collapsing distinct keys). Cleared when a Name target is
  // reassigned to a non-empty value.
  {
    const bool empty_dict_rhs = is_node_type(value, "Dict") &&
                                json_member(value, "keys").is_array() &&
                                as_array(json_member(value, "keys")).empty();
    for(const auto &tgt : as_array(targets))
      if(is_node_type(tgt, "Name"))
      {
        irep_idt sid{qualify_name(json_string(json_member(tgt, "id")))};
        if(empty_dict_rhs)
          empty_dict_pending.insert(sid);
        else
          empty_dict_pending.erase(sid);
      }
  }

  // PLR §22.7.1: typing.NewType('X', T) — a marker that creates
  // a callable identity alias. Detect 'X = NewType(...)' or
  // 'X = t.NewType(...)' / 'X = typing.NewType(...)' so that
  // subsequent X(arg) calls fold to arg.
  {
    auto detect_newtype_call = [this](const jsont &v) -> bool
    {
      if(!is_node_type(v, "Call"))
        return false;
      const jsont &fn = json_member(v, "func");
      if(
        is_node_type(fn, "Name") &&
        json_string(json_member(fn, "id")) == "NewType")
        return true;
      if(
        is_node_type(fn, "Attribute") &&
        json_string(json_member(fn, "attr")) == "NewType")
      {
        const jsont &recv = json_member(fn, "value");
        if(is_node_type(recv, "Name"))
        {
          std::string rn = json_string(json_member(recv, "id"));
          if(rn == "typing" || rn == "t")
            return true;
        }
      }
      return false;
    };
    if(detect_newtype_call(value))
    {
      for(const auto &tgt : as_array(targets))
      {
        if(is_node_type(tgt, "Name"))
        {
          std::string nm = json_string(json_member(tgt, "id"));
          newtype_aliases.insert(nm);
        }
      }
    }
  }

  // PLR §6.10.3: list/dict identity aliasing for plain Assign.
  // Mirror convert_ann_assign's alias handling: when a single
  // Name target receives another Name whose value is a list /
  // dict variable, record the alias chain so subsequent
  // 'lhs is rhs' compares true and so mutations through lhs
  // also affect rhs (the contents are shared, not snapshotted).
  if(as_array(targets).size() == 1)
  {
    const jsont &single_target = *as_array(targets).begin();
    if(is_node_type(single_target, "Name") && is_node_type(value, "Name"))
    {
      std::string lhs_name = json_string(json_member(single_target, "id"));
      std::string rhs_name = json_string(json_member(value, "id"));
      if(!lhs_name.empty() && !rhs_name.empty())
      {
        irep_idt lhs_id{qualify_name(lhs_name)};
        irep_idt rhs_id{qualify_name(rhs_name)};
        // Walk the rhs side to find the canonical source.
        auto it = alias_targets.find(rhs_id);
        irep_idt target_id = (it != alias_targets.end()) ? it->second : rhs_id;
        const symbolt *target_sym = symbol_table.lookup(target_id);
        // reference-semantics-for-instances (Phase 2): a local instance is a
        // python_class_* struct; `b = a` must alias it (pointer-promote +
        // address_of), exactly like list/dict, so a mutation through `b` is
        // visible via `a`. Without this it fell through to a struct copy (the
        // local-alias false proof).
        const bool tgt_is_instance =
          target_sym != nullptr &&
          ((target_sym->type.id() == ID_struct &&
            id2string(to_struct_type(target_sym->type).get_tag())
                .find("python_class_") != std::string::npos) ||
           (target_sym->type.id() == ID_struct_tag &&
            id2string(to_struct_tag_type(target_sym->type).get_identifier())
                .find("python_class_") != std::string::npos));
        if(
          target_sym != nullptr &&
          (is_python_list_type(target_sym->type) ||
           is_python_dict_type(target_sym->type) || tgt_is_instance))
        {
          // PLR §3.1: pointer-promotion. Bind lhs's symbol type
          // to pointer-to-target so subsequent reads through
          // lhs auto-dereference, and mutations through lhs
          // hit the same storage as rhs (alias semantics for
          // 'l2 = l1; l2.pop()' / 'd2 = d1; d2[k] = v').
          // Mirrors convert_ann_assign's path.
          pointer_typet ptr_type{target_sym->type, 64};
          if(symbol_table.lookup(lhs_id) == nullptr)
          {
            symbolt s{lhs_id, ptr_type, "python"};
            s.base_name = lhs_name;
            s.is_lvalue = true;
            s.is_state_var = true;
            s.is_static_lifetime = current_function.empty();
            symbol_table.add(s);
          }
          else
          {
            symbol_table.get_writeable_ref(lhs_id).type = ptr_type;
          }
          // Record the alias chain so the 'is' check walks to
          // canonical and treats both ends as the same object.
          alias_targets[lhs_id] = target_id;
          // PLR §3.1 / §6.2.9: a generator aliased by `it2 = it` shares its
          // consumption cursor (same object). Propagate the cursor so
          // next()/for on either name advances the same cursor.
          {
            auto gc = generator_cursors.find(target_id);
            if(gc != generator_cursors.end())
              generator_cursors[lhs_id] = gc->second;
          }
          // Drop any cached literal for the canonical source
          // because the alias makes its contents reachable
          // via two names — subsequent reads must re-fetch
          // from storage rather than the conversion-time
          // snapshot.
          list_literals.erase(target_id);
          dict_literals.erase(target_id);
          tuple_literals.erase(target_id);
          string_constants.erase(target_id);
          // Emit lhs = address_of(target_sym).
          source_locationt loc2 = get_location(stmt);
          code_frontend_assignt assign{
            symbol_table.lookup_ref(lhs_id).symbol_expr(),
            address_of_exprt{target_sym->symbol_expr()}};
          assign.add_source_location() = loc2;
          return std::move(assign);
        }
      }
    }
  }

  // Check if RHS is a constructor call
  if(
    is_node_type(value, "Call") &&
    is_node_type(json_member(value, "func"), "Name"))
  {
    std::string call_name =
      json_string(json_member(json_member(value, "func"), "id"));
    if(class_types.count(call_name))
    {
      const jsont &first_target = *as_array(targets).begin();

      // Attribute target: self.inner = ClassName(args)
      if(is_node_type(first_target, "Attribute"))
      {
        exprt obj = convert_expression(json_member(first_target, "value"));
        std::string attr = json_string(json_member(first_target, "attr"));
        exprt lhs_obj = obj;
        if(obj.type().id() == ID_pointer)
          lhs_obj = dereference_exprt{obj};

        if(lhs_obj.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(lhs_obj.type());
          if(st.has_component(attr))
          {
            member_exprt lhs{lhs_obj, attr, st.get_component(attr).type()};
            std::string tmp_name = "__ctor_tmp_" + attr;
            std::string tmp_qname = qualify_name(tmp_name);
            irep_idt tmp_id{tmp_qname};
            typet attr_type = st.get_component(attr).type();

            if(symbol_table.lookup(tmp_id) == nullptr)
            {
              symbolt tmp_sym{tmp_id, attr_type, "python"};
              tmp_sym.base_name = tmp_name;
              tmp_sym.is_lvalue = true;
              tmp_sym.is_state_var = true;
              symbol_table.add(tmp_sym);
            }

            const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
            code_blockt result;

            auto init_call = build_class_init_call(
              call_name, tmp_sym.symbol_expr(), value, loc);
            if(init_call)
              result.add(code_expressiont{*init_call});

            result.add(code_frontend_assignt{lhs, tmp_sym.symbol_expr()});
            return std::move(result);
          }
        }
      }

      // Name target: x = ClassName(args)
      if(is_node_type(first_target, "Name"))
      {
        const struct_typet &cls_type = class_types[call_name];
        std::string var_name = json_string(json_member(first_target, "id"));
        std::string qualified_name = qualify_name(var_name);
        irep_idt symbol_id{qualified_name};

        if(symbol_table.lookup(symbol_id) == nullptr)
        {
          symbolt new_symbol{symbol_id, cls_type, "python"};
          new_symbol.base_name = var_name;
          new_symbol.location = loc;
          new_symbol.is_lvalue = true;
          new_symbol.is_state_var = true;
          new_symbol.is_static_lifetime = current_function.empty();
          symbol_table.add(new_symbol);
        }
        // reference-semantics-for-instances (Phase 2): if the target was a
        // prior instance ALIAS (`b = a` -> pointer-to-instance), constructing a
        // FRESH instance must REBIND it to a new object, not write through the
        // stale alias pointer (which would corrupt the aliased object `a`).
        // Reset the symbol to the class struct type and drop the alias.
        if(is_instance_pointer(symbol_table.lookup_ref(symbol_id).type))
        {
          symbol_table.get_writeable_ref(symbol_id).type = cls_type;
          alias_targets.erase(symbol_id);
        }

        const symbolt &var_sym = symbol_table.lookup_ref(symbol_id);
        code_blockt result;

        // PLR §3.1/§3.2: destination is a tagged-union (e.g. a
        // variable declared `d: str | datetime`, or a class field
        // typed Optional[...]/Union[...]). `&d` is not a valid
        // class-struct pointer, so constructing in place would run
        // __init__ on a nondet self and never initialise the
        // instance. Instead construct into a temp of the class
        // struct type, run __init__ on it, then wrap the temp into
        // the union (CLASS tag + __class_ptr) — mirroring
        // coerce_to_typed_slot's class-into-union boundary — so
        // later attribute reads resolve through __class_ptr.
        if(is_python_value_type(var_sym.type))
        {
          std::string tmp_name = "__ctor_union_" + var_name;
          std::string tmp_qname = qualify_name(tmp_name);
          irep_idt tmp_id{tmp_qname};
          if(symbol_table.lookup(tmp_id) == nullptr)
          {
            symbolt tmp_sym{tmp_id, cls_type, "python"};
            tmp_sym.base_name = tmp_name;
            tmp_sym.is_lvalue = true;
            tmp_sym.is_state_var = true;
            tmp_sym.is_static_lifetime = current_function.empty();
            symbol_table.add(tmp_sym);
          }
          const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);

          if(class_tag_ids.count(call_name))
            result.add(code_frontend_assignt{
              member_exprt{
                tmp_sym.symbol_expr(), "__class_tag", signedbv_typet{32}},
              from_integer(class_tag_ids[call_name], signedbv_typet{32})});

          irep_idt class_obj_id{"python::" + call_name};
          const symbolt *class_obj = symbol_table.lookup(class_obj_id);
          if(class_obj != nullptr && !class_obj->value.is_nil())
            result.add(code_frontend_assignt{
              tmp_sym.symbol_expr(), class_obj->symbol_expr()});

          auto init_call =
            build_class_init_call(call_name, tmp_sym.symbol_expr(), value, loc);
          if(init_call)
          {
            code_expressiont call_stmt{*init_call};
            call_stmt.add_source_location() = loc;
            result.add(std::move(call_stmt));
          }

          exprt wrapped =
            coerce_assign_rhs(tmp_sym.symbol_expr(), python_value_type());
          code_frontend_assignt assign{var_sym.symbol_expr(), wrapped};
          assign.add_source_location() = loc;
          result.add(std::move(assign));
          return std::move(result);
        }

        // Identify whether the destination symbol's type can actually
        // hold class-instance state. Python allows rebinding a name
        // to a different type, so a previous scalar use of the same
        // name may leave the symbol with a non-struct type. In that
        // case we skip the class-tag / default-value initialisation —
        // the subsequent __init__ call will still run, and we log a
        // warning so the user knows the state will not be tracked
        // precisely.
        const typet &resolved_var_type = var_sym.type;
        const bool var_holds_struct = resolved_var_type.id() == ID_struct ||
                                      resolved_var_type.id() == ID_struct_tag;

        // Set __class_tag to the actual class being constructed
        if(var_holds_struct && class_tag_ids.count(call_name))
        {
          result.add(code_frontend_assignt{
            member_exprt{
              var_sym.symbol_expr(), "__class_tag", signedbv_typet{32}},
            from_integer(class_tag_ids[call_name], signedbv_typet{32})});
        }

        // Copy class-level default values from the class object
        irep_idt class_obj_id{"python::" + call_name};
        const symbolt *class_obj = symbol_table.lookup(class_obj_id);
        if(
          var_holds_struct && class_obj != nullptr &&
          !class_obj->value.is_nil())
        {
          result.add(code_frontend_assignt{
            var_sym.symbol_expr(), class_obj->symbol_expr()});
        }

        if(!var_holds_struct && class_tag_ids.count(call_name))
        {
          log.warning() << "Variable '" << var_sym.base_name
                        << "' is being rebound to class instance of '"
                        << call_name << "' but already has non-struct type '"
                        << resolved_var_type.id_string()
                        << "'; class state will not be tracked precisely."
                        << messaget::eom;
        }

        // Construct: __init__ call, or @dataclass field binding.
        for(auto &s : build_class_construction(
              call_name, var_sym.symbol_expr(), value, loc))
          result.add(std::move(s));

        if(result.statements().size() == 1)
          return result.statements().front();
        return std::move(result);
      }
    }
  }

  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  // Extraction-then-mutate soundness: if a single Name target is bound to a
  // mutable element extracted from a container (`r = c[i]`), remember the
  // source so a later in-place mutation of `r` havocs it (PLR reference
  // semantics). Also clears stale aliases on reassignment.
  {
    const jsont &targets_n = json_member(stmt, "targets");
    if(targets_n.is_array() && as_array(targets_n).size() == 1)
    {
      const jsont &t0 = *as_array(targets_n).begin();
      if(is_node_type(t0, "Name"))
        note_mutable_extraction(
          irep_idt{qualify_name(json_string(json_member(t0, "id")))},
          rhs,
          value);
    }
  }

  // PLR object identity (§9 #nested-aliasing): taint lists whose BY-VALUE
  // mutable elements get aliased by a replicating/sharing op, so a later
  // in-place element mutation is reported (not silently false-proved). The
  // sharing ops alias the operand's elements into the result (and, for
  // shallow copies, vice-versa), so we taint BOTH the target name(s) AND any
  // Name operand. Only applies when the result element type is a by-value
  // mutable container; python_value (pointer) elements are already aliased
  // soundly (escaped_mutables) and need no guard.
  {
    auto rhs_byval_mutable_list = [&]() -> bool
    {
      if(!is_python_list_type(rhs.type()))
        return false;
      const auto &st = to_struct_type(rhs.type());
      const typet &el = to_array_type(st.components()[1].type()).element_type();
      return is_python_list_type(el) || is_python_dict_type(el) ||
             is_python_set_type(el);
    };
    std::vector<const jsont *> share_operands;
    bool is_share = false;
    if(is_node_type(value, "BinOp"))
    {
      const std::string op =
        json_string(json_member(json_member(value, "op"), "_type"));
      if(op == "Mult" || op == "Add")
      {
        is_share = true;
        share_operands.push_back(&json_member(value, "left"));
        share_operands.push_back(&json_member(value, "right"));
      }
    }
    else if(
      is_node_type(value, "Subscript") &&
      is_node_type(json_member(value, "slice"), "Slice"))
    {
      is_share = true;
      share_operands.push_back(&json_member(value, "value"));
    }
    else if(is_node_type(value, "Call"))
    {
      const jsont &fn = json_member(value, "func");
      if(
        is_node_type(fn, "Attribute") &&
        json_string(json_member(fn, "attr")) == "copy")
      {
        is_share = true;
        share_operands.push_back(&json_member(fn, "value"));
      }
      else if(
        is_node_type(fn, "Name") &&
        json_string(json_member(fn, "id")) == "list")
      {
        const jsont &cargs = json_member(value, "args");
        if(cargs.is_array() && !as_array(cargs).empty())
        {
          is_share = true;
          share_operands.push_back(&(*as_array(cargs).begin()));
        }
      }
    }
    auto taint_if_name = [&](const jsont &n)
    {
      if(is_node_type(n, "Name"))
        aliased_mutable_lists.insert(
          irep_idt{qualify_name(json_string(json_member(n, "id")))});
    };
    if(is_share && rhs_byval_mutable_list())
    {
      for(const auto &tgt : as_array(targets))
        taint_if_name(tgt);
      for(const jsont *opnd : share_operands)
        taint_if_name(*opnd);
    }
    else
    {
      // Propagate a whole-list alias `h = g`; otherwise the target is bound to
      // something non-aliased -> clear any stale taint.
      const bool rhs_tainted_name =
        is_node_type(value, "Name") &&
        aliased_mutable_lists.count(
          irep_idt{qualify_name(json_string(json_member(value, "id")))}) > 0;
      for(const auto &tgt : as_array(targets))
      {
        if(!is_node_type(tgt, "Name"))
          continue;
        const irep_idt lid{qualify_name(json_string(json_member(tgt, "id")))};
        if(rhs_tainted_name)
          aliased_mutable_lists.insert(lid);
        else
          aliased_mutable_lists.erase(lid);
      }
    }

    // Shared-inner tracking (PLR §9): a name bound to an ELEMENT of an
    // aliased-mutable list (`row = g[i]`) holds a shared inner object;
    // mutating it in place is the same unmodelled aliasing. Also propagate
    // through a plain alias (`s = row`). Cleared on any other binding.
    {
      bool inner = false;
      if(is_node_type(value, "Subscript"))
      {
        const jsont &b = json_member(value, "value");
        if(
          is_node_type(b, "Name") &&
          aliased_mutable_lists.count(
            irep_idt{qualify_name(json_string(json_member(b, "id")))}) > 0)
          inner = true;
      }
      else if(
        is_node_type(value, "Name") &&
        shared_inner_mutables.count(
          irep_idt{qualify_name(json_string(json_member(value, "id")))}) > 0)
        inner = true;
      for(const auto &tgt : as_array(targets))
      {
        if(!is_node_type(tgt, "Name"))
          continue;
        const irep_idt lid{qualify_name(json_string(json_member(tgt, "id")))};
        if(inner)
          shared_inner_mutables.insert(lid);
        else
          shared_inner_mutables.erase(lid);
      }
    }
  }

  // Nested-aliasing soundness (PLR §9): a list LITERAL containing a
  // mutable inner element extracted from another list (`h = [g[i]]`) or
  // a shared-inner name holds an aliased object; mutating it later
  // (`h[0][j] = ...`) is the same unmodelled aliasing repetition/concat
  // taint. Taint the target(s) so the element-mutation guard fires
  // (sound over-approximation; a subscript of scalars is harmless since
  // no nested mutation follows).
  if(is_node_type(value, "List"))
  {
    const jsont &elts = json_member(value, "elts");
    bool holds_inner = false;
    if(elts.is_array())
      for(const auto &e : as_array(elts))
      {
        if(is_node_type(e, "Subscript"))
          holds_inner = true;
        else if(
          is_node_type(e, "Name") &&
          shared_inner_mutables.count(
            irep_idt{qualify_name(json_string(json_member(e, "id")))}) > 0)
          holds_inner = true;
      }
    if(holds_inner)
      for(const auto &tgt : as_array(targets))
        if(is_node_type(tgt, "Name"))
          aliased_mutable_lists.insert(
            irep_idt{qualify_name(json_string(json_member(tgt, "id")))});
  }

  // PLR §3.2: if the RHS is an empty list literal AND the
  // single Name target is in empty_list_inferred_types
  // (populated by collect_empty_list_inferred_types from a
  // following 'name.append(X)' in the same body), rebuild
  // the rhs with the inferred element type so the list's
  // backing array matches the appended values' type. Without
  // this the typecast at append time zeros struct-typed
  // elements (string / list / dict / class).
  if(
    is_node_type(value, "List") && json_member(value, "elts").is_array() &&
    as_array(json_member(value, "elts")).empty() &&
    as_array(targets).size() == 1 &&
    is_node_type(*as_array(targets).begin(), "Name"))
  {
    std::string lhs_name =
      json_string(json_member(*as_array(targets).begin(), "id"));
    irep_idt lhs_id{qualify_name(lhs_name)};
    auto inf_it = empty_list_inferred_types.find(lhs_id);
    if(inf_it != empty_list_inferred_types.end())
    {
      typet new_elem_t = inf_it->second;
      struct_typet new_list_type = python_list_type(new_elem_t);
      const auto &new_data_type =
        to_array_type(new_list_type.components()[1].type());
      exprt::operandst zeros;
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        zeros.push_back(safe_zero(new_elem_t));
      array_exprt new_data{std::move(zeros), new_data_type};
      rhs = struct_exprt{
        {from_integer(0, signedbv_typet{64}), new_data}, new_list_type};
    }
  }

  // PLR §3.2: same look-ahead for an unannotated empty dict. If a
  // following `name[k] = v` in this body let the pre-scan infer the
  // key/value element types, build `{}` with those types from the
  // start (works inside loops, unlike the first-assign rebuild).
  if(
    is_node_type(value, "Dict") && json_member(value, "keys").is_array() &&
    as_array(json_member(value, "keys")).empty() &&
    as_array(targets).size() == 1 &&
    is_node_type(*as_array(targets).begin(), "Name"))
  {
    irep_idt lhs_id{
      qualify_name(json_string(json_member(*as_array(targets).begin(), "id")))};
    auto di = empty_dict_inferred_types.find(lhs_id);
    if(di != empty_dict_inferred_types.end())
      rhs = safe_zero(python_dict_type(di->second.first, di->second.second));
  }

  // Lambda/function assignment: record alias instead of creating variable
  if(rhs.id() == ID_symbol && rhs.type().id() == ID_code)
  {
    const irep_idt target_id = to_symbol_expr(rhs).get_identifier();
    code_blockt block;
    for(const auto &target : as_array(targets))
    {
      if(is_node_type(target, "Name"))
      {
        std::string var_name = json_string(json_member(target, "id"));
        std::string q = qualify_name(var_name);
        function_aliases[q] = target_id;
        // §10: record this target as a candidate and emit a runtime
        // tag assignment so a branch-dependent reassignment dispatches
        // path-sensitively at the call site. The tag is this target's
        // index in the (accumulating) candidate list.
        auto &cands = callable_candidates[q];
        auto cit = std::find(cands.begin(), cands.end(), target_id);
        long idx;
        if(cit == cands.end())
        {
          idx = static_cast<long>(cands.size());
          cands.push_back(target_id);
        }
        else
          idx = static_cast<long>(cit - cands.begin());
        irep_idt tag_id{q + "$callable_tag"};
        if(symbol_table.lookup(tag_id) == nullptr)
        {
          symbolt ts{tag_id, signedbv_typet{64}, "python"};
          ts.base_name = var_name + "$callable_tag";
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        block.add(code_frontend_assignt{
          symbol_table.lookup_ref(tag_id).symbol_expr(),
          from_integer(idx, signedbv_typet{64})});
      }
    }
    if(block.statements().empty())
      return code_skipt{};
    return std::move(block);
  }

  // Check if RHS is a call to a lambda-returning function
  if(rhs.id() == ID_side_effect)
  {
    const auto &se = to_side_effect_expr(rhs);
    if(se.get_statement() == ID_function_call && !se.operands().empty())
    {
      const exprt &func_op = se.operands()[0];
      if(func_op.id() == ID_symbol)
      {
        std::string called =
          id2string(to_symbol_expr(func_op).get_identifier());
        // Strip "python::" prefix
        if(called.substr(0, 8) == "python::")
          called = called.substr(8);
        auto it = lambda_returning_functions.find(called);
        if(it != lambda_returning_functions.end())
        {
          code_blockt lam_block;
          // Per-closure-variable capture binding (PLR §4.2.2). Snapshot
          // each of the inner closure's captures into a fresh per-call
          // temp and record them against the target name(s), so distinct
          // factory invocations (g1 = make(); g2 = make()) stay
          // independent. Param captures bind from the call arguments;
          // LOCAL / nonlocal-CELL captures require running f's body
          // (discard its code-typed return) so the locals and heap cells
          // are computed, then snapshot from f's symbols.
          std::map<std::string, irep_idt> this_call_caps;
          irep_idt fid{"python::" + called};
          const symbolt *fsym = symbol_table.lookup(fid);
          auto ci = closure_captures.find(id2string(it->second));
          if(
            fsym != nullptr && fsym->type.id() == ID_code &&
            ci != closure_captures.end())
          {
            const auto &fp = to_code_type(fsym->type).parameters();
            std::set<std::string> fparam_ids;
            for(const auto &p : fp)
              fparam_ids.insert(id2string(p.get_identifier()));
            const jsont &call_args =
              json_member(json_member(stmt, "value"), "args");
            std::map<std::string, exprt> param_arg;
            if(call_args.is_array())
            {
              auto ai = as_array(call_args).begin();
              for(std::size_t i = 0;
                  i < fp.size() && ai != as_array(call_args).end();
                  i++, ++ai)
                param_arg[id2string(fp[i].get_identifier())] =
                  convert_expression(*ai);
            }
            bool has_local = false;
            for(const auto &cap : ci->second)
              if(
                fparam_ids.count(std::get<0>(cap)) == 0 &&
                symbol_table.lookup(irep_idt{std::get<0>(cap)}) != nullptr)
                has_local = true;
            if(has_local)
            {
              exprt::operandst fargs;
              if(call_args.is_array())
                for(const auto &a : as_array(call_args))
                  fargs.push_back(convert_expression(a));
              side_effect_expr_function_callt fcall{
                fsym->symbol_expr(),
                std::move(fargs),
                to_code_type(fsym->type).return_type(),
                get_location(stmt)};
              lam_block.add(code_expressiont{std::move(fcall)});
            }
            static unsigned lb = 0;
            for(const auto &cap : ci->second)
            {
              const std::string &outer_id = std::get<0>(cap);
              const std::string &capname = std::get<1>(cap);
              exprt src;
              if(fparam_ids.count(outer_id))
              {
                auto pa = param_arg.find(outer_id);
                if(pa == param_arg.end())
                  continue;
                src = pa->second;
              }
              else
              {
                const symbolt *ls = symbol_table.lookup(irep_idt{outer_id});
                if(ls == nullptr)
                  continue;
                src = ls->symbol_expr();
              }
              // A code-typed capture (a closure capturing another
              // closure / function value) cannot be snapshotted into a
              // temp (`temp := <code>` is not a valid assignment); leave
              // it bound from its original source via the call-site
              // fallback rather than aborting symex.
              if(src.type().id() == ID_code)
                continue;
              std::string tn = "__lam_bind_" + std::to_string(lb++);
              irep_idt ti{qualify_name(tn)};
              if(symbol_table.lookup(ti) == nullptr)
              {
                symbolt ts{ti, src.type(), "python"};
                ts.base_name = tn;
                ts.is_lvalue = true;
                ts.is_state_var = true;
                ts.is_static_lifetime = true;
                symbol_table.add(ts);
              }
              lam_block.add(code_frontend_assignt{
                symbol_table.lookup_ref(ti).symbol_expr(), src});
              this_call_caps[capname] = ti;
            }
          }
          for(const auto &target : as_array(targets))
          {
            if(is_node_type(target, "Name"))
            {
              std::string var_name = json_string(json_member(target, "id"));
              function_aliases[qualify_name(var_name)] = it->second;
              if(!this_call_caps.empty())
                closure_var_captures[qualify_name(var_name)] = this_call_caps;
            }
          }
          if(lam_block.statements().empty())
            return code_skipt{};
          return std::move(lam_block);
        }
      }
    }
  }

  // Detect bound method assignment: method = obj.func
  // Exclusion: when attr is a @property, obj.attr evaluates
  // the property method and produces a value — not a bound-
  // method reference. Fall through to the normal assign path.
  {
    const jsont &val_node = json_member(stmt, "value");
    if(is_node_type(val_node, "Attribute"))
    {
      std::string attr = json_string(json_member(val_node, "attr"));
      exprt obj_expr = convert_expression(json_member(val_node, "value"));
      if(
        !obj_expr.is_nil() && obj_expr.type().id() == ID_struct &&
        id2string(to_struct_type(obj_expr.type()).get_tag())
            .find("python_class_") != std::string::npos)
      {
        std::string tag = id2string(to_struct_type(obj_expr.type()).get_tag());
        std::string cls_name = tag.substr(13);
        // Skip @property — attribute access is a value, not a method alias.
        auto pit = class_property_methods.find(cls_name);
        if(pit != class_property_methods.end() && pit->second.count(attr))
        {
          // Fall through to normal assign path.
        }
        else
        {
          irep_idt method_id{"python::" + cls_name + "::" + attr};
          if(symbol_table.lookup(method_id) != nullptr)
          {
            code_blockt tag_block;
            for(const auto &target : as_array(targets))
            {
              if(is_node_type(target, "Name"))
              {
                std::string var_name = json_string(json_member(target, "id"));
                std::string qname = qualify_name(var_name);
                function_aliases[qname] = method_id;
                bound_methods[qname] = {method_id, address_of_exprt{obj_expr}};
                // §10: accumulate candidates + per-branch receiver and
                // emit a runtime tag so a branch-dependent bound-method
                // reassignment dispatches path-sensitively at the call.
                auto &cands = callable_candidates[qname];
                auto &recvs = bound_method_receivers[qname];
                long idx = static_cast<long>(cands.size());
                cands.push_back(method_id);
                recvs.push_back(address_of_exprt{obj_expr});
                irep_idt tag_id{qname + "$callable_tag"};
                if(symbol_table.lookup(tag_id) == nullptr)
                {
                  symbolt ts{tag_id, signedbv_typet{64}, "python"};
                  ts.base_name = var_name + "$callable_tag";
                  ts.is_lvalue = true;
                  ts.is_state_var = true;
                  ts.is_static_lifetime = current_function.empty();
                  symbol_table.add(ts);
                }
                tag_block.add(code_frontend_assignt{
                  symbol_table.lookup_ref(tag_id).symbol_expr(),
                  from_integer(idx, signedbv_typet{64})});
              }
            }
            if(tag_block.statements().empty())
              return code_skipt{};
            return std::move(tag_block);
          }
        }
      }
    }
  }

  code_blockt block;

  // PLR §7.2: a chained assignment `a = b = <mutable>` evaluates the RHS ONCE
  // and binds ALL targets to the SAME object, so a mutation through one target
  // is visible through the others. The default per-target loop below binds each
  // target to an independent copy of `rhs` (value semantics), which is a false
  // proof for a mutable container. When there is more than one plain Name target
  // and `rhs` is a mutable container, materialise the value in the FIRST target
  // and alias the rest to it (pointer + alias_targets) — the same mechanism as
  // `b = a`. Immutable rhs (int/str/tuple) needs no aliasing (copies are
  // equivalent), and non-Name targets (subscript/attribute/unpack) keep the
  // default path.
  if(
    as_array(targets).size() > 1 &&
    (is_python_list_type(rhs.type()) || is_python_dict_type(rhs.type()) ||
     is_python_set_type(rhs.type())))
  {
    bool all_names = true;
    for(const auto &t : as_array(targets))
      if(!is_node_type(t, "Name"))
      {
        all_names = false;
        break;
      }
    if(all_names)
    {
      // First target: materialise the value.
      const jsont &first = *as_array(targets).begin();
      const irep_idt first_id{
        qualify_name(json_string(json_member(first, "id")))};
      if(symbol_table.lookup(first_id) == nullptr)
      {
        symbolt fs{first_id, rhs.type(), "python"};
        fs.base_name = json_string(json_member(first, "id"));
        fs.location = loc;
        fs.is_lvalue = true;
        fs.is_state_var = true;
        fs.is_static_lifetime = current_function.empty();
        symbol_table.add(fs);
      }
      else
        symbol_table.get_writeable_ref(first_id).type = rhs.type();
      exprt first_sym = symbol_table.lookup_ref(first_id).symbol_expr();
      block.add(code_frontend_assignt{first_sym, rhs});
      // The aliased object is reachable through several names; drop any cached
      // literal so later reads go through storage (mirrors the `b = a` path).
      list_literals.erase(first_id);
      dict_literals.erase(first_id);
      tuple_literals.erase(first_id);
      // Remaining targets: alias to the first (pointer-promote + address_of).
      pointer_typet ptr_type{rhs.type(), 64};
      bool skip_first = true;
      for(const auto &t : as_array(targets))
      {
        if(skip_first)
        {
          skip_first = false;
          continue;
        }
        const irep_idt tid{qualify_name(json_string(json_member(t, "id")))};
        if(symbol_table.lookup(tid) == nullptr)
        {
          symbolt ts{tid, ptr_type, "python"};
          ts.base_name = json_string(json_member(t, "id"));
          ts.location = loc;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        else
          symbol_table.get_writeable_ref(tid).type = ptr_type;
        alias_targets[tid] = first_id;
        block.add(code_frontend_assignt{
          symbol_table.lookup_ref(tid).symbol_expr(),
          address_of_exprt{first_sym}});
      }
      return std::move(block);
    }
  }

  for(const auto &target : as_array(targets))
  {
    // Handle tuple unpacking: a, b, c = expr
    if(is_node_type(target, "Tuple") || is_node_type(target, "List"))
    {
      const jsont &elts = json_member(target, "elts");
      // PLR §3.3.1: unpacking requires an iterable. A concrete class instance
      // whose MRO defines neither __iter__ nor __getitem__ is not iterable, so
      // `a, b = C()` raises TypeError ('cannot unpack non-iterable ...'). Same
      // whole-group check (and gating) as the for-loop / comprehension sites.
      {
        std::string rtag;
        if(rhs.type().id() == ID_struct)
          rtag = id2string(to_struct_type(rhs.type()).get_tag());
        else if(rhs.type().id() == ID_struct_tag)
          rtag = id2string(to_struct_tag_type(rhs.type()).get_identifier());
        if(
          rtag.substr(0, 13) == "python_class_" &&
          !class_mro_defines(rtag.substr(13), "__iter__") &&
          !class_mro_defines(rtag.substr(13), "__getitem__"))
        {
          source_locationt tloc = loc;
          tloc.set_property_class("type-error");
          tloc.set_comment("cannot unpack non-iterable object");
          code_assertt te{false_exprt{}};
          te.add_source_location() = tloc;
          block.add(std::move(te));
          return std::move(block);
        }
      }
      // PLR §7.2.2: extended starred unpacking
      //   first, *rest = [1, 2, 3]
      //   *head, last = [1, 2, 3]
      //   a, *mid, z = [1, 2, 3, 4, 5]
      // Triggered for any Tuple/List target with at least one
      // Starred element AND a list-typed rhs. Handled in front
      // of the existing tuple-rhs path so that list rhs gets a
      // proper expansion.
      bool has_starred = false;
      std::size_t star_idx = 0;
      if(elts.is_array())
      {
        std::size_t i = 0;
        for(const auto &e : as_array(elts))
        {
          if(is_node_type(e, "Starred"))
          {
            has_starred = true;
            star_idx = i;
            break;
          }
          ++i;
        }
      }
      if(
        has_starred && elts.is_array() &&
        (is_python_list_type(rhs.type()) || is_python_tuple_type(rhs.type())))
      {
        // Materialise the rhs into a fresh tmp so the
        // assignment-target list reads from a snapshot
        // (matches simple-tuple-unpack semantics).
        static unsigned starred_ctr = 0;
        std::string tmpn = "__starred_" + std::to_string(starred_ctr++);
        std::string tmpq = qualify_name(tmpn);
        irep_idt tmpid{tmpq};
        if(symbol_table.lookup(tmpid) == nullptr)
        {
          symbolt ts{tmpid, rhs.type(), "python"};
          ts.base_name = tmpn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt rhs_snap = symbol_table.lookup_ref(tmpid).symbol_expr();
        block.add(code_frontend_assignt{rhs_snap, rhs});

        // Get data array + length of rhs
        bool rhs_is_list = is_python_list_type(rhs.type());
        if(rhs_is_list)
        {
          const auto &lst = to_struct_type(rhs.type());
          const auto &data_t = to_array_type(lst.components()[1].type());
          const typet &elem_t = data_t.element_type();
          member_exprt rhs_data{rhs_snap, "data", data_t};
          member_exprt rhs_len{rhs_snap, "length", signedbv_typet{64}};
          std::size_t n_elts = as_array(elts).size();
          std::size_t n_trailing = n_elts - star_idx - 1;

          auto emit_simple = [&](const jsont &elt_node, exprt rhs_val)
          {
            std::string en = json_string(json_member(elt_node, "id"));
            if(en.empty())
              return;
            std::string qn = qualify_name(en);
            irep_idt sid{qn};
            if(symbol_table.lookup(sid) == nullptr)
            {
              symbolt ns{sid, rhs_val.type(), "python"};
              ns.base_name = en;
              ns.location = loc;
              ns.is_lvalue = true;
              ns.is_state_var = true;
              ns.is_static_lifetime = current_function.empty();
              symbol_table.add(ns);
            }
            const symbolt &tsym = symbol_table.lookup_ref(sid);
            exprt rv = rhs_val;
            if(rv.type() != tsym.type)
              rv = safe_typecast(rv, tsym.type);
            block.add(code_frontend_assignt{tsym.symbol_expr(), rv});
          };

          // Pre-star elements: source[0..star_idx-1]
          std::size_t i = 0;
          for(const auto &elt : as_array(elts))
          {
            if(i >= star_idx)
              break;
            exprt idx_e = from_integer(i, signedbv_typet{64});
            emit_simple(elt, index_exprt{rhs_data, idx_e, elem_t});
            ++i;
          }
          // Starred element: build a list from source[star_idx ..
          // length - n_trailing - 1].
          {
            const jsont &starred = *std::next(as_array(elts).begin(), star_idx);
            const jsont &inner = json_member(starred, "value");
            std::string sn;
            if(is_node_type(inner, "Name"))
              sn = json_string(json_member(inner, "id"));
            if(!sn.empty())
            {
              // Build a fresh list with the same element type.
              typet rest_list_t = python_list_type(elem_t);
              std::string sq = qualify_name(sn);
              irep_idt rs_id{sq};
              if(symbol_table.lookup(rs_id) == nullptr)
              {
                symbolt rs{rs_id, rest_list_t, "python"};
                rs.base_name = sn;
                rs.location = loc;
                rs.is_lvalue = true;
                rs.is_state_var = true;
                rs.is_static_lifetime = current_function.empty();
                symbol_table.add(rs);
              }
              const symbolt &rsym = symbol_table.lookup_ref(rs_id);
              const auto &rest_st = to_struct_type(rest_list_t);
              const auto &rest_data_t =
                to_array_type(rest_st.components()[1].type());
              // length_rest = max(0, rhs_len - n_trailing - star_idx)
              exprt new_len = minus_exprt{
                rhs_len,
                from_integer(
                  static_cast<long long>(n_trailing + star_idx),
                  signedbv_typet{64})};
              exprt zero64 = from_integer(0LL, signedbv_typet{64});
              exprt len_clamped = if_exprt{
                binary_relation_exprt{new_len, ID_lt, zero64}, zero64, new_len};
              // Build elems: rhs_data[star_idx + i] if i < length_rest
              exprt::operandst elems;
              for(int k = 0; k < PYTHON_MAX_LIST_LENGTH; ++k)
              {
                exprt k_e = from_integer(k, signedbv_typet{64});
                exprt src_idx = plus_exprt{
                  from_integer(
                    static_cast<long long>(star_idx), signedbv_typet{64}),
                  k_e};
                exprt val = index_exprt{rhs_data, src_idx, elem_t};
                exprt in_range = binary_relation_exprt{k_e, ID_lt, len_clamped};
                exprt el = if_exprt{in_range, val, safe_zero(elem_t)};
                elems.push_back(std::move(el));
              }
              exprt rest_val = struct_exprt{
                {len_clamped, array_exprt{std::move(elems), rest_data_t}},
                rest_list_t};
              block.add(code_frontend_assignt{rsym.symbol_expr(), rest_val});
            }
          }
          // Post-star elements: source[length - n_trailing + j]
          std::size_t j = 0;
          for(const auto &elt : as_array(elts))
          {
            if(j > star_idx)
            {
              std::size_t off = j - star_idx - 1; // 0-indexed within trail
              exprt src_idx = plus_exprt{
                rhs_len,
                from_integer(
                  static_cast<long long>(off) -
                    static_cast<long long>(n_trailing),
                  signedbv_typet{64})};
              emit_simple(elt, index_exprt{rhs_data, src_idx, elem_t});
            }
            ++j;
          }
          continue;
        }
      }
      if(elts.is_array() && is_python_tuple_type(rhs.type()))
      {
        const auto &tuple_st = to_struct_type(rhs.type());
        // PLR §7.2.1: `a, b, ... = <tuple>` requires the tuple's arity to equal
        // the number of targets, else ValueError ("too many / not enough values
        // to unpack"). For a fixed-arity tuple both sides are known statically,
        // so a mismatch is a DEFINITE ValueError. Skipped when a Starred target
        // is present (it absorbs the surplus, so any arity >= n-1 is valid).
        // Without this the unpack silently skipped a missing `_i` field
        // (ty-015 / `a, b = (1,)`).
        if(!has_starred)
        {
          std::size_t n_targets = as_array(elts).size();
          std::size_t rhs_arity = 0;
          while(tuple_st.has_component("_" + std::to_string(rhs_arity)))
            ++rhs_arity;
          if(rhs_arity != n_targets)
            emit_conditional_exception(true_exprt{}, "ValueError");
        }
        // PLR §7.2.1: the assignment target list is bound
        // _after_ the expression list on the right is
        // fully evaluated, so the swap idiom
        //     a, b = b, a
        // must not read the updated a/b from its own LHS.
        // Materialise the RHS into a fresh tmp so every
        // field read references the snapshot before any
        // LHS update.
        static unsigned unpack_ctr = 0;
        std::string tmpn = "__unpack_" + std::to_string(unpack_ctr++);
        std::string tmpq = qualify_name(tmpn);
        irep_idt tmpid{tmpq};
        if(symbol_table.lookup(tmpid) == nullptr)
        {
          symbolt ts{tmpid, rhs.type(), "python"};
          ts.base_name = tmpn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt rhs_snapshot =
          symbol_table.lookup_ref(tmpid).symbol_expr();
        block.add(code_frontend_assignt{rhs_snapshot, rhs});
        exprt src = rhs_snapshot;
        std::size_t idx = 0;
        for(const auto &elt : as_array(elts))
        {
          std::string field = "_" + std::to_string(idx);
          if(!tuple_st.has_component(field))
          {
            idx++;
            continue;
          }
          typet field_type = tuple_st.get_component(field).type();
          member_exprt field_expr{src, field, field_type};

          // Recursive unpack: if elt is Tuple/List, unpack the field
          if(is_node_type(elt, "Tuple") || is_node_type(elt, "List"))
          {
            const jsont &sub_elts = json_member(elt, "elts");
            if(sub_elts.is_array() && is_python_tuple_type(field_type))
            {
              const auto &sub_st = to_struct_type(field_type);
              std::size_t sub_idx = 0;
              for(const auto &sub_elt : as_array(sub_elts))
              {
                std::string sub_field = "_" + std::to_string(sub_idx);
                if(sub_st.has_component(sub_field))
                {
                  typet sub_type = sub_st.get_component(sub_field).type();
                  member_exprt sub_expr{field_expr, sub_field, sub_type};
                  std::string name = json_string(json_member(sub_elt, "id"));
                  if(!name.empty())
                  {
                    std::string qname = qualify_name(name);
                    irep_idt sym_id{qname};
                    if(symbol_table.lookup(sym_id) == nullptr)
                    {
                      symbolt new_sym{sym_id, sub_type, "python"};
                      new_sym.base_name = name;
                      new_sym.location = loc;
                      new_sym.is_lvalue = true;
                      new_sym.is_state_var = true;
                      new_sym.is_static_lifetime = current_function.empty();
                      symbol_table.add(new_sym);
                    }
                    block.add(code_frontend_assignt{
                      symbol_table.lookup_ref(sym_id).symbol_expr(), sub_expr});
                  }
                }
                sub_idx++;
              }
            }
          }
          else
          {
            // Simple Name target
            std::string elt_name = json_string(json_member(elt, "id"));
            if(!elt_name.empty())
            {
              std::string qname = qualify_name(elt_name);
              irep_idt sym_id{qname};
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                symbolt new_sym{sym_id, field_type, "python"};
                new_sym.base_name = elt_name;
                new_sym.location = loc;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                new_sym.is_static_lifetime = current_function.empty();
                symbol_table.add(new_sym);
              }
              const symbolt &target_sym = symbol_table.lookup_ref(sym_id);
              exprt typed_field = field_expr;
              if(typed_field.type() != target_sym.type)
                typed_field = safe_typecast(typed_field, target_sym.type);
              block.add(
                code_frontend_assignt{target_sym.symbol_expr(), typed_field});
            }
          }
          idx++;
        }
        continue;
      }
      // PLR §7.2.1: list rhs unpacking, e.g.
      //   first, second, third = [10, 20, 30]
      //   def f(lst): a, b, c = lst
      // The list rhs is read by integer index, with implicit
      // length check (Python raises ValueError if lengths
      // differ — we match that semantics by emitting a runtime
      // length-equality assumption when the list isn't known
      // to be the right size). Recursive unpacking of nested
      // tuple/list targets is supported one level deep.
      if(elts.is_array() && is_python_list_type(rhs.type()))
      {
        const auto &list_st = to_struct_type(rhs.type());
        const auto &data_t = to_array_type(list_st.components()[1].type());
        const typet &elem_t = data_t.element_type();
        // Materialise rhs into a snapshot tmp.
        static unsigned lunpack_ctr = 0;
        std::string tmpn = "__list_unpack_" + std::to_string(lunpack_ctr++);
        std::string tmpq = qualify_name(tmpn);
        irep_idt tmpid{tmpq};
        if(symbol_table.lookup(tmpid) == nullptr)
        {
          symbolt ts{tmpid, rhs.type(), "python"};
          ts.base_name = tmpn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt rhs_snap = symbol_table.lookup_ref(tmpid).symbol_expr();
        block.add(code_frontend_assignt{rhs_snap, rhs});
        member_exprt rhs_data{rhs_snap, "data", data_t};
        member_exprt rhs_len{rhs_snap, "length", signedbv_typet{64}};
        std::size_t target_count = as_array(elts).size();
        // PLR ValueError: 'too many values to unpack' / 'not
        // enough values to unpack'. We assume length matches
        // for soundness of the unpack — under-approximating
        // the error case (a separate lint could flag it).
        // PLR §7.2.1 arity: if the rhs length folds to a CONSTANT that differs
        // from the number of targets, it is a DEFINITE ValueError ('too many' /
        // 'not enough values to unpack') -- e.g. `a, b, c = [1, 2]`. For a
        // symbolic-length list, assume the length matches (a sound-direction
        // under-approximation of the error case; changing it to an assertion
        // would spuriously fail legitimate symbolic-length unpacks).
        std::optional<mp_integer> const_len;
        if(
          rhs.id() == ID_struct && !rhs.operands().empty() &&
          rhs.operands()[0].is_constant())
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(rhs.operands()[0]), v))
            const_len = v;
        }
        if(
          const_len.has_value() &&
          *const_len != mp_integer{static_cast<long long>(target_count)})
          emit_conditional_exception(true_exprt{}, "ValueError");
        else
          block.add(code_assumet{equal_exprt{
            rhs_len,
            from_integer(
              static_cast<long long>(target_count), signedbv_typet{64})}});
        std::size_t i = 0;
        for(const auto &elt : as_array(elts))
        {
          exprt idx_e =
            from_integer(static_cast<long long>(i), signedbv_typet{64});
          exprt val = index_exprt{rhs_data, idx_e, elem_t};
          if(is_node_type(elt, "Name"))
          {
            std::string elt_name = json_string(json_member(elt, "id"));
            if(!elt_name.empty())
            {
              std::string qname = qualify_name(elt_name);
              irep_idt sym_id{qname};
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                symbolt new_sym{sym_id, elem_t, "python"};
                new_sym.base_name = elt_name;
                new_sym.location = loc;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                new_sym.is_static_lifetime = current_function.empty();
                symbol_table.add(new_sym);
              }
              const symbolt &ts = symbol_table.lookup_ref(sym_id);
              exprt rhs_v = val;
              if(rhs_v.type() != ts.type)
                rhs_v = safe_typecast(rhs_v, ts.type);
              block.add(code_frontend_assignt{ts.symbol_expr(), rhs_v});
            }
          }
          ++i;
        }
        continue;
      }
    }

    // Handle subscript assignment: lst[i] = value
    // PLR §3.2: "Tuples are immutable sequences"
    if(is_node_type(target, "Subscript"))
    {
      // PLR list slice-assignment: `a[lo:hi] = <iterable>` replaces the slice
      // with the RHS elements (the result is a[0:lo] ++ rhs ++ a[hi:], so the
      // length can change). Without this the Slice node was mis-converted as a
      // single index, corrupting the list (even len() became nondet).
      // Implemented for a list target + list RHS with step 1 and a matching
      // (homogeneous) element type; other shapes fall through to the existing
      // (sound) handling below.
      {
        const jsont &slice_node = json_member(target, "slice");
        if(is_node_type(slice_node, "Slice"))
        {
          const jsont &step_node = json_member(slice_node, "step");
          exprt obj = convert_expression(json_member(target, "value"));
          exprt rhs_list = rhs;
          if(
            step_node.is_null() && !obj.is_nil() &&
            is_python_list_type(obj.type()) &&
            is_python_list_type(rhs_list.type()))
          {
            const auto &ost = to_struct_type(obj.type());
            const auto &odt = to_array_type(ost.components()[1].type());
            const auto &rst = to_struct_type(rhs_list.type());
            const auto &rdt = to_array_type(rst.components()[1].type());
            if(odt.element_type() == rdt.element_type())
            {
              const member_exprt olen{obj, "length", signedbv_typet{64}};
              const member_exprt odata{obj, "data", odt};
              const member_exprt rlen{rhs_list, "length", signedbv_typet{64}};
              const member_exprt rdata{rhs_list, "data", rdt};
              auto i64 = [](long v)
              { return from_integer(v, signedbv_typet{64}); };
              // Normalise a bound: negative wraps relative to length, then
              // clamp to [0, length] (PLR §6.3.3 slice semantics).
              auto norm = [&](const jsont &b, const exprt &dflt) -> exprt
              {
                if(b.is_null())
                  return dflt;
                exprt e = convert_expression(b);
                if(e.type() != signedbv_typet{64})
                  e = safe_typecast(e, signedbv_typet{64});
                exprt wrapped = if_exprt{
                  binary_relation_exprt{e, ID_lt, i64(0)},
                  plus_exprt{e, olen},
                  e};
                exprt lo_clamp = if_exprt{
                  binary_relation_exprt{wrapped, ID_lt, i64(0)},
                  i64(0),
                  wrapped};
                return if_exprt{
                  binary_relation_exprt{lo_clamp, ID_gt, olen}, olen, lo_clamp};
              };
              exprt lo = norm(json_member(slice_node, "lower"), i64(0));
              exprt hi = norm(json_member(slice_node, "upper"), olen);
              // hi >= lo (an empty slice when hi < lo).
              hi = if_exprt{binary_relation_exprt{hi, ID_lt, lo}, lo, hi};
              exprt removed = minus_exprt{hi, lo};
              exprt new_len = plus_exprt{minus_exprt{olen, removed}, rlen};
              emit_count_capacity_guard(
                pending_checks, new_len, PYTHON_MAX_LIST_LENGTH, loc);
              exprt::operandst elems;
              for(std::size_t k = 0; k < PYTHON_MAX_LIST_LENGTH; k++)
              {
                exprt kk = i64(static_cast<long>(k));
                // before the slice: a[k]
                exprt before = index_exprt{odata, kk};
                // within rhs: rhs[k - lo]
                exprt in_rhs = index_exprt{rdata, minus_exprt{kk, lo}};
                // after: a[hi + (k - lo - rlen)]
                exprt after = index_exprt{
                  odata,
                  plus_exprt{hi, minus_exprt{minus_exprt{kk, lo}, rlen}}};
                exprt sel = if_exprt{
                  binary_relation_exprt{kk, ID_lt, lo},
                  before,
                  if_exprt{
                    binary_relation_exprt{kk, ID_lt, plus_exprt{lo, rlen}},
                    in_rhs,
                    after}};
                elems.push_back(std::move(sel));
              }
              array_exprt new_data{std::move(elems), odt};
              exprt new_list = struct_exprt{{new_len, new_data}, obj.type()};
              code_frontend_assignt sa{obj, new_list};
              sa.add_source_location() = loc;
              block.add(std::move(sa));
              continue;
            }
          }
        }
      }
      // PLR object identity (§9): `g[i][j] = v` mutates the element `g[i]` of a
      // list `g` whose by-value mutable elements are aliased -- not modelled,
      // so report + cut. (`g[i] = v`, whole-slot reassignment, has
      // target.value == Name g, not a subscript, so it is NOT guarded.)
      if(is_aliased_list_element(json_member(target, "value")))
        emit_aliased_mutation_guard(loc);
      // PLR §3.3.1: custom __setitem__ dunder — if the
      // subscripted value is a user-defined class instance
      // with a __setitem__ method, dispatch to it.
      bool dispatched_setitem = false;
      {
        const jsont &target_value_root = json_member(target, "value");
        exprt obj_probe = convert_expression(target_value_root);
        if(!obj_probe.is_nil())
        {
          std::string tag;
          if(obj_probe.type().id() == ID_struct)
            tag = id2string(to_struct_type(obj_probe.type()).get_tag());
          else if(obj_probe.type().id() == ID_struct_tag)
            tag =
              id2string(to_struct_tag_type(obj_probe.type()).get_identifier());
          if(tag.substr(0, 13) == "python_class_")
          {
            std::string bare = tag.substr(13);
            for(const std::string &prefix :
                {std::string{"python::"} + tag + "::__setitem__",
                 std::string{"python::"} + bare + "::__setitem__"})
            {
              const symbolt *ss = symbol_table.lookup(irep_idt{prefix});
              if(ss != nullptr)
              {
                exprt slice = convert_expression(json_member(target, "slice"));
                if(!slice.is_nil())
                {
                  side_effect_expr_function_callt call{
                    ss->symbol_expr(),
                    {address_of_exprt{obj_probe}, slice, rhs},
                    empty_typet{},
                    loc};
                  block.add(code_expressiont{std::move(call)});
                }
                dispatched_setitem = true;
                break;
              }
            }
          }
          // PLR §3.3.1: obj[k] = v requires __setitem__. A concrete class whose
          // MRO defines none does not support item assignment -> TypeError.
          if(
            !dispatched_setitem &&
            concrete_class_lacks_dunder(obj_probe.type(), "__setitem__"))
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            dispatched_setitem = true;
          }
        }
      }
      if(dispatched_setitem)
        continue;
      // Nested subscript (e.g. d["a"][0] = v) — the inner
      // read returns a struct copy, so writing into it is
      // lost. Rewrite at statement level to:
      //     __nest_N = d["a"]
      //     __nest_N[0] = v
      //     d["a"] = __nest_N
      // This makes the mutation visible through the outer
      // container. Recursive: the final write back to
      // d["a"] goes through the same subscript-assign
      // path, so triple-nested targets unfold one level
      // per rewrite.
      const jsont &target_value = json_member(target, "value");
      if(is_node_type(target_value, "Subscript"))
      {
        exprt inner_read = convert_expression(target_value);
        if(
          !inner_read.is_nil() && (is_python_list_type(inner_read.type()) ||
                                   is_python_dict_type(inner_read.type())))
        {
          static unsigned nest_ctr = 0;
          std::string tn = "__nest_" + std::to_string(nest_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, inner_read.type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt tmp_sym = symbol_table.lookup_ref(ti).symbol_expr();
          // 1. tmp = d["a"] (snapshot read)
          block.add(code_frontend_assignt{tmp_sym, inner_read});
          // 2. tmp[slice] = rhs (first-level write)
          const jsont &slice_node = json_member(target, "slice");
          exprt key = convert_expression(slice_node);
          if(!key.is_nil())
          {
            if(is_python_list_type(tmp_sym.type()))
            {
              const auto &list_st = to_struct_type(tmp_sym.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              member_exprt data{tmp_sym, "data", data_type};
              exprt typed_rhs = rhs;
              if(typed_rhs.type() != data_type.element_type())
                typed_rhs = coerce_element(typed_rhs, data_type.element_type());
              block.add(code_frontend_assignt{
                index_exprt{data, key}, std::move(typed_rhs)});
            }
            else if(is_python_dict_type(tmp_sym.type()))
            {
              const auto &dict_st = to_struct_type(tmp_sym.type());
              const auto &keys_type =
                to_array_type(dict_st.components()[1].type());
              const auto &vals_type =
                to_array_type(dict_st.components()[2].type());
              member_exprt length{tmp_sym, "length", signedbv_typet{64}};
              member_exprt keys_arr{tmp_sym, "keys", keys_type};
              member_exprt vals_arr{tmp_sym, "values", vals_type};
              exprt typed_key = key;
              if(typed_key.type() != keys_type.element_type())
                typed_key = coerce_element(typed_key, keys_type.element_type());
              exprt typed_rhs = rhs;
              if(typed_rhs.type() != vals_type.element_type())
                typed_rhs = coerce_element(typed_rhs, vals_type.element_type());
              // scan-replace-or-append (mirrors the existing
              // dict-subscript-assign path).
              static unsigned ns_fnd = 0;
              std::string fn = "__nest_fnd_" + std::to_string(ns_fnd++);
              std::string fq = qualify_name(fn);
              irep_idt fi{fq};
              if(symbol_table.lookup(fi) == nullptr)
              {
                symbolt fs{fi, bool_typet{}, "python"};
                fs.base_name = fn;
                fs.is_lvalue = true;
                fs.is_state_var = true;
                symbol_table.add(fs);
              }
              symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
              block.add(code_frontend_assignt{found, false_exprt{}});
              for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt in_range = binary_relation_exprt{idx, ID_lt, length};
                exprt match = equal_exprt{
                  python_dict_unbox_key(index_exprt{keys_arr, idx}),
                  python_dict_unbox_key(typed_key)};
                code_blockt upd;
                upd.add(
                  code_frontend_assignt{index_exprt{vals_arr, idx}, typed_rhs});
                upd.add(code_frontend_assignt{found, true_exprt{}});
                block.add(
                  code_ifthenelset{and_exprt{in_range, match}, std::move(upd)});
              }
              code_blockt append;
              emit_capacity_guard(append, length, PYTHON_MAX_DICT_SIZE);
              append.add(code_frontend_assignt{
                index_exprt{keys_arr, length}, typed_key});
              append.add(code_frontend_assignt{
                index_exprt{vals_arr, length}, std::move(typed_rhs)});
              append.add(code_frontend_assignt{
                length,
                plus_exprt{length, from_integer(1, signedbv_typet{64})}});
              block.add(code_ifthenelset{not_exprt{found}, std::move(append)});
            }
          }
          // 3. d["a"] = tmp (write back via outer subscript).
          // Emit a synthetic subscript-assign recursively by
          // constructing an equivalent statement through the
          // same handler. We do this by building the outer
          // target's base and slice from target_value, and
          // driving the assign inline. Since the outer is
          // itself potentially a Subscript, recursion would be
          // ideal, but we inline one level for now.
          exprt outer_base =
            convert_expression(json_member(target_value, "value"));
          const jsont &outer_slice_node = json_member(target_value, "slice");
          exprt outer_key = convert_expression(outer_slice_node);
          if(
            !outer_base.is_nil() && !outer_key.is_nil() &&
            is_python_dict_type(outer_base.type()))
          {
            // Invalidate dict_literals tracking: the outer
            // container's 'a' slot now points to a mutated
            // inner container.
            const jsont &outer_val_node = json_member(target_value, "value");
            if(is_node_type(outer_val_node, "Name"))
            {
              std::string outer_name =
                json_string(json_member(outer_val_node, "id"));
              dict_literals.erase(irep_idt{qualify_name(outer_name)});
            }
            const auto &dict_st = to_struct_type(outer_base.type());
            const auto &keys_type =
              to_array_type(dict_st.components()[1].type());
            const auto &vals_type =
              to_array_type(dict_st.components()[2].type());
            member_exprt length{outer_base, "length", signedbv_typet{64}};
            member_exprt keys_arr{outer_base, "keys", keys_type};
            member_exprt vals_arr{outer_base, "values", vals_type};
            exprt outer_typed_key = outer_key;
            if(outer_typed_key.type() != keys_type.element_type())
              outer_typed_key =
                coerce_element(outer_typed_key, keys_type.element_type());
            exprt outer_rhs = tmp_sym;
            if(outer_rhs.type() != vals_type.element_type())
              outer_rhs = coerce_element(outer_rhs, vals_type.element_type());
            for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt match = equal_exprt{
                python_dict_unbox_key(index_exprt{keys_arr, idx}),
                python_dict_unbox_key(outer_typed_key)};
              block.add(code_ifthenelset{
                and_exprt{in_range, match},
                code_frontend_assignt{index_exprt{vals_arr, idx}, outer_rhs}});
            }
          }
          else if(
            !outer_base.is_nil() && !outer_key.is_nil() &&
            is_python_list_type(outer_base.type()))
          {
            const auto &list_st = to_struct_type(outer_base.type());
            const auto &data_type =
              to_array_type(list_st.components()[1].type());
            member_exprt data{outer_base, "data", data_type};
            exprt outer_rhs = tmp_sym;
            if(outer_rhs.type() != data_type.element_type())
              outer_rhs = coerce_element(outer_rhs, data_type.element_type());
            block.add(code_frontend_assignt{
              index_exprt{data, outer_key}, std::move(outer_rhs)});
          }
          continue;
        }
      }
      exprt obj = convert_expression(json_member(target, "value"));

      // Immutable-sequence item assignment → raise TypeError.
      // PLR §3.2: tuples and strings are immutable sequences, so
      // `t[i] = v` / `s[i] = v` always raise TypeError.
      if(
        !obj.is_nil() &&
        (is_python_tuple_type(obj.type()) || is_python_string_type(obj.type())))
      {
        code_blockt type_error;
        const symbolt *exc_sym =
          symbol_table.lookup("python::__exception_active");
        const symbolt *exc_type_sym =
          symbol_table.lookup("python::__exception_type");
        if(exc_sym != nullptr)
        {
          type_error.add(
            code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
        }
        if(exc_type_sym != nullptr)
        {
          // TypeError hash
          long type_hash = exception_type_hash("TypeError");
          type_error.add(code_frontend_assignt{
            exc_type_sym->symbol_expr(),
            from_integer(type_hash, python_int_type())});
        }
        block.add(std::move(type_error));
        continue;
      }

      // Dict subscript assignment: d["key"] = value
      if(!obj.is_nil() && is_python_dict_type(obj.type()))
      {
        const jsont &slice_node = json_member(target, "slice");
        exprt key = convert_expression(slice_node);
        if(!key.is_nil())
        {
          // PLR §3.2: list/dict/set are unhashable, so using one as
          // a dict key raises TypeError.
          if(is_unhashable_type(key.type()))
          {
            code_blockt type_error;
            const symbolt *ea =
              symbol_table.lookup("python::__exception_active");
            const symbolt *et = symbol_table.lookup("python::__exception_type");
            if(ea != nullptr)
              type_error.add(
                code_frontend_assignt{ea->symbol_expr(), true_exprt{}});
            if(et != nullptr)
              type_error.add(code_frontend_assignt{
                et->symbol_expr(),
                from_integer(
                  exception_type_hash("TypeError"), python_int_type())});
            block.add(std::move(type_error));
            continue;
          }
          // First `d[k] = v` on an unannotated empty dict: rebuild the
          // dict with the actual key/value types so e.g. distinct int
          // keys aren't lossily coerced to the default str key type
          // (which collapses them and corrupts the found-search).
          // Sound because the dict is still empty (it was just `{}`).
          // Only outside loops: inside a loop the rebuild's re-init
          // would run every iteration; the loop case is handled by
          // look-ahead at the `d = {}` creation site instead.
          if(
            loop_depth == 0 && obj.id() == ID_symbol &&
            empty_dict_pending.count(to_symbol_expr(obj).get_identifier()))
          {
            irep_idt did = to_symbol_expr(obj).get_identifier();
            const auto &cs = to_struct_type(obj.type());
            const typet ck =
              to_array_type(cs.components()[1].type()).element_type();
            const typet cv =
              to_array_type(cs.components()[2].type()).element_type();
            auto storable = [this](const typet &t)
            {
              return t.id() == ID_signedbv || t.id() == ID_integer ||
                     t.id() == ID_floatbv || t.id() == ID_bool ||
                     is_python_string_type(t);
            };
            const typet nk = key.type();
            const typet nv = rhs.type();
            if((nk != ck || nv != cv) && storable(nk) && storable(nv))
            {
              struct_typet ndt = python_dict_type(nk, nv);
              symbol_table.get_writeable_ref(did).type = ndt;
              obj = symbol_table.lookup_ref(did).symbol_expr();
              block.add(code_frontend_assignt{obj, safe_zero(ndt)});
            }
            empty_dict_pending.erase(did);
          }
          const auto &dict_st = to_struct_type(obj.type());
          const auto &keys_type = to_array_type(dict_st.components()[1].type());
          const auto &vals_type = to_array_type(dict_st.components()[2].type());

          // --python-check-annotations: storing a value whose type is
          // definitely incompatible with a dict's CONCRETE value-element type
          // (`d: dict[str, int]; d[k] = "s"`) is an annotation mismatch -- the
          // dict-store analog of the list `append` check (ty-007). Opt-in only
          // (legal at runtime; the error arises on a later USE), runtime-legal
          // so gated behind the flag like the call-arg / assign / append checks.
          // A `python_value` (Any) value-type accepts anything, so never
          // mismatches. `rhs` is already converted once here -- no double-eval.
          if(
            python_check_annotations &&
            !is_python_value_type(vals_type.element_type()) && !rhs.is_nil() &&
            obj.id() == ID_symbol &&
            variable_annotations.count(to_symbol_expr(obj).get_identifier()) &&
            annotation_types_incompatible(vals_type.element_type(), rhs.type()))
            add_check(
              false_exprt{},
              "annotation-mismatch",
              "stored dict value type does not match dict value annotation",
              loc);

          // PLR §3.1, §3.2: when the stored value's type doesn't
          // match the declared element type and both the dict
          // and the key are statically resolvable, record the
          // original RHS in `dict_runtime_value_overrides` so
          // subsequent `d[<same constant>]` reads see the
          // actual stored value, not a coerced nondet. When the
          // key is non-constant, the assignment could affect any
          // existing entry, so we conservatively clear all
          // overrides for this dict.
          if(obj.id() == ID_symbol)
          {
            irep_idt did = to_symbol_expr(obj).get_identifier();
            bool key_is_constant = is_node_type(slice_node, "Constant");
            if(!key_is_constant)
            {
              dict_runtime_value_overrides.erase(did);
            }
            else if(rhs.type() != vals_type.element_type())
            {
              const jsont &kv = json_member(slice_node, "value");
              std::string key_repr;
              if(kv.is_string())
                key_repr = "s:" + kv.value;
              else if(kv.is_number())
                key_repr = "n:" + kv.value;
              else if(kv.is_true())
                key_repr = "b:1";
              else if(kv.is_false())
                key_repr = "b:0";
              if(!key_repr.empty())
                dict_runtime_value_overrides[did][key_repr] = rhs;
            }
            else
            {
              // Type matches: clear any prior override for this
              // key (the runtime value now agrees with the
              // declared type).
              const jsont &kv = json_member(slice_node, "value");
              std::string key_repr;
              if(kv.is_string())
                key_repr = "s:" + kv.value;
              else if(kv.is_number())
                key_repr = "n:" + kv.value;
              else if(kv.is_true())
                key_repr = "b:1";
              else if(kv.is_false())
                key_repr = "b:0";
              if(!key_repr.empty())
              {
                auto it = dict_runtime_value_overrides.find(did);
                if(it != dict_runtime_value_overrides.end())
                  it->second.erase(key_repr);
              }
            }
          }

          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt keys_arr{obj, "keys", keys_type};
          member_exprt vals_arr{obj, "values", vals_type};
          exprt typed_key = key;
          {
            const typet lkt =
              python_dict_logical_key_type(keys_type.element_type());
            if(typed_key.type() != lkt)
              typed_key = coerce_element(typed_key, lkt);
          }
          exprt typed_val = rhs;
          if(typed_val.type() != vals_type.element_type())
            typed_val = coerce_element(typed_val, vals_type.element_type());
          static unsigned dict_assign_ctr = 0;
          std::string fn = "__dict_found_" + std::to_string(dict_assign_ctr++);
          std::string fq = qualify_name(fn);
          irep_idt fi{fq};
          if(symbol_table.lookup(fi) == nullptr)
          {
            symbolt fs{fi, bool_typet{}, "python"};
            fs.base_name = fn;
            fs.is_lvalue = true;
            fs.is_state_var = true;
            symbol_table.add(fs);
          }
          symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
          block.add(code_frontend_assignt{found, false_exprt{}});
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt match = equal_exprt{
              python_dict_unbox_key(index_exprt{keys_arr, idx}), typed_key};
            code_blockt update;
            update.add(
              code_frontend_assignt{index_exprt{vals_arr, idx}, typed_val});
            update.add(code_frontend_assignt{found, true_exprt{}});
            block.add(
              code_ifthenelset{and_exprt{in_range, match}, std::move(update)});
          }
          code_blockt append;
          emit_capacity_guard(append, length, PYTHON_MAX_DICT_SIZE);
          append.add(code_frontend_assignt{
            index_exprt{keys_arr, length}, box_string_for_storage(typed_key)});
          append.add(
            code_frontend_assignt{index_exprt{vals_arr, length}, typed_val});
          append.add(code_frontend_assignt{
            length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
          block.add(code_ifthenelset{not_exprt{found}, std::move(append)});

          // dict_literals update: if obj is a symbol and the
          // assigned key is a compile-time constant string,
          // add the key to the tracked key-set. Later
          // subscript reads of the same key can then prove
          // the key exists. Value-tracking uses typed_val
          // when it's a constant, otherwise safe_zero.
          if(
            obj.id() == ID_symbol &&
            json_string(json_member(slice_node, "_type")) == "Constant")
          {
            auto key_str = extract_string_value(key);
            // Non-string constant key (int / bool): the dict_literals
            // key-array const-fold machinery below is string-keyed and cannot
            // update an int/bool entry in place, so it would leave a STALE
            // construction value (`d = {1: 10}; d[1] = 20; d[1]` folded to 10).
            // Drop the const-fold for this dict instead -> the subsequent
            // `d[<int key>]` read falls back to the runtime values array, which
            // the store above DID update, giving the correct value. (String
            // keys take the precise in-place update path below.)
            if(!key_str.has_value())
              dict_literals.erase(to_symbol_expr(obj).get_identifier());
            if(key_str.has_value())
            {
              irep_idt obj_id = to_symbol_expr(obj).get_identifier();
              auto dli = dict_literals.find(obj_id);
              if(dli != dict_literals.end())
              {
                // Append key to the literal's key array; grow length.
                exprt &dlit = dli->second;
                if(
                  dlit.id() == ID_struct && dlit.operands().size() >= 3 &&
                  dlit.operands()[0].is_constant())
                {
                  mp_integer cur_len;
                  if(!to_integer(to_constant_expr(dlit.operands()[0]), cur_len))
                  {
                    std::size_t idx = cur_len.to_ulong();
                    if(idx < PYTHON_MAX_DICT_SIZE)
                    {
                      // Check if the key already exists — if so,
                      // update the value in place, otherwise
                      // append. The dict_literals entry tracks
                      // BOTH keys and values; for the
                      // required-kwarg check (which cares only
                      // about key presence), losing the value
                      // tracking on a non-constant assignment is
                      // fine, but losing the KEY tracking would
                      // be incorrect — we KNOW the key exists.
                      // For non-constant values we therefore
                      // record the key and leave the value
                      // entry at whatever placeholder the dict
                      // literal already had (typically a
                      // safe_zero of the value-array element
                      // type from the original Dict literal
                      // construction in convert_dict).
                      bool have_key = false;
                      for(std::size_t j = 0; j < idx; j++)
                      {
                        auto ex = extract_string_value(
                          dlit.operands()[1].operands()[j]);
                        if(ex.has_value() && ex.value() == key_str.value())
                        {
                          have_key = true;
                          if(typed_val.is_constant())
                            dlit.operands()[2].operands()[j] = typed_val;
                          // else: leave existing value entry
                          // alone; key tracking is preserved.
                          break;
                        }
                      }
                      if(!have_key)
                      {
                        // Typecast the key literal to match
                        // the keys-array element type (the
                        // original convert_dict pass already
                        // typecast all keys to a uniform type
                        // — typically python_string but
                        // possibly tagged-union for
                        // dict[object, ...]).
                        const auto &keys_arr_type =
                          to_array_type(dlit.operands()[1].type());
                        exprt key_lit = python_string_literal(key_str.value());
                        if(key_lit.type() != keys_arr_type.element_type())
                          key_lit = safe_typecast(
                            key_lit, keys_arr_type.element_type());
                        dlit.operands()[1].operands()[idx] = key_lit;
                        if(typed_val.is_constant())
                          dlit.operands()[2].operands()[idx] = typed_val;
                        // else: leave value-array's existing
                        // entry (a safe_zero from the dict's
                        // construction) at this index; key
                        // tracking is preserved for the
                        // required-kwarg check.
                        dlit.operands()[0] =
                          from_integer(cur_len + 1, signedbv_typet{64});
                      }
                    }
                  }
                }
              }
            }
          }
          continue;
        }
      }
      // PLR §3.1: subscript-assign through a python_value (with LIST
      // or DICT tag). `outer[i]` returned a python_value whose
      // __list_ptr / __class_ptr points to inner's storage. We must
      // write through the deref so the mutation propagates to inner.
      if(!obj.is_nil() && is_python_value_type(obj.type()))
      {
        // Probe the slice type to decide between LIST and DICT.
        const jsont &slice_node = json_member(target, "slice");
        exprt slice_probe = convert_expression(slice_node);
        bool string_key =
          !slice_probe.is_nil() && is_python_string_type(slice_probe.type());

        // Try LIST tag first (integer slice).
        if(!string_key)
        {
          exprt list_val = python_value_list(obj);
          if(is_python_list_type(list_val.type()))
          {
            const auto &list_st = to_struct_type(list_val.type());
            const auto &data_type =
              to_array_type(list_st.components()[1].type());
            member_exprt data{list_val, "data", data_type};
            index_exprt lhs{data, slice_probe};
            // Inner is list[python_value]; the rhs must be a python_value.
            exprt typed_rhs = rhs;
            if(typed_rhs.type() != data_type.element_type())
              typed_rhs = wrap_value(typed_rhs);
            code_frontend_assignt assign{lhs, std::move(typed_rhs)};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
            continue;
          }
        }
        // DICT tag: the dict's storage is reached via __class_ptr cast.
        // Canonical dict type is `dict[refined_string, python_value]`.
        if(string_key)
        {
          typet dict_type =
            python_dict_type(python_string_type(), python_value_type());
          exprt class_ptr = python_value_class_ptr(obj);
          pointer_typet dict_ptr_type{dict_type, 64};
          dereference_exprt dict_val{
            typecast_exprt{class_ptr, dict_ptr_type}, dict_type};
          const auto &dict_st = to_struct_type(dict_type);
          const auto &keys_type = to_array_type(dict_st.components()[1].type());
          const auto &vals_type = to_array_type(dict_st.components()[2].type());
          member_exprt length{dict_val, "length", signedbv_typet{64}};
          member_exprt keys_arr{dict_val, "keys", keys_type};
          member_exprt vals_arr{dict_val, "values", vals_type};
          exprt typed_key = slice_probe;
          if(typed_key.type() != keys_type.element_type())
            typed_key = coerce_element(typed_key, keys_type.element_type());
          exprt typed_val = rhs;
          if(typed_val.type() != vals_type.element_type())
            typed_val = wrap_value(typed_val);
          for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt match = equal_exprt{
              python_dict_unbox_key(index_exprt{keys_arr, idx}),
              python_dict_unbox_key(typed_key)};
            block.add(code_ifthenelset{
              and_exprt{in_range, match},
              code_frontend_assignt{index_exprt{vals_arr, idx}, typed_val}});
          }
          continue;
        }
      }
      if(!obj.is_nil() && is_python_list_type(obj.type()))
      {
        const jsont &slice_node = json_member(target, "slice");
        exprt idx = convert_expression(slice_node);
        const auto &list_st = to_struct_type(obj.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{obj, "data", data_type};
        member_exprt length{obj, "length", signedbv_typet{64}};
        // PLR §6.10.1: list subscript assignment to an
        // out-of-range index raises IndexError. Python permits
        // negative indices (counting from the end); adjust before
        // the bounds check and store. Mirrors the read path in
        // convert_subscript — without this the write path
        // silently wrote into the size-64 backing array and
        // missed the runtime error (a false negative).
        exprt eff_idx = idx;
        if(idx.is_constant())
        {
          mp_integer iv;
          if(!to_integer(to_constant_expr(idx), iv) && iv < 0)
            eff_idx = plus_exprt{length, idx};
        }
        else
        {
          eff_idx = if_exprt{
            binary_relation_exprt{idx, ID_lt, safe_zero(idx.type())},
            plus_exprt{length, idx},
            idx};
        }
        {
          const symbolt *exc_sym =
            symbol_table.lookup("python::__exception_active");
          const symbolt *exc_type_sym =
            symbol_table.lookup("python::__exception_type");
          if(exc_sym != nullptr)
          {
            exprt in_range = and_exprt{
              binary_relation_exprt{eff_idx, ID_ge, safe_zero(eff_idx.type())},
              binary_relation_exprt{eff_idx, ID_lt, length}};
            exprt out_of_range = not_exprt{in_range};
            block.add(code_frontend_assignt{
              exc_sym->symbol_expr(),
              or_exprt{exc_sym->symbol_expr(), out_of_range}});
            if(exc_type_sym != nullptr)
            {
              long h = exception_type_hash("IndexError");
              block.add(code_frontend_assignt{
                exc_type_sym->symbol_expr(),
                if_exprt{
                  out_of_range,
                  from_integer(h, exc_type_sym->type),
                  exc_type_sym->symbol_expr()}});
            }
          }
        }
        index_exprt lhs{data, eff_idx};
        exprt typed_rhs = rhs;
        // PLR §3.1: coerce a SCALAR RHS into the list's element
        // type. For a list[python_value] target (e.g. a bare-
        // `list` parameter the callee mutates via `lst[i] = v`),
        // coerce_element wraps the scalar into the tagged union;
        // a plain typecast would bit-reinterpret the int as a
        // python_value struct, corrupting the stored value (so a
        // later read of `.__int_val` saw garbage). Restricted to
        // scalar RHS: a container RHS (list/dict/struct) keeps the
        // existing typecast path so the self-referential
        // value-copy semantics (documented in the soundness
        // pending suite) are unchanged.
        if(typed_rhs.type() != data_type.element_type())
        {
          const typet &rt = typed_rhs.type();
          // SPIKE (--python-ref-mutables): a mutable list RHS stored into a
          // python_value element slot is given a FRESH per-instance heap
          // reference (mirrors convert_list, canonicalised to
          // list[python_value] so python_value_list reads it back correctly).
          // Reassignment then rebinds the slot to a NEW object, so any
          // previously-extracted alias keeps pointing at the OLD object --
          // exactly CPython reference semantics for `c[i] = <new list>` (the
          // slot-aliasing approach got this wrong). A named-symbol RHS is left
          // to the existing escaped-mutable path; this covers literal /
          // freshly-built lists. Scope: lists only (dicts/sets are phase 3).
          if(
            ref_mutables && typed_rhs.id() != ID_symbol &&
            is_python_value_type(data_type.element_type()) &&
            is_python_list_type(rt))
          {
            exprt canon = rebuild_list_as_pv(typed_rhs);
            typed_rhs = make_python_value(
              python_type_tagt::LIST, allocate_boxed_leaf(canon, canon.type()));
          }
          else
          {
            bool scalar_rhs =
              rt.id() == ID_signedbv || rt.id() == ID_unsignedbv ||
              rt.id() == ID_floatbv || rt.id() == ID_bool ||
              rt.id() == ID_integer || is_python_string_type(rt);
            if(scalar_rhs && is_python_value_type(data_type.element_type()))
              typed_rhs = coerce_element(typed_rhs, data_type.element_type());
            else
              typed_rhs = typecast_exprt{typed_rhs, data_type.element_type()};
          }
        }
        code_frontend_assignt assign{lhs, typed_rhs};
        assign.add_source_location() = loc;
        block.add(std::move(assign));
        continue;
      }
    }

    // Handle attribute assignment: self.x = value
    if(is_node_type(target, "Attribute"))
    {
      exprt obj = convert_expression(json_member(target, "value"));
      std::string attr = json_string(json_member(target, "attr"));
      if(!obj.is_nil())
      {
        // If obj is a pointer (self in a method), dereference it
        typet obj_type = obj.type();
        // PLR §3.3.2: a DATA descriptor (class attr whose class defines
        // __set__) intercepts the assignment — route to __set__ instead
        // of storing into the descriptor field.
        {
          std::string dcls;
          exprt dobj;
          if(
            obj_type.id() == ID_pointer &&
            to_pointer_type(obj_type).base_type().id() == ID_struct)
          {
            dcls = id2string(
              to_struct_type(to_pointer_type(obj_type).base_type()).get_tag());
            dobj = obj;
          }
          else if(obj_type.id() == ID_struct)
          {
            dcls = id2string(to_struct_type(obj_type).get_tag());
            dobj = address_of_exprt{obj};
          }
          if(dcls.substr(0, 13) == "python_class_")
            dcls = dcls.substr(13);
          if(!dcls.empty())
          {
            // @property setter (a data descriptor): `obj.<attr> = v` invokes
            // the setter (with its side effects) instead of a shadowing store.
            if(auto ps = emit_property_set(dcls, attr, dobj, rhs, loc))
            {
              ps->add_source_location() = loc;
              block.add(std::move(*ps));
              continue;
            }
            if(auto ds = emit_descriptor_set(dcls, attr, dobj, rhs, loc))
            {
              ds->add_source_location() = loc;
              block.add(std::move(*ds));
              continue;
            }
          }
        }
        if(obj_type.id() == ID_pointer)
        {
          const auto &base = to_pointer_type(obj_type).base_type();
          if(base.id() == ID_struct)
          {
            const auto &st = to_struct_type(base);
            if(st.has_component(attr))
            {
              dereference_exprt deref{obj};
              member_exprt lhs{deref, attr, st.get_component(attr).type()};
              exprt typed_rhs = rhs;
              // PLR §3.1: list[python_value] LHS receiving a
              // list with a more specific element type — rebuild
              // RHS so element types match (list_extend / append
              // cprover_string_equal etc. all assume matching
              // element types between LHS and RHS).
              if(
                is_python_list_type(lhs.type()) &&
                is_python_list_type(typed_rhs.type()))
              {
                const auto &lhs_data = to_array_type(
                  to_struct_type(lhs.type()).components()[1].type());
                if(is_python_value_type(lhs_data.element_type()))
                  typed_rhs = rebuild_list_as_pv(typed_rhs);
              }
              // PLR §3.2: empty-dict literal RHS bound to a
              // typed dict[K, V] LHS — rebuild as zero-init
              // struct of LHS type so the keys/values arrays
              // carry the LHS's element types. Without this,
              // the RHS struct (built with default
              // dict[str, int] from convert_dict's empty path)
              // is just bit-cast to the LHS type, leaving the
              // stored keys as misaligned string-shape values.
              if(
                is_python_dict_type(lhs.type()) &&
                is_python_dict_type(typed_rhs.type()) &&
                typed_rhs.type() != lhs.type() && typed_rhs.id() == ID_struct &&
                !typed_rhs.operands().empty() &&
                typed_rhs.operands()[0].is_constant())
              {
                mp_integer rhs_len;
                if(
                  !to_integer(
                    to_constant_expr(typed_rhs.operands()[0]), rhs_len) &&
                  rhs_len == 0)
                  typed_rhs = safe_zero(lhs.type());
              }
              if(typed_rhs.type() != lhs.type())
                typed_rhs = safe_typecast(typed_rhs, lhs.type());
              code_frontend_assignt assign{lhs, typed_rhs};
              assign.add_source_location() = loc;
              block.add(std::move(assign));
              if(auto shadow = maybe_shadow_assign(deref, attr))
              {
                shadow->add_source_location() = loc;
                block.add(std::move(*shadow));
              }
              continue;
            }
          }
        }
        else if(obj_type.id() == ID_struct)
        {
          const auto &st = to_struct_type(obj_type);
          if(st.has_component(attr))
          {
            member_exprt lhs{obj, attr, st.get_component(attr).type()};
            exprt typed_rhs = rhs;
            // PLR §3.1: same list[python_value] element rebuild
            // as the pointer-base path above.
            if(
              is_python_list_type(lhs.type()) &&
              is_python_list_type(typed_rhs.type()))
            {
              const auto &lhs_data = to_array_type(
                to_struct_type(lhs.type()).components()[1].type());
              if(is_python_value_type(lhs_data.element_type()))
                typed_rhs = rebuild_list_as_pv(typed_rhs);
            }
            // PLR §3.2: empty-dict rebuild — see comment in
            // pointer-base branch above.
            if(
              is_python_dict_type(lhs.type()) &&
              is_python_dict_type(typed_rhs.type()) &&
              typed_rhs.type() != lhs.type() && typed_rhs.id() == ID_struct &&
              !typed_rhs.operands().empty() &&
              typed_rhs.operands()[0].is_constant())
            {
              mp_integer rhs_len;
              if(
                !to_integer(
                  to_constant_expr(typed_rhs.operands()[0]), rhs_len) &&
                rhs_len == 0)
                typed_rhs = safe_zero(lhs.type());
            }
            if(typed_rhs.type() != lhs.type())
              typed_rhs = safe_typecast(typed_rhs, lhs.type());
            code_frontend_assignt assign{lhs, typed_rhs};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
            if(auto shadow = maybe_shadow_assign(obj, attr))
            {
              shadow->add_source_location() = loc;
              block.add(std::move(*shadow));
            }
            continue;
          }
        }
        // Tagged-union base: route through __class_ptr. Same
        // selection logic as convert_attribute's read path.
        else if(
          obj_type.id() == ID_struct_tag &&
          id2string(to_struct_tag_type(obj_type).get_identifier()) ==
            std::string{PYTHON_VALUE_TAG})
        {
          bool done = false;
          for(const auto &[cls_name, cls_type] : class_types)
          {
            if(!cls_type.has_component(attr))
              continue;
            const typet &field_type = cls_type.get_component(attr).type();
            exprt class_ptr = python_value_class_ptr(obj);
            pointer_typet cls_ptr_type{cls_type, 64};
            dereference_exprt deref{
              typecast_exprt{class_ptr, cls_ptr_type}, cls_type};
            member_exprt lhs{deref, attr, field_type};
            exprt typed_rhs = rhs;
            if(typed_rhs.type() != lhs.type())
              typed_rhs = safe_typecast(typed_rhs, lhs.type());
            code_frontend_assignt assign{lhs, typed_rhs};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
            // PLR §9.4: also set the shadow flag through the
            // tagged-union path so subsequent reads on the
            // same underlying class instance see the
            // instance value rather than falling back to
            // class storage.
            if(auto shadow = maybe_shadow_assign(deref, attr))
            {
              shadow->add_source_location() = loc;
              block.add(std::move(*shadow));
            }
            done = true;
            break;
          }
          if(done)
            continue;
        }
      }
    }

    std::string var_name = json_string(json_member(target, "id"));
    if(var_name.empty())
      continue; // Not a Name target — already handled above
    std::string qualified_name = qualify_name(var_name);
    irep_idt symbol_id{qualified_name};

    // PLR §3.1: storage promotion for escaped mutables.
    // If this name is in `escaped_mutables` (appears in some
    // List/Dict literal elsewhere) and the RHS is a list whose
    // element type is not already python_value, rebuild the RHS
    // as `list[python_value]`. This makes the deref-cast in
    // python_value_list type-correct when this storage is
    // referenced via `make_python_value(LIST, &this_symbol)`.
    if(escaped_mutables.count(symbol_id) > 0 && is_python_list_type(rhs.type()))
    {
      const auto &src_st = to_struct_type(rhs.type());
      const auto &src_data_type = to_array_type(src_st.components()[1].type());
      if(!is_python_value_type(src_data_type.element_type()))
        rhs = rebuild_list_as_pv(rhs);
    }

    // Check if variable exists and has a different type (type change)
    const symbolt *existing = symbol_table.lookup(symbol_id);
    // Also check the versioned symbol
    auto ver_it = variable_versions.find(qualified_name);
    if(ver_it != variable_versions.end())
      existing = symbol_table.lookup(ver_it->second);

    // PLR §3.1: --python-check-annotations reassignment check.
    // When the variable was previously annotated (count: int =
    // 10) and a subsequent plain assign rebinds it to an
    // incompatible type (count = "wrong"), emit an
    // annotation-mismatch property. Performed BEFORE the
    // type-promotion / widening below so we see the original
    // annotation rather than whatever the symbol has been
    // widened to.
    if(python_check_annotations)
    {
      auto va = variable_annotations.find(symbol_id);
      if(
        va != variable_annotations.end() &&
        annotation_types_incompatible(va->second, rhs.type()))
      {
        add_check(
          false_exprt{},
          "annotation-mismatch",
          "assigned value's type does not match declared annotation of '" +
            var_name + "'",
          loc);
      }
    }

    bool rhs_has_side_effect = rhs.id() == ID_side_effect;

    // PLR §6.2 — Assignment statements: a plain assignment binds
    // the name to the value, with the value's runtime type. The
    // pre-pass (pass 0 in convert()) had to register module-level
    // globals before the RHS was converted, so unannotated names
    // get a tentative placeholder type (typically int). On the
    // FIRST pass-2 assignment to such a symbol we refine its
    // type to the actual RHS type and drop it from the
    // unannotated_globals set. Subsequent rebinds then take the
    // normal type-mismatch path (cast / versioning / wrap into
    // tagged union), matching Python's dynamic-typing semantics
    // where `x = 10; x = math.inf` is ambiguous and the existing
    // logic resolves to a single chosen target type.
    //
    // Refining only on the first assignment is essential: by the
    // time later assignments run, earlier `ASSIGN result := 10`
    // statements have already been emitted referencing the
    // symbol with its prior type. Changing the symbol's type
    // after such an emission produces a goto with type-
    // inconsistent ASSIGN/symbol pairs, which the symex code
    // path cannot interpret coherently.
    if(
      existing != nullptr && unannotated_globals.count(existing->name) > 0 &&
      rhs.type().id() != ID_empty && !rhs.is_nil())
    {
      if(existing->type != rhs.type())
      {
        symbol_table.get_writeable_ref(existing->name).type = rhs.type();
        existing = symbol_table.lookup(existing->name);
      }
      unannotated_globals.erase(existing->name);
    }

    if(
      existing != nullptr && existing->type != rhs.type() &&
      rhs.type().id() != ID_empty && !rhs.is_nil())
    {
      // If both types are numeric (int/float/bool), typecast the RHS
      // to match the existing variable's type. This avoids creating
      // versioned variables that cause type mismatches at merge points
      // (e.g., exception handlers).
      bool src_numeric =
        rhs.type().id() == ID_signedbv || rhs.type().id() == ID_floatbv ||
        rhs.type().id() == ID_bool || rhs.type().id() == ID_integer;
      bool tgt_numeric = existing->type.id() == ID_signedbv ||
                         existing->type.id() == ID_floatbv ||
                         existing->type.id() == ID_bool ||
                         existing->type.id() == ID_integer;
      // Also treat python_value_type → numeric as compatible
      // (unwrap the tagged union to the target type)
      if(is_python_value_type(rhs.type()) && tgt_numeric)
      {
        rhs = unwrap_value(rhs, existing->type);
      }
      else if(src_numeric && tgt_numeric)
      {
        rhs = safe_typecast(rhs, existing->type);
        // Fall through to normal assignment below
      }
      // numeric → python_value_type: wrap
      else if(src_numeric && is_python_value_type(existing->type))
      {
        rhs = wrap_value(rhs);
      }
      // python_value_type → struct: unwrap or nondet
      else if(
        is_python_value_type(rhs.type()) && existing->type.id() == ID_struct)
      {
        rhs = safe_typecast(rhs, existing->type);
      }
      // struct → different struct: version the variable for class types
      else if(
        rhs.type().id() == ID_struct && existing->type.id() == ID_struct &&
        rhs.type() != existing->type)
      {
        // For class instances (have __class_tag), create versioned variable
        if(
          to_struct_type(rhs.type()).has_component("__class_tag") ||
          to_struct_type(existing->type).has_component("__class_tag"))
        {
          unsigned &ver = version_counters[qualified_name];
          ver++;
          std::string versioned_name =
            qualified_name + "__v" + std::to_string(ver);
          irep_idt versioned_id{versioned_name};
          symbolt new_symbol{versioned_id, rhs.type(), "python"};
          new_symbol.base_name = var_name + "__v" + std::to_string(ver);
          new_symbol.location = loc;
          new_symbol.is_lvalue = true;
          new_symbol.is_state_var = true;
          symbol_table.add(new_symbol);
          variable_versions[qualified_name] = versioned_id;
          const symbolt &new_sym = symbol_table.lookup_ref(versioned_id);
          code_frontend_assignt assign{new_sym.symbol_expr(), rhs};
          assign.add_source_location() = loc;
          block.add(std::move(assign));
          continue;
        }
        rhs = safe_typecast(rhs, existing->type);
      }
      else if(if_else_depth > 0)
      {
        // Inside if/else: type change at a branch point.
        // Use python_value_type for the variable.
        // Create a tagged-union variable and wrap the value.
        typet val_type = python_value_type();
        unsigned &ver = version_counters[qualified_name];
        ver++;
        std::string versioned_name =
          qualified_name + "__v" + std::to_string(ver);
        irep_idt versioned_id{versioned_name};

        symbolt new_symbol{versioned_id, val_type, "python"};
        new_symbol.base_name = var_name + "__v" + std::to_string(ver);
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        symbol_table.add(new_symbol);

        variable_versions[qualified_name] = versioned_id;

        // Wrap the value in a tagged union
        python_type_tagt tag = python_type_tagt::INT;
        if(rhs.type().id() == ID_floatbv)
          tag = python_type_tagt::FLOAT;
        else if(rhs.type().id() == ID_bool)
          tag = python_type_tagt::BOOL;

        struct_exprt wrapped = make_python_value(tag, rhs);
        const symbolt &new_sym = symbol_table.lookup_ref(versioned_id);
        code_frontend_assignt assign{new_sym.symbol_expr(), wrapped};
        assign.add_source_location() = loc;
        block.add(std::move(assign));
        continue;
      }
      else
      {
        // Straight-line code: create a fresh versioned symbol
        unsigned &ver = version_counters[qualified_name];
        ver++;
        std::string versioned_name =
          qualified_name + "__v" + std::to_string(ver);
        irep_idt versioned_id{versioned_name};

        symbolt new_symbol{versioned_id, rhs.type(), "python"};
        new_symbol.base_name = var_name + "__v" + std::to_string(ver);
        new_symbol.location = loc;
        new_symbol.is_lvalue = true;
        new_symbol.is_state_var = true;
        new_symbol.is_static_lifetime = current_function.empty();
        symbol_table.add(new_symbol);

        // Update the version mapping
        variable_versions[qualified_name] = versioned_id;

        const symbolt &new_sym = symbol_table.lookup_ref(versioned_id);
        code_frontend_assignt assign{new_sym.symbol_expr(), rhs};
        assign.add_source_location() = loc;
        block.add(std::move(assign));
        // Track string constants for versioned variables
        if(is_python_string_type(rhs.type()))
        {
          auto sv = extract_string_value(rhs);
          if(!sv.has_value() && rhs.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(rhs).get_identifier());
            if(it != string_constants.end())
              sv = it->second;
          }
          if(sv.has_value())
            string_constants[versioned_id] = sv.value();
        }
        continue;
      }
    }

    if(symbol_table.lookup(symbol_id) == nullptr)
    {
      symbolt new_symbol{symbol_id, rhs.type(), "python"};
      new_symbol.base_name = var_name;
      new_symbol.location = loc;
      new_symbol.is_lvalue = true;
      new_symbol.is_state_var = true;
      new_symbol.is_static_lifetime = current_function.empty();
      symbol_table.add(new_symbol);
    }

    // PLR §3.1: if the RHS is pointer-to-list/dict (from a function
    // that returns its parameter pointer, or from a ternary that
    // forwards a pointer), promote the LHS symbol to the same pointer
    // type so the binding is an alias, not a struct copy.
    if(
      rhs.type().id() == ID_pointer &&
      (is_python_list_type(to_pointer_type(rhs.type()).base_type()) ||
       is_python_dict_type(to_pointer_type(rhs.type()).base_type()) ||
       is_instance_pointer(rhs.type())))
    {
      symbolt &lhs_sym = symbol_table.get_writeable_ref(symbol_id);
      if(lhs_sym.type != rhs.type())
        lhs_sym.type = rhs.type();
      code_frontend_assignt assign{lhs_sym.symbol_expr(), rhs};
      assign.add_source_location() = loc;
      block.add(std::move(assign));
      continue;
    }

    const symbolt &sym = symbol_table.lookup_ref(symbol_id);
    exprt typed_rhs = rhs;
    if(typed_rhs.type() != sym.type)
      typed_rhs = safe_typecast(typed_rhs, sym.type);

    // Inside a try block with a function call RHS: split into
    // call-into-temp + guarded-assign so that if the call raises,
    // the assignment is skipped.
    if(
      try_depth > 0 && (typed_rhs.id() == ID_side_effect ||
                        rhs_has_side_effect || !pending_checks.empty()))
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      if(exc_sym != nullptr)
      {
        // Evaluate the RHS (may contain function call)
        static unsigned try_tmp_counter = 0;
        std::string tmp_name = "__try_tmp_" + std::to_string(try_tmp_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, typed_rhs.type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);

        // Flush pending_checks (e.g., TypeError from complex //)
        // BEFORE the temp assignment so exception flag is set first
        for(auto &check : pending_checks)
          block.add(std::move(check));
        pending_checks.clear();

        code_frontend_assignt eval{tmp_sym.symbol_expr(), typed_rhs};
        eval.add_source_location() = loc;
        block.add(std::move(eval));

        // Guard the actual assignment (typecast if needed)
        exprt assign_rhs = tmp_sym.symbol_expr();
        if(assign_rhs.type() != sym.type)
          assign_rhs = safe_typecast(assign_rhs, sym.type);
        code_frontend_assignt assign{sym.symbol_expr(), assign_rhs};
        assign.add_source_location() = loc;
        code_ifthenelset guarded{
          not_exprt{exc_sym->symbol_expr()}, std::move(assign)};
        block.add(std::move(guarded));
        continue;
      }
    }

    code_frontend_assignt assign{sym.symbol_expr(), typed_rhs};
    assign.add_source_location() = loc;
    // Track constant string values (invalidate if non-constant)
    if(is_python_string_type(typed_rhs.type()))
    {
      auto sv = extract_string_value(typed_rhs);
      if(!sv.has_value() && typed_rhs.id() == ID_symbol)
      {
        auto it =
          string_constants.find(to_symbol_expr(typed_rhs).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
        string_constants[sym.name] = sv.value();
      else
        string_constants.erase(sym.name);
      // PLR correctness: if we're inside a branch (if_else_depth > 0),
      // the string_constants tracking is path-insensitive — the last
      // branch processed wins, which is wrong for code like:
      //   if cond: r = "two"
      //   else: r = "other"
      //   assert r == "two"
      // where the converter processes both branches sequentially and
      // the else-branch's "other" overwrites the if-branch's "two".
      // At the assert site, the constant-fold path then compares
      // "other" != "two" → false, which is unsound.
      //
      // Fix: when inside a branch, ERASE the tracking so the
      // comparison falls through to the string solver (which handles
      // path-sensitivity correctly via SSA).
      if(if_else_depth > 0)
        string_constants.erase(sym.name);
    }
    if(is_python_dict_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
      {
        dict_literals[sym.name] = typed_rhs;
        // Populate per-key static category from the original
        // AST. The struct exprt's value array has had all
        // values typecast to a single uniform type via
        // safe_typecast, which loses the original category.
        // The AST is the source of truth.
        if(is_node_type(value, "Dict"))
        {
          const jsont &keys = json_member(value, "keys");
          const jsont &vals = json_member(value, "values");
          if(keys.is_array() && vals.is_array())
          {
            std::map<std::string, std::string> cats;
            std::map<std::string, std::string> str_consts;
            auto kit = as_array(keys).begin();
            auto vit = as_array(vals).begin();
            for(; kit != as_array(keys).end() && vit != as_array(vals).end();
                ++kit, ++vit)
            {
              if(!is_node_type(*kit, "Constant"))
                continue;
              const jsont &kv = json_member(*kit, "value");
              if(!kv.is_string())
                continue;
              std::string cat = ast_value_category(*vit);
              if(!cat.empty())
                cats[kv.value] = cat;
              if(is_node_type(*vit, "Constant"))
              {
                const jsont &cval = json_member(*vit, "value");
                if(cval.is_string())
                  str_consts[kv.value] = cval.value;
              }
            }
            if(!cats.empty())
              dict_literal_value_categories[sym.name] = std::move(cats);
            if(!str_consts.empty())
              dict_literal_value_string_consts[sym.name] =
                std::move(str_consts);
          }
        }
      }
      else if(typed_rhs.id() == ID_side_effect)
      {
        // Inter-procedural dict-literal propagation: if the
        // RHS is a call to a function whose return-statement
        // we recorded as a dict literal, synthesize a
        // dict-struct with the known keys so the caller's
        // subscript / 'in' / len() operations can prove the
        // key exists.
        const auto &se = to_side_effect_expr(typed_rhs);
        if(
          se.get_statement() == ID_function_call && !se.operands().empty() &&
          se.operands()[0].id() == ID_symbol)
        {
          std::string callee =
            id2string(to_symbol_expr(se.operands()[0]).get_identifier());
          std::string p = "python::";
          if(callee.substr(0, p.size()) == p)
            callee = callee.substr(p.size());
          // PLR: prefer the full literal cache when the callee
          // has a single return statement of a constant dict.
          bool used_full = false;
          auto fl_it = function_returned_literal.find(callee);
          auto rc_it = function_return_count.find(callee);
          if(
            fl_it != function_returned_literal.end() &&
            rc_it != function_return_count.end() && rc_it->second == 1 &&
            fl_it->second.id() == ID_struct &&
            is_python_dict_type(fl_it->second.type()) &&
            fl_it->second.type() == typed_rhs.type())
          {
            dict_literals[sym.name] = fl_it->second;
            used_full = true;
          }
          if(!used_full)
          {
            auto ki = function_returned_dict_keys.find(callee);
            auto rc2_it = function_return_count.find(callee);
            if(
              ki != function_returned_dict_keys.end() && !ki->second.empty() &&
              rc2_it != function_return_count.end() && rc2_it->second == 1)
            {
              const auto &dt = to_struct_type(typed_rhs.type());
              const auto &keys_type = to_array_type(dt.components()[1].type());
              const auto &vals_type = to_array_type(dt.components()[2].type());
              exprt::operandst key_elems, val_elems;
              for(const auto &k : ki->second)
              {
                key_elems.push_back(python_string_literal(k));
                val_elems.push_back(safe_zero(vals_type.element_type()));
              }
              while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
              {
                key_elems.push_back(safe_zero(keys_type.element_type()));
                val_elems.push_back(safe_zero(vals_type.element_type()));
              }
              exprt length = from_integer(
                static_cast<long long>(ki->second.size()), signedbv_typet{64});
              dict_literals[sym.name] = struct_exprt{
                {length,
                 array_exprt{std::move(key_elems), keys_type},
                 array_exprt{std::move(val_elems), vals_type}},
                typed_rhs.type()};
            }
            else
              dict_literals.erase(sym.name);
          }
        }
        else
          dict_literals.erase(sym.name);
      }
      else if(typed_rhs.id() == ID_symbol)
      {
        // PLR §3.1: alias propagation for `kw_alias = kw_base`
        // where kw_base is in dict_literals. Propagates so
        // downstream consumers (e.g., math.X(**kw_alias)
        // TypeError detection) can resolve the underlying dict.
        auto rhs_id = to_symbol_expr(typed_rhs).get_identifier();
        auto rhs_it = dict_literals.find(rhs_id);
        if(rhs_it != dict_literals.end())
          dict_literals[sym.name] = rhs_it->second;
        else
          dict_literals.erase(sym.name);
      }
      else
        dict_literals.erase(sym.name);
    }
    if(is_python_list_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
        list_literals[sym.name] = typed_rhs;
      else if(typed_rhs.id() == ID_side_effect)
      {
        // PLR: inter-procedural list-literal propagation.
        const auto &se = to_side_effect_expr(typed_rhs);
        if(
          se.get_statement() == ID_function_call && !se.operands().empty() &&
          se.operands()[0].id() == ID_symbol)
        {
          std::string callee =
            id2string(to_symbol_expr(se.operands()[0]).get_identifier());
          std::string p = "python::";
          if(callee.substr(0, p.size()) == p)
            callee = callee.substr(p.size());
          auto fl_it = function_returned_literal.find(callee);
          auto rc_it = function_return_count.find(callee);
          if(
            fl_it != function_returned_literal.end() &&
            rc_it != function_return_count.end() && rc_it->second == 1 &&
            fl_it->second.id() == ID_struct &&
            is_python_list_type(fl_it->second.type()) &&
            fl_it->second.type() == typed_rhs.type())
          {
            list_literals[sym.name] = fl_it->second;
          }
          else
            list_literals.erase(sym.name);
        }
        else
          list_literals.erase(sym.name);
      }
      else
        list_literals.erase(sym.name);
    }
    if(is_python_tuple_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
        tuple_literals[sym.name] = typed_rhs;
      else if(typed_rhs.id() == ID_side_effect)
      {
        // PLR: inter-procedural tuple-literal propagation.
        const auto &se = to_side_effect_expr(typed_rhs);
        if(
          se.get_statement() == ID_function_call && !se.operands().empty() &&
          se.operands()[0].id() == ID_symbol)
        {
          std::string callee =
            id2string(to_symbol_expr(se.operands()[0]).get_identifier());
          std::string p = "python::";
          if(callee.substr(0, p.size()) == p)
            callee = callee.substr(p.size());
          auto fl_it = function_returned_literal.find(callee);
          auto rc_it = function_return_count.find(callee);
          if(
            fl_it != function_returned_literal.end() &&
            rc_it != function_return_count.end() && rc_it->second == 1 &&
            fl_it->second.id() == ID_struct &&
            is_python_tuple_type(fl_it->second.type()) &&
            fl_it->second.type() == typed_rhs.type())
          {
            tuple_literals[sym.name] = fl_it->second;
          }
          else
            tuple_literals.erase(sym.name);
        }
        else
          tuple_literals.erase(sym.name);
      }
      else
        tuple_literals.erase(sym.name);
    }
    // PLR §6.5: complex literal tracking (parallel to tuple).
    if(
      typed_rhs.type().id() == ID_struct &&
      to_struct_type(typed_rhs.type()).get_tag() == "python_complex")
    {
      if(typed_rhs.id() == ID_struct)
        complex_literals[sym.name] = typed_rhs;
      else
        complex_literals.erase(sym.name);
    }
    {
      auto ev = try_eval_double(typed_rhs);
      if(ev.has_value())
        float_constants[sym.name] = ev.value();
      else
        float_constants.erase(sym.name);
    }
    block.add(std::move(assign));

    // PLR §6.2.9: if the RHS is a call to a generator function,
    // allocate the hidden cursor so subsequent next(g) calls can
    // advance through the eager-yield list and raise
    // StopIteration when exhausted.
    codet gen_init = allocate_generator_cursor(sym.name, value, loc);
    if(gen_init.get_statement() != ID_skip)
      block.add(std::move(gen_init));

    // PLR §6.10.2: track when a name is bound to a type object
    // (e.g. `x = int`). isinstance(x, type) consults this set
    // to return True without inspecting the symbol's runtime
    // int value (which is the type-tag, not a real instance).
    if(is_node_type(value, "Name"))
    {
      std::string rhs_id = json_string(json_member(value, "id"));
      static const std::set<std::string> type_names = {
        "int",
        "float",
        "bool",
        "str",
        "list",
        "tuple",
        "dict",
        "set",
        "frozenset",
        "bytes",
        "bytearray",
        "object",
        "type",
        "Exception",
        "BaseException",
        "ValueError",
        "TypeError",
        "KeyError",
        "IndexError",
        "StopIteration",
        "AttributeError",
        "ArithmeticError",
        "ZeroDivisionError",
        "NotImplementedError",
        "RuntimeError",
        "OSError",
        "FileNotFoundError"};
      if(type_names.count(rhs_id) > 0 || class_types.count(rhs_id) > 0)
        name_holds_type_binding.insert(sym.name);
      else
        name_holds_type_binding.erase(sym.name);
    }
    else
    {
      name_holds_type_binding.erase(sym.name);
    }
  }

  if(block.statements().size() == 1)
    return block.statements().front();
  return std::move(block);
}

// PLR §7.2.1: Augmented assignment statements
// "An augmented assignment evaluates the target and the expression list,
// performs the binary operation, and assigns the result to the target."
codet python_convertert::convert_aug_assign(const jsont &stmt)
{
  // x += expr  →  x = x + expr
  // Also handles: lst[i] += expr, self.attr += expr
  const jsont &target = json_member(stmt, "target");
  const jsont &op_node = json_member(stmt, "op");
  const jsont &value = json_member(stmt, "value");
  source_locationt loc = get_location(stmt);

  // PLR §7.2.2: augmented assignment evaluates the LHS
  // expression exactly once. For a[idx()] += ... the
  // index must be snapshotted so the read and write
  // share the same index value (rather than calling
  // idx() twice). If the index is a plain Name or
  // constant, no rewrite is needed.
  //
  // Strategy for the a[side_effect()] case: materialise
  // the index value into a temp via pending_checks and
  // construct an index_exprt from the target's container
  // expression and the temp, building both lhs (read) and
  // an assignment target referencing the same temp.
  exprt subscript_lhs;
  exprt subscript_write_lhs;
  bool have_subscript_rewrite = false;
  if(is_node_type(target, "Subscript"))
  {
    const jsont &slice = json_member(target, "slice");
    if(is_node_type(slice, "Call"))
    {
      exprt container = convert_expression(json_member(target, "value"));
      exprt idx_expr = convert_expression(slice);
      if(!container.is_nil() && !idx_expr.is_nil())
      {
        static unsigned auglidx_ctr = 0;
        std::string tmpn = "__auglidx_" + std::to_string(auglidx_ctr++);
        std::string tmpq = qualify_name(tmpn);
        irep_idt tmpid{tmpq};
        if(symbol_table.lookup(tmpid) == nullptr)
        {
          symbolt ts{tmpid, idx_expr.type(), "python"};
          ts.base_name = tmpn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          ts.is_static_lifetime = current_function.empty();
          symbol_table.add(ts);
        }
        symbol_exprt snap = symbol_table.lookup_ref(tmpid).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{snap, idx_expr});
        // Build the indexed reads/writes using the snapshot.
        if(is_python_list_type(container.type()))
        {
          const auto &st = to_struct_type(container.type());
          typet elem_type =
            to_array_type(st.get_component("data").type()).element_type();
          member_exprt data_member{
            container, "data", to_array_type(st.get_component("data").type())};
          subscript_lhs = index_exprt{data_member, snap, elem_type};
          subscript_write_lhs = subscript_lhs;
          have_subscript_rewrite = true;
        }
      }
    }
  }

  // Determine the LHS expression based on target type.
  //
  // For dict-subscript targets we deliberately bypass
  // convert_subscript: that helper emits a KeyError check via
  // pending_checks for missing keys, which is correct for plain
  // d[k] reads but wrong for d[k] += rhs against a defaultdict
  // (where the read is allowed to return the factory's default).
  // We build the same chained `keys[i]==k ? values[i] : ... :
  // safe_zero` read inline so the KeyError check stays out of the
  // emitted GOTO. The follow-up store at the end of this function
  // performs an append for the missing-key case, matching
  // defaultdict's auto-insert semantics.
  exprt lhs;
  bool dict_subscript_aug = false;
  exprt dict_aug_container;
  exprt dict_aug_key;
  if(have_subscript_rewrite)
    lhs = subscript_lhs;
  else if(is_node_type(target, "Name"))
    lhs = convert_name(target);
  else if(is_node_type(target, "Subscript"))
  {
    exprt container_check = convert_expression(json_member(target, "value"));
    if(!container_check.is_nil() && is_python_dict_type(container_check.type()))
    {
      const auto &dict_st = to_struct_type(container_check.type());
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      member_exprt length{container_check, "length", signedbv_typet{64}};
      member_exprt keys_arr{container_check, "keys", keys_type};
      member_exprt vals_arr{container_check, "values", vals_type};
      exprt key_expr = convert_expression(json_member(target, "slice"));
      if(key_expr.type() != keys_type.element_type())
        key_expr = coerce_element(key_expr, keys_type.element_type());
      exprt result = safe_zero(vals_type.element_type());
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt key_i = python_dict_unbox_key(index_exprt{keys_arr, idx});
        exprt key_q = python_dict_unbox_key(key_expr);
        if(key_i.type() != key_q.type())
          key_i = safe_typecast(key_i, key_q.type());
        exprt match = equal_exprt{key_i, key_q};
        result = if_exprt{
          and_exprt{in_range, match}, index_exprt{vals_arr, idx}, result};
      }
      lhs = std::move(result);
      dict_subscript_aug = true;
      dict_aug_container = std::move(container_check);
      dict_aug_key = std::move(key_expr);
    }
    else
      lhs = convert_subscript(target);
  }
  else if(is_node_type(target, "Attribute"))
    lhs = convert_attribute(target);
  else
  {
    log.error() << "Unsupported augmented assignment target" << messaget::eom;
    return code_skipt{};
  }

  exprt rhs = convert_expression(value);
  if(lhs.is_nil() || rhs.is_nil())
    return code_skipt{};

  std::string op = json_string(json_member(op_node, "_type"));

  // PLR §7.2.2 / §6.7: augmented assignment applies the binary operator, so an
  // operand-type mismatch (int += str, list += int, str += int, ...) is the
  // SAME TypeError as the plain binary op. Reuse the shared operand-type check;
  // on a PROVABLE error emit TypeError and skip the (mixed-type) lowering.
  // Any/python_value operands are not flagged (no false positive).
  if(binop_operand_type_error(op, lhs, rhs))
  {
    emit_conditional_exception(true_exprt{}, "TypeError");
    return code_skipt{};
  }

  // PLR §7.2.1: For string +=, use content-tracking concat
  if(
    op == "Add" && is_python_string_type(lhs.type()) &&
    is_python_string_type(rhs.type()))
  {
    // Constant-string optimization for +=
    {
      auto lv = extract_string_value(lhs);
      if(!lv.has_value() && lhs.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(lhs).get_identifier());
        if(it != string_constants.end())
          lv = it->second;
      }
      auto rv = extract_string_value(rhs);
      if(!rv.has_value() && rhs.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(rhs).get_identifier());
        if(it != string_constants.end())
          rv = it->second;
      }
      if(lv.has_value() && rv.has_value())
      {
        std::string result = lv.value() + rv.value();
        // Update tracking
        if(lhs.id() == ID_symbol)
          string_constants[to_symbol_expr(lhs).get_identifier()] = result;
        return code_frontend_assignt{lhs, python_string_literal(result)};
      }
    }
    // Non-constant: route through cprover_string_concat_func so
    // the string solver tracks the runtime concatenation. Without
    // this, `word += char` inside a loop would lose word's value
    // and any post-loop assertion folded against word fails. The
    // helper builds string-struct views of both operands when
    // they aren't already in struct form, and havocs SSA outputs
    // when called from inside a loop body.
    if(
      use_smt_string_native && lhs.type().id() == ID_smt_string &&
      rhs.type().id() == ID_smt_string)
    {
      // Native SMT-String back-end: s += t  ->  s = str.++(s, t).
      if(lhs.id() == ID_symbol)
        string_constants.erase(to_symbol_expr(lhs).get_identifier());
      const exprt concat = string_concat(lhs, rhs);
      return code_frontend_assignt{
        lhs,
        bind_string_length_hint(
          concat,
          plus_exprt{
            native_or_member_string_length(lhs),
            native_or_member_string_length(rhs)},
          lhs.source_location())};
    }
    auto to_string_struct = [&](const exprt &s)
    {
      if(s.id() == ID_struct && s.operands().size() == 2)
        return s;
      return exprt{struct_exprt{
        {member_exprt{s, "length", signedbv_typet{64}},
         member_exprt{s, "data", pointer_typet(unsignedbv_typet{8}, 64)}},
        s.type()}};
    };
    exprt concat = emit_string_function(
      ID_cprover_string_concat_func,
      {to_string_struct(lhs), to_string_struct(rhs)},
      symbol_table,
      pending_checks,
      loop_depth > 0);
    if(lhs.id() == ID_symbol)
      string_constants.erase(to_symbol_expr(lhs).get_identifier());
    return code_frontend_assignt{lhs, std::move(concat)};
    typet str_type = python_string_type();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt left_len{lhs, "length", signedbv_typet{64}};
    member_exprt right_len{rhs, "length", signedbv_typet{64}};
    member_exprt left_data{lhs, "data", data_type};
    member_exprt right_data{rhs, "data", data_type};

    static unsigned str_aug_counter = 0;
    std::string tmp_name = "__str_aug_" + std::to_string(str_aug_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};
    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, str_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();

    code_blockt block;
    block.add(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}},
      plus_exprt{left_len, right_len}});

    member_exprt tmp_data{tmp, "data", data_type};
    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, left_len},
        index_exprt{left_data, idx},
        index_exprt{right_data, minus_exprt{idx, left_len}}};
      block.add(code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }

    // Assign result back to target
    if(is_node_type(target, "Name"))
    {
      std::string var_name = json_string(json_member(target, "id"));
      std::string qname = qualify_name(var_name);
      const symbolt *sym = symbol_table.lookup(irep_idt{qname});
      if(sym != nullptr)
      {
        string_constants.erase(sym->name);
        block.add(code_frontend_assignt{sym->symbol_expr(), tmp});
        return std::move(block);
      }
    }
  }

  // PLR §6.7: string repetition `s *= n`. Handle BEFORE type
  // promotion / unwrap_value, which would coerce the int n into
  // a string and lose its value. We constant-fold when both s
  // (or its tracked content) and n are known.
  if(
    op == "Mult" && is_python_string_type(lhs.type()) &&
    !is_python_string_type(rhs.type()))
  {
    auto sv = extract_string_value(lhs);
    if(!sv.has_value() && lhs.id() == ID_symbol)
    {
      auto it = string_constants.find(to_symbol_expr(lhs).get_identifier());
      if(it != string_constants.end())
        sv = it->second;
    }
    exprt n_expr = rhs;
    if(is_python_value_type(n_expr.type()))
      n_expr = unwrap_value(n_expr, signedbv_typet{64});
    auto nv = try_eval_double(n_expr);
    if(sv.has_value() && nv.has_value())
    {
      long long n = static_cast<long long>(nv.value());
      // Don't fold if the result wouldn't fit in
      // PYTHON_MAX_STRING_LENGTH — let the runtime path
      // (nondet) take over so out-of-bounds accesses are
      // properly reported.
      if(
        static_cast<long long>(sv->size()) * std::max<long long>(n, 0) >
        static_cast<long long>(PYTHON_MAX_STRING_LENGTH))
      {
        // fall through to nondet
      }
      else
      {
        std::string result;
        if(n > 0)
        {
          result.reserve(sv->size() * static_cast<std::size_t>(n));
          for(long long i = 0; i < n; ++i)
            result += sv.value();
        }
        exprt str_lit = python_string_literal(result);
        if(is_node_type(target, "Name"))
        {
          std::string var_name = json_string(json_member(target, "id"));
          std::string qname = qualify_name(var_name);
          const symbolt *sym = symbol_table.lookup(irep_idt{qname});
          if(sym != nullptr)
          {
            string_constants[sym->name] = result;
            return code_frontend_assignt{sym->symbol_expr(), str_lit};
          }
        }
      }
    }
  }

  // PLR §6.10.1: complex augmented assignment with non-complex
  // RHS. Handle BEFORE the generic safe_typecast(rhs, lhs.type())
  // — that path tries to typecast float/int into a complex
  // struct and produces a nondet complex. Detect early so we
  // can do field-wise arithmetic with the float/int kept as
  // a scalar.
  bool lhs_is_complex =
    lhs.type().id() == ID_struct &&
    to_struct_type(lhs.type()).get_tag() == "python_complex";
  bool rhs_is_complex =
    rhs.type().id() == ID_struct &&
    to_struct_type(rhs.type()).get_tag() == "python_complex";
  if(lhs_is_complex && !rhs_is_complex)
  {
    exprt rhs_d = rhs;
    if(rhs_d.type() != double_type())
      rhs_d = safe_typecast(rhs_d, double_type());
    exprt new_rhs;
    member_exprt lr{lhs, "real", double_type()};
    member_exprt li{lhs, "imag", double_type()};
    if(op == "Add")
      new_rhs = struct_exprt{{plus_exprt{lr, rhs_d}, exprt{li}}, lhs.type()};
    else if(op == "Sub")
      new_rhs = struct_exprt{{minus_exprt{lr, rhs_d}, exprt{li}}, lhs.type()};
    else if(op == "Mult")
      new_rhs = struct_exprt{
        {mult_exprt{lr, rhs_d}, mult_exprt{li, rhs_d}}, lhs.type()};
    else if(op == "Div")
    {
      // Both real and imag divided by rhs.
      new_rhs =
        struct_exprt{{div_exprt{lr, rhs_d}, div_exprt{li, rhs_d}}, lhs.type()};
    }
    else
    {
      // Other ops on complex are TypeError per PLR. Leave
      // empty so the generic path falls through and the
      // existing TypeError check fires.
    }
    if(!new_rhs.is_nil() && new_rhs.id() != ID_nil)
    {
      code_frontend_assignt assign{lhs, new_rhs};
      assign.add_source_location() = loc;
      return std::move(assign);
    }
  }

  // Type promotion
  if(lhs.type() != rhs.type())
  {
    rhs = safe_typecast(rhs, lhs.type());
  }

  // Unwrap tagged unions for arithmetic
  exprt arith_lhs = lhs;
  if(is_python_value_type(arith_lhs.type()))
    arith_lhs = unwrap_value(
      arith_lhs, rhs.type().id() != ID_struct ? rhs.type() : python_int_type());
  if(is_python_value_type(rhs.type()))
    rhs = unwrap_value(rhs, arith_lhs.type());

  exprt new_rhs;
  // For string/list operations, build a synthetic BinOp JSON and use
  // convert_bin_op which handles concatenation and repetition
  if(
    (op == "Add" || op == "Mult") &&
    (is_python_string_type(lhs.type()) || is_python_list_type(lhs.type())))
  {
    // Create a temporary BinOp expression through convert_bin_op
    // by directly constructing the result
    if(
      op == "Add" && is_python_string_type(lhs.type()) &&
      is_python_string_type(rhs.type()))
    {
      // String concatenation
      typet str_type = python_string_type();
      const auto &data_type = array_typet(
        unsignedbv_typet{8},
        from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
      // Pointer-based string: return nondet for non-constant
      return code_skipt{};
      member_exprt left_len{lhs, "length", signedbv_typet{64}};
      member_exprt right_len{rhs, "length", signedbv_typet{64}};
      member_exprt left_data{lhs, "data", data_type};
      member_exprt right_data{rhs, "data", data_type};
      exprt new_len = plus_exprt{left_len, right_len};
      exprt::operandst chars;
      for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        chars.push_back(if_exprt{
          binary_relation_exprt{idx, ID_lt, left_len},
          index_exprt{left_data, idx},
          index_exprt{right_data, minus_exprt{idx, left_len}}});
      }
      new_rhs = struct_exprt{
        {new_len, array_exprt{std::move(chars), data_type}}, str_type};
    }
    else if(op == "Add" && is_python_list_type(lhs.type()))
    {
      // List concatenation
      const auto &list_st = to_struct_type(lhs.type());
      const auto &data_type = to_array_type(list_st.components()[1].type());
      member_exprt left_len{lhs, "length", signedbv_typet{64}};
      member_exprt right_len{rhs, "length", signedbv_typet{64}};
      member_exprt left_data{lhs, "data", data_type};
      member_exprt right_data{rhs, "data", data_type};
      exprt new_len = plus_exprt{left_len, right_len};
      exprt::operandst elems;
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        elems.push_back(if_exprt{
          binary_relation_exprt{idx, ID_lt, left_len},
          index_exprt{left_data, idx},
          index_exprt{right_data, minus_exprt{idx, left_len}}});
      }
      new_rhs = struct_exprt{
        {new_len, array_exprt{std::move(elems), data_type}}, lhs.type()};
    }
    else
    {
      // String/list repetition: s *= n
      // PLR §6.7: when both s (or its tracked content) and n are
      // constants, fold to a literal so subsequent reads see the
      // expanded value.
      if(op == "Mult" && is_python_string_type(lhs.type()))
      {
        auto sv = extract_string_value(lhs);
        if(!sv.has_value() && lhs.id() == ID_symbol)
        {
          auto it = string_constants.find(to_symbol_expr(lhs).get_identifier());
          if(it != string_constants.end())
            sv = it->second;
        }
        auto nv = try_eval_double(rhs);
        if(sv.has_value() && nv.has_value())
        {
          long long n = static_cast<long long>(nv.value());
          if(n <= 0)
            new_rhs = python_string_literal(std::string{});
          else if(
            static_cast<long long>(sv->size()) * n <=
            static_cast<long long>(PYTHON_MAX_STRING_LENGTH))
          {
            std::string result;
            result.reserve(sv->size() * static_cast<std::size_t>(n));
            for(long long i = 0; i < n; ++i)
              result += sv.value();
            new_rhs = python_string_literal(result);
          }
          else
            new_rhs = side_effect_expr_nondett{lhs.type(), loc};
        }
        else
          new_rhs = side_effect_expr_nondett{lhs.type(), loc};
      }
      else
      {
        // For now, return nondet (proper repetition needs unrolling)
        new_rhs = side_effect_expr_nondett{lhs.type(), loc};
      }
    }
  }
  else if(op == "Add")
  {
    // PLR §6.10.1: complex += / -= / *= / /=. Detect both sides
    // are python_complex and do the field-wise arithmetic.
    auto is_complex = [](const exprt &e)
    {
      return e.type().id() == ID_struct &&
             to_struct_type(e.type()).get_tag() == "python_complex";
    };
    if(is_complex(arith_lhs) && is_complex(rhs))
    {
      // CBMC symex evaluates struct_exprt operands per-field
      // when assigning self-referentially: writing field 0
      // BEFORE evaluating field 1, so a struct_exprt's second
      // operand sees the new field-0 value. Snapshot all four
      // reads into temporaries first to keep the OLD values.
      static unsigned aug_c_add_ctr = 0;
      auto snap_field = [&](const exprt &owner, const std::string &fname)
      {
        std::string n =
          "__aug_c_" + fname + "_" + std::to_string(aug_c_add_ctr++);
        irep_idt sid{qualify_name(n)};
        if(symbol_table.lookup(sid) == nullptr)
        {
          symbolt s{sid, double_type(), "python"};
          s.base_name = n;
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        symbol_exprt sx = symbol_table.lookup_ref(sid).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{sx, member_exprt{owner, fname, double_type()}});
        return sx;
      };
      symbol_exprt lr_s = snap_field(arith_lhs, "real");
      symbol_exprt li_s = snap_field(arith_lhs, "imag");
      symbol_exprt rr_s = snap_field(rhs, "real");
      symbol_exprt ri_s = snap_field(rhs, "imag");
      new_rhs = struct_exprt{
        {plus_exprt{lr_s, rr_s}, plus_exprt{li_s, ri_s}}, arith_lhs.type()};
    }
    else if(is_complex(arith_lhs))
    {
      // complex += real-typed: add to real, leave imag.
      exprt rhs_d = rhs;
      if(rhs_d.type() != double_type())
        rhs_d = safe_typecast(rhs_d, double_type());
      member_exprt lr{arith_lhs, "real", double_type()};
      member_exprt li{arith_lhs, "imag", double_type()};
      new_rhs =
        struct_exprt{{plus_exprt{lr, rhs_d}, exprt{li}}, arith_lhs.type()};
    }
    else
    {
      if(arith_lhs.type().id() == ID_signedbv && rhs.type().id() == ID_signedbv)
        emit_int_overflow_guard(
          not_exprt{binary_overflow_exprt{arith_lhs, ID_overflow_plus, rhs}},
          loc);
      new_rhs = plus_exprt{arith_lhs, rhs};
    }
  }
  else if(op == "Sub")
  {
    auto is_complex = [](const exprt &e)
    {
      return e.type().id() == ID_struct &&
             to_struct_type(e.type()).get_tag() == "python_complex";
    };
    if(is_complex(arith_lhs) && is_complex(rhs))
    {
      static unsigned aug_c_sub_ctr = 0;
      auto snap_field = [&](const exprt &owner, const std::string &fname)
      {
        std::string n =
          "__aug_c_" + fname + "_" + std::to_string(aug_c_sub_ctr++);
        irep_idt sid{qualify_name(n)};
        if(symbol_table.lookup(sid) == nullptr)
        {
          symbolt s{sid, double_type(), "python"};
          s.base_name = n;
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        symbol_exprt sx = symbol_table.lookup_ref(sid).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{sx, member_exprt{owner, fname, double_type()}});
        return sx;
      };
      symbol_exprt lr_s = snap_field(arith_lhs, "real");
      symbol_exprt li_s = snap_field(arith_lhs, "imag");
      symbol_exprt rr_s = snap_field(rhs, "real");
      symbol_exprt ri_s = snap_field(rhs, "imag");
      new_rhs = struct_exprt{
        {minus_exprt{lr_s, rr_s}, minus_exprt{li_s, ri_s}}, arith_lhs.type()};
    }
    else if(is_complex(arith_lhs))
    {
      exprt rhs_d = rhs;
      if(rhs_d.type() != double_type())
        rhs_d = safe_typecast(rhs_d, double_type());
      member_exprt lr{arith_lhs, "real", double_type()};
      member_exprt li{arith_lhs, "imag", double_type()};
      new_rhs =
        struct_exprt{{minus_exprt{lr, rhs_d}, exprt{li}}, arith_lhs.type()};
    }
    else
    {
      if(arith_lhs.type().id() == ID_signedbv && rhs.type().id() == ID_signedbv)
        emit_int_overflow_guard(
          not_exprt{binary_overflow_exprt{arith_lhs, ID_overflow_minus, rhs}},
          loc);
      new_rhs = minus_exprt{arith_lhs, rhs};
    }
  }
  else if(op == "Mult")
  {
    auto is_complex = [](const exprt &e)
    {
      return e.type().id() == ID_struct &&
             to_struct_type(e.type()).get_tag() == "python_complex";
    };
    if(is_complex(arith_lhs) && is_complex(rhs))
    {
      // Snapshot the four field reads so the struct_exprt
      // operand evaluation order can't see partial writes.
      static unsigned aug_complex_ctr = 0;
      auto snap_field = [&](const exprt &owner, const std::string &fname)
      {
        std::string n =
          "__aug_c_" + fname + "_" + std::to_string(aug_complex_ctr++);
        irep_idt sid{qualify_name(n)};
        if(symbol_table.lookup(sid) == nullptr)
        {
          symbolt s{sid, double_type(), "python"};
          s.base_name = n;
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        symbol_exprt sx = symbol_table.lookup_ref(sid).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{sx, member_exprt{owner, fname, double_type()}});
        return sx;
      };
      symbol_exprt lr_s = snap_field(arith_lhs, "real");
      symbol_exprt li_s = snap_field(arith_lhs, "imag");
      symbol_exprt rr_s = snap_field(rhs, "real");
      symbol_exprt ri_s = snap_field(rhs, "imag");
      new_rhs = struct_exprt{
        {minus_exprt{mult_exprt{lr_s, rr_s}, mult_exprt{li_s, ri_s}},
         plus_exprt{mult_exprt{lr_s, ri_s}, mult_exprt{li_s, rr_s}}},
        arith_lhs.type()};
    }
    else if(is_complex(arith_lhs))
    {
      // complex *= real: scale both real and imag.
      exprt rhs_d = rhs;
      if(rhs_d.type() != double_type())
        rhs_d = safe_typecast(rhs_d, double_type());
      member_exprt lr{arith_lhs, "real", double_type()};
      member_exprt li{arith_lhs, "imag", double_type()};
      new_rhs = struct_exprt{
        {mult_exprt{lr, rhs_d}, mult_exprt{li, rhs_d}}, arith_lhs.type()};
    }
    else
    {
      if(arith_lhs.type().id() == ID_signedbv && rhs.type().id() == ID_signedbv)
        emit_int_overflow_guard(
          not_exprt{binary_overflow_exprt{arith_lhs, ID_overflow_mult, rhs}},
          loc);
      new_rhs = mult_exprt{arith_lhs, rhs};
    }
  }
  else if(op == "FloorDiv")
  {
    // PLR §6.7: x //= y must use Python's floored division (round
    // toward -inf), not C's truncated division (round toward 0).
    // The two diverge for negative-result cases: 7 //= -2 should
    // be -4 (Python), not -3 (C).
    //
    // Mirror the convert_bin_op "FloorDiv" branch's runtime form:
    //   q = a / b
    //   r = a % b
    //   if r != 0 && sign(a) != sign(b): q -= 1
    // (constant folding is left to safe_typecast / simplify.)
    if(
      arith_lhs.type().id() == ID_signedbv ||
      arith_lhs.type().id() == ID_unsignedbv)
    {
      exprt quotient = div_exprt{arith_lhs, rhs};
      exprt remainder = mod_exprt{arith_lhs, rhs};
      exprt has_remainder =
        notequal_exprt{remainder, from_integer(0, arith_lhs.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{arith_lhs, rhs}, ID_lt, from_integer(0, arith_lhs.type())};
      new_rhs = minus_exprt{
        quotient,
        if_exprt{
          and_exprt{has_remainder, diff_sign},
          from_integer(1, arith_lhs.type()),
          from_integer(0, arith_lhs.type())}};
    }
    else
      new_rhs = div_exprt{arith_lhs, rhs};
  }
  else if(op == "Div")
  {
    // PLR §6.10.1: complex /= complex, with snapshot guard.
    auto is_complex = [](const exprt &e)
    {
      return e.type().id() == ID_struct &&
             to_struct_type(e.type()).get_tag() == "python_complex";
    };
    if(is_complex(arith_lhs) && is_complex(rhs))
    {
      static unsigned aug_c_div_ctr = 0;
      auto snap_field = [&](const exprt &owner, const std::string &fname)
      {
        std::string n =
          "__aug_c_" + fname + "_" + std::to_string(aug_c_div_ctr++);
        irep_idt sid{qualify_name(n)};
        if(symbol_table.lookup(sid) == nullptr)
        {
          symbolt s{sid, double_type(), "python"};
          s.base_name = n;
          s.is_lvalue = true;
          s.is_state_var = true;
          symbol_table.add(s);
        }
        symbol_exprt sx = symbol_table.lookup_ref(sid).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{sx, member_exprt{owner, fname, double_type()}});
        return sx;
      };
      symbol_exprt lr_s = snap_field(arith_lhs, "real");
      symbol_exprt li_s = snap_field(arith_lhs, "imag");
      symbol_exprt rr_s = snap_field(rhs, "real");
      symbol_exprt ri_s = snap_field(rhs, "imag");
      exprt denom = plus_exprt{mult_exprt{rr_s, rr_s}, mult_exprt{ri_s, ri_s}};
      exprt real_num =
        plus_exprt{mult_exprt{lr_s, rr_s}, mult_exprt{li_s, ri_s}};
      exprt imag_num =
        minus_exprt{mult_exprt{li_s, rr_s}, mult_exprt{lr_s, ri_s}};
      new_rhs = struct_exprt{
        {div_exprt{real_num, denom}, div_exprt{imag_num, denom}},
        arith_lhs.type()};
    }
    else
    {
      // True division: result is float
      exprt fl = arith_lhs, fr = rhs;
      if(fl.type().id() != ID_floatbv)
        fl = typecast_exprt{fl, double_type()};
      if(fr.type().id() != ID_floatbv)
        fr = typecast_exprt{fr, double_type()};
      new_rhs = div_exprt{fl, fr};
    }
  }
  else if(op == "Mod")
  {
    // PLR §6.7: x %= y must follow Python's floored-division
    // remainder (sign matches divisor), not C's (sign matches
    // dividend). Mirror convert_bin_op's "Mod" runtime form:
    //   r = a % b
    //   if r != 0 && sign(a) != sign(b): r += b
    if(
      arith_lhs.type().id() == ID_signedbv ||
      arith_lhs.type().id() == ID_unsignedbv)
    {
      exprt remainder = mod_exprt{arith_lhs, rhs};
      exprt has_remainder =
        notequal_exprt{remainder, from_integer(0, arith_lhs.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{arith_lhs, rhs}, ID_lt, from_integer(0, arith_lhs.type())};
      new_rhs = plus_exprt{
        remainder,
        if_exprt{
          and_exprt{has_remainder, diff_sign},
          rhs,
          from_integer(0, arith_lhs.type())}};
    }
    else if(arith_lhs.type().id() == ID_floatbv)
    {
      // PLR §6.7: float modulo follows Python's floored
      // semantics: r = a - floor(a/b) * b. Sign matches
      // divisor.
      exprt rhs_d = rhs;
      if(rhs_d.type() != double_type())
        rhs_d = safe_typecast(rhs_d, double_type());
      // floor(a/b) via cast-to-int-then-back-to-float, with
      // correction for negative quotients (round toward zero
      // → round toward -inf).
      exprt q_raw = div_exprt{arith_lhs, rhs_d};
      exprt q_int = typecast_exprt{q_raw, python_int_type()};
      exprt q_back = typecast_exprt{q_int, double_type()};
      // Adjust: when q_back > q_raw (truncation toward 0
      // overshot for negatives), subtract 1.
      ieee_floatt fone{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      fone.from_double(1.0);
      ieee_floatt fzero = fone;
      fzero.make_zero();
      exprt floor_q = minus_exprt{
        q_back,
        if_exprt{
          binary_relation_exprt{q_back, ID_gt, q_raw},
          fone.to_expr(),
          fzero.to_expr()}};
      new_rhs = minus_exprt{arith_lhs, mult_exprt{floor_q, rhs_d}};
    }
    else
      new_rhs = mod_exprt{arith_lhs, rhs};
  }
  else if(op == "BitOr")
    new_rhs = bitor_exprt{arith_lhs, rhs};
  else if(op == "BitAnd")
    new_rhs = bitand_exprt{arith_lhs, rhs};
  else if(op == "BitXor")
    new_rhs = bitxor_exprt{arith_lhs, rhs};
  else if(op == "LShift")
  {
    mp_integer sh;
    if(
      arith_lhs.type().id() == ID_signedbv && rhs.is_constant() &&
      !to_integer(to_constant_expr(rhs), sh) && sh >= 0)
      emit_int_overflow_guard(
        not_exprt{binary_overflow_exprt{arith_lhs, ID_overflow_shl, rhs}}, loc);
    new_rhs = shl_exprt{arith_lhs, rhs};
  }
  else if(op == "RShift")
    new_rhs = ashr_exprt{arith_lhs, rhs};
  else if(op == "MatMult")
  {
    // See BinOp/MatMult note: model '@=' as scalar '*='; sound
    // over-approximation for non-scalar operands.
    if(arith_lhs.type() == rhs.type() && arith_lhs.type().id() != ID_struct)
      new_rhs = mult_exprt{arith_lhs, rhs};
    else
      new_rhs = side_effect_expr_nondett{arith_lhs.type(), loc};
  }
  else if(op == "Pow")
  {
    // x **= y — use constant evaluation if possible
    auto base_ev = try_eval_double(arith_lhs);
    auto exp_ev = try_eval_double(rhs);
    if(base_ev.has_value() && exp_ev.has_value())
    {
      double result_d = std::pow(base_ev.value(), exp_ev.value());
      if(arith_lhs.type().id() == ID_floatbv)
        new_rhs = double_to_floatbv(result_d);
      else
        new_rhs =
          from_integer(static_cast<long long>(result_d), arith_lhs.type());
    }
    else
    {
      // Fallback: if-then-else chain for small exponents
      new_rhs = arith_lhs; // x**1 as default
      for(int i = 16; i >= 1; i--)
      {
        exprt power = arith_lhs;
        for(int j = 1; j < i; j++)
          power = mult_exprt{power, arith_lhs};
        new_rhs = if_exprt{
          equal_exprt{rhs, from_integer(i, rhs.type())}, power, new_rhs};
      }
      new_rhs = if_exprt{
        equal_exprt{rhs, from_integer(0, rhs.type())},
        from_integer(1, arith_lhs.type()),
        new_rhs};
    }
  }
  else
  {
    log.warning() << "Unsupported augmented assignment operator: " << op
                  << messaget::eom;
    return code_skipt{};
  }

  // Wrap result back into tagged union if needed
  if(is_python_value_type(lhs.type()) && !is_python_value_type(new_rhs.type()))
    new_rhs = wrap_value(new_rhs);
  if(new_rhs.type() != lhs.type())
    new_rhs = safe_typecast(new_rhs, lhs.type());
  // Invalidate constant tracking for modified variables
  if(lhs.id() == ID_symbol)
  {
    irep_idt sid = to_symbol_expr(lhs).get_identifier();
    string_constants.erase(sid);
    dict_literals.erase(sid);
    float_constants.erase(sid);
    list_literals.erase(sid);
  }
  // Div-by-zero check for /= and //= and %=
  if(op == "Div" || op == "FloorDiv" || op == "Mod")
  {
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(exc_sym != nullptr)
    {
      exprt divisor = rhs;
      exprt is_zero = equal_exprt{divisor, safe_zero(divisor.type())};
      pending_checks.push_back(code_ifthenelset{
        is_zero, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
    }
  }
  // Negative shift check for <<= and >>=
  if(op == "LShift" || op == "RShift")
  {
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(exc_sym != nullptr)
    {
      exprt neg =
        binary_relation_exprt{rhs, ID_lt, from_integer(0, rhs.type())};
      pending_checks.push_back(code_ifthenelset{
        neg, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
    }
  }

  // Dict-subscript augmented assignment: d[key] += rhs.
  //
  // The lhs we built above is the read form: a chain of nested
  // (key_match[i] ? d.values[i] : ...) ending in a struct constant
  // default. Using that as a code_frontend_assignt LHS would have
  // CBMC's symex hit "l2_rename_rvalues case `struct' not handled"
  // on the leaf struct constant, which is not a writable location.
  //
  // Instead, follow the same pattern as the regular dict-subscript
  // assign in convert_assign: snapshot the new value, then iterate
  // over the dict's slots emitting `if(in_range && keys[i]==key)
  // values[i] = new_val` and append a new (key, new_val) entry if
  // the key wasn't found. This matches Python's defaultdict
  // semantics — d[k] += v on a missing key inserts (k, factory()+v).
  if(dict_subscript_aug)
  {
    code_blockt block;
    const auto &dict_st = to_struct_type(dict_aug_container.type());
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    const auto &vals_type = to_array_type(dict_st.components()[2].type());
    member_exprt length{dict_aug_container, "length", signedbv_typet{64}};
    member_exprt keys_arr{dict_aug_container, "keys", keys_type};
    member_exprt vals_arr{dict_aug_container, "values", vals_type};
    // Invalidate any constant-fold record of the dict's
    // contents — the aug-assign mutates it, so subsequent
    // reads must go through the runtime keys/values arrays
    // rather than the literal dict_literals snapshot.
    if(dict_aug_container.id() == ID_symbol)
    {
      irep_idt did = to_symbol_expr(dict_aug_container).get_identifier();
      dict_literals.erase(did);
    }
    exprt typed_new_val = new_rhs;
    if(typed_new_val.type() != vals_type.element_type())
      typed_new_val = coerce_element(typed_new_val, vals_type.element_type());

    static unsigned dict_aug_ctr = 0;
    std::string fn = "__dict_aug_found_" + std::to_string(dict_aug_ctr++);
    std::string fq = qualify_name(fn);
    irep_idt fi{fq};
    if(symbol_table.lookup(fi) == nullptr)
    {
      symbolt fs{fi, bool_typet{}, "python"};
      fs.base_name = fn;
      fs.is_lvalue = true;
      fs.is_state_var = true;
      symbol_table.add(fs);
    }
    symbol_exprt found = symbol_table.lookup_ref(fi).symbol_expr();
    block.add(code_frontend_assignt{found, false_exprt{}});
    for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt in_range = binary_relation_exprt{idx, ID_lt, length};
      exprt match = equal_exprt{
        python_dict_unbox_key(index_exprt{keys_arr, idx}),
        python_dict_unbox_key(dict_aug_key)};
      code_blockt update;
      update.add(
        code_frontend_assignt{index_exprt{vals_arr, idx}, typed_new_val});
      update.add(code_frontend_assignt{found, true_exprt{}});
      block.add(
        code_ifthenelset{and_exprt{in_range, match}, std::move(update)});
    }
    // defaultdict semantics: append (key, new_val) when missing.
    code_blockt append;
    emit_capacity_guard(append, length, PYTHON_MAX_DICT_SIZE);
    append.add(
      code_frontend_assignt{index_exprt{keys_arr, length}, dict_aug_key});
    append.add(
      code_frontend_assignt{index_exprt{vals_arr, length}, typed_new_val});
    append.add(code_frontend_assignt{
      length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
    block.add(code_ifthenelset{not_exprt{found}, std::move(append)});
    block.add_source_location() = loc;
    return std::move(block);
  }

  code_frontend_assignt assign{lhs, new_rhs};
  assign.add_source_location() = loc;
  return std::move(assign);
}
