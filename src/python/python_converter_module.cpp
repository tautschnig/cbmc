/// Python to GOTO converter — module-level handlers:
/// convert_module_body, process_imported_module, the
/// top-level convert() pass driver.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
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

#include <set>

// PLR §6.2.8: Comprehension displays

code_blockt python_convertert::convert_module_body(const jsont &body)
{
  code_blockt block;

  if(!body.is_array())
    return block;

  // PLR §8.7: flush the once-only initialisers for frozen method mutable-default
  // symbols (queued during convert_class_def across the earlier passes) FIRST,
  // so the shared default object is initialised before any method call in the
  // module body executes (a10_mutable_default).
  for(const auto &init : deferred_static_default_inits)
    block.add(init);

  for(const auto &stmt : as_array(body))
  {
    // Function and class definitions are handled in the first pass
    if((is_node_type(stmt, "FunctionDef") ||
        is_node_type(stmt, "AsyncFunctionDef")))
    {
      // Evaluate default parameter values NOW (pass 2) when variables
      // have their definition-time values (PLR §8.7)
      std::string fname = json_string(json_member(stmt, "name"));
      main_module_defs.insert(fname); // Root B: in-scope main def

      // PLR §8.7: a module-level `@dec def f` applies `f = dec(f)` at def-time,
      // so the decorator value must be callable. A provably non-callable
      // concrete value (int/float/str/list/...) raises TypeError at the def
      // site. The def statement otherwise emits nothing into the module block
      // (defs are registered in an earlier pass), so the check lives here.
      // Gated to a PROVABLE violation: a nil (unresolved) or python_value (Any)
      // decorator is never flagged; functions/classes/lambdas are ID_code.
      {
        const jsont &decos = json_member(stmt, "decorator_list");
        if(decos.is_array())
          for(const auto &dec : as_array(decos))
          {
            if(is_node_type(dec, "Name"))
            {
              const std::string did = json_string(json_member(dec, "id"));
              if(
                did == "overload" || did == "staticmethod" ||
                did == "classmethod" || did == "property")
                continue;
            }
            if(is_node_type(dec, "Call"))
            {
              const jsont &df = json_member(dec, "func");
              if(
                is_node_type(df, "Name") &&
                json_string(json_member(df, "id")) == "c_intrinsic")
                continue;
            }
            // Only a bare `@name` decorator is checked: a Call (`@factory(...)`)
            // or Attribute (`@mod.deco`, e.g. @icontract.require) form is a
            // decorator factory / library decorator whose callability cannot be
            // proven here -- skip to avoid false positives.
            if(!is_node_type(dec, "Name"))
              continue;
            exprt de = convert_expression(dec);
            if(
              !de.is_nil() && de.type().id() != ID_code &&
              !is_python_value_type(de.type()))
            {
              // Callable iff it is a user-class instance whose MRO defines
              // __call__; any other concrete value (int/str/list/...) or a
              // user-class lacking __call__ is non-callable.
              std::string dtag;
              if(de.type().id() == ID_struct)
                dtag = id2string(to_struct_type(de.type()).get_tag());
              else if(de.type().id() == ID_struct_tag)
                dtag =
                  id2string(to_struct_tag_type(de.type()).get_identifier());
              const bool is_user_class =
                dtag.compare(0, 13, "python_class_") == 0;
              const bool noncallable =
                !is_user_class ||
                concrete_class_lacks_dunder(de.type(), "__call__");
              if(noncallable)
              {
                const symbolt *ea =
                  symbol_table.lookup("python::__exception_active");
                const symbolt *et =
                  symbol_table.lookup("python::__exception_type");
                if(ea != nullptr)
                {
                  block.add(
                    code_frontend_assignt{ea->symbol_expr(), true_exprt{}});
                  if(et != nullptr)
                    block.add(code_frontend_assignt{
                      et->symbol_expr(),
                      from_integer(
                        exception_type_hash("TypeError"), et->type)});
                  // The FunctionDef statement is otherwise skipped for the
                  // per-statement uncaught-exception assert below, so emit one
                  // here so the def-time TypeError is actually verified.
                  source_locationt eloc = get_location(stmt);
                  eloc.set_property_class("exception");
                  eloc.set_comment("uncaught exception");
                  code_assertt exc_check{not_exprt{ea->symbol_expr()}};
                  exc_check.add_source_location() = eloc;
                  block.add(std::move(exc_check));
                }
                break;
              }
            }
          }
      }
      const jsont &func_args = json_member(stmt, "args");
      const jsont &defaults = json_member(func_args, "defaults");
      const jsont &params_json = json_member(func_args, "args");
      if(defaults.is_array() && params_json.is_array())
      {
        std::size_t n_params = as_array(params_json).size();
        std::size_t n_defaults = as_array(defaults).size();
        std::size_t first_default = n_params - n_defaults;
        auto def_it = as_array(defaults).begin();
        for(std::size_t i = first_default; i < n_params; i++, ++def_it)
        {
          exprt val = convert_expression(*def_it);
          if(!val.is_nil())
          {
            // PLR §8.7: if this default is a callable-valued NAME, snapshot
            // the callable it resolves to RIGHT NOW (definition time, after
            // the preceding module statements — e.g. `cur = mul` — have been
            // processed, so `function_aliases` holds the def-time binding).
            // Higher-order monomorphisation consults this instead of the
            // live alias, so a later `cur = sub` cannot redirect the default.
            if(is_node_type(*def_it, "Name"))
            {
              const std::string dn = json_string(json_member(*def_it, "id"));
              irep_idt cid;
              auto ai = function_aliases.find(qualify_name(dn));
              if(ai != function_aliases.end())
                cid = ai->second;
              else
              {
                const symbolt *bs = symbol_table.lookup("python::" + dn);
                if(bs != nullptr && bs->type.id() == ID_code)
                  cid = irep_idt{"python::" + dn};
              }
              if(!cid.empty())
                default_callable_snapshot[{fname, i}] = cid;
            }
            // Create a temp to freeze the value at definition time
            static unsigned def_freeze_ctr = 0;
            std::string tn =
              "__def_" + fname + "_" + std::to_string(def_freeze_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, val.type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              ts.is_static_lifetime = true;
              // Stash the literal value as the symbol's static
              // initial value too. The runtime
              // code_frontend_assignt below populates the
              // symbol's actual storage at __CPROVER_initialize
              // time, but PLR §3.2 boundary helpers (and the
              // is_python_none recognizer) inspect the symbol
              // table's `value` field to identify defaults that
              // were `None` in source — without this, frozen
              // None defaults would look like an arbitrary
              // python_value-typed expression and the
              // None-marker rewrites for non-string targets
              // (Optional[int] = None, etc.) would not fire.
              ts.value = val;
              symbol_table.add(ts);
            }
            symbol_exprt frozen = symbol_table.lookup_ref(ti).symbol_expr();
            block.add(code_frontend_assignt{frozen, val});
            default_values[{fname, i}] = frozen;
          }
        }
      }
      // PLR §4.2.1: a return/parameter annotation that is a bare name
      // bound nowhere raises NameError when the `def` executes (def-time
      // annotation evaluation). Module-level defs are handled here (they
      // bypass convert_statement); nested defs in convert_statement. Emit
      // the NameError into the module block.
      {
        std::vector<const jsont *> anns;
        anns.push_back(&json_member(stmt, "returns"));
        for(const char *grp : {"posonlyargs", "args", "kwonlyargs"})
        {
          const jsont &lst = json_member(func_args, grp);
          if(lst.is_array())
            for(const auto &a : as_array(lst))
              anns.push_back(&json_member(a, "annotation"));
        }
        for(const jsont *ann : anns)
        {
          if(!undefined_annotation_name(*ann).empty())
          {
            auto saved = pending_checks;
            pending_checks.clear();
            emit_conditional_exception(true_exprt{}, "NameError");
            for(auto &chk : pending_checks)
              block.add(chk);
            pending_checks = saved;
            break;
          }
        }
      }
      // PLR §8.7: evaluate this def's default argument values at its
      // source-order position (module globals assigned earlier are established).
      {
        code_blockt cb;
        collect_def_time_default_checks(
          json_member(stmt, "args"), get_location(stmt), cb);
        for(const auto &s : cb.statements())
          block.add(s);
      }
      continue;
    }
    if(is_node_type(stmt, "ClassDef"))
    {
      // Add class object initialization
      std::string cls_name = json_string(json_member(stmt, "name"));
      main_module_defs.insert(cls_name); // Root B: in-scope main def
      irep_idt cls_id{"python::" + cls_name};
      const symbolt *cls_sym = symbol_table.lookup(cls_id);
      if(cls_sym != nullptr && !cls_sym->value.is_nil())
      {
        code_frontend_assignt init{cls_sym->symbol_expr(), cls_sym->value};
        block.add(std::move(init));
      }
      // PLR §8.7: method default argument values are evaluated when the class
      // body executes (class-def time); evaluate each method's defaults at this
      // source-order position.
      {
        const jsont &cbody = json_member(stmt, "body");
        if(cbody.is_array())
          for(const auto &m : as_array(cbody))
            if(
              is_node_type(m, "FunctionDef") ||
              is_node_type(m, "AsyncFunctionDef"))
            {
              code_blockt cb;
              collect_def_time_default_checks(
                json_member(m, "args"), get_location(stmt), cb);
              for(const auto &s : cb.statements())
                block.add(s);
            }
      }
      continue;
    }

    codet code = convert_statement(stmt);
    block.add(std::move(code));

    // Check for uncaught exceptions after each module-level statement.
    // We deliberately check after If/While/For/Try statements too,
    // because bugs triggered inside 'if __name__ == "__main__":' blocks
    // are common and our frontend otherwise silently swallows them.
    //
    // Skipped when --python-no-exception-checks is set — useful for
    // benchmark suites that consider only assertion failures as bugs.
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(
      !python_no_exception_checks && exc_sym != nullptr &&
      !is_node_type(stmt, "FunctionDef") &&
      !is_node_type(stmt, "AsyncFunctionDef") &&
      !is_node_type(stmt, "ClassDef") && !is_node_type(stmt, "Import") &&
      !is_node_type(stmt, "ImportFrom"))
    {
      source_locationt eloc = get_location(stmt);
      eloc.set_property_class("exception");
      eloc.set_comment("uncaught exception");
      code_assertt exc_check{not_exprt{exc_sym->symbol_expr()}};
      exc_check.add_source_location() = eloc;
      block.add(std::move(exc_check));
    }
  }

  return block;
}

// --- Main entry point ---

void python_convertert::process_imported_module(
  const std::string &module_name,
  const jsont &module_ast)
{
  // Transitive resolution is idempotent and cycle-safe: a module is
  // processed at most once. main.py may import the same module twice
  // (`import ll` + `from ll import Foo`), and modules may import each
  // other; the guard makes both cases a no-op after the first pass.
  if(!processed_import_modules.insert(module_name).second)
    return;

  bool saved_processing = processing_import;
  processing_import = true;

  // Swap filename so source locations inside the imported
  // module point at the stub file rather than the main
  // source. Restore on exit.
  std::string saved_filename = filename;
  if(module_path_resolver)
  {
    std::string mp = module_path_resolver(module_name);
    if(!mp.empty())
      filename = mp;
  }
  // Process the module's top-level definitions:
  // - FunctionDef → register as python::module_name::func_name
  // - ClassDef → register class type and constructor
  // - Assign/AnnAssign → register module-level variables
  const jsont &body = json_member(module_ast, "body");
  if(!body.is_array())
    return;

  // PLR §7.12: an imported module's functions can mutate that module's
  // globals (`from modx import state, mutate; mutate()` then read
  // `state`). Scan this module's function bodies too so the post-call
  // invalidation covers globals mutated in imported modules — the
  // collector must cover the whole program, not just the main module.
  collect_function_global_mutations(body);
  collect_assigned_attr_names(body);

  std::string prefix = module_name + "::";

  for(const auto &stmt : as_array(body))
  {
    // Register top-level 'import MODULE' names so that downstream
    // references to e.g. 'sys' inside this imported module resolve
    // via the same module-value mechanism as main-file imports.
    if(is_node_type(stmt, "Import"))
    {
      const jsont &names = json_member(stmt, "names");
      if(names.is_array())
      {
        for(const auto &alias : as_array(names))
        {
          std::string nm = json_string(json_member(alias, "name"));
          std::string asnm = json_string(json_member(alias, "asname"));
          if(asnm.empty())
            asnm = nm;
          imported_modules.insert(asnm);
          irep_idt mod_sym_id{"python::" + asnm};
          if(symbol_table.lookup(mod_sym_id) == nullptr)
          {
            symbolt mod_sym{mod_sym_id, python_value_type(), "python"};
            mod_sym.base_name = asnm;
            mod_sym.is_lvalue = true;
            mod_sym.is_state_var = true;
            mod_sym.is_static_lifetime = true;
            symbol_table.add(mod_sym);
          }
          // Transitive resolution: process the imported module's own
          // sources so its definitions are available to this module's
          // bodies (e.g. `import md` here, then `md.Foo(...)` below).
          if(module_resolver && nm != "typing")
          {
            const jsont *sub_ast = module_resolver(nm);
            if(sub_ast != nullptr && !sub_ast->is_null())
              process_imported_module(nm, *sub_ast);
          }
        }
      }
      continue;
    }
    // Transitive `from <module> import ...`: resolve and process the
    // sub-module so its classes/functions are registered before this
    // module's function bodies (which may reference them) are
    // converted. Without this, e.g. ll.py's `from md import Foo` left
    // `Foo(...)` inside ll.create with no body.
    if(is_node_type(stmt, "ImportFrom"))
    {
      std::string submodule = json_string(json_member(stmt, "module"));
      if(module_resolver && !submodule.empty() && submodule != "typing")
      {
        const jsont *sub_ast = module_resolver(submodule);
        if(sub_ast != nullptr && !sub_ast->is_null())
          process_imported_module(submodule, *sub_ast);
      }
      continue;
    }
    if(
      is_node_type(stmt, "FunctionDef") ||
      is_node_type(stmt, "AsyncFunctionDef"))
    {
      std::string fname = json_string(json_member(stmt, "name"));
      // Root B: record that this name is defined in an imported
      // module (so a bare reference is in-scope only if it was
      // actually imported).
      imported_module_defs.insert(fname);
      // Constant-directed dispatcher folding: imported module functions
      // register under their FLAT name (call resolution binds
      // `boto3.client` to `python::client`) -- process_imported_module
      // does not reuse convert_function_def, so register here too.
      register_dispatcher_summary(
        "python::" + fname, stmt, /*first_param_index=*/0);
      // PLR §8.7 / typing.overload: @overload-decorated defs are
      // type-only stubs (empty `...` bodies). Skip them so the real
      // implementation (same name, no @overload) is the one registered;
      // otherwise the first stub claims `python::<fname>` and the real
      // body is dropped.
      {
        const jsont &decos = json_member(stmt, "decorator_list");
        bool is_overload = false;
        if(decos.is_array())
          for(const auto &dec : as_array(decos))
            if(
              is_node_type(dec, "Name") &&
              json_string(json_member(dec, "id")) == "overload")
              is_overload = true;
        if(is_overload)
          continue;
      }
      // Detect @c_intrinsic('NAME') decorators so imported library
      // stubs can route calls to C library functions (parallel to
      // the same detection in convert_function_def).
      {
        const jsont &decos = json_member(stmt, "decorator_list");
        if(decos.is_array())
        {
          for(const auto &dec : as_array(decos))
          {
            if(!is_node_type(dec, "Call"))
              continue;
            const jsont &dec_func = json_member(dec, "func");
            if(
              !is_node_type(dec_func, "Name") ||
              json_string(json_member(dec_func, "id")) != "c_intrinsic")
              continue;
            const jsont &dec_args = json_member(dec, "args");
            if(!dec_args.is_array() || as_array(dec_args).empty())
              continue;
            const jsont &first = *as_array(dec_args).begin();
            if(!is_node_type(first, "Constant"))
              continue;
            std::string c_name = json_string(json_member(first, "value"));
            if(!c_name.empty())
              c_intrinsic_map[irep_idt{"python::" + fname}] = c_name;
            // Pick up optional fold=, domain=, range=, int_width=
            // keywords.
            const jsont &dec_kwargs = json_member(dec, "keywords");
            if(!c_name.empty() && dec_kwargs.is_array())
            {
              for(const auto &kw : as_array(dec_kwargs))
              {
                const jsont &val = json_member(kw, "value");
                if(!is_node_type(val, "Constant"))
                  continue;
                std::string arg_name = json_string(json_member(kw, "arg"));
                if(arg_name == "int_width")
                {
                  // Integer literal: its stringified form is in
                  // .value directly (see parallel logic at
                  // convert_function_def).
                  const jsont &iv_node = json_member(val, "value");
                  try
                  {
                    int w = std::stoi(iv_node.value);
                    if(w == 32 || w == 64)
                      c_intrinsic_int_width_map[irep_idt{"python::" + fname}] =
                        w;
                  }
                  catch(...)
                  {
                  }
                  continue;
                }
                std::string arg_val = json_string(json_member(val, "value"));
                if(arg_val.empty())
                  continue;
                if(arg_name == "fold")
                  c_intrinsic_fold_map[irep_idt{"python::" + fname}] = arg_val;
                else if(arg_name == "domain")
                  c_intrinsic_domain_map[irep_idt{"python::" + fname}] =
                    arg_val;
                else if(arg_name == "range")
                  c_intrinsic_range_map[irep_idt{"python::" + fname}] = arg_val;
              }
            }
          }
        }
      }
      // Detect @may_raise('ExcType') decorators so imported library
      // stubs (e.g. os.* file ops) can declare the exception they may
      // raise; under --python-raising-ops-check a call to such a
      // function is modeled as may-raise (parallel to @c_intrinsic).
      {
        const jsont &decos = json_member(stmt, "decorator_list");
        if(decos.is_array())
        {
          for(const auto &dec : as_array(decos))
          {
            if(!is_node_type(dec, "Call"))
              continue;
            const jsont &dec_func = json_member(dec, "func");
            if(
              !is_node_type(dec_func, "Name") ||
              json_string(json_member(dec_func, "id")) != "may_raise")
              continue;
            const jsont &dec_args = json_member(dec, "args");
            if(!dec_args.is_array() || as_array(dec_args).empty())
              continue;
            const jsont &first = *as_array(dec_args).begin();
            if(!is_node_type(first, "Constant"))
              continue;
            std::string exc = json_string(json_member(first, "value"));
            if(!exc.empty())
              may_raise_map[irep_idt{"python::" + fname}] = exc;
          }
        }
      }
      // PLR §8.7: record this module-level function's trailing parameter
      // defaults. process_imported_module otherwise registers the function
      // symbol WITHOUT its defaults, so a call that omits them (e.g.
      // re.split(p, s) leaving maxsplit/flags, re.sub(p, r, s) leaving
      // count/flags) saw the missing params as unconstrained nondet -- the
      // module-call dispatch fills trailing params from default_values and
      // stops at the first gap. Only class methods were populated before
      // (defs.cpp), so a module function whose defaults did not collide with a
      // same-named method's defaults (e.g. re.split's `flags`, index 3, vs
      // Pattern.split which has no index 3) was left nondet. Mirrors the
      // main-module pre-pass in convert_module_body; keyed by bare name to
      // match the call-site lookup. (Whole-group fix: every imported
      // module-level function with defaulted trailing params.)
      {
        const jsont &fa = json_member(stmt, "args");
        const jsont &fparams = json_member(fa, "args");
        const jsont &fdefaults = json_member(fa, "defaults");
        if(fparams.is_array() && fdefaults.is_array())
        {
          const std::size_t np = as_array(fparams).size();
          const std::size_t nd = as_array(fdefaults).size();
          if(nd <= np)
          {
            const std::size_t first_default = np - nd;
            auto dit = as_array(fdefaults).begin();
            for(std::size_t i = first_default; i < np; i++, ++dit)
            {
              exprt val = convert_expression(*dit);
              if(val.is_nil() || val.type().id() == ID_pointer)
                continue;
              // Built-in container struct_tags are safe to snapshot; raw
              // class-instance structs are not (call-time evaluation).
              if(val.type().id() == ID_struct)
              {
                const std::string tag =
                  id2string(to_struct_type(val.type()).get_tag());
                if(
                  tag != "python_string" && tag != "python_tuple" &&
                  tag != "python_value" && tag != "python_set" &&
                  tag != "python_complex" && tag != "python_list")
                  continue;
              }
              default_values[{fname, i}] = val;
            }
          }
        }
      }
      // Register as a module-level function
      // The function will be callable as module.func()
      irep_idt sym_id{"python::" + fname};
      if(symbol_table.lookup(sym_id) == nullptr)
      {
        // Parse return type annotation
        typet ret_type = python_int_type();
        const jsont &returns = json_member(stmt, "returns");
        if(!returns.is_null())
          ret_type = convert_type_annotation(returns);
        if(ret_type.id() == ID_empty)
          ret_type = python_int_type();

        // PLR §6.10.5: if there's no return annotation, scan the
        // body for a Tuple-shaped return and infer the result as
        // a python_tuple_type. Mirror the inference in
        // convert_function_def, since process_imported_module
        // doesn't reuse that path.
        if(returns.is_null())
        {
          const jsont &fbody = json_member(stmt, "body");
          if(fbody.is_array())
          {
            for(const auto &bs : as_array(fbody))
            {
              if(!is_node_type(bs, "Return"))
                continue;
              const jsont &rv = json_member(bs, "value");
              if(!is_node_type(rv, "Tuple"))
                continue;
              const jsont &telts = json_member(rv, "elts");
              if(!telts.is_array() || as_array(telts).empty())
                continue;
              std::vector<typet> elem_types;
              for(const auto &e : as_array(telts))
              {
                typet et = python_int_type();
                if(is_node_type(e, "Constant"))
                {
                  const jsont &cv = json_member(e, "value");
                  if(cv.is_string())
                    et = python_string_type();
                  else if(cv.is_number())
                  {
                    std::string vs = cv.value;
                    if(
                      vs.find('.') != std::string::npos ||
                      vs.find('e') != std::string::npos)
                      et = double_type();
                  }
                }
                else if(is_node_type(e, "Name"))
                {
                  // Look for an enclosing AnnAssign 'name: T = ...'
                  // in the function body to recover the annotated
                  // type.
                  std::string nm = json_string(json_member(e, "id"));
                  for(const auto &bs2 : as_array(fbody))
                  {
                    if(!is_node_type(bs2, "AnnAssign"))
                      continue;
                    const jsont &target = json_member(bs2, "target");
                    if(
                      !is_node_type(target, "Name") ||
                      json_string(json_member(target, "id")) != nm)
                      continue;
                    const jsont &ann = json_member(bs2, "annotation");
                    if(!ann.is_null())
                      et = convert_type_annotation(ann);
                    break;
                  }
                  if(et == python_int_type())
                    et = double_type();
                }
                elem_types.push_back(et);
              }
              ret_type = python_tuple_type(elem_types);
              break;
            }
          }
        }

        // PLR §3.2: detect `return ClassName(args)` so functions
        // in imported library modules (e.g. re.match returning a
        // Match instance) carry the right return type. Without
        // this, the convert_return path inside the body would
        // safe_typecast the constructed instance to python_int
        // (the empty-annotation default), collapsing it to 0.
        if(returns.is_null())
        {
          const jsont &fbody = json_member(stmt, "body");
          if(fbody.is_array())
          {
            std::function<void(const jsont &)> scan_class_returns =
              [&](const jsont &nodes)
            {
              if(!nodes.is_array())
                return;
              for(const auto &bs : as_array(nodes))
              {
                if(is_node_type(bs, "Return"))
                {
                  const jsont &rv = json_member(bs, "value");
                  if(
                    is_node_type(rv, "Call") &&
                    is_node_type(json_member(rv, "func"), "Name"))
                  {
                    std::string call_name =
                      json_string(json_member(json_member(rv, "func"), "id"));
                    auto cit = class_types.find(call_name);
                    if(cit != class_types.end())
                    {
                      const typet &this_type = cit->second;
                      if(ret_type == python_int_type())
                        ret_type = this_type;
                      else if(ret_type != this_type)
                        ret_type = python_value_type();
                    }
                  }
                }
                // Recurse into common nested-control bodies.
                for(const char *key : {"body", "orelse", "finalbody"})
                {
                  const jsont &child = json_member(bs, key);
                  if(child.is_array())
                    scan_class_returns(child);
                }
                const jsont &handlers = json_member(bs, "handlers");
                if(handlers.is_array())
                {
                  for(const auto &h : as_array(handlers))
                  {
                    const jsont &hb = json_member(h, "body");
                    if(hb.is_array())
                      scan_class_returns(hb);
                  }
                }
              }
            };
            scan_class_returns(fbody);
          }
        }

        // Parse parameters
        code_typet::parameterst params;
        const jsont &args_node = json_member(stmt, "args");
        auto collect_params = [&](const jsont &list)
        {
          if(list.is_array())
          {
            for(const auto &p : as_array(list))
            {
              std::string pname = json_string(json_member(p, "arg"));
              const jsont &ann = json_member(p, "annotation");
              typet ptype = ann.is_null() ? python_value_type()
                                          : convert_type_annotation(ann);
              // PLR §3.1: mutable containers are passed by reference.
              // Mirror convert_function_def's add_positional.
              if(
                pname != "self" &&
                (is_python_list_type(ptype) || is_python_dict_type(ptype)))
              {
                ptype = pointer_type(ptype);
              }
              code_typet::parametert param{ptype};
              param.set_identifier("python::" + fname + "::" + pname);
              param.set_base_name(pname);
              params.push_back(param);
            }
          }
        };
        collect_params(json_member(args_node, "posonlyargs"));
        collect_params(json_member(args_node, "args"));
        collect_params(json_member(args_node, "kwonlyargs"));
        const jsont &vararg = json_member(args_node, "vararg");
        if(!vararg.is_null())
        {
          std::string va_name = json_string(json_member(vararg, "arg"));
          code_typet::parametert param{python_list_type(python_value_type())};
          param.set_identifier("python::" + fname + "::" + va_name);
          param.set_base_name(va_name);
          params.push_back(param);
        }
        const jsont &kwarg = json_member(args_node, "kwarg");
        if(!kwarg.is_null())
        {
          std::string kw_name = json_string(json_member(kwarg, "arg"));
          code_typet::parametert param{
            python_dict_type(python_string_type(), python_value_type())};
          param.set_identifier("python::" + fname + "::" + kw_name);
          param.set_base_name(kw_name);
          params.push_back(param);
        }

        // Architectural: imported-module functions share the SAME return-type
        // inference as convert_function_def, rather than the partial Tuple/
        // class scans above. This is what makes an unannotated X-or-None
        // function infer Optional (python_value) instead of erasing None by
        // coercing the `return None` branch to X (a latent false proof —
        // see plans §0). Generators (has_yield) and no-value-return functions
        // keep the existing handling.
        if(returns.is_null())
        {
          inferred_returnt inf = infer_return_type_from_body(
            json_member(stmt, "body"), params, fname, "");
          if(!inf.has_yield && inf.type.id() != ID_empty)
            ret_type = inf.type;
        }

        code_typet func_type{params, ret_type};
        symbolt func_sym{sym_id, func_type, "python"};
        func_sym.base_name = fname;
        func_sym.is_lvalue = true;
        symbol_table.add(func_sym);

        // Create parameter symbols
        for(const auto &param : params)
        {
          if(symbol_table.lookup(param.get_identifier()) == nullptr)
          {
            symbolt psym{param.get_identifier(), param.type(), "python"};
            psym.base_name = param.get_base_name();
            psym.is_lvalue = true;
            psym.is_state_var = true;
            psym.is_parameter = true;
            symbol_table.add(psym);
          }
        }

        // Convert the function body
        std::string saved_func = current_function;
        if(!current_function.empty())
          enclosing_functions.push_back(current_function);
        current_function = fname;
        code_blockt body_block;
        // Lazy-stubs mode: skip body conversion entirely for
        // imported-module functions. Calls to the function
        // return nondet (the symex fallback for no-body
        // callees). This skips large stub method bodies with
        // their embedded assertions — the stub becomes a pure
        // type surface. The main-file functions are handled
        // by convert_function_def (non-lazy) so they still
        // get their bodies.
        if(!python_lazy_stubs)
        {
          const jsont &func_body = json_member(stmt, "body");
          if(func_body.is_array())
          {
            for(const auto &s : as_array(func_body))
              body_block.add(convert_statement(s));
          }
          // Add default return
          if(ret_type.id() != ID_empty)
            body_block.add(code_frontend_returnt{safe_zero(ret_type)});
        }
        current_function = saved_func;
        if(
          !enclosing_functions.empty() &&
          enclosing_functions.back() == saved_func)
          enclosing_functions.pop_back();

        symbol_table.get_writeable_ref(sym_id).value = body_block;
      }
    }
    else if(is_node_type(stmt, "ClassDef"))
    {
      // Process class definition from the module
      convert_class_def(stmt);
    }
    else if(is_node_type(stmt, "Assign") || is_node_type(stmt, "AnnAssign"))
    {
      // Module-level variable — register as a global symbol.
      //
      // General case: a module-level constant binding
      // `name = <const>` / `name: T = <const>` is registered as
      // `python::name` (unprefixed, mirroring how classes and
      // functions are registered) carrying its value, so that a
      // `from MODULE import name` in the importer binds the actual
      // value. Without this, the imported name is undefined and
      // assertions over it collapse to vacuously-true — a false
      // proof (github_2897_2_fail).
      {
        const jsont *target = nullptr;
        if(is_node_type(stmt, "AnnAssign"))
          target = &json_member(stmt, "target");
        else
        {
          const jsont &tgts = json_member(stmt, "targets");
          if(tgts.is_array() && !as_array(tgts).empty())
            target = &(*as_array(tgts).begin());
        }
        const jsont &value = json_member(stmt, "value");
        // nondet_* intrinsic initializer (library stubs: `argv: list =
        // nondet_list(4, nondet_str())`): register the symbol WITHOUT a value
        // -- an uninitialised global is nondet, which is exactly the model.
        // The declared/converted TYPE is what matters (a list struct makes
        // `sys.argv[i]` subscriptable with proper bounds checks; the guard
        // `len(sys.argv) == 2` then constrains the same symbol's length).
        bool nondet_init = false;
        if(is_node_type(value, "Call"))
        {
          const jsont &cf = json_member(value, "func");
          if(
            is_node_type(cf, "Name") &&
            json_string(json_member(cf, "id")).rfind("nondet_", 0) == 0)
            nondet_init = true;
        }
        if(
          target != nullptr && is_node_type(*target, "Name") &&
          (is_node_type(value, "Constant") || is_node_type(value, "Dict") ||
           is_node_type(value, "List") || nondet_init))
        {
          std::string vname = json_string(json_member(*target, "id"));
          irep_idt vid{"python::" + vname};
          if(symbol_table.lookup(vid) == nullptr)
          {
            exprt v = convert_expression(value);
            if(v.is_not_nil())
            {
              symbolt vs{vid, v.type(), "python"};
              vs.base_name = vname;
              vs.is_lvalue = true;
              vs.is_state_var = true;
              vs.is_static_lifetime = true;
              // A nondet initializer registers the TYPE only; the global stays
              // uninitialised (= nondet), the sound model.
              if(!nondet_init)
                vs.value = v;
              symbol_table.add(vs);
            }
          }
        }
      }
      //
      // Special case: TypedDict definitions of the functional
      // form 'Name = TypedDict("Name", {"K": Required[T], ...})'
      // are the PySpec stub convention. Scan for these and
      // record the Required field names so the Unpack-method
      // handler in convert_class_def can emit key-presence
      // checks without processing the full stub body.
      if(is_node_type(stmt, "Assign"))
      {
        const jsont &targets = json_member(stmt, "targets");
        const jsont &value = json_member(stmt, "value");
        if(
          targets.is_array() && !as_array(targets).empty() &&
          is_node_type(value, "Call"))
        {
          const jsont &callee = json_member(value, "func");
          std::string cname;
          if(is_node_type(callee, "Name"))
            cname = json_string(json_member(callee, "id"));
          if(cname == "TypedDict")
          {
            const jsont &first_target = *as_array(targets).begin();
            if(is_node_type(first_target, "Name"))
            {
              std::string td_name =
                json_string(json_member(first_target, "id"));
              const jsont &cargs = json_member(value, "args");
              if(cargs.is_array() && as_array(cargs).size() >= 2)
              {
                auto it = as_array(cargs).begin();
                ++it;
                const jsont &dict_ast = *it;
                if(is_node_type(dict_ast, "Dict"))
                {
                  const jsont &keys = json_member(dict_ast, "keys");
                  const jsont &values = json_member(dict_ast, "values");
                  if(
                    keys.is_array() && values.is_array() &&
                    as_array(keys).size() == as_array(values).size())
                  {
                    std::vector<std::string> required;
                    std::map<std::string, std::string> field_types;
                    auto kit = as_array(keys).begin();
                    auto vit = as_array(values).begin();
                    for(; kit != as_array(keys).end(); ++kit, ++vit)
                    {
                      // Key is Constant(str). Value is
                      // Subscript of Required[...] or
                      // NotRequired[...].
                      if(!is_node_type(*kit, "Constant"))
                        continue;
                      std::string kstr =
                        json_string(json_member(*kit, "value"));
                      if(kstr.empty())
                        continue;
                      if(!is_node_type(*vit, "Subscript"))
                        continue;
                      const jsont &vv = json_member(*vit, "value");
                      if(!is_node_type(vv, "Name"))
                        continue;
                      std::string wrapper = json_string(json_member(vv, "id"));
                      if(wrapper == "Required")
                        required.push_back(kstr);
                      // Record the field's underlying type
                      // category. The slice of Required[T] /
                      // NotRequired[T] is T, which can be:
                      //   * Name(id="str"|"int"|"float"|"bool"|
                      //                "bytes"|"list"|"dict"|
                      //                "set")  — recorded as is
                      //   * Subscript(Name("List"|...), [...])
                      //                       — strip to outer
                      //   * anything else     — skipped
                      const jsont &slice = json_member(*vit, "slice");
                      auto category = [&](const jsont &n) -> std::string
                      {
                        if(is_node_type(n, "Name"))
                        {
                          std::string id = json_string(json_member(n, "id"));
                          static const std::set<std::string> known{
                            "str",
                            "int",
                            "float",
                            "bool",
                            "bytes",
                            "list",
                            "dict",
                            "set",
                            "List",
                            "Dict",
                            "Set",
                            "Tuple",
                            "FrozenSet"};
                          if(known.count(id) > 0)
                          {
                            // Map capital aliases to lower-case
                            // categories.
                            if(id == "List")
                              return "list";
                            if(id == "Dict")
                              return "dict";
                            if(id == "Set" || id == "FrozenSet")
                              return "set";
                            if(id == "Tuple")
                              return "tuple";
                            return id;
                          }
                        }
                        if(is_node_type(n, "Subscript"))
                        {
                          const jsont &sv = json_member(n, "value");
                          if(is_node_type(sv, "Name"))
                          {
                            std::string id = json_string(json_member(sv, "id"));
                            if(id == "List")
                              return "list";
                            if(id == "Dict")
                              return "dict";
                            if(id == "Set")
                              return "set";
                            if(id == "Tuple")
                              return "tuple";
                          }
                        }
                        return std::string{};
                      };
                      std::string cat = category(slice);
                      if(!cat.empty())
                        field_types[kstr] = cat;
                    }
                    if(!required.empty())
                      typed_dict_required[td_name] = std::move(required);
                    if(!field_types.empty())
                      typed_dict_field_types[td_name] = std::move(field_types);
                  }
                }
              }
            }
          }
        }
      }
    }
    else if(is_node_type(stmt, "ImportFrom"))
    {
      // Handle from-imports that register known functions
      std::string mod = json_string(json_member(stmt, "module"));
      if(mod == "re")
      {
        const jsont &names = json_member(stmt, "names");
        if(names.is_array())
        {
          for(const auto &alias : as_array(names))
          {
            std::string nm = json_string(json_member(alias, "name"));
            std::string as = json_string(json_member(alias, "asname"));
            if(as.empty())
              as = nm;
            irep_idt fid{"python::" + as};
            if(symbol_table.lookup(fid) == nullptr)
            {
              code_typet ft{
                {code_typet::parametert{python_string_type()}},
                python_int_type()};
              symbolt fs{fid, ft, "python"};
              fs.base_name = as;
              fs.is_lvalue = true;
              symbol_table.add(fs);
            }
          }
        }
      }
    }
  }
  processing_import = saved_processing;
  filename = saved_filename;
}

bool python_convertert::convert()
{
  // Native backend: ensure the strtab UF symbol exists up-front --
  // python_dict_unbox_key (a free function) references it by fixed name
  // for HANDLE keys, so the symbol-table entry must exist even when no
  // handle-allocation site ran first.
  if(python_smt_string_native_flag())
    strtab_symbol();
  if(python_unbounded_ints_flag())
    inttab_symbol();

  const jsont &body = json_member(parse_tree.ast_json, "body");

  // PLR §4.2.1: the authoritative over-inclusive set of names bound
  // anywhere in the main module (computed by the AST server). The
  // undefined-name NameError check gates on absence from this set.
  {
    const jsont &bn = json_member(parse_tree.ast_json, "_all_bound_names");
    if(bn.is_array())
      for(const auto &n : as_array(bn))
        if(n.is_string())
          all_bound_names.insert(json_string(n));
  }

  // PEP 563: `from __future__ import annotations` defers all annotations to
  // strings (not evaluated), so they never raise NameError — disable the
  // undefined-annotation check when present.
  if(body.is_array())
    for(const auto &stmt : as_array(body))
    {
      if(!is_node_type(stmt, "ImportFrom"))
        continue;
      if(json_string(json_member(stmt, "module")) != "__future__")
        continue;
      const jsont &names = json_member(stmt, "names");
      if(names.is_array())
        for(const auto &a : as_array(names))
          if(json_string(json_member(a, "name")) == "annotations")
            future_annotations = true;
    }
  // PEP 649/749 (Python 3.14): annotations are LAZILY evaluated by default --
  // `def f() -> Any:` without importing Any does NOT raise NameError at the
  // def; evaluation happens only on __annotations__ access. Version-dependent
  // PLR semantics: follow the parsing interpreter's version (reported by the
  // AST payload as `_python_version`), so verification matches the CPython the
  // program would actually run under. Under <= 3.13 the eager check stays
  // (a missing `from typing import Any` IS a def-time NameError).
  {
    const jsont &pv = json_member(parse_tree.ast_json, "_python_version");
    if(pv.is_array() && as_array(pv).size() >= 2)
    {
      const auto to_int = [](const jsont &j) -> int
      {
        if(j.value.empty())
          return -1;
        for(const char ch : j.value)
          if(!isdigit(ch))
            return -1;
        return std::atoi(j.value.c_str());
      };
      auto it = as_array(pv).begin();
      const int maj = to_int(*it);
      ++it;
      const int min = to_int(*it);
      if(maj > 3 || (maj == 3 && min >= 14))
        future_annotations = true;
    }
  }

  // PLR §7.12: pre-scan all function/method bodies for global mutations
  // (dict subscript-assign, `global X` rebind, dict-mutating methods) so
  // the post-call invalidation only invalidates globals that some
  // function actually mutates (never-mutated globals keep their
  // conversion-time folding). Must run before any function body /
  // module statement is converted.
  collect_function_global_mutations(body);
  collect_assigned_attr_names(body);

  // PLR §8.13: pre-scan top-level `from enum import <base> as <alias>` so
  // a class deriving from the alias is recognised as an enum during the
  // (earlier) class-definition / signature passes. The standard base
  // names are already seeded in enum_base_aliases.
  if(body.is_array())
    for(const auto &stmt : as_array(body))
    {
      if(!is_node_type(stmt, "ImportFrom"))
        continue;
      if(json_string(json_member(stmt, "module")) != "enum")
        continue;
      const jsont &names = json_member(stmt, "names");
      if(!names.is_array())
        continue;
      for(const auto &alias : as_array(names))
      {
        std::string nm = json_string(json_member(alias, "name"));
        std::string as = json_string(json_member(alias, "asname"));
        if(
          (nm == "Enum" || nm == "IntEnum" || nm == "IntFlag" || nm == "Flag" ||
           nm == "StrEnum" || nm == "ReprEnum") &&
          !as.empty())
          enum_base_aliases.insert(as);
      }
    }
  // Now (aliases known) pre-scan top-level enum class definitions so their
  // member names and value type are available to every later pass — in
  // particular parameter-annotation conversion, which runs before
  // convert_class_def.
  if(body.is_array())
    for(const auto &stmt : as_array(body))
    {
      if(!is_node_type(stmt, "ClassDef"))
        continue;
      const jsont &cbases = json_member(stmt, "bases");
      bool is_enum = false;
      if(cbases.is_array())
        for(const auto &b : as_array(cbases))
          if(
            is_node_type(b, "Name") &&
            enum_base_aliases.count(json_string(json_member(b, "id"))))
            is_enum = true;
      if(!is_enum)
        continue;
      const std::string cname = json_string(json_member(stmt, "name"));
      auto &members = enum_members[cname];
      typet &vtype = enum_value_type[cname];
      vtype = python_int_type();
      const jsont &cbody = json_member(stmt, "body");
      if(cbody.is_array())
        for(const auto &item : as_array(cbody))
        {
          const jsont *tgt = nullptr;
          const jsont *valnode = nullptr;
          if(is_node_type(item, "Assign"))
          {
            const jsont &tgts = json_member(item, "targets");
            if(tgts.is_array() && !as_array(tgts).empty())
              tgt = &(*as_array(tgts).begin());
            valnode = &json_member(item, "value");
          }
          else if(is_node_type(item, "AnnAssign"))
          {
            tgt = &json_member(item, "target");
            valnode = &json_member(item, "value");
          }
          if(tgt != nullptr && is_node_type(*tgt, "Name"))
          {
            const bool first = members.empty();
            members.insert(json_string(json_member(*tgt, "id")));
            if(
              first && valnode != nullptr &&
              is_node_type(*valnode, "Constant") &&
              json_member(*valnode, "value").is_string())
              vtype = python_string_type();
          }
        }
    }

  // Register python_value_type as a named type in the symbol table.
  // This enables self-referential types (list[python_value_type])
  // via struct_tag_typet.
  {
    irep_idt tag_id{PYTHON_VALUE_TAG};
    if(symbol_table.lookup(tag_id) == nullptr)
    {
      type_symbolt type_sym{tag_id, python_value_struct_def(), "python"};
      type_sym.base_name = "python_value";
      symbol_table.add(type_sym);
    }
  }
  {
    irep_idt tag_id{PYTHON_STRING_TAG};
    if(symbol_table.lookup(tag_id) == nullptr)
    {
      type_symbolt type_sym{tag_id, python_string_struct_def(), "python"};
      type_sym.base_name = "__CPROVER_refined_string_type";
      symbol_table.add(type_sym);
    }
  }
  // Register python_set_type as a named type in the symbol table.
  // This makes is_python_set_type and downstream emission work
  // through a single struct_tag_typet rather than a fresh
  // struct_typet at every call site.
  {
    irep_idt tag_id{PYTHON_SET_TAG};
    if(symbol_table.lookup(tag_id) == nullptr)
    {
      type_symbolt type_sym{tag_id, python_set_struct_def(), "python"};
      type_sym.base_name = "python_set";
      symbol_table.add(type_sym);
    }
  }

  // Create __python_exception_active flag early (needed during function
  // body conversion for raise statements)
  irep_idt exc_flag_id{"python::__exception_active"};
  if(symbol_table.lookup(exc_flag_id) == nullptr)
  {
    symbolt exc_symbol{exc_flag_id, bool_typet{}, "python"};
    exc_symbol.base_name = "__exception_active";
    exc_symbol.is_static_lifetime = true;
    exc_symbol.is_state_var = true;
    exc_symbol.is_lvalue = true;
    exc_symbol.value = false_exprt{};
    symbol_table.add(exc_symbol);
  }

  // Exception type variable (string hash for type name matching)
  irep_idt exc_type_id{"python::__exception_type"};
  if(symbol_table.lookup(exc_type_id) == nullptr)
  {
    symbolt exc_type_sym{exc_type_id, python_int_type(), "python"};
    exc_type_sym.base_name = "__exception_type";
    exc_type_sym.is_static_lifetime = true;
    exc_type_sym.is_state_var = true;
    exc_type_sym.is_lvalue = true;
    exc_type_sym.value = from_integer(0, python_int_type());
    symbol_table.add(exc_type_sym);
  }

  // Pass 0.1: resolve imports so module symbols are available.
  // Note: Python allows import statements inside function bodies
  // (`def f(): import asyncio`). We collect imports from the whole
  // module tree — including nested function and class bodies —
  // so library models are loaded regardless of where the import
  // appears. References to these modules from anywhere in the file
  // share the same library-backed symbol table.
  std::vector<jsont> all_imports;
  std::function<void(const jsont &)> collect_imports = [&](const jsont &b)
  {
    if(!b.is_array())
      return;
    for(const auto &stmt : as_array(b))
    {
      if(is_node_type(stmt, "Import") || is_node_type(stmt, "ImportFrom"))
        all_imports.push_back(stmt);
      // Recurse into bodies that may contain nested imports.
      if(
        is_node_type(stmt, "FunctionDef") ||
        is_node_type(stmt, "AsyncFunctionDef") ||
        is_node_type(stmt, "ClassDef"))
        collect_imports(json_member(stmt, "body"));
      else if(
        is_node_type(stmt, "If") || is_node_type(stmt, "While") ||
        is_node_type(stmt, "For") || is_node_type(stmt, "With") ||
        is_node_type(stmt, "Try"))
      {
        collect_imports(json_member(stmt, "body"));
        collect_imports(json_member(stmt, "orelse"));
        collect_imports(json_member(stmt, "finalbody"));
        const jsont &handlers = json_member(stmt, "handlers");
        if(handlers.is_array())
        {
          for(const auto &h : as_array(handlers))
            collect_imports(json_member(h, "body"));
        }
      }
    }
  };
  collect_imports(body);

  // Process collected imports via the same machinery as before.
  for(const auto &stmt : all_imports)
  {
    if(is_node_type(stmt, "Import"))
    {
      const jsont &names = json_member(stmt, "names");
      if(names.is_array())
      {
        for(const auto &alias : as_array(names))
        {
          std::string name = json_string(json_member(alias, "name"));
          std::string asname = json_string(json_member(alias, "asname"));
          if(asname.empty())
            asname = name;
          imported_modules.insert(asname);
          // Bind the imported module's name as a module symbol so
          // that attribute accesses and calls on it don't resolve
          // as 'Unknown variable'. Under-approximated as a nondet
          // value; proper modelling belongs to Step 2.
          irep_idt mod_sym_id{"python::" + asname};
          if(symbol_table.lookup(mod_sym_id) == nullptr)
          {
            symbolt mod_sym{mod_sym_id, python_value_type(), "python"};
            mod_sym.base_name = asname;
            mod_sym.is_lvalue = true;
            mod_sym.is_state_var = true;
            mod_sym.is_static_lifetime = true;
            symbol_table.add(mod_sym);
          }
          // Also process the library file so its decorators
          // populate c_intrinsic_{fold,domain,range,int_width}_map.
          // Previously only ImportFrom triggered library
          // loading, which meant 'import math; math.sqrt(x)'
          // didn't reach the decorator semantics. The
          // attribute-style handler uses these maps, so both
          // import forms now share the same source of truth.
          if(module_resolver && name != "typing")
          {
            const jsont *mod_ast = module_resolver(name);
            if(mod_ast != nullptr && !mod_ast->is_null())
              process_imported_module(name, *mod_ast);
            else
            {
              // Import failed to resolve — mark the import
              // alias (or module name) so subsequent calls
              // to it skip the no-body property.
              std::string alias_name =
                json_string(json_member(alias, "asname"));
              unresolved_imports.insert(alias_name.empty() ? name : alias_name);
            }
          }
        }
      }
    }
    else if(is_node_type(stmt, "ImportFrom"))
    {
      std::string module = json_string(json_member(stmt, "module"));
      if(module_resolver && module != "typing")
      {
        const jsont *mod_ast = module_resolver(module);
        if(mod_ast != nullptr && !mod_ast->is_null())
          process_imported_module(module, *mod_ast);
        else
        {
          // 'from <module> import X, Y': register X and Y
          // (or asnames) as unresolved names.
          const jsont &names_arr = json_member(stmt, "names");
          if(names_arr.is_array())
          {
            for(const auto &alias : as_array(names_arr))
            {
              std::string imp_name = json_string(json_member(alias, "name"));
              std::string asn = json_string(json_member(alias, "asname"));
              unresolved_imports.insert(asn.empty() ? imp_name : asn);
            }
          }
        }
      }
    }
  }

  // Pass 0.25: pre-register class names so type annotations can reference
  // them during pass 0 (e.g., x: MyClass = MyClass()).
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if(is_node_type(stmt, "ClassDef"))
      {
        std::string name = json_string(json_member(stmt, "name"));
        if(!class_types.count(name))
        {
          struct_typet::componentst comps;
          comps.push_back(
            struct_typet::componentt{"__class_tag", signedbv_typet{32}});
          struct_typet placeholder{comps};
          placeholder.set_tag("python_class_" + name);
          class_types[name] = placeholder;
          class_tag_ids[name] = static_cast<int>(class_tag_ids.size()) + 1;
        }
      }
      // PLR §22.7.1: typing.NewType callable aliases. Scan for
      // module-level 'X = NewType(...)' / 'X = t.NewType(...)' /
      // 'X = typing.NewType(...)' so the alias is registered
      // before function bodies are processed (a function body
      // calling X(arg) is converted before the module-level
      // assignment is evaluated, otherwise).
      if(is_node_type(stmt, "Assign"))
      {
        const jsont &v = json_member(stmt, "value");
        if(is_node_type(v, "Call"))
        {
          const jsont &fn = json_member(v, "func");
          bool is_newtype = false;
          if(
            is_node_type(fn, "Name") &&
            json_string(json_member(fn, "id")) == "NewType")
            is_newtype = true;
          else if(
            is_node_type(fn, "Attribute") &&
            json_string(json_member(fn, "attr")) == "NewType")
          {
            const jsont &recv = json_member(fn, "value");
            if(is_node_type(recv, "Name"))
            {
              std::string rn = json_string(json_member(recv, "id"));
              if(rn == "typing" || rn == "t")
                is_newtype = true;
            }
          }
          if(is_newtype)
          {
            const jsont &targets = json_member(stmt, "targets");
            if(targets.is_array())
              for(const auto &tgt : as_array(targets))
                if(is_node_type(tgt, "Name"))
                  newtype_aliases.insert(json_string(json_member(tgt, "id")));
          }
        }
      }
    }
  }

  // Pass 0.27: PLR 6.3.1 dynamic-attribute discovery from free
  // functions. A function `def f(x: A): x.v = 1` introduces
  // attribute `v` on the class A even though it's not declared
  // in any of A's methods. Since GOTO structs are static, we
  // collect such attrs here so convert_class_def can declare
  // them at class-definition time.
  if(body.is_array())
  {
    std::function<void(const jsont &)> scan_func_for_dyn_attrs =
      [&](const jsont &fn_node)
    {
      const jsont &args_node = json_member(fn_node, "args");
      const jsont &params = json_member(args_node, "args");
      std::map<std::string, std::string> param_to_class;
      if(params.is_array())
      {
        for(const auto &p : as_array(params))
        {
          const jsont &ann = json_member(p, "annotation");
          std::string pname = json_string(json_member(p, "arg"));
          if(is_node_type(ann, "Name"))
          {
            std::string ann_name = json_string(json_member(ann, "id"));
            if(class_types.count(ann_name))
              param_to_class[pname] = ann_name;
          }
        }
      }
      if(param_to_class.empty())
        return;
      std::function<void(const jsont &)> scan_body = [&](const jsont &b)
      {
        if(!b.is_array())
          return;
        for(const auto &s : as_array(b))
        {
          auto record_target = [&](const jsont &target)
          {
            if(!is_node_type(target, "Attribute"))
              return;
            const jsont &tv = json_member(target, "value");
            if(!is_node_type(tv, "Name"))
              return;
            std::string base = json_string(json_member(tv, "id"));
            auto it = param_to_class.find(base);
            if(it == param_to_class.end())
              return;
            std::string attr = json_string(json_member(target, "attr"));
            dynamic_class_attrs[it->second].insert(attr);
          };
          if(is_node_type(s, "Assign"))
          {
            const jsont &targets = json_member(s, "targets");
            if(targets.is_array())
              for(const auto &t : as_array(targets))
                record_target(t);
          }
          else if(is_node_type(s, "AnnAssign"))
            record_target(json_member(s, "target"));
          // Recurse into nested control-flow blocks.
          if(
            is_node_type(s, "If") || is_node_type(s, "While") ||
            is_node_type(s, "For") || is_node_type(s, "With") ||
            is_node_type(s, "Try"))
          {
            scan_body(json_member(s, "body"));
            scan_body(json_member(s, "orelse"));
            scan_body(json_member(s, "finalbody"));
            const jsont &handlers = json_member(s, "handlers");
            if(handlers.is_array())
              for(const auto &h : as_array(handlers))
                scan_body(json_member(h, "body"));
          }
        }
      };
      scan_body(json_member(fn_node, "body"));
    };
    for(const auto &stmt : as_array(body))
    {
      if(
        is_node_type(stmt, "FunctionDef") ||
        is_node_type(stmt, "AsyncFunctionDef"))
        scan_func_for_dyn_attrs(stmt);
    }
  }

  // Pass 0.27b (PLR §6.3.1): extend dynamic instance-attribute discovery to
  // LOCAL variables bound to a class instance (`c = C(); c.y = 42` /
  // `c: C = ...`), not only typed parameters (handled above). The struct type
  // is fixed at class-definition time, so an attribute assigned to such a
  // local must be declared as a field up front, else `c.y = 42` is lost and
  // `c.y == 42` fails. Whole-program scan (recurses into all nested bodies);
  // var->class is a SET (over-approximation across same-named locals is sound
  // -- it only adds spare fields). Method-named attributes are skipped:
  // shadowing a method with an instance attribute is a separate substrate
  // (method-shadow-knownbug). Sound: a discovered attr becomes a struct field,
  // consistent with the existing model (which does not flow-sensitively raise
  // AttributeError on attribute reads).
  if(body.is_array())
  {
    // class -> declared method names (to skip method shadowing).
    std::map<std::string, std::set<std::string>> class_method_names;
    std::function<void(const jsont &)> collect_methods = [&](const jsont &b)
    {
      if(!b.is_array())
        return;
      for(const auto &s : as_array(b))
      {
        if(is_node_type(s, "ClassDef"))
        {
          const std::string cn = json_string(json_member(s, "name"));
          const jsont &cb = json_member(s, "body");
          if(cb.is_array())
            for(const auto &m : as_array(cb))
              if(
                is_node_type(m, "FunctionDef") ||
                is_node_type(m, "AsyncFunctionDef"))
                class_method_names[cn].insert(
                  json_string(json_member(m, "name")));
        }
        for(const char *k : {"body", "orelse", "finalbody"})
        {
          const jsont &sub = json_member(s, k);
          if(sub.is_array())
            collect_methods(sub);
        }
      }
    };
    collect_methods(body);

    // var name -> set of classes it is bound to (c = C() / c: C = ...).
    std::map<std::string, std::set<std::string>> var_classes;
    std::function<void(const jsont &)> collect_vars = [&](const jsont &b)
    {
      if(!b.is_array())
        return;
      for(const auto &s : as_array(b))
      {
        if(is_node_type(s, "Assign"))
        {
          const jsont &val = json_member(s, "value");
          if(is_node_type(val, "Call"))
          {
            const jsont &f = json_member(val, "func");
            if(is_node_type(f, "Name"))
            {
              const std::string cn = json_string(json_member(f, "id"));
              if(class_types.count(cn))
              {
                const jsont &tgts = json_member(s, "targets");
                if(tgts.is_array())
                  for(const auto &t : as_array(tgts))
                    if(is_node_type(t, "Name"))
                      var_classes[json_string(json_member(t, "id"))].insert(cn);
              }
            }
          }
        }
        else if(is_node_type(s, "AnnAssign"))
        {
          const jsont &tgt = json_member(s, "target");
          const jsont &ann = json_member(s, "annotation");
          if(is_node_type(tgt, "Name") && is_node_type(ann, "Name"))
          {
            const std::string cn = json_string(json_member(ann, "id"));
            if(class_types.count(cn))
              var_classes[json_string(json_member(tgt, "id"))].insert(cn);
          }
        }
        for(const char *k : {"body", "orelse", "finalbody"})
        {
          const jsont &sub = json_member(s, k);
          if(sub.is_array())
            collect_vars(sub);
        }
        const jsont &handlers = json_member(s, "handlers");
        if(handlers.is_array())
          for(const auto &h : as_array(handlers))
            collect_vars(json_member(h, "body"));
      }
    };
    collect_vars(body);

    // Record `<var>.attr = ...` for every class `var` may hold (skip methods,
    // skip `self` which the per-class method scan already handles).
    std::function<void(const jsont &)> scan_attr_targets = [&](const jsont &b)
    {
      if(!b.is_array())
        return;
      auto record = [&](const jsont &target)
      {
        if(!is_node_type(target, "Attribute"))
          return;
        const jsont &tv = json_member(target, "value");
        if(!is_node_type(tv, "Name"))
          return;
        const std::string base = json_string(json_member(tv, "id"));
        if(base == "self")
          return;
        auto vit = var_classes.find(base);
        if(vit == var_classes.end())
          return;
        const std::string attr = json_string(json_member(target, "attr"));
        for(const std::string &cn : vit->second)
        {
          auto mit = class_method_names.find(cn);
          if(mit != class_method_names.end() && mit->second.count(attr))
          {
            // Method shadowing: an instance attribute shadows a same-named
            // method. Declare a python_value storage field (via
            // dynamic_class_attrs) AND record it as a method-shadow attr so
            // the read path uses the runtime shadow ternary with a sound
            // nondet fallback. (Previously skipped/deferred.)
            method_shadow_attrs[cn].insert(attr);
          }
          dynamic_class_attrs[cn].insert(attr);
        }
      };
      for(const auto &s : as_array(b))
      {
        if(is_node_type(s, "Assign"))
        {
          const jsont &tgts = json_member(s, "targets");
          if(tgts.is_array())
            for(const auto &t : as_array(tgts))
              record(t);
        }
        else if(is_node_type(s, "AnnAssign"))
          record(json_member(s, "target"));
        for(const char *k : {"body", "orelse", "finalbody"})
        {
          const jsont &sub = json_member(s, k);
          if(sub.is_array())
            scan_attr_targets(sub);
        }
        const jsont &handlers = json_member(s, "handlers");
        if(handlers.is_array())
          for(const auto &h : as_array(handlers))
            scan_attr_targets(json_member(h, "body"));
      }
    };
    scan_attr_targets(body);
  }

  // Pass 0.28: PLR §3.1 type inference for unannotated parameters
  // from concrete call-site argument types.
  //
  // For each call `f(a, b, c)` in the AST, if `f` is a known
  // module-level FunctionDef and one of its parameters has no
  // type annotation, infer the parameter's type from the
  // statically-knowable arg AST node (Constant int/string/bool,
  // List/Dict/Set/Tuple display, or NameConstant for None).
  // When all callers agree on the same inferred type, the
  // function body is converted with that type as the
  // parameter's effective annotation.
  //
  // This unlocks idiomatic Python where annotations are
  // omitted but the call sites are uniform — common for
  // helper functions that take strings or dicts and use
  // them through method calls (`s.lower()`, `d.get(k)`).
  if(body.is_array())
  {
    // Collect param annotations to determine which params are
    // unannotated for each known function.
    std::map<std::string, std::vector<bool>> func_param_unannotated;
    for(const auto &stmt : as_array(body))
    {
      if(
        !is_node_type(stmt, "FunctionDef") &&
        !is_node_type(stmt, "AsyncFunctionDef"))
        continue;
      std::string fn_name = json_string(json_member(stmt, "name"));
      const jsont &fn_args = json_member(stmt, "args");
      const jsont &params = json_member(fn_args, "args");
      if(!params.is_array())
        continue;
      auto &flags = func_param_unannotated[fn_name];
      for(const auto &p : as_array(params))
        flags.push_back(json_member(p, "annotation").is_null());
    }
    // Infer arg types per call site.
    auto infer_arg_type = [&](const jsont &arg) -> typet
    {
      if(is_node_type(arg, "Constant"))
      {
        const jsont &v = json_member(arg, "value");
        if(v.is_string())
          return python_string_type();
        if(v.is_true() || v.is_false())
          return bool_typet{};
        if(v.is_number())
        {
          // Distinguish int from float by presence of '.' or 'e'.
          if(
            v.value.find('.') != std::string::npos ||
            v.value.find('e') != std::string::npos)
            return double_type();
          return python_int_type();
        }
        // None / null
        if(v.is_null())
          return python_value_type();
      }
      if(is_node_type(arg, "List"))
        return python_list_type(python_value_type());
      if(is_node_type(arg, "Dict"))
        return python_dict_type(python_string_type(), python_value_type());
      if(is_node_type(arg, "Set"))
        return python_set_type();
      if(is_node_type(arg, "JoinedStr"))
        return python_string_type();
      if(is_node_type(arg, "FormattedValue"))
        return python_string_type();
      // Cannot infer.
      return typet{};
    };
    // Per-function: per-param-index inferred type so far,
    // and a flag if conflicting types were observed.
    std::map<std::string, std::map<std::size_t, typet>> agreed;
    std::map<std::string, std::set<std::size_t>> conflict;
    auto record_call = [&](const jsont &n)
    {
      if(!is_node_type(n, "Call"))
        return;
      const jsont &fn = json_member(n, "func");
      if(!is_node_type(fn, "Name"))
        return;
      std::string callee = json_string(json_member(fn, "id"));
      auto fp_it = func_param_unannotated.find(callee);
      if(fp_it == func_param_unannotated.end())
        return;
      const jsont &cargs = json_member(n, "args");
      if(!cargs.is_array())
        return;
      std::size_t i = 0;
      for(const auto &a : as_array(cargs))
      {
        if(
          i < fp_it->second.size() && fp_it->second[i] &&
          conflict[callee].count(i) == 0)
        {
          typet inferred = infer_arg_type(a);
          if(!inferred.id().empty())
          {
            auto &m = agreed[callee];
            auto e = m.find(i);
            if(e == m.end())
              m[i] = inferred;
            else if(e->second != inferred)
            {
              conflict[callee].insert(i);
              m.erase(e);
            }
          }
        }
        ++i;
      }
    };
    std::function<void(const jsont &)> walk_expr = [&](const jsont &n)
    {
      record_call(n);
      if(is_node_type(n, "Call"))
      {
        walk_expr(json_member(n, "func"));
        const jsont &cargs = json_member(n, "args");
        if(cargs.is_array())
          for(const auto &a : as_array(cargs))
            walk_expr(a);
      }
      else if(is_node_type(n, "BinOp"))
      {
        walk_expr(json_member(n, "left"));
        walk_expr(json_member(n, "right"));
      }
      else if(is_node_type(n, "BoolOp"))
      {
        const jsont &vs = json_member(n, "values");
        if(vs.is_array())
          for(const auto &v : as_array(vs))
            walk_expr(v);
      }
      else if(is_node_type(n, "Compare"))
      {
        walk_expr(json_member(n, "left"));
        const jsont &cs = json_member(n, "comparators");
        if(cs.is_array())
          for(const auto &c : as_array(cs))
            walk_expr(c);
      }
      else if(is_node_type(n, "UnaryOp"))
        walk_expr(json_member(n, "operand"));
      else if(is_node_type(n, "Subscript"))
      {
        walk_expr(json_member(n, "value"));
        walk_expr(json_member(n, "slice"));
      }
      else if(is_node_type(n, "Attribute"))
        walk_expr(json_member(n, "value"));
      else if(is_node_type(n, "IfExp"))
      {
        walk_expr(json_member(n, "test"));
        walk_expr(json_member(n, "body"));
        walk_expr(json_member(n, "orelse"));
      }
    };
    std::function<void(const jsont &)> walk_body = [&](const jsont &b)
    {
      if(!b.is_array())
        return;
      for(const auto &s : as_array(b))
      {
        if(is_node_type(s, "Assign"))
          walk_expr(json_member(s, "value"));
        else if(is_node_type(s, "AnnAssign"))
          walk_expr(json_member(s, "value"));
        else if(is_node_type(s, "AugAssign"))
          walk_expr(json_member(s, "value"));
        else if(is_node_type(s, "Return"))
          walk_expr(json_member(s, "value"));
        else if(is_node_type(s, "Expr"))
          walk_expr(json_member(s, "value"));
        else if(is_node_type(s, "Assert"))
        {
          walk_expr(json_member(s, "test"));
          walk_expr(json_member(s, "msg"));
        }
        // Recurse into block-bearing statements.
        if(
          is_node_type(s, "If") || is_node_type(s, "While") ||
          is_node_type(s, "For") || is_node_type(s, "With") ||
          is_node_type(s, "Try") || is_node_type(s, "FunctionDef") ||
          is_node_type(s, "AsyncFunctionDef") || is_node_type(s, "ClassDef"))
        {
          if(is_node_type(s, "If") || is_node_type(s, "While"))
            walk_expr(json_member(s, "test"));
          walk_body(json_member(s, "body"));
          walk_body(json_member(s, "orelse"));
          walk_body(json_member(s, "finalbody"));
          const jsont &handlers = json_member(s, "handlers");
          if(handlers.is_array())
            for(const auto &h : as_array(handlers))
              walk_body(json_member(h, "body"));
        }
      }
    };
    walk_body(body);
    // Move agreed inferences into the persistent map.
    for(auto &[fname, params_map] : agreed)
      for(auto &[idx, t] : params_map)
        inferred_param_types[fname][idx] = std::move(t);
  }

  // Pass 0: register top-level annotated variable names as global symbols
  // (so functions can reference them during pass 1).
  // Only AnnAssign (with explicit type) is handled here; plain Assign
  // variables get their type from the RHS during pass 2.
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if(is_node_type(stmt, "AnnAssign"))
      {
        const jsont &target = json_member(stmt, "target");
        if(is_node_type(target, "Name"))
        {
          std::string var_name = json_string(json_member(target, "id"));
          // Skip dict/set annotations — their placeholder type (int)
          // would lose the struct from the RHS. Defer to pass 2.
          const jsont &ann = json_member(stmt, "annotation");
          if(is_node_type(ann, "Name"))
          {
            std::string tname = json_string(json_member(ann, "id"));
            if(
              tname == "dict" || tname == "Dict" || tname == "set" ||
              tname == "Set")
              continue;
          }
          typet var_type =
            convert_type_annotation(json_member(stmt, "annotation"));
          irep_idt sym_id{"python::" + var_name};
          if(symbol_table.lookup(sym_id) == nullptr)
          {
            symbolt new_sym{sym_id, var_type, "python"};
            new_sym.base_name = var_name;
            new_sym.is_lvalue = true;
            new_sym.is_state_var = true;
            new_sym.is_static_lifetime = true;
            symbol_table.add(new_sym);
          }
        }
      }
      else if(is_node_type(stmt, "Assign"))
      {
        // For plain assignments, pre-register with a placeholder type.
        // The type will be corrected during pass 2 when the RHS is
        // evaluated. We only do this so functions can find the symbol.
        const jsont &targets = json_member(stmt, "targets");
        if(targets.is_array())
        {
          for(const auto &target : as_array(targets))
          {
            if(is_node_type(target, "Name"))
            {
              std::string var_name = json_string(json_member(target, "id"));
              irep_idt sym_id{"python::" + var_name};
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                // Try to infer type from the RHS constant
                const jsont &val = json_member(stmt, "value");
                typet var_type = python_int_type();
                if(is_node_type(val, "Constant"))
                {
                  const jsont &v = json_member(val, "value");
                  if(v.is_true() || v.is_false())
                    var_type = bool_typet{};
                  else if(v.is_number())
                  {
                    std::string vs = v.value;
                    if(
                      vs.find('.') != std::string::npos ||
                      vs.find('e') != std::string::npos ||
                      vs.find('E') != std::string::npos)
                      var_type = double_type();
                  }
                  else if(v.is_string())
                  {
                    std::string sv = v.value;
                    if(sv.size() >= 3 && sv[0] == 'b' && sv[1] == '\'')
                      var_type = python_list_type(python_int_type());
                    else
                      var_type = python_string_type();
                  }
                }
                else if(is_node_type(val, "List"))
                {
                  const jsont &elts = json_member(val, "elts");
                  // Check for list of tuples first
                  if(
                    elts.is_array() && !as_array(elts).empty() &&
                    is_node_type(*as_array(elts).begin(), "Tuple"))
                  {
                    const jsont &first_elt = *as_array(elts).begin();
                    const jsont &telts = json_member(first_elt, "elts");
                    struct_typet::componentst comps;
                    int tidx = 0;
                    if(telts.is_array())
                    {
                      for(const auto &te : as_array(telts))
                      {
                        (void)te;
                        comps.push_back(struct_typet::componentt{
                          "_" + std::to_string(tidx++), python_int_type()});
                      }
                    }
                    struct_typet tuple_type{comps};
                    tuple_type.set_tag("python_tuple");
                    var_type = python_list_type(tuple_type);
                  }
                  else
                  {
                    // Only pre-register with int element type if elements
                    // are simple constants.
                    bool simple = true;
                    if(elts.is_array())
                    {
                      for(const auto &e : as_array(elts))
                      {
                        if(!is_node_type(e, "Constant"))
                        {
                          simple = false;
                          break;
                        }
                      }
                    }
                    if(simple)
                    {
                      // Infer element type from first constant
                      typet elem_type = python_int_type();
                      bool mixed_types = false;
                      if(elts.is_array() && !as_array(elts).empty())
                      {
                        const jsont &first = *as_array(elts).begin();
                        const jsont &fv = json_member(first, "value");
                        bool first_is_str = fv.is_string() &&
                                            !fv.value.empty() &&
                                            fv.value[0] != 'b';
                        bool first_is_num = fv.is_number();
                        if(first_is_str)
                          elem_type = python_string_type();
                        else if(first_is_num)
                        {
                          std::string vs = fv.value;
                          if(
                            vs.find('.') != std::string::npos ||
                            vs.find('e') != std::string::npos)
                            elem_type = double_type();
                        }
                        // Check all elements for consistency
                        for(const auto &e : as_array(elts))
                        {
                          const jsont &ev = json_member(e, "value");
                          bool is_str = ev.is_string() && !ev.value.empty() &&
                                        ev.value[0] != 'b';
                          if(first_is_str != is_str)
                            mixed_types = true;
                        }
                      }
                      if(mixed_types)
                        elem_type = python_value_type();
                      var_type = python_list_type(elem_type);
                    }
                    else
                    {
                      // PLR §3.1: list with at least one non-constant
                      // element (e.g. `rand1 = [0, n - 1]`). Infer the
                      // element type from any constant elements that
                      // are present, falling back to int. Without
                      // pre-registering, function bodies in pass 1c
                      // that read this global see Name → nil and
                      // silently drop the enclosing statement.
                      typet elem_type = python_int_type();
                      if(elts.is_array() && !as_array(elts).empty())
                      {
                        for(const auto &e : as_array(elts))
                        {
                          if(!is_node_type(e, "Constant"))
                            continue;
                          const jsont &ev = json_member(e, "value");
                          if(ev.is_string())
                          {
                            elem_type = python_string_type();
                            break;
                          }
                          if(ev.is_number())
                          {
                            std::string vs = ev.value;
                            if(
                              vs.find('.') != std::string::npos ||
                              vs.find('e') != std::string::npos)
                              elem_type = double_type();
                            break;
                          }
                          if(ev.is_true() || ev.is_false())
                          {
                            elem_type = bool_typet{};
                            break;
                          }
                        }
                      }
                      var_type = python_list_type(elem_type);
                    } // end else (non-simple list — register with default)
                  }   // end else (non-tuple list)
                }
                else if(is_node_type(val, "Tuple"))
                {
                  // Tuple literal — infer struct type from elements
                  const jsont &telts = json_member(val, "elts");
                  if(telts.is_array())
                  {
                    struct_typet::componentst comps;
                    int tidx = 0;
                    for(const auto &te : as_array(telts))
                    {
                      typet ct = python_int_type();
                      if(is_node_type(te, "Constant"))
                      {
                        const jsont &tv = json_member(te, "value");
                        if(tv.is_string())
                          ct = python_string_type();
                        else if(tv.is_number())
                        {
                          std::string vs = tv.value;
                          if(vs.find('.') != std::string::npos)
                            ct = double_type();
                        }
                      }
                      comps.push_back(struct_typet::componentt{
                        "_" + std::to_string(tidx++), ct});
                    }
                    struct_typet tuple_type{comps};
                    tuple_type.set_tag("python_tuple");
                    var_type = tuple_type;
                  }
                  else
                    continue;
                }
                else if(is_node_type(val, "List"))
                {
                  // List of tuples — check if elements are Tuple nodes
                  const jsont &lelts = json_member(val, "elts");
                  if(lelts.is_array() && !as_array(lelts).empty())
                  {
                    const jsont &first_elt = *as_array(lelts).begin();
                    if(is_node_type(first_elt, "Tuple"))
                    {
                      // Infer tuple element type
                      const jsont &telts = json_member(first_elt, "elts");
                      struct_typet::componentst comps;
                      int tidx = 0;
                      if(telts.is_array())
                      {
                        for(const auto &te : as_array(telts))
                        {
                          (void)te;
                          comps.push_back(struct_typet::componentt{
                            "_" + std::to_string(tidx++), python_int_type()});
                        }
                      }
                      struct_typet tuple_type{comps};
                      tuple_type.set_tag("python_tuple");
                      var_type = python_list_type(tuple_type);
                    }
                    else
                      continue;
                  }
                  else
                    continue;
                }
                else if(is_node_type(val, "Dict"))
                {
                  // PLR §3.1: a module-global bound to a dict literal
                  // (`d = {"a": 1}`) must be pre-registered with its
                  // DICT type so a function body converted in sub-pass
                  // 1c that mutates the global (`d[k] = v`) sees a
                  // dict-typed symbol — otherwise the subscript-assign,
                  // which requires is_python_dict_type, is silently
                  // dropped (a false proof: the mutation vanishes).
                  // Infer key/value types from the first constant
                  // entry; fall back to the universal python_value for
                  // empty/mixed/non-constant dicts (pass 2 replaces the
                  // type from the concrete RHS, tracked via
                  // unannotated_globals).
                  typet kt = python_string_type();
                  typet vt = python_value_type();
                  const jsont &keys = json_member(val, "keys");
                  const jsont &vals = json_member(val, "values");
                  auto const_type =
                    [this](const jsont &node, const typet &fallback) -> typet
                  {
                    if(!is_node_type(node, "Constant"))
                      return fallback;
                    const jsont &cv = json_member(node, "value");
                    if(cv.is_true() || cv.is_false())
                      return bool_typet{};
                    if(cv.is_string())
                      return python_string_type();
                    if(cv.is_number())
                    {
                      const std::string &vs = cv.value;
                      if(
                        vs.find('.') != std::string::npos ||
                        vs.find('e') != std::string::npos ||
                        vs.find('E') != std::string::npos)
                        return double_type();
                      return python_int_type();
                    }
                    return fallback;
                  };
                  if(
                    keys.is_array() && !as_array(keys).empty() &&
                    vals.is_array() && !as_array(vals).empty())
                  {
                    kt = const_type(*as_array(keys).begin(), kt);
                    vt = const_type(*as_array(vals).begin(), vt);
                  }
                  var_type = python_dict_type(kt, vt);
                }
                else if(
                  is_node_type(val, "Call") || is_node_type(val, "ListComp") ||
                  is_node_type(val, "Lambda") || is_node_type(val, "Set") ||
                  is_node_type(val, "Subscript") ||
                  is_node_type(val, "BinOp") || is_node_type(val, "UnaryOp") ||
                  is_node_type(val, "Compare") || is_node_type(val, "BoolOp") ||
                  is_node_type(val, "IfExp"))
                {
                  // Recognise the common ``flag = nondet_X()`` /
                  // ``n = random.randint(...)`` shapes so functions
                  // converted in pass 1c can resolve the global.
                  // Without this, function-body conversion in pass
                  // 1c sees the Name lookup return nil and the
                  // entire enclosing if/while/for body silently
                  // collapses. PLR §3.1: only pre-register for
                  // calls whose return type we can determine
                  // confidently — using a wrong placeholder type
                  // here causes downstream bitvector-encoding
                  // crashes when the function body uses the
                  // global in a typed-key dict or comparable.
                  if(is_node_type(val, "Call"))
                  {
                    const jsont &fn = json_member(val, "func");
                    if(is_node_type(fn, "Name"))
                    {
                      std::string callee = json_string(json_member(fn, "id"));
                      if(
                        callee == "nondet_int" ||
                        callee == "__VERIFIER_nondet_int")
                        var_type = python_int_type();
                      else if(
                        callee == "nondet_float" ||
                        callee == "__VERIFIER_nondet_float")
                        var_type = double_type();
                      else if(
                        callee == "nondet_bool" ||
                        callee == "__VERIFIER_nondet_bool")
                        var_type = bool_typet{};
                      else if(
                        callee == "nondet_str" || callee == "nondet_string")
                        var_type = python_string_type();
                      else
                        continue; // defer to pass 2
                    }
                    else if(is_node_type(fn, "Attribute"))
                    {
                      // Attribute calls like `random.randint(0, 100)`,
                      // `random.uniform(0, 1)`. Pre-register only
                      // when both the module and the method are in
                      // our known-typed table; everything else
                      // defers to pass 2 (since a wrong placeholder
                      // type breaks function bodies that use the
                      // global as a typed key / index / arg).
                      const jsont &av = json_member(fn, "value");
                      const std::string attr =
                        json_string(json_member(fn, "attr"));
                      std::string base;
                      if(is_node_type(av, "Name"))
                        base = json_string(json_member(av, "id"));
                      // random.randint / random.randrange → int
                      if(
                        base == "random" &&
                        (attr == "randint" || attr == "randrange" ||
                         attr == "getrandbits"))
                        var_type = python_int_type();
                      // random.uniform / random.random / random.gauss → float
                      else if(
                        base == "random" &&
                        (attr == "uniform" || attr == "random" ||
                         attr == "gauss" || attr == "expovariate" ||
                         attr == "triangular" || attr == "betavariate"))
                        var_type = double_type();
                      // math.sqrt / math.log / math.exp / trig → float
                      else if(
                        base == "math" &&
                        (attr == "sqrt" || attr == "log" || attr == "exp" ||
                         attr == "sin" || attr == "cos" || attr == "tan" ||
                         attr == "asin" || attr == "acos" || attr == "atan" ||
                         attr == "atan2" || attr == "ceil" || attr == "floor" ||
                         attr == "fabs" || attr == "pow"))
                        var_type = double_type();
                      else
                        continue; // defer to pass 2
                    }
                    else
                    {
                      continue; // defer to pass 2
                    }
                  }
                  else
                  {
                    // Complex non-Call RHS — skip pre-registration,
                    // let pass 2 handle it
                    continue;
                  }
                }
                symbolt new_sym{sym_id, var_type, "python"};
                new_sym.base_name = var_name;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                new_sym.is_static_lifetime = true;
                symbol_table.add(new_sym);
                // Track for pass 2: this symbol's type is a tentative
                // placeholder. If pass 2 sees a different concrete RHS
                // type it should replace, not cast (PLR §6.2 — a plain
                // assignment binds the name to the value's type, with
                // no implicit numeric coercion).
                unannotated_globals.insert(sym_id);
              }
            }
          }
        }
      }
    }
  }

  // First pass: register all function and class definitions
  // Sub-pass 1a: register class definitions first (needed for type annotations)
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if(is_node_type(stmt, "ClassDef"))
        convert_class_def(stmt);
    }
  }
  // Sub-pass 1a-bis: re-convert classes so method bodies that
  // reference classes defined later in the source file (e.g.
  // `class Foo: def bar() -> 'Bar': return Bar(self); class Bar: ...`)
  // are processed with the full class registry available. The
  // first sub-pass registered all class types and their
  // __init__ signatures; this one re-emits method bodies with
  // forward-class references resolvable. convert_class_def is
  // idempotent — class_mro / class_bases dedup, and method
  // symbols are overwritten with the corrected body.
  // Sub-pass 1a-bis: re-convert classes whose method bodies
  // reference forward-class names that were only registered as
  // placeholders during the first 1a pass. Re-running every
  // class is correct but expensive — for self-referential
  // classes (e.g. recursive linked-list / tree shapes) the
  // method bodies cite the class itself and re-running them
  // forces a re-walk of nested calls. Restrict this pass to
  // classes that contain at least one method with a return-
  // type annotation that is a string forward reference (or
  // a Subscript whose slice is one) — those are the only
  // shapes that benefit. Self-referential classes whose
  // forward refs resolve to themselves would have produced
  // the correct type on the first pass already; skipping
  // them here keeps that case fast.
  if(body.is_array())
  {
    std::function<bool(const jsont &)> mentions_forward_string =
      [&](const jsont &node) -> bool
    {
      if(node.is_null())
        return false;
      if(is_node_type(node, "Constant"))
      {
        const jsont &cv = json_member(node, "value");
        if(cv.is_string())
        {
          // Treat as a forward reference only when the string
          // names a class that DIDN'T resolve to the same
          // class being processed (e.g. 'Bar' inside Foo's
          // method body, not 'Task' inside Task itself).
          return true;
        }
      }
      if(is_node_type(node, "Subscript"))
        return mentions_forward_string(json_member(node, "slice"));
      return false;
    };
    for(const auto &stmt : as_array(body))
    {
      if(!is_node_type(stmt, "ClassDef"))
        continue;
      std::string class_name = json_string(json_member(stmt, "name"));
      const jsont &cbody = json_member(stmt, "body");
      if(!cbody.is_array())
        continue;
      bool needs_repass = false;
      // First sub-condition: a method's return-type annotation
      // is a string forward reference to a class OTHER than
      // the enclosing one.
      for(const auto &item : as_array(cbody))
      {
        if(
          !is_node_type(item, "FunctionDef") &&
          !is_node_type(item, "AsyncFunctionDef"))
          continue;
        const jsont &returns = json_member(item, "returns");
        if(returns.is_null())
          continue;
        bool fr = false;
        if(is_node_type(returns, "Constant"))
        {
          const jsont &cv = json_member(returns, "value");
          if(cv.is_string() && cv.value != class_name)
            fr = true;
        }
        else if(is_node_type(returns, "Subscript"))
        {
          fr = mentions_forward_string(json_member(returns, "slice"));
        }
        if(fr)
        {
          needs_repass = true;
          break;
        }
      }
      // Second sub-condition: a method's body calls another
      // method of the same class whose source-order position
      // is AFTER this one. Without a re-pass, the earlier
      // method's body conversion sees only an empty
      // method-symbol entry and falls through to the missing-
      // method nondet. The cheap detection below scans each
      // method's body for `self.<name>()` calls and flags
      // when <name> is a method declared later in the class
      // body.
      if(!needs_repass)
      {
        std::vector<std::string> method_order;
        for(const auto &item : as_array(cbody))
        {
          if(
            is_node_type(item, "FunctionDef") ||
            is_node_type(item, "AsyncFunctionDef"))
            method_order.push_back(json_string(json_member(item, "name")));
        }
        std::function<bool(const jsont &, const std::set<std::string> &)>
          calls_later =
            [&](const jsont &node, const std::set<std::string> &later) -> bool
        {
          if(node.is_null() || !node.is_object())
            return false;
          if(is_node_type(node, "Call"))
          {
            const jsont &fn = json_member(node, "func");
            if(is_node_type(fn, "Attribute"))
            {
              const jsont &obj = json_member(fn, "value");
              if(
                is_node_type(obj, "Name") &&
                json_string(json_member(obj, "id")) == "self")
              {
                std::string name = json_string(json_member(fn, "attr"));
                if(later.count(name))
                  return true;
              }
            }
          }
          for(const char *key :
              {"value",
               "test",
               "left",
               "right",
               "operand",
               "values",
               "args",
               "elts",
               "keys",
               "comparators",
               "body",
               "orelse",
               "finalbody"})
          {
            const jsont &child = json_member(node, key);
            if(child.is_null())
              continue;
            if(child.is_array())
              for(const auto &c : as_array(child))
                if(calls_later(c, later))
                  return true;
            if(calls_later(child, later))
              return true;
          }
          return false;
        };
        std::set<std::string> seen_methods;
        for(const auto &item : as_array(cbody))
        {
          if(
            !is_node_type(item, "FunctionDef") &&
            !is_node_type(item, "AsyncFunctionDef"))
            continue;
          std::string mname = json_string(json_member(item, "name"));
          // 'later' = methods declared after this one.
          std::set<std::string> later;
          bool past = false;
          for(const auto &mn : method_order)
          {
            if(past)
              later.insert(mn);
            if(mn == mname)
              past = true;
          }
          if(later.empty())
            continue;
          const jsont &mbody = json_member(item, "body");
          if(mbody.is_array())
          {
            for(const auto &s : as_array(mbody))
            {
              if(calls_later(s, later))
              {
                needs_repass = true;
                break;
              }
            }
          }
          if(needs_repass)
            break;
        }
      }
      // Third sub-condition (PLR §3.3.2 / forward-referenced fields): a
      // method accesses `<p>.attr` where `p` is a non-self parameter
      // WITHOUT a class annotation. Such `p` is a python_value, and
      // `p.attr` resolves at conversion time against the THEN-registered
      // classes; if the field belongs to a class defined LATER in the
      // file, the first 1a pass baked a nondet over-approximation. A
      // re-pass (after the full 1a loop registered every class struct,
      // including discovered dynamic attrs) lets it resolve. The common
      // case is a data/descriptor method (`__get__`/`__set__`) reading or
      // writing `obj.<field>` of an instance whose class is defined after
      // the descriptor class.
      if(!needs_repass)
      {
        std::function<bool(const jsont &, const std::set<std::string> &)>
          accesses_param_attr =
            [&](const jsont &node, const std::set<std::string> &params) -> bool
        {
          if(node.is_array())
          {
            for(const auto &c : as_array(node))
              if(accesses_param_attr(c, params))
                return true;
            return false;
          }
          if(!node.is_object())
            return false;
          if(is_node_type(node, "Attribute"))
          {
            const jsont &v = json_member(node, "value");
            if(
              is_node_type(v, "Name") &&
              params.count(json_string(json_member(v, "id"))) > 0)
              return true;
          }
          for(const char *key :
              {"value",  "test",   "left",      "right",   "operand",
               "values", "args",   "elts",      "keys",    "comparators",
               "body",   "orelse", "finalbody", "targets", "target",
               "slice",  "func",   "keywords",  "items",   "operands"})
          {
            const jsont &child = json_member(node, key);
            if(child.is_null())
              continue;
            if(child.is_array())
            {
              for(const auto &c : as_array(child))
                if(accesses_param_attr(c, params))
                  return true;
            }
            else if(accesses_param_attr(child, params))
              return true;
          }
          return false;
        };
        for(const auto &item : as_array(cbody))
        {
          if(
            !is_node_type(item, "FunctionDef") &&
            !is_node_type(item, "AsyncFunctionDef"))
            continue;
          // Collect non-self, unannotated parameters.
          std::set<std::string> generic_params;
          const jsont &args_node = json_member(item, "args");
          const jsont &params_arr = json_member(args_node, "args");
          if(params_arr.is_array())
          {
            bool first = true;
            for(const auto &p : as_array(params_arr))
            {
              const std::string pn = json_string(json_member(p, "arg"));
              const bool is_self = first && pn == "self";
              first = false;
              if(!is_self && json_member(p, "annotation").is_null())
                generic_params.insert(pn);
            }
          }
          if(generic_params.empty())
            continue;
          if(accesses_param_attr(json_member(item, "body"), generic_params))
          {
            needs_repass = true;
            break;
          }
        }
      }
      if(needs_repass)
        convert_class_def(stmt);
    }
  }
  // Sub-pass 1b: register all function signatures (without bodies)
  // so forward references between functions work
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if(
        is_node_type(stmt, "FunctionDef") ||
        is_node_type(stmt, "AsyncFunctionDef"))
      {
        // Skip @overload decorated functions
        const jsont &decorators = json_member(stmt, "decorator_list");
        bool is_overload = false;
        if(decorators.is_array())
        {
          for(const auto &dec : as_array(decorators))
          {
            if(
              is_node_type(dec, "Name") &&
              json_string(json_member(dec, "id")) == "overload")
              is_overload = true;
          }
        }
        if(is_overload)
          continue;
        std::string fname = json_string(json_member(stmt, "name"));
        irep_idt sym_id{"python::" + fname};
        if(symbol_table.lookup(sym_id) == nullptr)
        {
          // Parse return type annotation for better forward reference types
          typet ret_type = python_int_type();
          const jsont &returns = json_member(stmt, "returns");
          if(!returns.is_null())
            ret_type = convert_type_annotation(returns);
          if(ret_type.id() == ID_empty)
            ret_type = python_int_type();
          code_typet fn_type{{}, ret_type};
          symbolt fn_sym{sym_id, fn_type, "python"};
          fn_sym.base_name = fname;
          fn_sym.location = get_location(stmt);
          fn_sym.is_lvalue = true;
          symbol_table.add(fn_sym);
        }
      }
    }
  }
  // Sub-pass 1b.4 (PLR §3.2 / §4.2.2): forward-reference return-type
  // fixpoint. Sub-pass 1b registers UNANNOTATED functions with the
  // python_int_type() default; their real return type is otherwise only
  // set later (sub-pass 1c, in source order) while the body is converted.
  // So a function `f` whose body is `return g()` was converted reading
  // g's INT default when g is defined AFTER f — mis-typing f and causing
  // downstream false alarms (e.g. `f() == "global"` or `f() == {...}`
  // folding to a constant). Resolve it here, BEFORE bodies are converted,
  // using the SAME inference as sub-pass 1c (infer_return_type_from_body:
  // constants, containers, tuples, class constructors, and forward
  // tail-calls), iterating to a fixpoint so chains of forward tail-calls
  // (f→g→…) converge. We only ever REFINE a determinable type (conflicts
  // → python_value, undetermined value-returns → the int default), so it
  // never makes a type worse or introduces a false proof.
  if(body.is_array())
  {
    std::vector<std::pair<irep_idt, const jsont *>> unann;
    for(const auto &stmt : as_array(body))
    {
      if(
        !is_node_type(stmt, "FunctionDef") &&
        !is_node_type(stmt, "AsyncFunctionDef"))
        continue;
      if(!json_member(stmt, "returns").is_null())
        continue; // annotated — trust the annotation
      const jsont &decorators = json_member(stmt, "decorator_list");
      if(decorators.is_array() && !as_array(decorators).empty())
        continue; // decorated — dispatch differs
      std::string fname = json_string(json_member(stmt, "name"));
      irep_idt sid{"python::" + fname};
      const symbolt *s = symbol_table.lookup(sid);
      if(s != nullptr && s->type.id() == ID_code)
        unann.emplace_back(sid, &stmt);
    }
    const code_typet::parameterst no_params;
    bool changed = true;
    for(std::size_t pass = 0; changed && pass <= unann.size() + 1; ++pass)
    {
      changed = false;
      for(const auto &[sid, stmtp] : unann)
      {
        const std::string fname = json_string(json_member(*stmtp, "name"));
        inferred_returnt inf = infer_return_type_from_body(
          json_member(*stmtp, "body"), no_params, fname, "");
        typet rt =
          inf.has_yield ? python_list_type(inf.yield_element_type) : inf.type;
        if(rt.id() == ID_empty)
          continue;
        symbolt &s = symbol_table.get_writeable_ref(sid);
        if(to_code_type(s.type).return_type() != rt)
        {
          to_code_type(s.type).return_type() = rt;
          changed = true;
        }
      }
    }
  }
  // Sub-pass 1b.5: argument-side constant propagation. For each
  // Call site `func(arg)` where the arg is a string constant
  // (either a Constant literal or a Name resolved to a literal
  // in the local scope), record the value. After scanning the
  // whole AST, for each (callee, param_index) pair where ALL
  // call sites pass the SAME constant value, register that
  // constant in `string_constants[<param_id>]` so the body
  // conversion in pass 1c can fold expressions involving it.
  //
  // PLR §4.2.4: parameter binding. Each call binds the actual
  // arguments to the corresponding formal parameters; if all
  // call sites bind a parameter to the same constant string,
  // the parameter is effectively constant for verification.
  //
  // Limitation: only string constants are propagated for now.
  // Recursive functions, functions with no call sites, and
  // functions with mixed constant / symbolic args don't
  // populate any entry.
  {
    // (callee_bare_name, param_index) -> {observed string values}.
    // A parameter with size==1 has a unique constant value at
    // every observed call site. size==0 means no string constant
    // was ever observed (don't propagate). We use a special
    // sentinel value "" with size>=2 to mark "saw a non-string
    // or differing value" — never propagate.
    using key_t = std::pair<std::string, std::size_t>;
    std::map<key_t, std::set<std::string>> seen_constants;
    std::set<key_t> tainted; // any non-constant arg or differing values

    auto extract_string_const =
      [&](const jsont &node, const std::map<std::string, std::string> &scope)
      -> std::optional<std::string>
    {
      if(is_node_type(node, "Constant"))
      {
        const jsont &v = json_member(node, "value");
        if(v.is_string())
          return v.value;
      }
      if(is_node_type(node, "Name"))
      {
        std::string n = json_string(json_member(node, "id"));
        auto it = scope.find(n);
        if(it != scope.end())
          return it->second;
      }
      return std::nullopt;
    };

    std::function<void(const jsont &, std::map<std::string, std::string> &)>
      walk =
        [&](
          const jsont &stmts, std::map<std::string, std::string> &scope) -> void
    {
      if(!stmts.is_array())
        return;
      for(const auto &s : as_array(stmts))
      {
        // Track local string-constant assignments so calls a few
        // statements down can resolve `Name`-style args.
        if(is_node_type(s, "Assign"))
        {
          const jsont &targets = json_member(s, "targets");
          const jsont &value = json_member(s, "value");
          if(targets.is_array() && as_array(targets).size() == 1)
          {
            const auto &t0 = *as_array(targets).begin();
            if(is_node_type(t0, "Name"))
            {
              std::string n = json_string(json_member(t0, "id"));
              if(is_node_type(value, "Constant"))
              {
                const jsont &v = json_member(value, "value");
                if(v.is_string())
                  scope[n] = v.value;
                else
                  scope.erase(n);
              }
              else
                scope.erase(n);
            }
          }
        }
        if(is_node_type(s, "AnnAssign"))
        {
          const jsont &target = json_member(s, "target");
          const jsont &value = json_member(s, "value");
          if(is_node_type(target, "Name") && !value.is_null())
          {
            std::string n = json_string(json_member(target, "id"));
            if(is_node_type(value, "Constant"))
            {
              const jsont &v = json_member(value, "value");
              if(v.is_string())
                scope[n] = v.value;
              else
                scope.erase(n);
            }
            else
              scope.erase(n);
          }
        }

        // Inspect any expression for nested Call sites: top-level
        // Expr-Call, Assert(test=Call), Assign(value=Call),
        // AnnAssign(value=Call), Return(value=Call), etc. The
        // inner walk only descends until it finds a Call —
        // arg-shape propagation is opportunistic and a missed
        // call site only loses an optimisation, not soundness.
        std::function<void(const jsont &)> scan_expr =
          [&](const jsont &node) -> void
        {
          if(node.is_null() || !node.is_object())
            return;
          if(is_node_type(node, "Call"))
          {
            const jsont &fn = json_member(node, "func");
            if(is_node_type(fn, "Name"))
            {
              std::string callee = json_string(json_member(fn, "id"));
              const jsont &args = json_member(node, "args");
              if(args.is_array())
              {
                std::size_t i = 0;
                for(const auto &a : as_array(args))
                {
                  key_t k{callee, i};
                  if(tainted.count(k) == 0)
                  {
                    auto val = extract_string_const(a, scope);
                    if(val.has_value())
                      seen_constants[k].insert(val.value());
                    else if(!seen_constants[k].empty())
                      tainted.insert(k);
                  }
                  ++i;
                }
              }
            }
            // Recurse into args/func to find nested calls.
            scan_expr(json_member(node, "func"));
            const jsont &args = json_member(node, "args");
            if(args.is_array())
              for(const auto &a : as_array(args))
                scan_expr(a);
          }
          else
          {
            // Recurse into common AST node fields that may
            // contain expressions.
            for(const char *key :
                {"value",
                 "test",
                 "left",
                 "right",
                 "operand",
                 "values",
                 "args",
                 "elts",
                 "keys",
                 "comparators"})
            {
              const jsont &child = json_member(node, key);
              if(child.is_null())
                continue;
              if(child.is_array())
                for(const auto &c : as_array(child))
                  scan_expr(c);
              else
                scan_expr(child);
            }
          }
        };

        // Inspect Expr / Call statements for top-level calls.
        if(is_node_type(s, "Expr"))
        {
          scan_expr(json_member(s, "value"));
        }
        else if(is_node_type(s, "Assert"))
        {
          scan_expr(json_member(s, "test"));
        }
        else if(
          is_node_type(s, "Assign") || is_node_type(s, "AnnAssign") ||
          is_node_type(s, "AugAssign") || is_node_type(s, "Return"))
        {
          scan_expr(json_member(s, "value"));
        }
        // Recurse into nested control-flow / function bodies. We
        // use a fresh scope inside FunctionDef bodies (the local
        // variables of the inner function) but pass the outer
        // scope through If/While/For/With/Try since those don't
        // open a new local scope.
        if(
          is_node_type(s, "FunctionDef") ||
          is_node_type(s, "AsyncFunctionDef") || is_node_type(s, "ClassDef"))
        {
          std::map<std::string, std::string> nested_scope;
          walk(json_member(s, "body"), nested_scope);
        }
        else if(
          is_node_type(s, "If") || is_node_type(s, "While") ||
          is_node_type(s, "For") || is_node_type(s, "With") ||
          is_node_type(s, "Try"))
        {
          walk(json_member(s, "body"), scope);
          walk(json_member(s, "orelse"), scope);
          walk(json_member(s, "finalbody"), scope);
          const jsont &handlers = json_member(s, "handlers");
          if(handlers.is_array())
          {
            for(const auto &h : as_array(handlers))
              walk(json_member(h, "body"), scope);
          }
        }
      }
    };
    std::map<std::string, std::string> top_scope;
    walk(body, top_scope);

    // Apply: register parameter string constants for each unambiguous
    // (callee, param_index) entry. Look up the function's parameters
    // by symbol; the parameter id is `python::<callee>::<param_name>`
    // for module-level functions.
    // Find the FunctionDef AST node for a given callee in the body.
    // Module-scope only for now (matching the propagation scope).
    auto find_function_def = [&](const std::string &name) -> const jsont *
    {
      if(!body.is_array())
        return nullptr;
      for(const auto &s : as_array(body))
      {
        if(
          (is_node_type(s, "FunctionDef") ||
           is_node_type(s, "AsyncFunctionDef")) &&
          json_string(json_member(s, "name")) == name)
          return &s;
      }
      return nullptr;
    };

    for(const auto &[k, vals] : seen_constants)
    {
      if(tainted.count(k) || vals.size() != 1)
        continue;
      const std::string &callee = k.first;
      std::size_t idx = k.second;
      const jsont *fdef = find_function_def(callee);
      if(fdef == nullptr)
        continue;
      const jsont &args_node = json_member(*fdef, "args");
      const jsont &params = json_member(args_node, "args");
      if(!params.is_array())
        continue;
      const auto &arr = as_array(params);
      if(idx >= arr.size())
        continue;
      auto it = arr.begin();
      std::advance(it, idx);
      const std::string param_name = json_string(json_member(*it, "arg"));
      irep_idt pid{"python::" + callee + "::" + param_name};
      string_constants[pid] = *vals.begin();
    }
  }

  // Sub-pass 1c: convert function bodies (signatures already registered)
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if((is_node_type(stmt, "FunctionDef") ||
          is_node_type(stmt, "AsyncFunctionDef")))
        convert_function_def(stmt);
    }
  }

  // Pass 1.5: update symbols that were registered with placeholder class
  // types during pass 0 (before the full class struct was built in pass 1).
  for(const auto &[cls_name, cls_type] : class_types)
  {
    std::string tag = "python_class_" + cls_name;
    for(const auto &sym_pair : symbol_table)
    {
      const symbolt &sym = sym_pair.second;
      // Update struct types
      if(
        sym.type.id() == ID_struct &&
        to_struct_type(sym.type).get_tag() == tag && sym.type != cls_type)
      {
        symbol_table.get_writeable_ref(sym.name).type = cls_type;
      }
      // Update pointer-to-struct types (e.g., self parameters)
      if(
        sym.type.id() == ID_pointer &&
        to_pointer_type(sym.type).base_type().id() == ID_struct &&
        to_struct_type(to_pointer_type(sym.type).base_type()).get_tag() ==
          tag &&
        to_pointer_type(sym.type).base_type() != cls_type)
      {
        symbol_table.get_writeable_ref(sym.name).type =
          pointer_typet{cls_type, to_pointer_type(sym.type).get_width()};
      }
    }
  }

  // Second pass: convert top-level statements (excluding function defs)
  // PLR §3.1: pre-scan module body to identify names that escape
  // into a container literal (so the literal-construction path can
  // wrap them as python_value pointers, preserving aliasing).
  // current_function is "" at module scope, so escapees get
  // qualified as `python::<name>`.
  collect_escaped_mutables(body);
  collect_empty_list_inferred_types(body);
  code_blockt module_body = convert_module_body(body);

  // Store the module body as a function symbol for later use by
  // generate_support_functions (which creates __CPROVER__start).
  irep_idt module_body_id{"python::__module_body"};
  symbolt module_body_sym{
    module_body_id, code_typet{{}, empty_typet{}}, "python"};
  module_body_sym.base_name = "__module_body";
  module_body_sym.is_lvalue = true;
  module_body_sym.value = module_body;
  if(symbol_table.lookup(module_body_id) != nullptr)
    symbol_table.remove(module_body_id);
  symbol_table.add(module_body_sym);

  // Create __CPROVER_initialize (empty for now)
  const std::string init_name = std::string{CPROVER_PREFIX} + "initialize";
  irep_idt init_id{init_name};
  if(symbol_table.lookup(init_id) == nullptr)
  {
    symbolt init_symbol{init_id, code_typet{{}, empty_typet{}}, "python"};
    init_symbol.base_name = init_name;
    init_symbol.is_lvalue = true;
    init_symbol.value = code_blockt{};
    symbol_table.add(init_symbol);
  }

  // Create __CPROVER_rounding_mode (needed for float operations)
  irep_idt rounding_id{CPROVER_PREFIX "rounding_mode"};
  if(symbol_table.lookup(rounding_id) == nullptr)
  {
    symbolt rounding_symbol{rounding_id, signed_int_type(), "python"};
    rounding_symbol.base_name = CPROVER_PREFIX "rounding_mode";
    rounding_symbol.is_thread_local = true;
    rounding_symbol.is_static_lifetime = true;
    rounding_symbol.value = from_integer(
      static_cast<int>(ieee_floatt::rounding_modet::ROUND_TO_EVEN),
      signed_int_type());
    symbol_table.add(rounding_symbol);
  }

  // Create __CPROVER_memory (needed for pointer operations)
  irep_idt memory_id{CPROVER_PREFIX "memory"};
  if(symbol_table.lookup(memory_id) == nullptr)
  {
    array_typet mem_type{
      unsignedbv_typet{8}, from_integer(0, signedbv_typet{64})};
    symbolt memory_symbol{memory_id, mem_type, "python"};
    memory_symbol.base_name = CPROVER_PREFIX "memory";
    memory_symbol.is_static_lifetime = true;
    memory_symbol.is_lvalue = true;
    symbol_table.add(memory_symbol);
  }

  return false;
}
