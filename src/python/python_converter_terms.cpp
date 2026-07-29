/// Python to GOTO converter — Constant (PLR §6.2.2) and
/// Name (PLR §6.2.1) terminal-expression handlers.
///
/// Extracted from python_converter.cpp to reduce the main file size.
/// All logic and class-member state remains unchanged — this file is
/// a pure source-split.

#include <util/arith_tools.h>
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

#include "python_complex_parser.h"
#include "python_converter.h"
#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

// PLR §6.2.2: Literals
// "Python supports string and bytes literals and various numeric literals."
exprt python_convertert::convert_constant(const jsont &expr)
{
  const jsont &value = json_member(expr, "value");
  source_locationt loc = get_location(expr);

  // PLR §6.10.1: imaginary literal (`2j`, `1+2j` etc.) emitted by
  // the AST server as {"__complex__": true, "real": ..., "imag":
  // ...}. Distinguish from a string literal of the same form
  // (e.g. `"2j"` is a string in CPython, not complex).
  if(value.is_object() && !json_member(value, "__complex__").is_null())
  {
    const jsont &re_node = json_member(value, "real");
    const jsont &im_node = json_member(value, "imag");
    double re = 0.0, im = 0.0;
    if(re_node.is_number())
      re = std::stod(re_node.value);
    if(im_node.is_number())
      im = std::stod(im_node.value);
    ieee_floatt real_f{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    real_f.from_double(re);
    ieee_floatt imag_f{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    imag_f.from_double(im);
    struct_typet::componentst comps;
    comps.push_back(struct_typet::componentt{"real", double_type()});
    comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet ct{comps};
    ct.set_tag("python_complex");
    return struct_exprt{{real_f.to_expr(), imag_f.to_expr()}, ct};
  }

  if(value.is_true())
  {
    return true_exprt{};
  }
  else if(value.is_false())
  {
    return false_exprt{};
  }
  else if(value.is_null())
  {
    // PLR §3.2: "None — This type has a single value... used to signify
    // the absence of a value." Encoded as the canonical NONE-tagged
    // python_value. Typed numeric slots receive the legacy int
    // sentinel via safe_typecast/unwrap_value at the assignment site;
    // this preserves backwards compatibility while letting NONE-aware
    // consumers see a tagged value. See
    // doc/python-frontend-plans.md (deferred residuals).
    return python_none_value();
  }
  else if(value.is_number())
  {
    std::string val_str = value.value;
    // Check if it's a float
    if(
      val_str.find('.') != std::string::npos ||
      val_str.find('e') != std::string::npos ||
      val_str.find('E') != std::string::npos)
    {
      ieee_floatt ieee_val{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      // strtod is exception-free, reports parse position via endptr,
      // and signals overflow via errno (returning ±HUGE_VAL, which is
      // the right representation for an overflowed float literal).
      // The AST gives us a well-formed float literal here, so the
      // unconsumed-input branch is mostly defensive — but it does
      // catch e.g. AST values like "1e10000garbage" cleanly rather
      // than aborting with std::invalid_argument as std::stod would.
      errno = 0;
      char *endp = nullptr;
      const double v = std::strtod(val_str.c_str(), &endp);
      if(endp != val_str.c_str() + val_str.size())
        return side_effect_expr_nondett{
          ieee_val.to_expr().type(), source_locationt{}};
      ieee_val.from_double(v);
      return ieee_val.to_expr();
    }
    else
    {
      mp_integer int_val = string2integer(val_str);
      return from_integer(int_val, python_int_type());
    }
  }
  else if(value.is_string())
  {
    std::string str_val = value.value;

    // PLR §2.4.2: Detect bytes literals (b"Hello" → "b'Hello'" in JSON)
    if(str_val.size() >= 3 && str_val[0] == 'b' && str_val[1] == '\'')
    {
      // Extract bytes content between b' and '
      std::string raw = str_val.substr(2, str_val.size() - 3);
      // Parse escape sequences: \xNN, \n, \t, \r, \\, etc.
      std::vector<unsigned char> bytes;
      for(std::size_t i = 0; i < raw.size(); i++)
      {
        if(raw[i] == '\\' && i + 1 < raw.size())
        {
          char next = raw[i + 1];
          if(next == 'x' && i + 3 < raw.size())
          {
            std::string hex = raw.substr(i + 2, 2);
            bytes.push_back(
              static_cast<unsigned char>(std::stoi(hex, nullptr, 16)));
            i += 3;
          }
          else if(next == 'n')
          {
            bytes.push_back('\n');
            i++;
          }
          else if(next == 't')
          {
            bytes.push_back('\t');
            i++;
          }
          else if(next == 'r')
          {
            bytes.push_back('\r');
            i++;
          }
          else if(next == '\\')
          {
            bytes.push_back('\\');
            i++;
          }
          else if(next == '0')
          {
            bytes.push_back(0);
            i++;
          }
          else
            bytes.push_back(static_cast<unsigned char>(raw[i]));
        }
        else
          bytes.push_back(static_cast<unsigned char>(raw[i]));
      }
      // Model as list of uint8 — matches the type produced by
      // 'arg: bytes' annotations and lets bytes.decode/indexing
      // work uniformly across literals and arguments.
      typet u8 = unsignedbv_typet{8};
      typet lt = python_list_type(u8);
      const auto &data_type =
        to_array_type(to_struct_type(lt).components()[1].type());
      exprt::operandst elems;
      for(unsigned char b : bytes)
        elems.push_back(from_integer(b, u8));
      while(elems.size() < PYTHON_MAX_LIST_LENGTH)
        elems.push_back(from_integer(0, u8));
      // Structural metadata is ALWAYS signedbv[64] (the container structs
      // declare i64 lengths); python_int_type() would diverge under
      // --python-unbounded-ints (integer_typet) and break the
      // struct-assignment type check (the io/BytesIO b"" default crash).
      return struct_exprt{
        {from_integer(static_cast<long long>(bytes.size()), signedbv_typet{64}),
         array_exprt{std::move(elems), data_type}},
        lt};
    }

    // Note: We previously auto-parsed Constant string nodes that
    // syntactically looked like complex literals (e.g. "1+2j",
    // "(1-2j)") into python_complex struct values at term-conversion
    // time. That was wrong: in CPython such expressions remain
    // strings unless explicitly passed through `complex(s)`. The
    // auto-parse made `assert str(complex(1, 2)) == "(1+2j)"` fail
    // because the comparison's RHS was silently re-typed as a complex
    // struct. String-to-complex conversion now lives only in the
    // `complex()` builtin (see python_converter_call_builtins.cpp's
    // func_name == "complex" branch), which is also the only path
    // CPython's reference semantics require.

    // String literal
    return python_string_literal(str_val);
  }

  log.error() << "Unsupported constant value" << messaget::eom;
  return nil_exprt{};
}

// PLR §6.2.1: Identifiers (Names)
std::string
python_convertert::undefined_annotation_name(const jsont &annotation) const
{
  // PEP 563: deferred annotations are strings, never evaluated -> no error.
  if(future_annotations)
    return {};
  // Only a *bare name* annotation is evaluated to a single name. Subscripts
  // (List[int]), attributes (typing.List), and string forward-references
  // are not checkable here (and string forward-refs are not NameErrors).
  if(!annotation.is_object() || !is_node_type(annotation, "Name"))
    return {};
  const std::string id = json_string(json_member(annotation, "id"));
  if(id.empty())
    return {};
  // Bound somewhere in the module (incl. imported typing names / classes /
  // aliases) or a builtin -> resolves fine.
  if(all_bound_names.count(id) > 0 || is_python_builtin_name(id))
    return {};
  return id;
}

// "An identifier occurring as an atom is a name."
exprt python_convertert::convert_name(const jsont &expr)
{
  std::string id = json_string(json_member(expr, "id"));

  if(id == "True")
    return true_exprt{};
  else if(id == "False")
    return false_exprt{};
  else if(id == "None")
    return from_integer(python_none_sentinel_int(), python_int_type());
  else if(id == "NotImplemented")
  {
    // NotImplemented is a Python singleton returned by __op__ methods
    // when the operation is not supported for the given operand types.
    // A distinct sentinel integer lets callers at least detect it;
    // precise modelling is left to Step 2 (module support plan).
    return from_integer(mp_integer{-4611686018427387903LL}, python_int_type());
  }
  else if(id == "Ellipsis")
  {
    // "..." is used by type hints and slicing; return a sentinel.
    return from_integer(mp_integer{-4611686018427387902LL}, python_int_type());
  }
  else if(id == "__debug__")
    return true_exprt{};
  else if(id == "__name__")
    return python_string_literal("__main__");
  // PLR: __file__ is the module's path -- a str, NEVER None. The exact
  // path is environment-dependent, so model it as a fixed non-empty
  // string constant (its VALUE is rarely asserted; what matters is that
  // `__file__ is None` / `is not None` and truthiness are correct --
  // ESBMC github_4662_fail asserted `__file__ is None`, which must be
  // False). A symbolic nondet string would lose the `is None` precision.
  else if(id == "__file__")
    return python_string_literal("main.py");

  // PLR §7.5: reading a name that was `del`-eted (and not since reassigned)
  // raises NameError. Guard on the name's `<qname>$deleted` flag — only names
  // that appear as a `del` target anywhere (deleted_name_targets) carry a flag,
  // so every other name read stays on the fast path. The flag flows through the
  // SSA, so the guard is control-flow-precise: unconditional after a
  // straight-line del, conditional after a branch del, and false after a
  // reassignment (the binding/assignment clears it).
  if(deleted_name_targets.count(id) > 0)
  {
    const irep_idt fid{id2string(qualify_name(id)) + "$deleted"};
    if(symbol_table.lookup(fid) != nullptr)
      emit_conditional_exception(
        symbol_table.lookup_ref(fid).symbol_expr(), "NameError");
  }

  // Closure cell substrate (PLR §4.2.2), comprehension late-binding: a
  // loop variable referenced inside a comprehension's element closure
  // resolves to the unique per-comprehension symbol (final value),
  // giving late binding without clobbering an enclosing same-named var.
  if(!comprehension_var_redirect.empty())
  {
    auto rd = comprehension_var_redirect.find(id);
    if(rd != comprehension_var_redirect.end())
    {
      const symbolt *rs = symbol_table.lookup(rd->second);
      if(rs != nullptr)
        return rs->symbol_expr();
    }
  }

  // Closure cell substrate (PLR §4.2.2), mutating slice: a NONLOCAL
  // cell-variable captured by heap cell resolves to the dereference of
  // this function's pointer capture-param, bypassing qualify_name's
  // nonlocal redirect (which points at the single shared enclosing
  // symbol and would alias across factory invocations). The cell
  // pointer is bound per-invocation at the factory call site.
  if(!current_function.empty())
  {
    auto fcc = function_cell_capture_names.find("python::" + current_function);
    if(fcc != function_cell_capture_names.end() && fcc->second.count(id) > 0)
    {
      irep_idt pid{"python::" + current_function + "::" + id};
      const symbolt *ps = symbol_table.lookup(pid);
      if(ps != nullptr && ps->type.id() == ID_pointer)
        return dereference_exprt{ps->symbol_expr()};
    }
  }

  // Look up in symbol table — check versioned names first, then
  // function-scoped, then global
  const symbolt *sym = nullptr;

  // Check if this variable has been versioned (type change renaming)
  std::string qname = qualify_name(id);
  auto ver_it = variable_versions.find(qname);
  if(ver_it != variable_versions.end())
    sym = symbol_table.lookup(ver_it->second);

  // PLR §7.12 / §7.13: if qualify_name has redirected us to a
  // global or nonlocal binding, honour that before falling back
  // to the current function's local scope. Without this, the
  // local-scope lookup below clobbers the nonlocal redirect.
  if(sym == nullptr)
    sym = symbol_table.lookup(irep_idt{qname});

  if(sym == nullptr && !current_function.empty())
  {
    irep_idt scoped_id{"python::" + current_function + "::" + id};
    sym = symbol_table.lookup(scoped_id);
  }

  // §12b: UnboundLocalError. `id` is assigned somewhere in this
  // function, so it is local for the whole function (PLR §4.2.2), but
  // no local binding exists yet at this read. Report it and stop
  // falling back to an outer scope — CPython would raise here rather
  // than read the global/enclosing value.
  if(
    !current_function.empty() && current_function_bit_locals.count(id) > 0 &&
    global_names.count(id) == 0 && nonlocal_names.count(id) == 0)
  {
    // Path-sensitive: assert the runtime is-bound flag at the read, so
    // a path that didn't assign `id` (e.g. a different if-branch) is
    // caught even though the local symbol exists from another path.
    const symbolt *bit =
      symbol_table.lookup(irep_idt{qualify_name(id) + "$bound"});
    if(bit != nullptr)
      add_check(
        bit->symbol_expr(),
        "python-unbound-local",
        "local variable '" + id +
          "' referenced before assignment (UnboundLocalError)",
        get_location(expr));
    if(sym == nullptr)
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(
    sym == nullptr && !current_function.empty() &&
    current_function_locals.count(id) > 0 && global_names.count(id) == 0 &&
    nonlocal_names.count(id) == 0)
  {
    add_check(
      false_exprt{},
      "python-unbound-local",
      "local variable '" + id +
        "' referenced before assignment (UnboundLocalError)",
      get_location(expr));
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }

  // Fall back to enclosing (parent) function scopes — this makes
  // closure variables resolvable, e.g. 'self' used inside a nested
  // 'def' within a method body.
  if(sym == nullptr && !enclosing_functions.empty())
  {
    for(auto it = enclosing_functions.rbegin();
        sym == nullptr && it != enclosing_functions.rend();
        ++it)
    {
      irep_idt scoped_id{"python::" + *it + "::" + id};
      sym = symbol_table.lookup(scoped_id);
    }
  }
  if(sym == nullptr)
  {
    irep_idt global_id{"python::" + id};
    sym = symbol_table.lookup(global_id);
    // Root B (import scoping, PLR §4.2 / §7.11): a name READ that
    // resolves in the flat python:: table ONLY because some module's
    // stub defines it (imported_module_defs) — while the name itself
    // was never imported (`from datetime import datetime` binds ONLY
    // `datetime`; reading `timezone` is a NameError CPython raises) —
    // must not silently resolve to the leaked module-level symbol.
    // Mirrors the existing bare-CALL check in convert_call_user; same
    // gates (main-module code only, disabled under import-*).
    if(sym != nullptr && !saw_import_star)
    {
      const std::string cf_root2 =
        current_function.substr(0, current_function.find("::"));
      if(
        (current_function.empty() || main_module_defs.count(cf_root2) > 0) &&
        imported_module_defs.count(id) > 0 &&
        explicitly_imported_names.count(id) == 0 &&
        main_module_defs.count(id) == 0 && all_bound_names.count(id) == 0)
      {
        emit_conditional_exception(true_exprt{}, "NameError");
        return side_effect_expr_nondett{python_int_type(), get_location(expr)};
      }
    }
  }
  if(sym == nullptr)
  {
    // Built-in type names used as values (e.g., type(x) == int)
    static const std::map<std::string, int> type_tags = {
      {"int", 1},
      {"float", 2},
      {"bool", 3},
      {"str", 4},
      {"list", 5},
      {"tuple", 6},
      {"dict", 7},
      {"set", 8},
      {"frozenset", 9},
      {"bytes", 10},
      {"bytearray", 11},
      {"object", 12},
      {"type", 13},
      {"Exception", 14},
      {"BaseException", 15},
      {"ValueError", 16},
      {"TypeError", 17},
      {"KeyError", 18},
      {"IndexError", 19},
      {"StopIteration", 20},
      {"AttributeError", 21},
      {"ArithmeticError", 22},
      {"ZeroDivisionError", 23},
      {"NotImplementedError", 24},
      {"RuntimeError", 25},
      {"OSError", 26},
      {"FileNotFoundError", 27},
      {"GeneratorExit", 28},
      {"LookupError", 29},
      {"ImportError", 30},
      {"NameError", 31},
      {"UnicodeError", 32},
      {"MemoryError", 33},
      {"IOError", 34},
      {"EOFError", 35}};
    auto tt = type_tags.find(id);
    if(tt != type_tags.end())
      return from_integer(tt->second, python_int_type());

    // PLR §4.2.1: a reference to a name that is bound NOWHERE in the
    // module (the authoritative, over-inclusive all_bound_names set from
    // the AST server — covering assignment / tuple-or-list unpack / for /
    // with-as / except-as / comprehension / walrus targets, plus def /
    // class / arg / import / global / nonlocal names) and is not a builtin
    // raises NameError at runtime. Model it as an uncaught NameError
    // instead of silently returning a nondet placeholder (a soundness gap:
    // a real NameError bug would be proved away).
    //
    // Guards against over-reporting:
    //  * gated on ABSENCE from all_bound_names, never on "reached this
    //    fallback" — names bound only via side-tables (e.g. `x, y = f()`)
    //    ARE in all_bound_names, so they are never misflagged;
    //  * restricted to code syntactically in the MAIN module (module level
    //    or a main-module function), since all_bound_names describes the
    //    main module — imported-module bodies have their own scopes;
    //  * skipped under `from X import *` (names not enumerable);
    //  * the Python builtin functions / constants below may be referenced
    //    as bare values (`sorted(xs, key=len)`, `x is None`, `__name__`).
    // Nested functions carry a qualified current_function
    // ("outer::inner"); their code is still main-module code when the
    // ROOT of the chain is a main-module definition.
    const std::string cf_root =
      current_function.substr(0, current_function.find("::"));
    const bool in_main_module =
      current_function.empty() || main_module_defs.count(cf_root) > 0;
    if(
      !saw_import_star && in_main_module && all_bound_names.count(id) == 0 &&
      !is_python_builtin_name(id))
    {
      emit_conditional_exception(true_exprt{}, "NameError");
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }

    log.error() << "Unknown variable: " << id << messaget::eom;
    return nil_exprt{};
  }

  // PLR §3.1: Mutable containers passed by reference.
  // Parameters of list / dict type are registered as pointer-to-struct
  // (see convert_function_def's add_positional). Locals promoted to
  // pointer storage by an aliasing assignment (`b: list = a`, see
  // convert_ann_assign / convert_assign) are also pointer-to-struct.
  // Auto-dereference both at use sites so the rest of the converter's
  // member_exprt-based access machinery (xs.length, xs.data,
  // .keys/.values for dicts) keeps working transparently. The
  // resulting `*xs` is an lvalue that points at the underlying
  // container's storage, so mutations through it propagate.
  if(
    sym->type.id() == ID_pointer &&
    (is_python_list_type(to_pointer_type(sym->type).base_type()) ||
     is_python_dict_type(to_pointer_type(sym->type).base_type()) ||
     // reference-semantics-for-instances (Phase 2): a local instance ALIAS
     // (`b = a`) is pointer-promoted and recorded in alias_targets. Deref it so
     // attribute access / method dispatch through `b` go through the aliased
     // object's storage. Gated on alias_targets membership so a method `self`
     // pointer or a by-ref instance PARAM (pointer-to-instance but NOT an
     // alias) is untouched -- those are dereferenced at the attribute-access
     // sites, not here.
     (is_instance_pointer(sym->type) && alias_targets.count(irep_idt{qname}))))
  {
    return dereference_exprt{sym->symbol_expr()};
  }

  return sym->symbol_expr();
}
