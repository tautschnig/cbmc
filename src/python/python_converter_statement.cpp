/// Python to GOTO converter — Statement dispatcher
/// (PLR §7 / §8). Includes the match/case handler inline.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
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

#include <cmath>

// --- Statement conversion ---

codet python_convertert::convert_statement(const jsont &stmt)
{
  std::string node_type = json_string(json_member(stmt, "_type"));

  // Clear pending checks before converting this statement
  pending_checks.clear();

  // Dict-value-by-reference: erase tracked literals for dicts whose
  // container values this statement mutates in place (see the helper's
  // comment; keeps the constant-key fold fully precise for reads while
  // preventing stale-snapshot folds after mutation).
  invalidate_mutated_dict_literals(stmt);

  // §0 write-through: demote slot aliases this statement could disturb
  // (BEFORE conversion -- a demoted alias falls back to sound havoc).
  demote_slot_aliases_for_statement(stmt);

  codet result = code_skipt{};

  if(node_type == "AnnAssign")
  {
    result = convert_ann_assign(stmt);
    // PLR §7.5: an annotated (re)binding of a del-tracked name clears its flag.
    const jsont &t = json_member(stmt, "target");
    if(is_node_type(t, "Name"))
    {
      const std::string nm = json_string(json_member(t, "id"));
      if(deleted_name_targets.count(nm) > 0)
      {
        code_blockt w;
        w.add(result);
        w.add(code_frontend_assignt{
          deleted_name_flag(qualify_name(nm)), false_exprt{}});
        result = std::move(w);
      }
    }
  }
  else if(node_type == "Assign")
  {
    result = convert_assign(stmt);
    // PLR §7.5: an assignment to a del-tracked name (re)binds it, clearing its
    // deleted flag (this also provides the flag's init-false at the binding).
    if(!deleted_name_targets.empty())
    {
      code_blockt w;
      w.add(result);
      for(const auto &t : as_array(json_member(stmt, "targets")))
        if(is_node_type(t, "Name"))
        {
          const std::string nm = json_string(json_member(t, "id"));
          if(deleted_name_targets.count(nm) > 0)
            w.add(code_frontend_assignt{
              deleted_name_flag(qualify_name(nm)), false_exprt{}});
        }
      result = std::move(w);
    }
  }
  else if(node_type == "AugAssign")
    result = convert_aug_assign(stmt);
  else if(node_type == "Assert")
    result = convert_assert(stmt);
  else if(node_type == "If")
    result = convert_if(stmt);
  else if(node_type == "While")
    result = convert_while(stmt);
  else if(node_type == "For")
    result = convert_for(stmt);
  else if(node_type == "Return")
    result = convert_return(stmt);
  else if(node_type == "FunctionDef" || node_type == "AsyncFunctionDef")
    result = convert_function_def(stmt);
  else if(node_type == "ClassDef")
    result = convert_class_def(stmt);
  else if(node_type == "Expr")
    result = convert_expr_stmt(stmt);
  else if(node_type == "Break")
    result = convert_break();
  else if(node_type == "Continue")
    result = convert_continue();
  else if(node_type == "Pass")
    result = convert_pass();
  else if(node_type == "Import" || node_type == "ImportFrom")
  {
    // Handle imports by registering known standard library functions.
    // Unknown imports are silently ignored (functions will get no-body
    // warnings when called).
    // Block to collect explicit ASSIGNs for module constants
    // imported via `from X import Y` (e.g. pi/e/tau/inf/nan from
    // math). Without an explicit ASSIGN here the symbol's
    // static-init value may be missed by __CPROVER_initialize
    // when the symbol was added late (after module pass).
    code_blockt import_block;
    bool has_assigns = false;
    // Handle 'import MODULE' — register module name for MODULE.func() calls
    if(node_type == "Import")
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
          // PLR §6.10.7: 'import non_existent_module' raises
          // ImportError. The module-resolution pass populates
          // unresolved_imports for any 'import' / 'from'
          // whose module file isn't on the search path. Emit
          // __exception_active so try/except ImportError can
          // catch the path; the downstream uncaught_exception
          // assertion fires for unhandled cases.
          //
          // Skip well-known stdlib stubs (typing) and any
          // module whose alias was successfully bound (i.e.
          // present in imported_modules but NOT in
          // unresolved_imports).
          if(unresolved_imports.count(asname) > 0 && name != "typing")
          {
            const symbolt *exc_sym =
              symbol_table.lookup("python::__exception_active");
            const symbolt *exc_type_sym =
              symbol_table.lookup("python::__exception_type");
            if(exc_sym != nullptr)
            {
              import_block.add(
                code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
              if(exc_type_sym != nullptr)
              {
                long h = exception_type_hash("ImportError");
                import_block.add(code_frontend_assignt{
                  exc_type_sym->symbol_expr(),
                  from_integer(h, exc_type_sym->type)});
              }
              has_assigns = true;
            }
          }
          // Module symbol was registered in Pass 0.1 so function
          // bodies processed earlier can already see it.
        }
      }
    }

    // Handle 'from MODULE import NAME'
    if(node_type == "ImportFrom")
    {
      std::string module = json_string(json_member(stmt, "module"));
      const jsont &names = json_member(stmt, "names");
      if(names.is_array())
      {
        for(const auto &alias : as_array(names))
        {
          std::string name = json_string(json_member(alias, "name"));
          std::string asname = json_string(json_member(alias, "asname"));
          if(asname.empty())
            asname = name;

          // Root B (import scoping): record the bound name as
          // explicitly in scope. `from X import *` cannot be
          // enumerated, so it disables the leaked-name NameError
          // check (conservative).
          if(name == "*")
            saw_import_star = true;
          else
          {
            explicitly_imported_names.insert(name);
            explicitly_imported_names.insert(asname);
          }

          // Register known math functions
          if(module == "math")
          {
            // library/math.py is loaded via the ImportFrom
            // library-resolve path (see Pass 0.2 at the top of
            // this function). Here we only handle the module
            // constants (pi, e, tau, inf, nan) as static double
            // symbols; the function entries are populated by
            // the library's @c_intrinsic decorators.
            if(
              name == "pi" || name == "e" || name == "tau" || name == "inf" ||
              name == "nan")
            {
              irep_idt sym_id{"python::" + asname};
              // Build the IEEE-754 value once; bind on whichever
              // symbol-creation path applies (new add OR existing
              // entry whose value is currently nondet because the
              // library's module-level annotated assignment got
              // converted before we knew it was a constant).
              exprt const_val;
              if(name == "pi")
                const_val = double_to_floatbv(M_PI);
              else if(name == "e")
                const_val = double_to_floatbv(M_E);
              else if(name == "tau")
                const_val = double_to_floatbv(2.0 * M_PI);
              else if(name == "inf")
              {
                ieee_floatt v{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                v.make_plus_infinity();
                const_val = v.to_expr();
              }
              else // nan
              {
                ieee_floatt v{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                v.make_NaN();
                const_val = v.to_expr();
              }
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                symbolt sym{sym_id, double_type(), "python"};
                sym.base_name = asname;
                sym.is_lvalue = true;
                sym.is_state_var = true;
                sym.is_static_lifetime = true;
                sym.value = const_val;
                symbol_table.add(sym);
              }
              else
              {
                // Symbol exists (likely from the library's
                // module-level assignment which converts to a
                // nondet at the time it's encountered) — patch
                // its value to the IEEE-754 constant so user
                // code that reads it sees the correct value.
                symbolt &sym = *symbol_table.get_writeable(sym_id);
                sym.value = const_val;
              }
              // Emit an explicit ASSIGN so the binding is
              // visible to symex regardless of whether
              // __CPROVER_initialize picks up the static-init
              // value (which depends on when the symbol was
              // first added to the symbol table).
              import_block.add(code_frontend_assignt{
                symbol_table.lookup_ref(sym_id).symbol_expr(), const_val});
              has_assigns = true;
              continue;
            }
            // Other math functions: handled by the library's
            // @c_intrinsic decorators; no registration here.
            // Track the alias -> math-name binding so direct
            // calls (e.g. comb(5,2) after 'from math import
            // comb') are routed to the math intrinsic dispatch.
            math_imports[asname] = name;
          }
          // typing module — type aliases, no-op
          else if(module == "typing")
          {
            // Names like Any, Optional, List, Dict are type aliases
          }
          else if(module == "re")
          {
            // re module: compile/search/match/sub/findall/split
            // Return nondet int (truthy, not None) so stub assertions
            // like `assert compile(r).search(v) is not None` pass.
            irep_idt fid{"python::" + asname};
            if(symbol_table.lookup(fid) == nullptr)
            {
              code_typet ft{
                {code_typet::parametert{python_string_type()}},
                python_int_type()};
              symbolt fs{fid, ft, "python"};
              fs.base_name = asname;
              fs.is_lvalue = true;
              symbol_table.add(fs);
            }
          }
          else if(
            module == "urllib.parse" || module == "os" || module == "os.path" ||
            module == "sys" || module == "json" || module == "datetime" ||
            module == "time" || module == "collections" ||
            module == "functools" || module == "itertools" || module == "io" ||
            module == "pathlib" || module == "hashlib" || module == "base64" ||
            module == "copy" || module == "enum" || module == "dataclasses" ||
            module == "abc" || module == "random" || module == "decimal" ||
            module == "operator" || module == "string" || module == "struct" ||
            module == "csv" || module == "logging" || module == "unittest" ||
            module == "argparse" || module == "textwrap" ||
            module == "contextlib" || module == "warnings" ||
            module == "traceback" || module == "inspect" ||
            module == "threading" || module == "multiprocessing" ||
            module == "subprocess" || module == "shutil" ||
            module == "tempfile" || module == "glob" || module == "fnmatch" ||
            module == "socket" || module == "http" || module == "http.client" ||
            module == "urllib" || module == "urllib.request" ||
            module == "cmath" || module == "statistics" ||
            module == "fractions" || module == "numbers" ||
            module == "asyncio" || module == "yaml" || module == "requests" ||
            module == "numpy" || module == "pandas" || module == "sqlalchemy" ||
            module == "click" || module == "pytest")
          {
            // Stdlib modules: register imported names as variables (not
            // functions) so the unknown-function handler returns nondet
            // instead of CBMC trying to inline a no-body function.
            irep_idt fid{"python::" + asname};
            if(symbol_table.lookup(fid) == nullptr)
            {
              symbolt fs{fid, python_int_type(), "python"};
              fs.base_name = asname;
              fs.is_lvalue = true;
              fs.is_state_var = true;
              symbol_table.add(fs);
            }
            // PLR §8.5: track collections.X bindings so the
            // call site can route to the right constructor
            // regardless of how the user imported it.
            if(module == "collections")
              collections_imports[asname] = name;
            // PLR §8.13: `from enum import Enum as E` — record the
            // alias so a class deriving from E is recognised as an
            // enum (for .value / .name resolution).
            if(
              module == "enum" &&
              (name == "Enum" || name == "IntEnum" || name == "IntFlag" ||
               name == "Flag" || name == "StrEnum" || name == "ReprEnum"))
              enum_base_aliases.insert(asname);
          }

          // Generic: bind an imported module-level CONSTANT to its
          // value. process_imported_module registered the source
          // module's `python::<name>` with the constant value, but
          // the static initialiser isn't reliably applied when the
          // symbol is added during the import pre-pass — so emit an
          // explicit ASSIGN here (mirroring the math-constant path
          // above). Guarded to data values only: function/class
          // symbols (ID_code / type symbols) and nondet stubs (nil
          // value) are skipped, so this only binds genuine constants.
          {
            const symbolt *src = symbol_table.lookup("python::" + name);
            if(
              src != nullptr && src->value.is_not_nil() &&
              src->type.id() != ID_code && src->value.id() != ID_code)
            {
              irep_idt did{"python::" + asname};
              if(symbol_table.lookup(did) == nullptr)
              {
                symbolt ds{did, src->type, "python"};
                ds.base_name = asname;
                ds.is_lvalue = true;
                ds.is_state_var = true;
                ds.is_static_lifetime = true;
                ds.value = src->value;
                symbol_table.add(ds);
              }
              import_block.add(code_frontend_assignt{
                symbol_table.lookup_ref(did).symbol_expr(), src->value});
              has_assigns = true;
            }
          }
        }
      }
    }
    if(has_assigns)
      result = std::move(import_block);
    else
      result = code_skipt{};
  }
  else if(node_type == "Raise")
    result = convert_raise(stmt);
  else if(node_type == "Delete")
  {
    // del lst[i]: shift elements left, decrement length
    const jsont &targets = json_member(stmt, "targets");
    if(targets.is_array())
    {
      code_blockt del_block;
      source_locationt loc = get_location(stmt);
      for(const auto &target : as_array(targets))
      {
        if(is_node_type(target, "Subscript"))
        {
          exprt obj = convert_expression(json_member(target, "value"));
          // Unwrap a by-reference container (a python_value carrying
          // __list_ptr) to the shared list lvalue, so `del l[i]` through a
          // function parameter shifts the CALLER's list (mirrors how
          // append/sort reach the container via unwrap_any_container_receiver).
          if(!obj.is_nil() && is_python_value_type(obj.type()))
            obj = python_value_list(obj);
          // PLR §3.3.1: del obj[k] requires __delitem__. A concrete class whose
          // MRO defines none does not support item deletion -> TypeError.
          if(concrete_class_lacks_dunder(obj.type(), "__delitem__"))
          {
            emit_conditional_exception(true_exprt{}, "TypeError");
            continue;
          }
          if(!obj.is_nil() && is_python_list_type(obj.type()))
          {
            exprt idx = convert_expression(json_member(target, "slice"));
            const auto &list_st = to_struct_type(obj.type());
            const auto &data_type =
              to_array_type(list_st.components()[1].type());
            member_exprt data{obj, "data", data_type};
            member_exprt length{obj, "length", signedbv_typet{64}};

            // Shift elements: for j in [i, length-2]: data[j] = data[j+1]
            // For simplicity, generate unrolled shifts up to MAX_LIST_LENGTH
            for(std::size_t j = 0; j < PYTHON_MAX_LIST_LENGTH - 1; j++)
            {
              exprt jexpr = from_integer(j, signedbv_typet{64});
              // Guard: j >= idx and j < length - 1
              exprt guard = and_exprt{
                binary_relation_exprt{jexpr, ID_ge, idx},
                binary_relation_exprt{
                  jexpr,
                  ID_lt,
                  minus_exprt{length, from_integer(1, signedbv_typet{64})}}};
              exprt src = index_exprt{
                data, plus_exprt{jexpr, from_integer(1, signedbv_typet{64})}};
              index_exprt dst{data, jexpr};
              code_ifthenelset shift{guard, code_frontend_assignt{dst, src}};
              del_block.add(std::move(shift));
            }

            // length -= 1
            del_block.add(code_frontend_assignt{
              length,
              minus_exprt{length, from_integer(1, signedbv_typet{64})}});

            // PLR §5.6.1: after the shift the now-stale tail element
            // at the OLD length-1 position (== NEW length, since we
            // just decremented) still holds the value copied from the
            // (now removed) last entry. Zero it so a subsequent
            // struct-level equality compare against a freshly-built
            // list literal (which has zero padding past its length)
            // matches. Without this, `del lst[i]` followed by
            // `assert lst == [..]` fails the equality on the stale
            // data[length] slot. The index uses the just-decremented
            // length, which equals the OLD length-1, i.e. the first
            // out-of-bounds slot.
            del_block.add(code_frontend_assignt{
              index_exprt{data, length}, safe_zero(data_type.element_type())});
          }
          // PLR §7.5: del d["key"] on dict — scan, shift, decrement
          else if(!obj.is_nil() && is_python_dict_type(obj.type()))
          {
            // Invalidate dict literal tracking
            if(
              json_member(target, "value").is_object() &&
              is_node_type(json_member(target, "value"), "Name"))
            {
              std::string vn =
                json_string(json_member(json_member(target, "value"), "id"));
              dict_literals.erase(irep_idt{qualify_name(vn)});
            }
            exprt key = convert_expression(json_member(target, "slice"));
            if(!key.is_nil())
            {
              const auto &dict_st = to_struct_type(obj.type());
              const auto &keys_type =
                to_array_type(dict_st.components()[1].type());
              const auto &vals_type =
                to_array_type(dict_st.components()[2].type());
              member_exprt length{obj, "length", signedbv_typet{64}};
              member_exprt keys_arr{obj, "keys", keys_type};
              member_exprt vals_arr{obj, "values", vals_type};

              if(key.type() != keys_type.element_type())
                key = coerce_element(key, keys_type.element_type());

              // Find key, shift remaining left, decrement length
              static unsigned del_dict_ctr = 0;
              std::string fn =
                "__del_dict_found_" + std::to_string(del_dict_ctr++);
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
              del_block.add(code_frontend_assignt{found, false_exprt{}});
              for(std::size_t i = 0; i + 1 < PYTHON_MAX_DICT_SIZE; i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt in_range = binary_relation_exprt{idx, ID_lt, length};
                exprt match = equal_exprt{
                  python_dict_unbox_key(index_exprt{keys_arr, idx}),
                  python_dict_unbox_key(key)};
                // Set found on match
                del_block.add(code_ifthenelset{
                  and_exprt{in_range, and_exprt{not_exprt{found}, match}},
                  code_frontend_assignt{found, true_exprt{}}});
                // If found, shift left
                exprt next = from_integer(i + 1, signedbv_typet{64});
                code_blockt shift;
                shift.add(code_frontend_assignt{
                  index_exprt{keys_arr, idx}, index_exprt{keys_arr, next}});
                shift.add(code_frontend_assignt{
                  index_exprt{vals_arr, idx}, index_exprt{vals_arr, next}});
                del_block.add(code_ifthenelset{
                  and_exprt{in_range, found}, std::move(shift)});
              }
              del_block.add(code_ifthenelset{
                found,
                code_frontend_assignt{
                  length,
                  minus_exprt{length, from_integer(1, signedbv_typet{64})}}});

              // PLR §7.5: 'del d[key]' raises KeyError when the
              // key is not present. Emit a conditional raise so
              // try/except can catch it and bare uses surface
              // the missing-key bug.
              const symbolt *exc_sym =
                symbol_table.lookup("python::__exception_active");
              const symbolt *exc_type_sym =
                symbol_table.lookup("python::__exception_type");
              if(exc_sym != nullptr)
              {
                exprt missing = not_exprt{found};
                del_block.add(code_ifthenelset{
                  missing,
                  code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
                if(exc_type_sym != nullptr)
                {
                  long type_hash = exception_type_hash("KeyError");
                  del_block.add(code_ifthenelset{
                    missing,
                    code_frontend_assignt{
                      exc_type_sym->symbol_expr(),
                      from_integer(type_hash, python_int_type())}});
                }
              }
            }
          }
        }
        // PLR §7.4: 'del x' on a Name target removes the
        // binding. We can't model Python's NameError-on-
        // subsequent-read precisely without name-binding
        // tracking, so the converter approximates by
        // resetting the slot to the per-target-type None
        // marker via coerce_to_typed_slot. Subsequent reads
        // see the marker and `x is None` correctly returns
        // True. This is sound for the common idiom
        //
        //   del x
        //   if x is None: ...   # True after del
        //
        // and an over-approximation when later code expects
        // a NameError (we'd say "x is None" rather than
        // "x is unbound"). Acceptable since NameError on
        // dead-name use is a runtime check Python users
        // would catch with linters, not verifier-level
        // properties.
        else if(is_node_type(target, "Name"))
        {
          std::string nm = json_string(json_member(target, "id"));
          irep_idt sid{qualify_name(nm)};
          const symbolt *s = symbol_table.lookup(sid);
          if(s != nullptr)
          {
            exprt none_marker =
              coerce_to_typed_slot(python_none_value(), s->type);
            if(none_marker.type() != s->type)
              none_marker = safe_typecast(none_marker, s->type);
            del_block.add(
              code_frontend_assignt{s->symbol_expr(), std::move(none_marker)});
          }
          // PLR §7.5: mark the name deleted so a subsequent read (before any
          // reassignment) raises NameError. The `<qname>$deleted` flag flows
          // through the SSA; convert_name emits the conditional NameError, and
          // any assignment to the name clears it. Only del-tracked names carry
          // a flag (deleted_name_targets), so the read path stays cheap.
          if(deleted_name_targets.count(nm) > 0)
            del_block.add(
              code_frontend_assignt{deleted_name_flag(sid), true_exprt{}});
        }
        // PLR §7.4 / §9.4: `del obj.attr` on an instance
        // attribute. For a class-level attr, clear the
        // shadow flag so subsequent reads fall back to
        // class storage. For an instance-only attr, no
        // canonical "missing" representation exists; we
        // approximate by resetting the slot to the per-type
        // None marker (matches the `del Name` approximation).
        else if(is_node_type(target, "Attribute"))
        {
          exprt obj = convert_expression(json_member(target, "value"));
          std::string attr = json_string(json_member(target, "attr"));
          if(!obj.is_nil())
          {
            exprt obj_lvalue = obj;
            if(obj_lvalue.type().id() == ID_pointer)
              obj_lvalue = dereference_exprt{obj_lvalue};
            if(
              obj_lvalue.type().id() == ID_struct ||
              obj_lvalue.type().id() == ID_struct_tag)
            {
              std::string tag;
              if(obj_lvalue.type().id() == ID_struct)
                tag = id2string(to_struct_type(obj_lvalue.type()).get_tag());
              else
                tag = id2string(
                  to_struct_tag_type(obj_lvalue.type()).get_identifier());
              std::string cls_name =
                tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
              auto cla_it = class_level_attrs.find(cls_name);
              std::string shadow_name = "__shadow_" + attr;
              const struct_typet *st = nullptr;
              if(obj_lvalue.type().id() == ID_struct)
                st = &to_struct_type(obj_lvalue.type());
              else
              {
                auto cls_it = class_types.find(cls_name);
                if(cls_it != class_types.end())
                  st = &cls_it->second;
              }
              // PLR §7.5/§6.10: mark the attribute ABSENT on this instance so a
              // later read raises AttributeError. Soundness-critical: this fires
              // for every `del c.a` of a present-tracked attr reaching this
              // handler (all struct / struct-pointer receivers), before the
              // shadow/instance value handling below.
              {
                const std::string present_name = "__present_" + attr;
                if(st != nullptr && st->has_component(present_name))
                  del_block.add(code_frontend_assignt{
                    member_exprt{obj_lvalue, present_name, c_bool_typet{8}},
                    from_integer(0, c_bool_typet{8})});
              }
              if(
                cla_it != class_level_attrs.end() &&
                cla_it->second.count(attr) > 0 && st != nullptr &&
                st->has_component(shadow_name))
              {
                del_block.add(code_frontend_assignt{
                  member_exprt{obj_lvalue, shadow_name, c_bool_typet{8}},
                  from_integer(0, c_bool_typet{8})});
              }
              else if(st != nullptr && st->has_component(attr))
              {
                // PLR §7.4 / §3.3.2: `del obj.attr` on an instance-only
                // attribute. If the class defines __getattr__, a subsequent
                // read falls through to it -- store __getattr__'s (possibly
                // differently-typed) result into the slot so the retype is
                // modelled and a later cross-type use is caught (the slot was
                // typed `python_value` for this; closes c4/laurel-006).
                // Otherwise over-approximate by havocking the slot to nondet so
                // a later read cannot be proved equal to any concrete value
                // (resetting to the None marker was UNSOUND).
                const typet ft = st->get_component(attr).type();
                member_exprt slot{obj_lvalue, attr, ft};
                exprt ga = emit_getattr_fallback(obj_lvalue, attr, loc);
                if(!ga.is_nil())
                  del_block.add(
                    code_frontend_assignt{slot, coerce_to_typed_slot(ga, ft)});
                else
                  del_block.add(code_frontend_assignt{
                    slot, side_effect_expr_nondett{ft, loc}});
              }
            }
          }
        }
      }
      result = std::move(del_block);
    }
    else
      result = code_skipt{};
  }
  // PLR §10.6: The match statement — desugar to if-elif chain
  else if(node_type == "Match")
  {
    exprt subject = convert_expression(json_member(stmt, "subject"));
    const jsont &cases = json_member(stmt, "cases");
    if(!cases.is_array() || subject.is_nil())
      return code_skipt{};

    // Helper: compile a pattern into a (condition, bindings)
    // pair. Bindings are statements that bind names from the
    // matched subject; they prepend the case body.
    // `pattern_captures` accumulates the names a pattern binds so
    // the caller can invalidate their tracking (a capture rebinds
    // the name to the subject -- PLR §11.6 -- so a stale scalar
    // constant must not survive into the arm body or the merged
    // post-match state). Cleared before each top-level call.
    std::set<irep_idt> pattern_captures;
    std::function<std::pair<exprt, code_blockt>(const jsont &, const exprt &)>
      compile_pattern =
        [&](
          const jsont &pat, const exprt &subj) -> std::pair<exprt, code_blockt>
    {
      code_blockt binds;
      // MatchValue: constant comparison.
      if(is_node_type(pat, "MatchValue"))
      {
        exprt val = convert_expression(json_member(pat, "value"));
        if(val.is_nil())
          return {false_exprt{}, std::move(binds)};
        if(val.type() != subj.type())
          val = safe_typecast(val, subj.type());
        return {equal_exprt{subj, val}, std::move(binds)};
      }
      // MatchSingleton: None, True, False.
      if(is_node_type(pat, "MatchSingleton"))
      {
        exprt val = convert_expression(json_member(pat, "value"));
        if(val.is_nil())
          return {true_exprt{}, std::move(binds)};
        if(val.type() != subj.type())
          val = safe_typecast(val, subj.type());
        return {equal_exprt{subj, val}, std::move(binds)};
      }
      // MatchOr: alternation.
      if(is_node_type(pat, "MatchOr"))
      {
        const jsont &alts = json_member(pat, "patterns");
        exprt any_match = false_exprt{};
        code_blockt any_binds;
        if(alts.is_array())
        {
          for(const auto &alt : as_array(alts))
          {
            auto [c, b] = compile_pattern(alt, subj);
            any_match = or_exprt{std::move(any_match), std::move(c)};
            for(const auto &st : b.statements())
              any_binds.add(st);
          }
        }
        return {std::move(any_match), std::move(any_binds)};
      }
      // MatchAs: 'pattern as name' binds name on match; also
      // covers wildcard (no pattern, no name) and name-only
      // (binding wildcard: 'x' matches anything).
      if(is_node_type(pat, "MatchAs"))
      {
        const jsont &name = json_member(pat, "name");
        const jsont &inner = json_member(pat, "pattern");
        // Wildcard: _ — matches anything, no binding.
        if(name.is_null() && (!inner.is_object() || inner.is_null()))
          return {true_exprt{}, std::move(binds)};
        // Inner pattern (if any) contributes the condition.
        exprt cond = true_exprt{};
        if(inner.is_object() && !inner.is_null())
        {
          auto [c, b] = compile_pattern(inner, subj);
          cond = std::move(c);
          for(const auto &st : b.statements())
            binds.add(st);
        }
        // Name binding.
        if(name.is_string() && !name.value.empty())
        {
          std::string var_name = name.value;
          std::string qname = qualify_name(var_name);
          irep_idt sym_id{qname};
          if(symbol_table.lookup(sym_id) == nullptr)
          {
            symbolt ns{sym_id, subj.type(), "python"};
            ns.base_name = var_name;
            ns.is_lvalue = true;
            ns.is_state_var = true;
            ns.is_static_lifetime = current_function.empty();
            symbol_table.add(ns);
          }
          binds.add(code_frontend_assignt{
            symbol_table.lookup_ref(sym_id).symbol_expr(), subj});
          pattern_captures.insert(sym_id);
        }
        return {std::move(cond), std::move(binds)};
      }
      // MatchClass: case Point(x, y) or Point(x=a, y=b).
      // Check isinstance(subj, cls) then bind positional /
      // keyword attributes. Positional bindings require the
      // class to expose __match_args__ — we approximate by
      // using the class's declared fields in order (first
      // after __class_tag).
      if(is_node_type(pat, "MatchClass"))
      {
        const jsont &cls_node = json_member(pat, "cls");
        std::string cls_name;
        if(is_node_type(cls_node, "Name"))
          cls_name = json_string(json_member(cls_node, "id"));
        if(cls_name.empty() || class_types.count(cls_name) == 0)
          return {true_exprt{}, std::move(binds)};
        const auto &cls_type = class_types.at(cls_name);
        // isinstance-style check. For tagged-union subjects,
        // read __class_tag through __class_ptr; for concrete
        // struct subjects, compare the declared type.
        exprt cond = true_exprt{};
        // Normalise a pointer subject by dereferencing it,
        // so member access works uniformly.
        exprt usubj = subj;
        if(
          usubj.type().id() == ID_pointer &&
          to_pointer_type(usubj.type()).base_type().id() == ID_struct)
          usubj = dereference_exprt{usubj};
        if(is_python_value_type(subj.type()))
        {
          pointer_typet i32_ptr{signedbv_typet{32}, 64};
          dereference_exprt class_tag{
            typecast_exprt{python_value_class_ptr(subj), i32_ptr},
            signedbv_typet{32}};
          auto ti = class_tag_ids.find(cls_name);
          if(ti != class_tag_ids.end())
          {
            cond = and_exprt{
              python_value_is(subj, python_type_tagt::CLASS),
              equal_exprt{
                class_tag, from_integer(ti->second, signedbv_typet{32})}};
          }
        }
        else if(
          usubj.type().id() == ID_struct || usubj.type().id() == ID_struct_tag)
        {
          std::string stag;
          if(usubj.type().id() == ID_struct)
            stag = id2string(to_struct_type(usubj.type()).get_tag());
          else
            stag = id2string(to_struct_tag_type(usubj.type()).get_identifier());
          if(stag.find("python_class_" + cls_name) != std::string::npos)
            cond = true_exprt{};
          else
            cond = false_exprt{};
        }
        // Cast the subject to the class struct so we can
        // access fields for binding.
        exprt cls_subj;
        if(is_python_value_type(subj.type()))
        {
          pointer_typet cls_ptr_type{cls_type, 64};
          cls_subj = dereference_exprt{
            typecast_exprt{python_value_class_ptr(subj), cls_ptr_type},
            cls_type};
        }
        else if(
          usubj.type().id() == ID_struct || usubj.type().id() == ID_struct_tag)
        {
          cls_subj = usubj;
        }
        // Positional patterns: bind the first N declared
        // fields (skipping __class_tag) in declaration order.
        const jsont &pos_pats = json_member(pat, "patterns");
        if(
          pos_pats.is_array() && !cls_subj.is_nil() &&
          (cls_subj.type().id() == ID_struct ||
           cls_subj.type().id() == ID_struct_tag))
        {
          std::vector<std::string> fields;
          for(const auto &c : cls_type.components())
          {
            std::string nm = id2string(c.get_name());
            if(nm == "__class_tag")
              continue;
            fields.push_back(nm);
          }
          std::size_t i = 0;
          for(const auto &sub : as_array(pos_pats))
          {
            if(i >= fields.size())
              break;
            typet ft = cls_type.get_component(fields[i]).type();
            member_exprt field_expr{cls_subj, fields[i], ft};
            auto [sc, sb] = compile_pattern(sub, field_expr);
            cond = and_exprt{std::move(cond), std::move(sc)};
            for(const auto &st : sb.statements())
              binds.add(st);
            i++;
          }
        }
        // Keyword patterns: bind by attribute name.
        const jsont &kwd_attrs = json_member(pat, "kwd_attrs");
        const jsont &kwd_patterns = json_member(pat, "kwd_patterns");
        if(
          kwd_attrs.is_array() && kwd_patterns.is_array() &&
          !cls_subj.is_nil() &&
          (cls_subj.type().id() == ID_struct ||
           cls_subj.type().id() == ID_struct_tag))
        {
          const auto &ka = as_array(kwd_attrs);
          const auto &kp = as_array(kwd_patterns);
          auto ait = ka.begin();
          auto pit = kp.begin();
          while(ait != ka.end() && pit != kp.end())
          {
            std::string attr_name = ait->is_string() ? ait->value : "";
            if(!attr_name.empty() && cls_type.has_component(attr_name))
            {
              typet ft = cls_type.get_component(attr_name).type();
              member_exprt field_expr{cls_subj, attr_name, ft};
              auto [sc, sb] = compile_pattern(*pit, field_expr);
              cond = and_exprt{std::move(cond), std::move(sc)};
              for(const auto &st : sb.statements())
                binds.add(st);
            }
            ++ait;
            ++pit;
          }
        }
        return {std::move(cond), std::move(binds)};
      }
      // MatchSequence: case [a, b, c] or case [a, *rest, b].
      // Check list type + length, then recurse on each element.
      if(is_node_type(pat, "MatchSequence"))
      {
        if(!is_python_list_type(subj.type()))
          return {false_exprt{}, std::move(binds)};
        const jsont &pats = json_member(pat, "patterns");
        if(!pats.is_array())
          return {true_exprt{}, std::move(binds)};
        const auto &pat_arr = as_array(pats);
        // Find star index (if any).
        std::size_t star_idx = pat_arr.size();
        {
          std::size_t i = 0;
          for(const auto &p : pat_arr)
          {
            if(is_node_type(p, "MatchStar"))
            {
              star_idx = i;
              break;
            }
            i++;
          }
        }
        const auto &list_st = to_struct_type(subj.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt length{subj, "length", signedbv_typet{64}};
        member_exprt data{subj, "data", data_type};
        exprt cond;
        if(star_idx == pat_arr.size())
        {
          // No star: exact length match.
          cond = equal_exprt{
            length,
            from_integer((long long)pat_arr.size(), signedbv_typet{64})};
          std::size_t i = 0;
          for(const auto &p : pat_arr)
          {
            exprt elem = index_exprt{data, from_integer(i, signedbv_typet{64})};
            auto [sc, sb] = compile_pattern(p, elem);
            cond = and_exprt{std::move(cond), std::move(sc)};
            for(const auto &st : sb.statements())
              binds.add(st);
            i++;
          }
        }
        else
        {
          // With star: length >= prefix + suffix.
          std::size_t prefix = star_idx;
          std::size_t suffix = pat_arr.size() - star_idx - 1;
          long long minlen = (long long)(prefix + suffix);
          cond = binary_relation_exprt{
            length, ID_ge, from_integer(minlen, signedbv_typet{64})};
          // Index into pat_arr by copying to a vector first.
          std::vector<const jsont *> pvec;
          for(const auto &p : pat_arr)
            pvec.push_back(&p);
          // Match prefix against index 0..prefix-1.
          for(std::size_t i = 0; i < prefix; i++)
          {
            exprt elem = index_exprt{data, from_integer(i, signedbv_typet{64})};
            auto [sc, sb] = compile_pattern(*pvec[i], elem);
            cond = and_exprt{std::move(cond), std::move(sc)};
            for(const auto &st : sb.statements())
              binds.add(st);
          }
          // Bind the star name if present.
          {
            const jsont &star_pat = *pvec[star_idx];
            const jsont &star_name = json_member(star_pat, "name");
            if(star_name.is_string() && !star_name.value.empty())
            {
              // Bind a copy of the input list as the star binding;
              // precise slice modelling would be per-index.
              std::string var_name = star_name.value;
              std::string qname = qualify_name(var_name);
              irep_idt sym_id{qname};
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                symbolt ns{sym_id, subj.type(), "python"};
                ns.base_name = var_name;
                ns.is_lvalue = true;
                ns.is_state_var = true;
                ns.is_static_lifetime = current_function.empty();
                symbol_table.add(ns);
              }
              binds.add(code_frontend_assignt{
                symbol_table.lookup_ref(sym_id).symbol_expr(), subj});
              pattern_captures.insert(sym_id);
            }
          }
          // Match suffix against the last `suffix` elements.
          for(std::size_t j = 0; j < suffix; j++)
          {
            exprt idx = minus_exprt{
              length,
              from_integer((long long)(suffix - j), signedbv_typet{64})};
            exprt elem = index_exprt{data, idx};
            auto [sc, sb] = compile_pattern(*pvec[star_idx + 1 + j], elem);
            cond = and_exprt{std::move(cond), std::move(sc)};
            for(const auto &st : sb.statements())
              binds.add(st);
          }
        }
        return {std::move(cond), std::move(binds)};
      }
      // MatchMapping: case {"k": v, "k2": w}. Check dict type
      // and each key's presence + pattern match.
      if(is_node_type(pat, "MatchMapping"))
      {
        if(!is_python_dict_type(subj.type()))
          return {false_exprt{}, std::move(binds)};
        const jsont &keys = json_member(pat, "keys");
        const jsont &pats = json_member(pat, "patterns");
        const jsont &rest = json_member(pat, "rest");
        if(!keys.is_array() || !pats.is_array())
          return {true_exprt{}, std::move(binds)};
        const auto &key_arr = as_array(keys);
        const auto &pat_arr = as_array(pats);
        exprt cond = true_exprt{};
        auto kit = key_arr.begin();
        auto pit = pat_arr.begin();
        const auto &dict_st = to_struct_type(subj.type());
        const auto &keys_type = to_array_type(dict_st.components()[1].type());
        const auto &vals_type = to_array_type(dict_st.components()[2].type());
        member_exprt length{subj, "length", signedbv_typet{64}};
        member_exprt dkeys{subj, "keys", keys_type};
        member_exprt dvals{subj, "values", vals_type};
        while(kit != key_arr.end() && pit != pat_arr.end())
        {
          // Each key pattern should be a Constant. Evaluate.
          exprt key_expr = convert_expression(*kit);
          if(key_expr.is_nil())
          {
            ++kit;
            ++pit;
            continue;
          }
          if(key_expr.type() != keys_type.element_type())
            key_expr = coerce_element(key_expr, keys_type.element_type());
          // Search for the key in dict.keys. Build condition
          // "key is present AND its value matches pattern".
          exprt key_found = false_exprt{};
          exprt val_match = true_exprt{};
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt key_eq = equal_exprt{
              python_dict_unbox_key(index_exprt{dkeys, idx}),
              python_dict_unbox_key(key_expr)};
            exprt slot_match = and_exprt{in_range, std::move(key_eq)};
            key_found = or_exprt{std::move(key_found), slot_match};
            // Value pattern match: if key is at this slot, val
            // must match. OR together.
          }
          // For the binding part, recursive compile_pattern on
          // the slot value. Approximate by picking slot 0 when
          // key_found — precise matching would need per-slot
          // dispatch.
          exprt val_expr =
            index_exprt{dvals, from_integer(0, signedbv_typet{64})};
          auto [vc, vb] = compile_pattern(*pit, val_expr);
          val_match = std::move(vc);
          for(const auto &st : vb.statements())
            binds.add(st);
          cond = and_exprt{
            std::move(cond),
            and_exprt{std::move(key_found), std::move(val_match)}};
          ++kit;
          ++pit;
        }
        // rest name: bind the dict itself (approximation —
        // precise rest would exclude matched keys).
        if(rest.is_string() && !rest.value.empty())
        {
          std::string var_name = rest.value;
          std::string qname = qualify_name(var_name);
          irep_idt sym_id{qname};
          if(symbol_table.lookup(sym_id) == nullptr)
          {
            symbolt ns{sym_id, subj.type(), "python"};
            ns.base_name = var_name;
            ns.is_lvalue = true;
            ns.is_state_var = true;
            ns.is_static_lifetime = current_function.empty();
            symbol_table.add(ns);
          }
          binds.add(code_frontend_assignt{
            symbol_table.lookup_ref(sym_id).symbol_expr(), subj});
        }
        return {std::move(cond), std::move(binds)};
      }
      // MatchStar alone (shouldn't appear at top-level): treat
      // as match-anything.
      if(is_node_type(pat, "MatchStar"))
        return {true_exprt{}, std::move(binds)};
      // Unknown pattern kind — match-anything for soundness.
      return {true_exprt{}, std::move(binds)};
    };

    // Build if-elif chain from cases (reverse order).
    codet chain = code_skipt{};
    std::vector<const jsont *> case_list;
    for(const auto &c : as_array(cases))
      case_list.push_back(&c);

    // PLR §8.6 control-flow correctness: snapshot tracking before
    // the first case, restore between cases so each arm is
    // processed against the pre-match state, capture per-arm
    // post-state, and merge all arm states at the end (after the
    // chain is built). The chain itself iterates reverse-then-
    // wrap, which doesn't affect arm-state capture as long as
    // each arm's body is processed against a restored snapshot.
    tracking_snapshott pre_match_tracking = snapshot_tracking();
    std::vector<tracking_snapshott> arm_states;

    for(auto it = case_list.rbegin(); it != case_list.rend(); ++it)
    {
      const jsont &match_case = **it;
      const jsont &pattern = json_member(match_case, "pattern");
      const jsont &guard = json_member(match_case, "guard");
      const jsont &body = json_member(match_case, "body");

      pattern_captures.clear();
      auto [cond, binds] = compile_pattern(pattern, subject);
      code_blockt body_block;
      // Restore tracking to pre-match so this arm starts fresh.
      restore_tracking(pre_match_tracking);
      // The capture patterns rebind their names to (parts of) the
      // subject, so invalidate their tracking AFTER the restore --
      // else a stale scalar constant from before the match folds
      // inside the body and (via this arm's snapshot) into the
      // merged post-match state (`b = 1; match 3: case int() as b:
      // ...; t[b]` folded t[b] on the stale b=1). The restore above
      // would otherwise undo an invalidation done during
      // compile_pattern, so it must happen here.
      for(const auto &cap : pattern_captures)
        invalidate_reassigned_symbol(cap);
      // PLR §8.6: match case bodies are branches — bump
      // if_else_depth so legacy-callers of the path-insensitive
      // gate are still inhibited inside.
      if_else_depth++;
      if(body.is_array())
      {
        for(const auto &s : as_array(body))
          body_block.add(convert_statement(s));
      }
      if_else_depth--;
      arm_states.push_back(snapshot_tracking());
      // When the pattern matches, run bindings. Then check
      // the guard — if it fails, fall through to the rest of
      // the chain (PLR 10.6: 'If the guard evaluates as
      // false, the match statement proceeds to check the
      // next case block').
      code_blockt matched;
      for(const auto &st : binds.statements())
        matched.add(st);
      if(guard.is_object() && !guard.is_null())
      {
        exprt g = convert_expression(guard);
        if(!g.is_nil())
        {
          matched.add(code_ifthenelset{
            std::move(g), std::move(body_block), codet{chain}});
        }
        else
        {
          for(const auto &st : body_block.statements())
            matched.add(st);
        }
      }
      else
      {
        for(const auto &st : body_block.statements())
          matched.add(st);
      }
      chain = code_ifthenelset{cond, std::move(matched), std::move(chain)};
    }
    // Merge arm states: starting from the first arm's post-state,
    // pairwise merge with the rest. This produces a final
    // tracking state where only entries that ALL arms agree on
    // survive. If there's only one arm, merge it with the
    // pre-match snapshot (the case where the match doesn't fire
    // is equivalent to the snapshot continuing unchanged).
    if(arm_states.empty())
      restore_tracking(pre_match_tracking);
    else if(arm_states.size() == 1)
      merge_tracking(arm_states[0], pre_match_tracking);
    else
    {
      // Initialise the live maps from arm_states[0], then merge
      // with each subsequent arm via merge_tracking (which
      // overwrites).
      restore_tracking(arm_states[0]);
      for(std::size_t i = 1; i < arm_states.size(); i++)
      {
        tracking_snapshott current = snapshot_tracking();
        merge_tracking(current, arm_states[i]);
      }
    }
    result = std::move(chain);
  }
  else if(node_type == "With")
    result = convert_with(stmt);
  else if(node_type == "Try" || node_type == "TryStar")
    result = convert_try(stmt);
  else if(node_type == "Global" || node_type == "Nonlocal")
  {
    // PLR §7.12 (global) / §7.13 (nonlocal): track names for
    // scope resolution. The two differ in where the target
    // symbol lives — global writes module scope, nonlocal
    // writes the nearest enclosing function scope.
    const jsont &names = json_member(stmt, "names");
    if(names.is_array())
    {
      for(const auto &name : as_array(names))
      {
        if(name.is_string())
        {
          if(node_type == "Global")
            global_names.insert(name.value);
          else
            nonlocal_names.insert(name.value);
        }
      }
    }
    result = code_skipt{};
  }
  // PLR §7.13 / PEP 695: `type X = ...` — type alias
  // statement. The alias name is tracked as a type
  // reference; for verification we accept it as a no-op
  // (annotations referring to it resolve via the generic
  // annotation handler).
  else if(node_type == "TypeAlias")
  {
    result = code_skipt{};
  }
  else
  {
    log.warning() << "Unsupported Python statement type: " << node_type
                  << messaget::eom;
    result = code_skipt{};
  }

  // §12b: after a plain `x = ...` assignment statement, mark x's
  // runtime is-bound flag true. Keyed on the AST target (not the
  // emitted GOTO), so it is independent of how convert_assign built
  // the value write (constructor call, comprehension result, dict
  // rebuild, ...): this is the single assignment chokepoint. The flag
  // is asserted at every read (convert_name) for path-sensitive
  // UnboundLocalError detection.
  if(node_type == "Assign" && !current_function_bit_locals.empty())
  {
    code_blockt with_bind;
    with_bind.add(std::move(result));
    for(const auto &tgt : as_array(json_member(stmt, "targets")))
      if(is_node_type(tgt, "Name"))
      {
        std::string nm = json_string(json_member(tgt, "id"));
        if(current_function_bit_locals.count(nm) > 0)
        {
          const symbolt *b =
            symbol_table.lookup(irep_idt{qualify_name(nm) + "$bound"});
          if(b != nullptr)
            with_bind.add(
              code_frontend_assignt{b->symbol_expr(), true_exprt{}});
        }
      }
    if(with_bind.statements().size() > 1)
      result = std::move(with_bind);
    else
      result = std::move(with_bind.statements().front());
  }

  // PLR §4.2.1: an annotation that is a bare name bound nowhere raises
  // NameError when it is evaluated — at def time for function return /
  // parameter annotations, at statement time for a variable annotation.
  // Reuses the all_bound_names oracle (so typing imports / classes /
  // aliases resolve), is disabled under `from __future__ import
  // annotations`, and is restricted to main-module code (imported modules
  // have their own scopes). Emitting via pending_checks lets the flush
  // below prepend it so the exception is active at this statement.
  if(
    (node_type == "FunctionDef" || node_type == "AsyncFunctionDef" ||
     node_type == "AnnAssign") &&
    (current_function.empty() || main_module_defs.count(current_function) > 0))
  {
    std::vector<const jsont *> anns;
    if(node_type == "AnnAssign")
      anns.push_back(&json_member(stmt, "annotation"));
    else
    {
      anns.push_back(&json_member(stmt, "returns"));
      const jsont &args = json_member(stmt, "args");
      for(const char *grp : {"posonlyargs", "args", "kwonlyargs"})
      {
        const jsont &lst = json_member(args, grp);
        if(lst.is_array())
          for(const auto &a : as_array(lst))
            anns.push_back(&json_member(a, "annotation"));
      }
    }
    for(const jsont *ann : anns)
    {
      if(!undefined_annotation_name(*ann).empty())
      {
        emit_conditional_exception(true_exprt{}, "NameError");
        break;
      }
    }
  }

  // PLR §8.7: a NESTED def (or method body) reached via convert_statement
  // evaluates its default argument values at def-time too (top-level module defs
  // are handled in the module pass). Collect the checks at this source-order
  // position and route them through pending_checks (flushed below).
  if(node_type == "FunctionDef" || node_type == "AsyncFunctionDef")
  {
    code_blockt cb;
    collect_def_time_default_checks(
      json_member(stmt, "args"), get_location(stmt), cb);
    for(const auto &s : cb.statements())
      pending_checks.push_back(s);
  }

  // If expression conversion generated checks, prepend them
  if(!pending_checks.empty())
  {
    code_blockt block;
    for(auto &check : pending_checks)
      block.add(std::move(check));
    // Pending checks may set __exception_active (e.g. the Option-4
    // math-domain check raises ValueError for a known out-of-domain
    // input). If such a check is present, guard the main body so
    // a subsequent 'return math.sqrt(-1.0)' inside a try/except
    // doesn't return before the handler can run. We only install
    // the guard when at least one pending check actually assigns
    // to __exception_active — a blanket guard on every statement
    // with any pending_checks would (a) pessimise the symex graph
    // with a boolean test on every single statement and (b)
    // create spurious control-flow dependency on the exception
    // state that CBMC's solver then has to reason about, which
    // has been observed to slow the boto3-heavy benchmarks and
    // to introduce spurious verification failures where the
    // dependency combines poorly with refined-string reasoning.
    auto pending_sets_exception = [](const code_blockt &b)
    {
      std::function<bool(const exprt &)> has_exc_assign =
        [&](const exprt &e) -> bool
      {
        if(
          e.id() == ID_code && e.get(ID_statement) == ID_assign &&
          e.operands().size() >= 1 && e.operands()[0].id() == ID_symbol &&
          to_symbol_expr(e.operands()[0]).get_identifier() ==
            "python::__exception_active")
          return true;
        for(const auto &op : e.operands())
          if(has_exc_assign(op))
            return true;
        return false;
      };
      for(const auto &stmt : b.statements())
        if(has_exc_assign(stmt))
          return true;
      return false;
    };
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(exc_sym != nullptr && pending_sets_exception(block))
      block.add(
        code_ifthenelset{not_exprt{exc_sym->symbol_expr()}, std::move(result)});
    else
      block.add(std::move(result));
    pending_checks.clear();
    // PLR §3.1: emit by-ref mutation write-backs after the body.
    for(auto &pc : pending_post_checks)
      block.add(std::move(pc));
    pending_post_checks.clear();
    return std::move(block);
  }

  // PLR §3.1: a statement may have produced by-ref write-backs
  // (mutable-container argument mutated through a promoted copy)
  // without any pending pre-checks. Emit them after the result.
  if(!pending_post_checks.empty())
  {
    code_blockt block;
    block.add(std::move(result));
    for(auto &pc : pending_post_checks)
      block.add(std::move(pc));
    pending_post_checks.clear();
    return std::move(block);
  }

  return result;
}
