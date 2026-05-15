/// Python to GOTO converter — module-level handlers:
/// convert_module_body, process_imported_module, the
/// top-level convert() pass driver.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include "python_converter.h"

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

  for(const auto &stmt : as_array(body))
  {
    // Function and class definitions are handled in the first pass
    if((is_node_type(stmt, "FunctionDef") ||
        is_node_type(stmt, "AsyncFunctionDef")))
    {
      // Evaluate default parameter values NOW (pass 2) when variables
      // have their definition-time values (PLR §8.7)
      std::string fname = json_string(json_member(stmt, "name"));
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
              symbol_table.add(ts);
            }
            symbol_exprt frozen = symbol_table.lookup_ref(ti).symbol_expr();
            block.add(code_frontend_assignt{frozen, val});
            default_values[{fname, i}] = frozen;
          }
        }
      }
      continue;
    }
    if(is_node_type(stmt, "ClassDef"))
    {
      // Add class object initialization
      std::string cls_name = json_string(json_member(stmt, "name"));
      irep_idt cls_id{"python::" + cls_name};
      const symbolt *cls_sym = symbol_table.lookup(cls_id);
      if(cls_sym != nullptr && !cls_sym->value.is_nil())
      {
        code_frontend_assignt init{cls_sym->symbol_expr(), cls_sym->value};
        block.add(std::move(init));
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
        }
      }
      continue;
    }
    if(
      is_node_type(stmt, "FunctionDef") ||
      is_node_type(stmt, "AsyncFunctionDef"))
    {
      std::string fname = json_string(json_member(stmt, "name"));
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
  const jsont &body = json_member(parse_tree.ast_json, "body");

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

  // Pass 0.1: resolve imports so module symbols are available
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
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
                unresolved_imports.insert(
                  alias_name.empty() ? name : alias_name);
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
    }
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
                      continue; // defer to pass 2
                  }             // end else (non-tuple list)
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
                else if(
                  is_node_type(val, "Dict") || is_node_type(val, "Call") ||
                  is_node_type(val, "ListComp") ||
                  is_node_type(val, "Lambda") || is_node_type(val, "Set") ||
                  is_node_type(val, "Subscript") ||
                  is_node_type(val, "BinOp") || is_node_type(val, "UnaryOp") ||
                  is_node_type(val, "Compare") || is_node_type(val, "BoolOp") ||
                  is_node_type(val, "IfExp"))
                {
                  // Complex RHS — skip pre-registration, let pass 2 handle it
                  continue;
                }
                symbolt new_sym{sym_id, var_type, "python"};
                new_sym.base_name = var_name;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                new_sym.is_static_lifetime = true;
                symbol_table.add(new_sym);
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
