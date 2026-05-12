/// \file
/// Python to GOTO converter — converts Python JSON AST to CBMC symbol table
///
/// This converter implements the semantics defined in the Python Language
/// Reference (https://docs.python.org/3/reference/). Comments throughout
/// this file cite specific sections using the format:
///
///   PLR §X.Y: section title     (Python Language Reference)
///   PLib: section title          (Python Library Reference — stdtypes/builtins)
///
/// The Language Reference defines syntax and core semantics. The Library
/// Reference defines built-in types and functions.
/// Source: ~/cpython.git/Doc/reference/ and ~/cpython.git/Doc/library/.
///
/// Key reference files:
///   reference/expressions.rst    — PLR §6: Expressions
///   reference/simple_stmts.rst   — PLR §7: Simple statements
///   reference/compound_stmts.rst — PLR §8: Compound statements
///   reference/datamodel.rst      — PLR §3: Data model
///   library/stdtypes.rst         — PLib: Built-in Types (truth testing,
///                                  numeric ops, sequences, mappings, sets)
///   library/functions.rst        — PLib: Built-in Functions (len, range,
///                                  abs, min, max, sum, sorted, etc.)

#include "python_converter.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>

#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>

python_convertert::python_convertert(
  symbol_table_baset &_symbol_table,
  const python_parse_treet &_parse_tree,
  message_handlert &_message_handler)
  : symbol_table{_symbol_table},
    parse_tree{_parse_tree},
    log{_message_handler},
    filename{_parse_tree.filename}
{
}

// --- JSON helpers ---

const jsont &
python_convertert::json_member(const jsont &obj, const std::string &key) const
{
  if(obj.is_object())
  {
    const auto &object = to_json_object(obj);
    auto it = object.find(key);
    if(it != object.end())
      return it->second;
  }
  return jsont::null_json_object;
}

std::string python_convertert::json_string(const jsont &node) const
{
  if(node.is_string())
    return node.value;
  return "";
}

long long python_convertert::json_integer(const jsont &node) const
{
  if(node.is_number())
  {
    std::istringstream iss{node.value};
    long long val = 0;
    iss >> val;
    return val;
  }
  return 0;
}

bool python_convertert::is_node_type(
  const jsont &node,
  const std::string &type_name) const
{
  return json_string(json_member(node, "_type")) == type_name;
}

const json_arrayt &python_convertert::as_array(const jsont &node) const
{
  if(node.is_array())
    return to_json_array(node);
  static const json_arrayt empty;
  return empty;
}

void python_convertert::add_check(
  exprt condition,
  const std::string &property_class,
  const std::string &comment,
  const source_locationt &loc)
{
  if(condition.type() != bool_typet{})
    condition = safe_typecast(condition, bool_typet{});

  source_locationt check_loc = loc;
  check_loc.set_property_class(property_class);
  check_loc.set_comment(comment);

  code_assertt assertion{condition};
  assertion.add_source_location() = check_loc;
  pending_checks.push_back(std::move(assertion));
}

// Helper: extract std::string from a constant string struct expression
// Also checks string_constants map for tracked symbol values
std::optional<double> python_convertert::try_eval_double(const exprt &e) const
{
  const exprt *ce = &e;
  if(ce->id() == ID_typecast && ce->operands().size() == 1)
    ce = &ce->operands()[0];
  if(ce->id() == ID_symbol)
  {
    auto it = float_constants.find(to_symbol_expr(*ce).get_identifier());
    if(it != float_constants.end())
      return it->second;
    return std::nullopt;
  }
  if(ce->is_constant() && ce->type().id() == ID_signedbv)
  {
    mp_integer iv;
    if(!to_integer(to_constant_expr(*ce), iv))
      return static_cast<double>(iv.to_long());
  }
  if(ce->is_constant() && ce->type().id() == ID_floatbv)
  {
    ieee_floatt fv{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    fv.from_expr(to_constant_expr(*ce));
    return std::stod(fv.to_ansi_c_string());
  }
  // Binary and comparison operations (all 2-operand cases). We only
  // evaluate the operands once per invocation, regardless of which
  // category matches, otherwise three separate size==2 blocks
  // (binary ops, modulo, comparisons) each call try_eval_double on
  // the same two operands, which turns into exponential work for
  // deeply-nested expressions whose root id matches none of the
  // categories below (e.g. large string-concat chains at the top of
  // a function body whose id is ID_side_effect etc.).
  if(ce->operands().size() == 2)
  {
    auto l = try_eval_double(ce->operands()[0]);
    auto r = try_eval_double(ce->operands()[1]);
    if(l.has_value() && r.has_value())
    {
      const irep_idt id = ce->id();
      if(id == ID_plus || id == ID_floatbv_plus)
        return l.value() + r.value();
      if(id == ID_minus || id == ID_floatbv_minus)
        return l.value() - r.value();
      if(id == ID_mult || id == ID_floatbv_mult)
        return l.value() * r.value();
      if((id == ID_div || id == ID_floatbv_div) && r.value() != 0)
        return l.value() / r.value();
      if((id == ID_mod || id == ID_floatbv_mod) && r.value() != 0)
        return std::fmod(l.value(), r.value());
      if(id == ID_lt)
        return l.value() < r.value() ? 1.0 : 0.0;
      if(id == ID_le)
        return l.value() <= r.value() ? 1.0 : 0.0;
      if(id == ID_gt)
        return l.value() > r.value() ? 1.0 : 0.0;
      if(id == ID_ge)
        return l.value() >= r.value() ? 1.0 : 0.0;
      if(id == ID_equal)
        return l.value() == r.value() ? 1.0 : 0.0;
      if(id == ID_notequal)
        return l.value() != r.value() ? 1.0 : 0.0;
    }
  }
  // Unary minus
  if(ce->id() == ID_unary_minus && ce->operands().size() == 1)
  {
    auto v = try_eval_double(ce->operands()[0]);
    if(v.has_value())
      return -v.value();
  }
  // If-then-else (from div-by-zero guards)
  if(ce->id() == ID_if && ce->operands().size() == 3)
  {
    // Try to evaluate the condition
    auto cond = try_eval_double(ce->operands()[0]);
    if(cond.has_value())
      return cond.value() != 0.0 ? try_eval_double(ce->operands()[1])
                                 : try_eval_double(ce->operands()[2]);
    // If condition unknown, can't evaluate
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::string>
python_convertert::extract_string_value(const exprt &e) const
{
  // Direct struct literal
  if(
    e.id() == ID_struct && e.operands().size() >= 2 &&
    e.operands()[0].is_constant())
  {
    mp_integer slen;
    if(!to_integer(to_constant_expr(e.operands()[0]), slen))
    {
      // New format: operands()[1] is address_of(index(array, 0))
      const exprt &data_op = e.operands()[1];
      const exprt *arr = nullptr;
      if(data_op.id() == ID_address_of &&
         data_op.operands().size() == 1 &&
         data_op.operands()[0].id() == ID_index)
        arr = &data_op.operands()[0].operands()[0];
      // Legacy format: operands()[1] is array directly
      if(data_op.id() == ID_array)
        arr = &data_op;
      if(arr != nullptr && arr->id() == ID_array)
      {
        std::string s;
        for(mp_integer i = 0; i < slen; ++i)
        {
          auto idx = i.to_ulong();
          if(idx >= arr->operands().size())
            return std::nullopt;
          if(!arr->operands()[idx].is_constant())
            return std::nullopt;
          mp_integer ch;
          if(to_integer(to_constant_expr(arr->operands()[idx]), ch))
            return std::nullopt;
          s += static_cast<char>(ch.to_ulong());
        }
        return s;
      }
    }
  }
  // Symbol — check tracked constants
  if(e.id() == ID_symbol)
  {
    auto it = string_constants.find(to_symbol_expr(e).get_identifier());
    if(it != string_constants.end())
      return it->second;
  }
  return std::nullopt;
}

// Helper: build a string struct from a std::string
// double_to_floatbv is defined in python_converter_helpers.h.

// build_string_struct is defined in python_converter_helpers.h.

/// Persistent-storage variant of python_string_literal: installs
/// the backing array as a static-lifetime symbol and returns a
/// struct_exprt whose .data points into that symbol. Kept as a
/// hook for future callers; the current @c_intrinsic path uses a
/// string_constantt directly, which gives the same guarantees
/// and plugs straight into CBMC's existing string-literal
/// machinery (__CPROVER_initialize, dead-object exemption, etc.).
exprt python_convertert::build_string_literal(const std::string &s)
{
  // Intern by content: reuse the same symbol for identical
  // literals so the symbol table doesn't grow unbounded for
  // programs with many string constants.
  static std::unordered_map<std::string, irep_idt> literal_to_symbol;
  auto it = literal_to_symbol.find(s);
  irep_idt sym_id;
  if(it != literal_to_symbol.end())
  {
    sym_id = it->second;
  }
  else
  {
    exprt::operandst chars;
    for(char c : s)
      chars.push_back(
        from_integer(static_cast<unsigned char>(c), unsignedbv_typet{8}));
    chars.push_back(from_integer(0, unsignedbv_typet{8})); // trailing NUL
    array_typet at{
      unsignedbv_typet{8}, from_integer(chars.size(), signedbv_typet{64})};
    array_exprt arr(std::move(chars), at);

    static unsigned lit_ctr = 0;
    std::string base = "__str_lit_" + std::to_string(lit_ctr++);
    sym_id = irep_idt{"python::" + base};
    if(symbol_table.lookup(sym_id) == nullptr)
    {
      // Store the symbol in C mode. The Python front-end's
      // startup doesn't copy 'value' into mode="python" static
      // symbols at __CPROVER_initialize time, so mode="python"
      // static arrays would look "dead" to CBMC's safety checks.
      // Registering the literal in C mode makes it a proper
      // string-literal-style static, matching the behaviour of
      // a C source with `const char s[] = "…";`.
      symbolt ls{sym_id, at, ID_C};
      ls.base_name = base;
      ls.is_lvalue = true;
      ls.is_state_var = true;
      ls.is_static_lifetime = true;
      ls.value = arr;
      symbol_table.add(ls);
    }
    literal_to_symbol[s] = sym_id;
  }

  const symbolt &sym = symbol_table.lookup_ref(sym_id);
  symbol_exprt sym_expr = sym.symbol_expr();
  exprt content = address_of_exprt{
    index_exprt{sym_expr, from_integer(0, signedbv_typet{64})}};
  exprt length =
    from_integer(static_cast<long long>(s.size()), signedbv_typet{64});
  return struct_exprt{{length, content}, python_string_type()};
}

/// Back-end-dispatching Python string literal. See
/// python-string-phase2-backend-abstraction.md.
exprt python_convertert::python_string_literal(const std::string &s)
{
  // Refined-string back-end: the struct-exprt shape is
  // what every downstream site already expects. For the
  // SMT-string back-end this will emit an
  // smt_string_constant_exprt instead; the lowering lands in
  // a follow-up PR and currently falls back to refined.
  return build_string_struct(s);
}

/// Shared math-intrinsic nondet-with-constraints emitter.
/// Used by both the decorator-driven @c_intrinsic path and
/// the attribute-style math.X(...) path so both share a
/// single source of truth for domain checks and range
/// constraints.
exprt python_convertert::emit_math_intrinsic_nondet(
  const std::string &domain_kind,
  const std::string &range_kind,
  const exprt &arg,
  const source_locationt &loc)
{
  ieee_floatt zf{
    ieee_float_spect::double_precision(),
    ieee_floatt::rounding_modet::ROUND_TO_EVEN};
  zf.from_double(0.0);
  ieee_floatt onef = zf;
  onef.from_double(1.0);
  ieee_floatt neg_onef = zf;
  neg_onef.from_double(-1.0);

  // Domain check: emit guarded ValueError so any caller's
  // try/except ValueError correctly intercepts.
  if(!domain_kind.empty())
  {
    exprt farg = arg;
    if(farg.type().id() != ID_floatbv)
      farg = safe_typecast(farg, double_type());
    exprt in_domain;
    if(domain_kind == "nonneg")
      in_domain = binary_relation_exprt{farg, ID_ge, zf.to_expr()};
    else if(domain_kind == "positive")
      in_domain = binary_relation_exprt{farg, ID_gt, zf.to_expr()};
    else if(domain_kind == "gt_neg_one")
      in_domain = binary_relation_exprt{farg, ID_gt, neg_onef.to_expr()};
    else if(domain_kind == "abs_le_1")
      in_domain = and_exprt{
        binary_relation_exprt{farg, ID_ge, neg_onef.to_expr()},
        binary_relation_exprt{farg, ID_le, onef.to_expr()}};
    else if(domain_kind == "abs_lt_1")
      in_domain = and_exprt{
        binary_relation_exprt{farg, ID_gt, neg_onef.to_expr()},
        binary_relation_exprt{farg, ID_lt, onef.to_expr()}};
    else if(domain_kind == "ge_1")
      in_domain = binary_relation_exprt{farg, ID_ge, onef.to_expr()};
    else
      in_domain = true_exprt{};
    emit_value_error(in_domain);
  }

  // Fresh nondet return.
  side_effect_expr_nondett nondet_ret{double_type(), loc};
  static unsigned math_nondet_ctr = 0;
  std::string tmp_name = "__math_nondet_" + std::to_string(math_nondet_ctr++);
  std::string tmp_qname = qualify_name(tmp_name);
  irep_idt tmp_id{tmp_qname};
  if(symbol_table.lookup(tmp_id) == nullptr)
  {
    symbolt tmp_sym{tmp_id, double_type(), "python"};
    tmp_sym.base_name = tmp_name;
    tmp_sym.is_lvalue = true;
    tmp_sym.is_state_var = true;
    symbol_table.add(tmp_sym);
  }
  symbol_exprt tmp_var = symbol_table.lookup_ref(tmp_id).symbol_expr();
  pending_checks.push_back(code_frontend_assignt{tmp_var, nondet_ret});

  // Range constraint.
  if(range_kind == "bound_pm_1")
  {
    pending_checks.push_back(
      code_assumet{binary_relation_exprt{tmp_var, ID_ge, neg_onef.to_expr()}});
    pending_checks.push_back(
      code_assumet{binary_relation_exprt{tmp_var, ID_le, onef.to_expr()}});
  }
  else if(range_kind == "nonneg")
  {
    pending_checks.push_back(
      code_assumet{binary_relation_exprt{tmp_var, ID_ge, zf.to_expr()}});
  }
  else if(range_kind == "positive")
  {
    pending_checks.push_back(
      code_assumet{binary_relation_exprt{tmp_var, ID_gt, zf.to_expr()}});
  }

  return std::move(tmp_var);
}

/// make_nondet_string is defined in python_converter_helpers.h.
/// emit_string_function is defined in python_converter_helpers.h.

/// Register a string expression with the string solver's array_pool.
/// Emits ID_cprover_associate_array_to_pointer_func and
/// ID_cprover_associate_length_to_array_func so the solver knows
/// about this string's content and length.
// register_string_with_solver is defined in python_converter_helpers.h.

/// Emit a boolean cprover_string_* function (equal, contains, etc.).
/// Returns a boolean expression. Definition shared with other
/// python_converter_*.cpp TUs via python_converter_helpers.h.
// emit_string_bool_function is defined in python_converter_helpers.h.

/// Emit a one-string-argument int-returning intrinsic call (e.g.,
/// cprover_string_length_func).
// emit_string_int_function is defined in python_converter_helpers.h.

/// Build a string literal and register it with the string solver.
// build_solver_string_literal is defined in python_converter_helpers.h.

// Helper: collect all Name references in a JSON AST subtree
// Uses operator[] which returns json_nullt for missing keys
// collect_name_refs and collect_param_names are defined in
// python_converter_helpers.h.

std::string python_convertert::qualify_name(const std::string &name) const
{
  // PLR §7.12: 'global x' — bind to module scope.
  if(!current_function.empty() && global_names.count(name))
    return "python::" + name;
  // PLR §7.13: 'nonlocal x' — bind to the nearest enclosing
  // function whose scope has x. We look at enclosing_functions
  // bottom-up; if x isn't found there (e.g. will be created
  // by the nonlocal statement itself), fall back to the
  // outermost enclosing function so the assignment writes
  // somewhere useful.
  if(!current_function.empty() && nonlocal_names.count(name))
  {
    for(auto it = enclosing_functions.rbegin();
        it != enclosing_functions.rend();
        ++it)
    {
      std::string candidate = "python::" + *it + "::" + name;
      if(symbol_table.lookup(irep_idt{candidate}) != nullptr)
        return candidate;
    }
    // Fallback: outermost enclosing function (if any).
    if(!enclosing_functions.empty())
      return "python::" + enclosing_functions.front() + "::" + name;
    return "python::" + name;
  }
  // Otherwise use function scope if inside a function
  if(!current_function.empty())
    return "python::" + current_function + "::" + name;
  return "python::" + name;
}

exprt python_convertert::unwrap_value(const exprt &e, const typet &target_type)
{
  if(!is_python_value_type(e.type()))
    return e; // already concrete

  // Extract the appropriate field based on target type
  if(
    target_type.id() == ID_signedbv || target_type.id() == ID_integer ||
    target_type == python_int_type())
    return python_value_int(e);
  else if(target_type.id() == ID_floatbv)
    return python_value_float(e);
  else if(target_type.id() == ID_bool)
  {
    // PLib stdtypes: Truth Value Testing (precise)
    // Falsy: None, False, 0, 0.0, empty string "", empty list []
    exprt bool_true = and_exprt{
      python_value_is(e, python_type_tagt::BOOL),
      notequal_exprt{
        python_value_bool(e), from_integer(0, signedbv_typet{32})}};
    exprt int_true = and_exprt{
      python_value_is(e, python_type_tagt::INT),
      notequal_exprt{python_value_int(e), from_integer(0, signedbv_typet{64})}};
    exprt float_true = and_exprt{
      python_value_is(e, python_type_tagt::FLOAT),
      notequal_exprt{python_value_float(e), safe_zero(double_type())}};
    // For STR: check length-is-nonzero through the string
    // intrinsic so consumers see the same precise value as
    // len() does. Previously used member_exprt on .length,
    // which required producer-side dual-emission ASSUMEs.
    exprt str_val = python_value_str(e);
    exprt str_len = emit_string_int_function(
      ID_cprover_string_length_func, str_val, symbol_table, pending_checks);
    exprt str_true = and_exprt{
      python_value_is(e, python_type_tagt::STR),
      notequal_exprt{str_len, from_integer(0, signedbv_typet{64})}};
    exprt list_true = and_exprt{
      python_value_is(e, python_type_tagt::LIST),
      notequal_exprt{
        member_exprt{python_value_list(e), "length", signedbv_typet{64}},
        from_integer(0, signedbv_typet{64})}};
    // NONE tag → false (not in any of the above)
    return or_exprt{
      or_exprt{bool_true, int_true},
      or_exprt{float_true, or_exprt{str_true, list_true}}};
  }
  else if(is_python_string_type(target_type))
    return python_value_str(e);
  else if(is_python_list_type(target_type))
    return python_value_list(e);
  else if(is_python_dict_type(target_type))
    return side_effect_expr_nondett{target_type, source_locationt{}};
  else if(target_type.id() == ID_struct && !is_python_value_type(target_type))
  {
    // Class narrowing via annotation: trust the caller's
    // declared Dog/Cat/etc. type and dereference
    // __class_ptr as that class struct. Note the target
    // class struct itself may have tagged-union fields
    // (when the class body assigns self.x = value without
    // an explicit type annotation) — the narrowing returns
    // a correct Dog struct but attribute reads still see
    // tagged-union field types. Adding 'self.x: int' class
    // annotations fixes that case. PLR §3.3.2.
    std::string ttag = id2string(to_struct_type(target_type).get_tag());
    if(ttag.substr(0, 13) == "python_class_")
    {
      pointer_typet cls_ptr_type{target_type, 64};
      return dereference_exprt{
        typecast_exprt{python_value_class_ptr(e), cls_ptr_type},
        target_type};
    }
    return side_effect_expr_nondett{target_type, source_locationt{}};
  }
  else if(
    target_type.id() == ID_struct_tag && !is_python_value_type(target_type))
    return side_effect_expr_nondett{target_type, source_locationt{}};

  // Default: extract int
  return python_value_int(e);
}

exprt python_convertert::wrap_value(const exprt &e)
{
  if(is_python_value_type(e.type()))
    return e; // already wrapped

  python_type_tagt tag = python_type_tagt::INT;
  if(e.type().id() == ID_floatbv)
    tag = python_type_tagt::FLOAT;
  else if(e.type().id() == ID_bool)
    tag = python_type_tagt::BOOL;
  else if(is_python_string_type(e.type()))
  {
    // Materialize string into a temporary symbol for the pointer
    static unsigned str_wrap_counter = 0;
    std::string tmp_name = "__str_val_" + std::to_string(str_wrap_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};
    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, python_string_type(), "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }
    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    // Assign the string value to the temp via pending_checks
    pending_checks.push_back(code_frontend_assignt{tmp_sym.symbol_expr(), e});
    return make_python_value(
      python_type_tagt::STR, address_of_exprt{tmp_sym.symbol_expr()});
  }

  // List: convert to list[python_value_type] and store pointer
  if(is_python_list_type(e.type()))
  {
    typet pv_list_type = python_list_type(python_value_type());
    const auto &pv_data_type =
      to_array_type(to_struct_type(pv_list_type).components()[1].type());

    const auto &src_st = to_struct_type(e.type());
    const auto &src_data_type = to_array_type(src_st.components()[1].type());
    member_exprt src_len{e, "length", signedbv_typet{64}};
    member_exprt src_data{e, "data", src_data_type};

    exprt::operandst wrapped_elems;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
    {
      exprt elem = index_exprt{src_data, from_integer(i, signedbv_typet{64})};
      wrapped_elems.push_back(wrap_value(elem));
    }

    exprt new_list = struct_exprt{
      {src_len, array_exprt{std::move(wrapped_elems), pv_data_type}},
      pv_list_type};

    static unsigned list_wrap_counter = 0;
    std::string tmp_name = "__list_val_" + std::to_string(list_wrap_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};
    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, pv_list_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }
    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    pending_checks.push_back(
      code_frontend_assignt{tmp_sym.symbol_expr(), new_list});
    return make_python_value(
      python_type_tagt::LIST, address_of_exprt{tmp_sym.symbol_expr()});
  }

  // For struct types (class instances, dicts, etc.) that don't
  // fit in the tagged union as a primitive, wrap with tag CLASS
  // and an address-of pointer. This preserves non-None identity
  // (tag != NONE) and keeps the underlying struct reachable via
  // python_value_class_ptr for future per-class dispatch.
  if(
    e.type().id() == ID_struct && !is_python_string_type(e.type()) &&
    !is_python_list_type(e.type()))
  {
    // We need a persistent pointer target. Materialise the struct
    // into a static-lifetime symbol so address_of yields a valid
    // pointer across statement boundaries.
    static unsigned class_wrap_counter = 0;
    std::string tmp_name = "__class_val_" + std::to_string(class_wrap_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};
    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, e.type(), "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }
    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    pending_checks.push_back(code_frontend_assignt{tmp_sym.symbol_expr(), e});
    // Ensure the materialised copy carries a valid __class_tag.
    // The 'return ClassName(args)' path doesn't explicitly set
    // __class_tag on the return-tmp before the wrap, so we
    // re-emit it here based on the struct's declared class tag.
    // The struct's type-tag 'python_class_<Name>' gives us the
    // class name.
    const auto &st = to_struct_type(e.type());
    std::string stag = id2string(st.get_tag());
    const std::string prefix = "python_class_";
    if(stag.compare(0, prefix.size(), prefix) == 0)
    {
      std::string cls_name = stag.substr(prefix.size());
      auto ti = class_tag_ids.find(cls_name);
      if(ti != class_tag_ids.end())
      {
        pending_checks.push_back(code_frontend_assignt{
          member_exprt{
            tmp_sym.symbol_expr(), "__class_tag", signedbv_typet{32}},
          from_integer(ti->second, signedbv_typet{32})});
      }
    }
    return make_python_value(
      python_type_tagt::CLASS, address_of_exprt{tmp_sym.symbol_expr()});
  }

  return make_python_value(tag, e);
}

exprt python_convertert::safe_typecast(const exprt &e, const typet &target)
{
  if(e.type() == target)
    return e;

  // Unwrap tagged unions
  if(is_python_value_type(e.type()))
    return unwrap_value(e, target);

  // Wrap into tagged union if target is tagged union
  if(is_python_value_type(target))
    return wrap_value(e);

  // Scalar-to-scalar casts are safe
  bool src_scalar = e.type().id() == ID_signedbv ||
                    e.type().id() == ID_unsignedbv ||
                    e.type().id() == ID_floatbv || e.type().id() == ID_bool ||
                    e.type().id() == ID_integer || e.type().id() == ID_c_bool;
  bool tgt_scalar = target.id() == ID_signedbv ||
                    target.id() == ID_unsignedbv || target.id() == ID_floatbv ||
                    target.id() == ID_bool || target.id() == ID_integer ||
                    target.id() == ID_c_bool;

  if(src_scalar && tgt_scalar)
  {
    // PLR §3.2: "Integers have unlimited precision" / "floating-point numbers"
    // For int constant → float, use ieee_floatt for exact conversion
    // (typecast_exprt can produce off-by-one-ULP results in the solver)
    if(
      target.id() == ID_floatbv && e.is_constant() &&
      (e.type().id() == ID_signedbv || e.type().id() == ID_unsignedbv))
    {
      mp_integer iv;
      if(!to_integer(to_constant_expr(e), iv))
      {
        ieee_floatt fv{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fv.from_integer(iv);
        return fv.to_expr();
      }
    }

    // PLib stdtypes: Truth Value Testing — "the following values are
    // considered false: None, False, zero, empty sequences/mappings"
    // None sentinel → false for truthiness
    if(target.id() == ID_bool && e.type().id() == ID_signedbv)
    {
      exprt none_val =
        from_integer(mp_integer{-4611686018427387904LL}, e.type());
      return and_exprt{
        notequal_exprt{e, from_integer(0, e.type())},
        notequal_exprt{e, none_val}};
    }
    return typecast_exprt{e, target};
  }

  // Class pointer cast: Derived* → Base* (layout-compatible)
  if(
    e.type().id() == ID_pointer && target.id() == ID_pointer &&
    to_pointer_type(e.type()).base_type().id() == ID_struct &&
    to_pointer_type(target).base_type().id() == ID_struct)
  {
    return typecast_exprt{e, target};
  }

  // Class struct cast: Derived → Base (take address, cast, deref)
  if(
    e.type().id() == ID_struct && target.id() == ID_pointer &&
    to_pointer_type(target).base_type().id() == ID_struct)
  {
    return typecast_exprt{address_of_exprt{e}, target};
  }

  // Class struct cast: Derived → Base (reinterpret via byte_extract)
  if(
    e.type().id() == ID_struct && target.id() == ID_struct &&
    to_struct_type(e.type()).get_tag() != to_struct_type(target).get_tag())
  {
    const auto &src_tag = to_struct_type(e.type()).get_tag();
    const auto &tgt_tag = to_struct_type(target).get_tag();
    if(
      !src_tag.empty() && !tgt_tag.empty() &&
      id2string(src_tag).substr(0, 13) == "python_class_" &&
      id2string(tgt_tag).substr(0, 13) == "python_class_")
    {
      // Check if source has all target's fields (inheritance)
      const auto &src_st = to_struct_type(e.type());
      const auto &tgt_st = to_struct_type(target);
      bool compatible = true;
      for(const auto &comp : tgt_st.components())
      {
        if(!src_st.has_component(comp.get_name()))
        {
          compatible = false;
          break;
        }
      }
      if(compatible)
      {
        exprt::operandst fields;
        for(const auto &comp : tgt_st.components())
          fields.push_back(member_exprt{e, comp.get_name(), comp.type()});
        return struct_exprt{std::move(fields), target};
      }
      // Incompatible class types — return nondet
    }
  }

  // PLR §4.1: Tagged union → bool (truth value testing)
  if(is_python_value_type(e.type()) && target.id() == ID_bool)
  {
    // Dispatch on tag: INT→int_val!=0, FLOAT→float_val!=0,
    // BOOL→bool_val, STR/LIST→true (non-empty assumed)
    return or_exprt{
      and_exprt{
        python_value_is(e, python_type_tagt::BOOL),
        notequal_exprt{
          python_value_bool(e), from_integer(0, signedbv_typet{32})}},
      and_exprt{
        python_value_is(e, python_type_tagt::INT),
        notequal_exprt{
          python_value_int(e), from_integer(0, signedbv_typet{64})}}};
  }

  // PLib stdtypes: list/string/dict truthiness — non-empty is truthy
  if(target.id() == ID_bool && e.type().id() == ID_struct)
  {
    if(
      is_python_string_type(e.type()) || is_python_list_type(e.type()) ||
      is_python_dict_type(e.type()))
      return notequal_exprt{
        member_exprt{e, "length", signedbv_typet{64}},
        from_integer(0, signedbv_typet{64})};
    // Complex truthiness: 0+0j is falsy
    if(
      e.type().id() == ID_struct &&
      to_struct_type(e.type()).get_tag() == "python_complex")
      return or_exprt{
        notequal_exprt{
          member_exprt{e, "real", double_type()}, safe_zero(double_type())},
        notequal_exprt{
          member_exprt{e, "imag", double_type()}, safe_zero(double_type())}};
    // Other structs (class instances) are always truthy
    return true_exprt{};
  }

  // List/dict type coercion: list[float] → list[int] etc.
  // The struct layout is the same (length + data array), only element type differs.
  if(
    (is_python_list_type(e.type()) && is_python_list_type(target)) ||
    (is_python_dict_type(e.type()) && is_python_dict_type(target)))
    return typecast_exprt{e, target};

  // Struct-to-scalar: a concrete class instance coerced to a
  // numeric target is always non-None. Return 0 instead of the
  // default nondet so the None-sentinel (a specific int value)
  // can't be chosen — otherwise a 'return ClassInstance()' path
  // from a function whose inferred return type is int would
  // allow 'result is None' to be satisfiable. Zero is a sound
  // choice: it's the default numeric value, and in boolean
  // context the caller's usual 'if result:' check would treat
  // it as falsy, but 'result is None' still distinguishes it
  // from the None sentinel (which is -2^62).
  if(e.type().id() == ID_struct && tgt_scalar)
    return from_integer(0, target);

  // Struct-to-scalar or other incompatible: return a nondet value
  // of the target type (overapproximation, avoids crash)
  return side_effect_expr_nondett{target, source_locationt{}};
}

long python_convertert::exception_type_hash(const std::string &type_name) const
{
  // Use class_tag_ids if the exception type is a known class
  auto it = class_tag_ids.find(type_name);
  if(it != class_tag_ids.end())
    return static_cast<long>(it->second) + 10000; // offset to avoid collision
  // Fallback: sum of ASCII values
  long hash = 0;
  for(char c : type_name)
    hash += static_cast<unsigned char>(c);
  return hash;
}

std::optional<exprt> python_convertert::math_function_domain(
  const std::string &fn,
  const exprt &x) const
{
  ieee_floatt zf{
    ieee_float_spect::double_precision(),
    ieee_floatt::rounding_modet::ROUND_TO_EVEN};
  zf.from_double(0.0);
  ieee_floatt onef = zf;
  onef.from_double(1.0);
  ieee_floatt neg_onef = zf;
  neg_onef.from_double(-1.0);

  // PLR / library reference:
  //   math.sqrt(x)         x >= 0
  //   math.log(x)          x > 0
  //   math.log2(x)         x > 0
  //   math.log10(x)        x > 0
  //   math.log1p(x)        x > -1
  //   math.asin(x)         -1 <= x <= 1
  //   math.acos(x)         -1 <= x <= 1
  //   math.atanh(x)        -1 <  x <  1
  //   math.acosh(x)        x >= 1
  if(fn == "sqrt")
    return binary_relation_exprt{x, ID_ge, zf.to_expr()};
  if(fn == "log" || fn == "log2" || fn == "log10")
    return binary_relation_exprt{x, ID_gt, zf.to_expr()};
  if(fn == "log1p")
    return binary_relation_exprt{x, ID_gt, neg_onef.to_expr()};
  if(fn == "asin" || fn == "acos")
    return and_exprt{
      binary_relation_exprt{x, ID_ge, neg_onef.to_expr()},
      binary_relation_exprt{x, ID_le, onef.to_expr()}};
  if(fn == "atanh")
    return and_exprt{
      binary_relation_exprt{x, ID_gt, neg_onef.to_expr()},
      binary_relation_exprt{x, ID_lt, onef.to_expr()}};
  if(fn == "acosh")
    return binary_relation_exprt{x, ID_ge, onef.to_expr()};
  // sin, cos, tan, exp, exp2, expm1, atan, sinh, cosh, tanh, asinh,
  // ceil, floor, fabs, trunc, copysign, etc. — no domain restriction.
  return std::nullopt;
}

void python_convertert::emit_value_error(const exprt &in_domain)
{
  const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
  const symbolt *exc_type_sym = symbol_table.lookup("python::__exception_type");
  if(exc_sym == nullptr)
    return;
  if(in_domain.is_true())
    return; // no domain violation possible
  const long type_hash = exception_type_hash("ValueError");
  if(in_domain.is_false() || in_domain.is_nil())
  {
    // Definitely out of domain — raise unconditionally.
    pending_checks.push_back(
      code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
    if(exc_type_sym != nullptr)
      pending_checks.push_back(code_frontend_assignt{
        exc_type_sym->symbol_expr(),
        from_integer(type_hash, python_int_type())});
    return;
  }
  // Conditional raise: if !in_domain, set exception flags.
  exprt out_of_domain = not_exprt{in_domain};
  pending_checks.push_back(code_ifthenelset{
    out_of_domain,
    code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
  if(exc_type_sym != nullptr)
    pending_checks.push_back(code_ifthenelset{
      out_of_domain,
      code_frontend_assignt{
        exc_type_sym->symbol_expr(),
        from_integer(type_hash, python_int_type())}});
}

exprt python_convertert::safe_zero(const typet &type) const
{
  // Resolve struct_tag_typet to actual struct for zero construction
  if(type.id() == ID_struct_tag)
  {
    const auto &tag = to_struct_tag_type(type);
    const symbolt *sym = symbol_table.lookup(tag.get_identifier());
    if(sym != nullptr && sym->is_type)
    {
      exprt result = safe_zero(sym->type);
      result.type() = type; // keep the tag type
      return result;
    }
  }
  if(
    type.id() == ID_signedbv || type.id() == ID_unsignedbv ||
    type.id() == ID_integer || type.id() == ID_natural ||
    type.id() == ID_c_bool)
    return from_integer(0, type);
  if(type.id() == ID_bool)
    return false_exprt{};
  if(type.id() == ID_floatbv)
  {
    ieee_floatt zero{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    return zero.to_expr();
  }
  // For string/list structs, build a zero-length struct with zeroed data
  if(type.id() == ID_struct)
  {
    const auto &st = to_struct_type(type);
    exprt::operandst fields;
    for(const auto &comp : st.components())
    {
      if(comp.type().id() == ID_array)
      {
        // Zero-fill the array
        const auto &arr_type = to_array_type(comp.type());
        exprt::operandst elems;
        mp_integer size;
        if(!to_integer(to_constant_expr(arr_type.size()), size))
        {
          for(mp_integer i = 0; i < size; ++i)
            elems.push_back(safe_zero(arr_type.element_type()));
        }
        fields.push_back(array_exprt{std::move(elems), arr_type});
      }
      else
        fields.push_back(safe_zero(comp.type()));
    }
    return struct_exprt{std::move(fields), st};
  }
  if(type.id() == ID_pointer)
    return null_pointer_exprt{to_pointer_type(type)};
  if(type.id() == ID_array)
  {
    const auto &arr_type = to_array_type(type);
    exprt::operandst elems;
    mp_integer size;
    if(!to_integer(to_constant_expr(arr_type.size()), size))
    {
      for(mp_integer i = 0; i < size; ++i)
        elems.push_back(safe_zero(arr_type.element_type()));
    }
    return array_exprt{std::move(elems), arr_type};
  }
  return side_effect_expr_nondett{type, source_locationt{}};
}

source_locationt python_convertert::get_location(const jsont &node) const
{
  source_locationt loc;
  loc.set_file(filename);

  const jsont &lineno = json_member(node, "lineno");
  if(lineno.is_number())
    loc.set_line(lineno.value);

  const jsont &col = json_member(node, "col_offset");
  if(col.is_number())
    loc.set_column(col.value);

  return loc;
}

// --- Type conversion ---
// PLR §3.2: The standard type hierarchy
// "Below is a list of the types that are built into Python."
// PLR §4.7.2: Annotation scopes
// "Type annotations are evaluated lazily in some contexts."

typet python_convertert::convert_type_annotation(const jsont &annotation)
{
  if(annotation.is_null())
    return python_int_type(); // default to int

  // Handle parameterized types: list[int], dict[str, int], Optional[T]
  // These appear as Subscript nodes: annotation.value.id is the base type
  if(is_node_type(annotation, "Subscript"))
  {
    std::string base =
      json_string(json_member(json_member(annotation, "value"), "id"));
    if(base == "list")
    {
      // Extract element type from the slice
      typet elem_type =
        convert_type_annotation(json_member(annotation, "slice"));
      return python_list_type(elem_type);
    }
    else if(base == "Optional")
    {
      // Optional[T] — for now, treat as T (None handling is future work)
      return convert_type_annotation(json_member(annotation, "slice"));
    }
    // dict[K, V], Set[T], etc. — fall through to base type
    if(base == "dict")
      return python_int_type(); // TODO: proper dict type
    if(base == "tuple")
    {
      // tuple[int, int] → struct with _0, _1, ... components
      const jsont &slice = json_member(annotation, "slice");
      struct_typet::componentst comps;
      if(is_node_type(slice, "Tuple"))
      {
        const jsont &elts = json_member(slice, "elts");
        if(elts.is_array())
        {
          int idx = 0;
          for(const auto &e : as_array(elts))
          {
            typet ct = convert_type_annotation(e);
            comps.push_back(
              struct_typet::componentt{"_" + std::to_string(idx++), ct});
          }
        }
      }
      else
      {
        // tuple[int] — single element
        typet ct = convert_type_annotation(slice);
        comps.push_back(struct_typet::componentt{"_0", ct});
      }
      struct_typet tuple_type{comps};
      tuple_type.set_tag("python_tuple");
      return tuple_type;
    }
    // Unknown parameterized type — use the base
    return convert_type_annotation(json_member(annotation, "value"));
  }

  // Handle Constant None annotation (-> None)
  if(is_node_type(annotation, "Constant"))
  {
    const jsont &val = json_member(annotation, "value");
    if(val.is_null())
      return empty_typet{};
  }

  // PLR §4.7.2: Forward references — string annotations like -> "Foo"
  if(is_node_type(annotation, "Constant"))
  {
    const jsont &val = json_member(annotation, "value");
    if(val.is_string())
    {
      std::string ref_name = val.value;
      if(class_types.count(ref_name))
        return class_types[ref_name];
      // Try as a built-in type name
      if(ref_name == "int")
        return python_int_type();
      if(ref_name == "float")
        return double_type();
      if(ref_name == "str")
        return python_string_type();
      if(ref_name == "bool")
        return bool_typet{};
      if(ref_name == "None")
        return empty_typet{};
      return python_int_type(); // unknown forward ref
    }
    if(val.is_null())
      return empty_typet{};
  }

  // PLR §3.2: Union types (X | Y) — use tagged union
  if(is_node_type(annotation, "BinOp"))
  {
    std::string op =
      json_string(json_member(json_member(annotation, "op"), "_type"));
    if(op == "BitOr")
      return python_value_type();
  }

  // Handle Attribute annotations (e.g., typing.List)
  if(is_node_type(annotation, "Attribute"))
  {
    std::string attr = json_string(json_member(annotation, "attr"));
    return convert_type_annotation(
      json_member(annotation, "value")); // recurse on the attribute name
  }

  std::string type_name = json_string(json_member(annotation, "id"));

  if(type_name == "int")
    return python_int_type();
  else if(type_name == "float")
    return double_type();
  else if(type_name == "bool")
    return bool_typet{};
  else if(type_name == "str")
    return python_string_type();
  else if(type_name == "None" || type_name == "NoneType")
    return empty_typet{};
  else if(type_name == "list" || type_name == "List")
    return python_list_type(python_int_type());
  else if(type_name == "dict" || type_name == "Dict")
    return python_int_type(); // dict type not fully modeled
  else if(type_name == "set" || type_name == "Set")
    return python_set_type();
  else if(type_name == "tuple" || type_name == "Tuple")
    return python_int_type(); // unparameterized tuple
  else if(
    type_name == "Any" || type_name == "Union" || type_name == "Literal" ||
    type_name == "Callable" || type_name == "BinaryIO" ||
    type_name == "TextIO" || type_name == "Iterator" ||
    type_name == "Iterable" || type_name == "Sequence" ||
    type_name == "Mapping" || type_name == "Type" || type_name == "ClassVar" ||
    type_name == "Final")
    return python_value_type();
  else if(class_types.count(type_name))
    return class_types[type_name];
  else
  {
    // Try to resolve from imported modules
    // Only for names that look like class names (uppercase first letter)
    // Only when processing the main source file (not imported modules)
    if(
      module_resolver && !processing_import && !type_name.empty() &&
      std::isupper(static_cast<unsigned char>(type_name[0])))
    {
      for(const auto &mod : imported_modules)
      {
        std::string sub_mod = mod + "." + type_name;
        const jsont *sub_ast = module_resolver(sub_mod);
        if(sub_ast != nullptr && !sub_ast->is_null())
        {
          process_imported_module(sub_mod, *sub_ast);
          if(class_types.count(type_name))
            return class_types[type_name];
        }
      }
    }
    if(
      type_name != "Any" && type_name != "S3" && !type_name.empty() &&
      !std::isupper(static_cast<unsigned char>(type_name[0])))
      log.warning() << "Unknown Python type annotation: " << type_name
                    << ", defaulting to int" << messaget::eom;
    return python_int_type();
  }
}

// --- Expression conversion ---
// PLR §6: Expressions
// "This chapter explains the meaning of the elements of expressions in Python."

exprt python_convertert::convert_expression(const jsont &expr)
{
  std::string node_type = json_string(json_member(expr, "_type"));

  exprt result = nil_exprt{};

  if(node_type == "Constant")
    result = convert_constant(expr);
  else if(node_type == "Name")
    result = convert_name(expr);
  else if(node_type == "BinOp")
    result = convert_bin_op(expr);
  else if(node_type == "UnaryOp")
    result = convert_unary_op(expr);
  else if(node_type == "BoolOp")
    result = convert_bool_op(expr);
  else if(node_type == "Compare")
    result = convert_compare(expr);
  else if(node_type == "Call")
    result = convert_call(expr);
  else if(node_type == "IfExp")
    result = convert_if_exp(expr);
  else if(node_type == "Subscript")
    result = convert_subscript(expr);
  else if(node_type == "Tuple")
    result = convert_tuple(expr);
  else if(node_type == "List")
    result = convert_list(expr);
  else if(node_type == "Attribute")
    result = convert_attribute(expr);
  else if(node_type == "Dict")
    result = convert_dict(expr);
  else if(node_type == "Set")
  {
    // PLR §6.2.6: Set displays — bitmap for int sets, list for others
    const jsont &elts = json_member(expr, "elts");
    if(!elts.is_array() || as_array(elts).empty())
    {
      result = struct_exprt{
        {from_integer(0, unsignedbv_typet{64}),
         from_integer(0, signedbv_typet{64})},
        python_set_type()};
    }
    else
    {
      // Check if all elements are constant integers
      bool all_int_constant = true;
      std::vector<mp_integer> values;
      for(const auto &elt : as_array(elts))
      {
        exprt val = convert_expression(elt);
        if(val.is_constant() && val.type().id() == ID_signedbv)
        {
          mp_integer iv;
          if(!to_integer(to_constant_expr(val), iv))
            values.push_back(iv);
          else
            all_int_constant = false;
        }
        else
          all_int_constant = false;
      }
      if(all_int_constant && !values.empty())
      {
        mp_integer bitmap{0};
        for(const auto &v : values)
        {
          if(v >= 0 && v < 64)
            bitmap = bitmap + power(2, v);
        }
        result = struct_exprt{
          {from_integer(bitmap, unsignedbv_typet{64}),
           from_integer(0, signedbv_typet{64})},
          python_set_type()};
      }
      else
      {
        // Non-integer set: fall back to list model
        result = convert_list(expr);
      }
    }
  }
  else if(node_type == "ListComp")
    result = convert_list_comp(expr);
  // PLR §6.2.4 / 6.2.5: GeneratorExp / SetComp share ListComp's AST
  // (elt + generators). A precise model of each container type is
  // not on the Step 1 path, but routing them through
  // convert_list_comp at least exercises the comprehension's body
  // and generators, so closure/name resolution inside them still
  // works. The resulting list is consumed as an opaque iterable by
  // callers.
  else if(node_type == "GeneratorExp" || node_type == "SetComp")
  {
    result = convert_list_comp(expr);
  }
  // PLR §6.2.7: dict comprehension. Built on top of the same
  // generator/unrolling logic as list comprehensions but emits a
  // python_dict struct at the end.
  else if(node_type == "DictComp")
    result = convert_dict_comp(expr);
  // PLR §6.12: named expressions (walrus operator, 'name := expr').
  //
  // 'x := value' evaluates 'value', assigns it to 'x', and yields
  // the value as the expression's result. Since side effects in
  // expressions are not part of CBMC's expression model, we use
  // the existing pending_checks queue: the assignment is emitted
  // as a separate code_frontend_assignt that runs at the enclosing
  // statement, and the expression itself evaluates to the value.
  //
  // Target must be a bare Name (that is what the grammar allows).
  else if(node_type == "NamedExpr")
  {
    const jsont &target = json_member(expr, "target");
    const jsont &value = json_member(expr, "value");
    exprt rhs = convert_expression(value);
    if(!rhs.is_nil() && target.is_object() && is_node_type(target, "Name"))
    {
      std::string name = json_string(json_member(target, "id"));
      irep_idt sym_id{qualify_name(name)};
      if(symbol_table.lookup(sym_id) == nullptr)
      {
        symbolt new_sym{sym_id, rhs.type(), "python"};
        new_sym.base_name = name;
        new_sym.is_lvalue = true;
        new_sym.is_state_var = true;
        new_sym.is_static_lifetime = current_function.empty();
        symbol_table.add(new_sym);
      }
      const symbolt &target_sym = symbol_table.lookup_ref(sym_id);
      exprt target_expr = target_sym.symbol_expr();
      // Harmonise types: if the target symbol exists with a different
      // type we typecast the rhs rather than overwriting the symbol.
      if(rhs.type() != target_expr.type())
        rhs = safe_typecast(rhs, target_expr.type());
      pending_checks.push_back(code_frontend_assignt{target_expr, rhs});
      result = target_expr;
    }
    else
    {
      result = rhs;
    }
  }
  else if(node_type == "JoinedStr")
  {
    // PLR §2.4.3: f-strings — concatenate literal parts with formatted values
    const jsont &values = json_member(expr, "values");
    if(!values.is_array() || as_array(values).empty())
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};

    // Build result by concatenating all parts
    struct_typet str_type = python_string_struct_def();
    const auto &data_type = array_typet(unsignedbv_typet{8}, from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));

    // Collect all bytes from constant parts; use nondet for formatted values
    std::string all_bytes;
    bool all_constant = true;
    for(const auto &v : as_array(values))
    {
      if(is_node_type(v, "Constant"))
      {
        const jsont &val = json_member(v, "value");
        if(val.is_string())
          all_bytes += val.value;
        else
          all_constant = false;
      }
      else
        all_constant = false; // FormattedValue — can't track content
    }

    if(!all_constant)
    {
      // Multi-part f-string: convert each part to a string,
      // then chain-concat them via cprover_string_concat_func.
      //
      // Parts:
      //   - Constant string: python_string_literal.
      //   - FormattedValue with int-typed value: route via
      //     cprover_string_of_int_func.
      //   - FormattedValue with float-typed value: route
      //     via cprover_string_of_double_func.
      //   - FormattedValue with str-typed value: use the
      //     value directly.
      //   - Anything else: fall through to full nondet.
      //
      // Format specs (:d, :.2f) and conversions (!r, !s, !a)
      // are not yet honoured — we convert as if unspecified.
      auto ensure_fn = [&](const irep_idt &fid)
      {
        if(symbol_table.lookup(fid) == nullptr)
        {
          array_typet inf_array_type{
            unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64})};
          std::vector<typet> at;
          if(fid == ID_cprover_associate_array_to_pointer_func)
          {
            at.push_back(inf_array_type);
            at.push_back(pointer_typet(unsignedbv_typet{8}, 64));
          }
          else
          {
            at.push_back(inf_array_type);
            at.push_back(signedbv_typet{64});
          }
          symbolt fs{
            fid,
            mathematical_function_typet(std::move(at), signedbv_typet{32}),
            "python"};
          fs.base_name = id2string(fid);
          symbol_table.add(fs);
        }
      };
      std::vector<exprt> parts;
      bool parts_ok = true;
      for(const auto &v : as_array(values))
      {
        if(is_node_type(v, "Constant"))
        {
          const jsont &cval = json_member(v, "value");
          if(cval.is_string())
            parts.push_back(python_string_literal(cval.value));
          else
          {
            parts_ok = false;
            break;
          }
          continue;
        }
        if(!is_node_type(v, "FormattedValue"))
        {
          parts_ok = false;
          break;
        }
        const jsont &conversion = json_member(v, "conversion");
        const jsont &format_spec = json_member(v, "format_spec");
        bool no_spec = !format_spec.is_object() || format_spec.is_null();
        bool no_conv = !conversion.is_object() ||
                       (conversion.is_number() && conversion.value == "-1");
        // Extract a constant format-spec string if present.
        // f"{x:05}" gives format_spec = JoinedStr([Constant("05")]).
        std::string spec_str;
        bool spec_constant = false;
        if(!no_spec && is_node_type(format_spec, "JoinedStr"))
        {
          const jsont &sp_values = json_member(format_spec, "values");
          if(sp_values.is_array() && as_array(sp_values).size() == 1)
          {
            const jsont &sp0 = *as_array(sp_values).begin();
            if(is_node_type(sp0, "Constant"))
            {
              const jsont &cv = json_member(sp0, "value");
              if(cv.is_string())
              {
                spec_str = cv.value;
                spec_constant = true;
              }
            }
          }
        }
        // Treat ':0N' (zero-pad to width N) for int args as a
        // precision win: emit a nondet string whose length is
        // exactly N. The content won't match Python's exact
        // padded decimal but for verification purposes the
        // length constraint is what callers usually check.
        bool handled_by_pad_spec = false;
        // :0N, :>N, :<N, :^N — pad to width N. Result length
        // is exactly N when the value fits (our
        // over-approximation). The spec can optionally lead
        // with an alignment char and/or fill char; we only
        // recognise the zero-pad and bare alignment variants.
        auto parse_width_spec =
          [&](const std::string &s) -> std::optional<long long>
        {
          std::string digits;
          if(s.empty())
            return std::nullopt;
          if(s[0] == '0' && s.size() >= 2 && std::isdigit((unsigned char)s[1]))
            digits = s.substr(1);
          else if((s[0] == '>' || s[0] == '<' || s[0] == '^') && s.size() >= 2)
            digits = s.substr(1);
          else
            digits = s;
          if(digits.empty())
            return std::nullopt;
          for(char c : digits)
            if(!std::isdigit((unsigned char)c))
              return std::nullopt;
          try
          {
            return std::stoll(digits);
          }
          catch(...)
          {
            return std::nullopt;
          }
        };
        std::optional<long long> width_opt;
        if(spec_constant)
          width_opt = parse_width_spec(spec_str);
        if(width_opt.has_value())
        {
          exprt inner_pad = convert_expression(json_member(v, "value"));
          if(
            inner_pad.type().id() == ID_signedbv ||
            inner_pad.type().id() == ID_integer ||
            is_python_string_type(inner_pad.type()))
          {
            long long width = *width_opt;
            // Materialise a nondet string and constrain its length.
            static unsigned pad_ctr = 0;
            std::string tn = "__fstr_pad_" + std::to_string(pad_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, python_string_type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
              }
              symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
              pending_checks.push_back(code_frontend_assignt{
                tv,
                side_effect_expr_nondett{
                  python_string_type(), get_location(expr)}});
              exprt len_intr = emit_string_int_function(
                ID_cprover_string_length_func,
                tv,
                symbol_table,
                pending_checks);
              pending_checks.push_back(code_assumet{equal_exprt{
                len_intr, from_integer(width, signedbv_typet{64})}});
              parts.push_back(std::move(tv));
              handled_by_pad_spec = true;
          }
        }
        if(handled_by_pad_spec)
          continue;
        if(!(no_spec && no_conv))
        {
          parts_ok = false;
          break;
        }
        exprt inner = convert_expression(json_member(v, "value"));
        if(inner.type().id() == ID_signedbv || inner.type().id() == ID_integer)
        {
          exprt i64 = inner.type() == signedbv_typet{64}
                        ? inner
                        : safe_typecast(inner, signedbv_typet{64});
          parts.push_back(emit_string_function(
            ID_cprover_string_of_int_func,
            {i64},
            symbol_table,
            pending_checks));
          ensure_fn(ID_cprover_associate_array_to_pointer_func);
          ensure_fn(ID_cprover_associate_length_to_array_func);
        }
        else if(inner.type().id() == ID_floatbv)
        {
          parts.push_back(emit_string_function(
            ID_cprover_string_of_double_func,
            {inner},
            symbol_table,
            pending_checks));
          ensure_fn(ID_cprover_associate_array_to_pointer_func);
          ensure_fn(ID_cprover_associate_length_to_array_func);
        }
        else if(is_python_string_type(inner.type()))
          parts.push_back(inner);
        else
        {
          parts_ok = false;
          break;
        }
      }
      if(parts_ok && !parts.empty())
      {
        // Chain-concatenate. emit_string_function with
        // ID_cprover_string_concat_func takes two string
        // args (each a {length, data} struct) and returns a
        // new string.
        auto to_struct = [](const exprt &s) -> exprt
        {
          if(s.id() == ID_struct && s.operands().size() == 2)
            return s;
          return struct_exprt(
            {member_exprt(s, "length", signedbv_typet{64}),
             member_exprt(s, "data", pointer_typet(unsignedbv_typet{8}, 64))},
            s.type());
        };
        exprt acc = parts[0];
        for(std::size_t i = 1; i < parts.size(); i++)
        {
          acc = emit_string_function(
            ID_cprover_string_concat_func,
            {to_struct(acc), to_struct(parts[i])},
            symbol_table,
            pending_checks);
        }
        return acc;
      }
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }

    // All parts are constant — build the string literal
    result = python_string_literal(all_bytes);
  }
  else if(node_type == "Lambda")
    result = convert_lambda(expr);
  // PLR §6.2.4: Starred expression — unwrap for basic support
  else if(node_type == "Starred")
  {
    result = convert_expression(json_member(expr, "value"));
  }
  // PLR §8.8: Await expression — evaluate sequentially (no concurrency)
  else if(node_type == "Await")
  {
    result = convert_expression(json_member(expr, "value"));
  }
  // PLR §6.3.3: slicings (x[a:b], x[a:b:c]). A full precise model is
  // out of scope for Step 1 of the module support plan, but emitting
  // a warning on every occurrence (281+ across the stdlib corpus)
  // drowns out actually actionable diagnostics. Treat a Slice node
  // as a nondet integer here; the enclosing Subscript handler falls
  // back to a nondet result when it can't fold the slice. Kept at
  // log.debug() so the occurrence is still visible with --verbosity
  // 9 or when --python-strict-warnings is set.
  else if(node_type == "Slice")
  {
    log_overapprox("Slice expression: using nondet over-approximation");
    result = side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
  else if(node_type == "Yield" || node_type == "YieldFrom")
  {
    log_overapprox(
      std::string{node_type} + " expression: using nondet over-approximation");
    const jsont &v = json_member(expr, "value");
    if(!v.is_null())
      result = convert_expression(v);
    else
      result = side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
  else
  {
    log.warning() << "Unsupported Python expression type: " << node_type
                  << messaget::eom;
  }

  // Return nil for unsupported expressions — callers handle nil gracefully
  return result;
}

// PLR §6.10: Comparisons
// "Comparisons can be chained arbitrarily, e.g., x < y <= z is equivalent
// to x < y and y <= z."

// PLR §6.2.5: List displays
// "A list display yields a new list object, the contents being specified
// by either a list of expressions or a comprehension."
// PLR §6.2.8: Comprehension displays

// PLR §8.7: Function definitions
// "A function definition defines a user-defined function object."
codet python_convertert::convert_function_def(const jsont &stmt)
{
  // PLR §8.7: skip @overload decorated functions (type hints only)
  const jsont &decorators = json_member(stmt, "decorator_list");
  bool is_c_intrinsic = false;
  std::string c_intrinsic_name;
  std::string c_intrinsic_fold;
  std::string c_intrinsic_domain;
  std::string c_intrinsic_range;
  int c_intrinsic_int_width = 0;
  if(decorators.is_array())
  {
    for(const auto &dec : as_array(decorators))
    {
      if(
        is_node_type(dec, "Name") &&
        json_string(json_member(dec, "id")) == "overload")
        return code_skipt{};
      // @c_intrinsic('NAME', fold='OP', domain='KIND', range='KIND',
      //              int_width=N) — route calls to the named C
      // function. Optional ``fold`` enables parse-time constant
      // folding; optional ``domain`` raises Python ValueError for
      // constant arguments that fail the named domain predicate;
      // optional ``range`` constrains the nondet return for
      // symbolic arguments; optional ``int_width`` overrides the
      // default Python-int→signedbv64 projection for C functions
      // that take/return 32-bit int.
      if(is_node_type(dec, "Call"))
      {
        const jsont &dec_func = json_member(dec, "func");
        if(
          is_node_type(dec_func, "Name") &&
          json_string(json_member(dec_func, "id")) == "c_intrinsic")
        {
          const jsont &dec_args = json_member(dec, "args");
          if(dec_args.is_array() && !as_array(dec_args).empty())
          {
            const jsont &first = *as_array(dec_args).begin();
            if(is_node_type(first, "Constant"))
            {
              c_intrinsic_name = json_string(json_member(first, "value"));
              is_c_intrinsic = !c_intrinsic_name.empty();
            }
          }
          const jsont &dec_kwargs = json_member(dec, "keywords");
          if(dec_kwargs.is_array())
          {
            for(const auto &kw : as_array(dec_kwargs))
            {
              const jsont &val = json_member(kw, "value");
              if(!is_node_type(val, "Constant"))
                continue;
              std::string arg_name = json_string(json_member(kw, "arg"));
              if(arg_name == "int_width")
              {
                // Integer literal: its stringified form lives in
                // .value as a decimal string, not in the json_string()
                // wrapper (which expects a string-typed value).
                const jsont &iv_node = json_member(val, "value");
                try
                {
                  c_intrinsic_int_width = std::stoi(iv_node.value);
                }
                catch(...)
                {
                  c_intrinsic_int_width = 0;
                }
                continue;
              }
              std::string arg_val = json_string(json_member(val, "value"));
              if(arg_name == "fold")
                c_intrinsic_fold = arg_val;
              else if(arg_name == "domain")
                c_intrinsic_domain = arg_val;
              else if(arg_name == "range")
                c_intrinsic_range = arg_val;
            }
          }
        }
      }
    }
  }

  std::string func_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // Build parameter list
  const jsont &args_node = json_member(stmt, "args");
  const jsont &params = json_member(args_node, "args");

  code_typet::parameterst parameters;

  // PLR §8.7: positional-only parameters (before the '/' marker).
  // These are ordinary parameters from a call-site perspective; we
  // must still bind them so the function body can reference them.
  const jsont &posonlyargs = json_member(args_node, "posonlyargs");
  auto add_positional = [&](const jsont &param)
  {
    std::string param_name = json_string(json_member(param, "arg"));
    const jsont &annotation = json_member(param, "annotation");
    if(annotation.is_null())
    {
      log.warning() << "parameter '" << param_name << "' of function '"
                    << func_name << "' has no type annotation" << messaget::eom;
    }
    typet param_type = annotation.is_null()
                         ? python_value_type()
                         : convert_type_annotation(annotation);

    // PLR §4.2.1: Class instances are passed by reference.
    if(
      param_type.id() == ID_struct &&
      id2string(to_struct_type(param_type).get_tag()).find("python_class_") !=
        std::string::npos &&
      param_name != "self")
    {
      param_type = pointer_type(param_type);
    }

    code_typet::parametert p{param_type};
    p.set_identifier("python::" + func_name + "::" + param_name);
    p.set_base_name(param_name);
    parameters.push_back(p);
  };

  if(posonlyargs.is_array())
  {
    for(const auto &param : as_array(posonlyargs))
      add_positional(param);
  }
  if(params.is_array())
  {
    for(const auto &param : as_array(params))
      add_positional(param);
  }

  // PLR §8.7: keyword-only arguments (after * in parameter list)
  const jsont &kwonlyargs = json_member(args_node, "kwonlyargs");
  if(kwonlyargs.is_array())
  {
    for(const auto &param : as_array(kwonlyargs))
    {
      std::string param_name = json_string(json_member(param, "arg"));
      const jsont &annotation = json_member(param, "annotation");
      typet param_type = annotation.is_null()
                           ? python_int_type()
                           : convert_type_annotation(annotation);
      code_typet::parametert p{param_type};
      p.set_identifier("python::" + func_name + "::" + param_name);
      p.set_base_name(param_name);
      parameters.push_back(p);
    }
  }

  // PLR §8.7: *args — catch-all positional argument tuple.
  // We model it as a list (our tuple model is effectively a list here).
  const jsont &vararg = json_member(args_node, "vararg");
  std::string varargs_name;
  if(!vararg.is_null())
  {
    varargs_name = json_string(json_member(vararg, "arg"));
    typet va_type = python_list_type(python_value_type());
    code_typet::parametert p{va_type};
    p.set_identifier("python::" + func_name + "::" + varargs_name);
    p.set_base_name(varargs_name);
    parameters.push_back(p);
  }

  // PLR §8.7: keyword-only arguments (anything after *args or a bare *).
  {
    const jsont &kwonlyargs = json_member(args_node, "kwonlyargs");
    if(kwonlyargs.is_array())
    {
      for(const auto &arg : as_array(kwonlyargs))
      {
        std::string param_name = json_string(json_member(arg, "arg"));
        typet ptype;
        const jsont &annot = json_member(arg, "annotation");
        if(!annot.is_null())
          ptype = convert_type_annotation(annot);
        else
          ptype = python_value_type();
        code_typet::parametert p{ptype};
        p.set_identifier("python::" + func_name + "::" + param_name);
        p.set_base_name(param_name);
        parameters.push_back(p);
      }
    }
  }

  // PLR §8.7: **kwargs — catch-all keyword argument dict
  const jsont &kwarg = json_member(args_node, "kwarg");
  std::string kwargs_name;
  if(!kwarg.is_null())
  {
    kwargs_name = json_string(json_member(kwarg, "arg"));
    typet kw_type = python_dict_type(python_string_type(), python_value_type());
    code_typet::parametert p{kw_type};
    p.set_identifier("python::" + func_name + "::" + kwargs_name);
    p.set_base_name(kwargs_name);
    parameters.push_back(p);
  }

  // Evaluate default parameter values at definition time (PLR §8.7)
  // Only for simple types — class instances use call-time evaluation
  {
    const jsont &defaults = json_member(args_node, "defaults");
    if(defaults.is_array() && !as_array(defaults).empty())
    {
      std::size_t n_defaults = as_array(defaults).size();
      std::size_t first_default = parameters.size() - n_defaults;
      auto def_it = as_array(defaults).begin();
      for(std::size_t i = first_default; i < parameters.size(); i++, ++def_it)
      {
        exprt val = convert_expression(*def_it);
        if(
          !val.is_nil() && val.type().id() != ID_struct &&
          val.type().id() != ID_pointer)
          default_values[{func_name, i}] = val;
      }
    }
  }

  // Return type
  bool has_yield = false;
  const jsont &returns = json_member(stmt, "returns");
  typet return_type = empty_typet{};
  if(!returns.is_null())
  {
    return_type = convert_type_annotation(returns);
  }
  else
  {
    // No return annotation — scan body for return/yield statements
    bool has_value_return = false;
    bool has_bare_return = false;
    bool has_none_return = false;
    // Inferred yield-element type for generator functions.
    // Starts as python_int_type; widens to float / str / bool
    // when the scanner observes a matching yield-literal.
    typet yield_element_type = python_int_type();
    std::function<void(const jsont &)> scan = [&](const jsont &body_node)
    {
      if(!body_node.is_array())
        return;
      for(const auto &s : as_array(body_node))
      {
        if(is_node_type(s, "Return"))
        {
          const jsont &rv = json_member(s, "value");
          if(rv.is_null())
            has_bare_return = true;
          else
          {
            has_value_return = true;
            // Detect explicit 'return None' — lets us infer
            // Optional[T] when a class-constructor return and a
            // None return coexist.
            if(
              is_node_type(rv, "Constant") &&
              json_member(rv, "value").is_null())
            {
              has_none_return = true;
            }
            else if(
              is_node_type(rv, "Name") &&
              json_string(json_member(rv, "id")) == "None")
            {
              has_none_return = true;
            }
            // Check if return value is a constructor call
            if(
              is_node_type(rv, "Call") &&
              is_node_type(json_member(rv, "func"), "Name"))
            {
              std::string call_name =
                json_string(json_member(json_member(rv, "func"), "id"));
              if(class_types.count(call_name))
              {
                typet this_type = class_types[call_name];
                if(return_type.id() == ID_empty)
                  return_type = this_type;
                else if(return_type != this_type)
                  // Multiple distinct class return types —
                  // Union[T1, T2] → widen to python_value_type.
                  // A concrete ClassInstance wraps to tag INT
                  // (non-None) in wrap_value, so the caller's
                  // 'is not None' check works; per-class
                  // dispatch inside the tagged union is still
                  // deferred (flagged as CLASS tag follow-up).
                  return_type = python_value_type();
              }
            }
          }
        }
        // Detect yield (generator function) and infer the
        // yielded-value type from the yield expression. Used
        // below to pick the generator's list element type.
        if(is_node_type(s, "Expr"))
        {
          const jsont &val = json_member(s, "value");
          if(is_node_type(val, "Yield") || is_node_type(val, "YieldFrom"))
          {
            has_yield = true;
            const jsont &yield_val = json_member(val, "value");
            if(is_node_type(yield_val, "Constant"))
            {
              const jsont &cv = json_member(yield_val, "value");
              if(cv.is_number())
              {
                if(cv.value.find('.') != std::string::npos)
                  yield_element_type = double_type();
              }
              else if(cv.is_string())
                yield_element_type = python_string_type();
              else if(cv.is_true() || cv.is_false())
                yield_element_type = bool_typet{};
            }
          }
        }
        // Recurse into if/else/while/for/try bodies
        if(json_member(s, "body").is_array())
          scan(json_member(s, "body"));
        if(json_member(s, "orelse").is_array())
          scan(json_member(s, "orelse"));
        if(json_member(s, "handlers").is_array())
        {
          for(const auto &h : as_array(json_member(s, "handlers")))
            if(json_member(h, "body").is_array())
              scan(json_member(h, "body"));
        }
      }
    };
    scan(json_member(stmt, "body"));

    // Generator functions return a list of the inferred yield-
    // element type (eager-evaluation model).
    if(has_yield)
      return_type = python_list_type(yield_element_type);
    else if(
      has_value_return && has_none_return &&
      (return_type.id() == ID_struct_tag || return_type.id() == ID_struct))
    {
      // Optional[ClassName] — function returns either a class
      // instance or None. Widen to python_value_type so the
      // caller's 'is not None' check dispatches precisely on the
      // tagged union's tag.
      return_type = python_value_type();
    }
    else if(has_value_return && return_type.id() == ID_empty)
    {
      // If any parameter is float, return type is likely float
      bool has_float_param = false;
      for(const auto &p : parameters)
      {
        if(p.type().id() == ID_floatbv)
          has_float_param = true;
      }
      return_type = has_float_param ? double_type() : python_int_type();
    }
    else if(!has_value_return)
      return_type = empty_typet{};
  }

  code_typet func_type{parameters, return_type};

  // Add closure captures as extra parameters
  irep_idt func_qid{"python::" + func_name};
  auto cap_it = closure_captures.find(id2string(func_qid));
  if(cap_it != closure_captures.end())
  {
    for(const auto &[outer_id, name, type] : cap_it->second)
    {
      code_typet::parametert p{type};
      std::string cap_param_id = "python::" + func_name + "::" + name;
      p.set_identifier(cap_param_id);
      p.set_base_name(name);
      parameters.push_back(p);
    }
    func_type = code_typet{parameters, return_type};
  }

  if(has_yield)
    generator_functions.insert(func_name);

  // Create function symbol BEFORE converting the body
  // (so recursive calls can find it)
  irep_idt symbol_id{"python::" + func_name};

  if(symbol_table.lookup(symbol_id) == nullptr)
  {
    symbolt func_symbol{symbol_id, func_type, "python"};
    func_symbol.base_name = func_name;
    func_symbol.location = loc;
    func_symbol.is_lvalue = true;
    symbol_table.add(func_symbol);
  }
  else
  {
    // Update pre-registered placeholder with real signature
    symbol_table.get_writeable_ref(symbol_id).type = func_type;
  }

  // Record @c_intrinsic mapping so convert_call can redirect to
  // the named C function instead of executing the Python body.
  if(is_c_intrinsic)
  {
    c_intrinsic_map[symbol_id] = c_intrinsic_name;
    if(!c_intrinsic_fold.empty())
      c_intrinsic_fold_map[symbol_id] = c_intrinsic_fold;
    if(!c_intrinsic_domain.empty())
      c_intrinsic_domain_map[symbol_id] = c_intrinsic_domain;
    if(!c_intrinsic_range.empty())
      c_intrinsic_range_map[symbol_id] = c_intrinsic_range;
    if(c_intrinsic_int_width == 32 || c_intrinsic_int_width == 64)
      c_intrinsic_int_width_map[symbol_id] = c_intrinsic_int_width;
  }

  // Create parameter symbols
  for(const auto &p : parameters)
  {
    symbolt param_symbol{p.get_identifier(), p.type(), "python"};
    param_symbol.base_name = p.get_base_name();
    param_symbol.location = loc;
    param_symbol.is_lvalue = true;
    param_symbol.is_state_var = true;
    param_symbol.is_parameter = true;
    if(symbol_table.lookup(param_symbol.name) == nullptr)
      symbol_table.add(param_symbol);
  }

  // Convert function body
  std::string saved_function = current_function;
  auto saved_globals = global_names;
  if(!current_function.empty())
    enclosing_functions.push_back(current_function);
  current_function = func_name;
  global_names.clear();
  nonlocal_names.clear();

  // For generator functions, create __gen_result list
  bool is_generator = generator_functions.count(func_name) > 0;
  irep_idt gen_result_id;
  if(is_generator)
  {
    std::string grn = "__gen_result_" + func_name;
    std::string grq = qualify_name(grn);
    gen_result_id = irep_idt{grq};
    if(symbol_table.lookup(gen_result_id) == nullptr)
    {
      symbolt grs{gen_result_id, return_type, "python"};
      grs.base_name = grn;
      grs.is_lvalue = true;
      grs.is_state_var = true;
      symbol_table.add(grs);
    }
  }

  code_blockt body_block;

  // For generators, initialize __gen_result.length = 0
  if(is_generator)
  {
    body_block.add(code_frontend_assignt{
      member_exprt{
        symbol_table.lookup_ref(gen_result_id).symbol_expr(),
        "length",
        signedbv_typet{64}},
      from_integer(0, signedbv_typet{64})});
  }

  const jsont &body = json_member(stmt, "body");

  // Detect closures: scan for nested FunctionDefs that reference
  // our parameters or local variables (free variables).
  if(body.is_array())
  {
    // Collect our parameters
    std::set<std::string> our_params;
    for(const auto &p : parameters)
      our_params.insert(std::string{id2string(p.get_base_name())});

    // Also collect local variable names (assigned in our body)
    // These are Name targets in Assign/AnnAssign statements
    for(const auto &s : as_array(body))
    {
      if(is_node_type(s, "AnnAssign"))
      {
        const jsont &tgt = json_member(s, "target");
        if(is_node_type(tgt, "Name"))
          our_params.insert(json_string(json_member(tgt, "id")));
      }
      else if(is_node_type(s, "Assign"))
      {
        const jsont &tgts = json_member(s, "targets");
        if(tgts.is_array())
        {
          for(const auto &t : as_array(tgts))
          {
            if(is_node_type(t, "Name"))
              our_params.insert(json_string(json_member(t, "id")));
          }
        }
      }
    }

    for(const auto &s : as_array(body))
    {
      if(is_node_type(s, "FunctionDef") || is_node_type(s, "AsyncFunctionDef"))
      {
        std::string nested_name = json_string(json_member(s, "name"));
        std::set<std::string> nested_params = collect_param_names(s);
        // Collect names declared 'nonlocal' or 'global' inside the
        // nested function — these must not be captured by value;
        // they resolve through qualify_name's nonlocal/global
        // redirect directly to the enclosing/module scope.
        std::set<std::string> nested_nonlocal_global;
        const jsont &nbody = json_member(s, "body");
        if(nbody.is_array())
        {
          for(const auto &ns : as_array(nbody))
          {
            if(is_node_type(ns, "Nonlocal") || is_node_type(ns, "Global"))
            {
              const jsont &nnames = json_member(ns, "names");
              if(nnames.is_array())
              {
                for(const auto &n : as_array(nnames))
                  if(n.is_string())
                    nested_nonlocal_global.insert(n.value);
              }
            }
          }
        }
        std::set<std::string> refs;
        collect_name_refs(json_member(s, "body"), refs);
        std::vector<std::tuple<std::string, std::string, typet>> captures;
        for(const auto &ref : refs)
        {
          if(nested_params.count(ref) || !our_params.count(ref))
            continue;
          if(nested_nonlocal_global.count(ref))
            continue; // handled by qualify_name redirect at use site
          // Find the variable's symbol and type
          std::string var_id = "python::" + func_name + "::" + ref;
          const symbolt *var_sym = symbol_table.lookup(irep_idt{var_id});
          if(var_sym == nullptr)
          {
            // Try as a parameter
            bool found = false;
            for(const auto &p : parameters)
            {
              if(id2string(p.get_base_name()) == ref)
              {
                captures.push_back(
                  {id2string(p.get_identifier()), ref, p.type()});
                found = true;
                break;
              }
            }
            // Local variable not yet in symbol table — create it
            if(!found)
            {
              typet var_type = python_int_type();
              // Try to infer type from AnnAssign annotation
              for(const auto &bs : as_array(body))
              {
                if(is_node_type(bs, "AnnAssign"))
                {
                  const jsont &tgt = json_member(bs, "target");
                  if(
                    is_node_type(tgt, "Name") &&
                    json_string(json_member(tgt, "id")) == ref)
                  {
                    var_type =
                      convert_type_annotation(json_member(bs, "annotation"));
                    break;
                  }
                }
              }
              // Create the symbol so it exists for the nested function
              if(symbol_table.lookup(irep_idt{var_id}) == nullptr)
              {
                symbolt new_sym{irep_idt{var_id}, var_type, "python"};
                new_sym.base_name = ref;
                new_sym.is_lvalue = true;
                new_sym.is_state_var = true;
                symbol_table.add(new_sym);
              }
              captures.push_back({var_id, ref, var_type});
            }
          }
          else
          {
            captures.push_back({var_id, ref, var_sym->type});
          }
        }
        if(!captures.empty())
          closure_captures["python::" + nested_name] = captures;
      }
    }
  }

  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  current_function = saved_function;
  global_names = saved_globals;
  if(
    !enclosing_functions.empty() &&
    enclosing_functions.back() == saved_function)
    enclosing_functions.pop_back();

  // PLR §7.6: if function doesn't end with return, append return
  // For generators: return __gen_result list
  if(is_generator)
  {
    body_block.add(code_frontend_returnt{
      symbol_table.lookup_ref(gen_result_id).symbol_expr()});
  }
  else if(return_type.id() != ID_empty)
  {
    exprt none_expr;
    if(is_python_value_type(return_type))
      none_expr = make_python_value(
        python_type_tagt::NONE, from_integer(0, signedbv_typet{64}));
    else
    {
      mp_integer none_val = mp_integer(1) << 62;
      none_val = -none_val;
      none_expr =
        safe_typecast(from_integer(none_val, python_int_type()), return_type);
    }
    body_block.add(code_frontend_returnt{none_expr});
  }

  // Update the symbol with the body
  symbolt *sym_ptr = symbol_table.get_writeable(symbol_id);
  if(sym_ptr != nullptr)
    sym_ptr->value = body_block;

  // Function definitions don't produce executable code at the call site
  return code_skipt{};
}

// PLR §8.9: Class definitions
// PLR §3.2: "Class instances have a namespace implemented as a dictionary."
// We model classes as structs with __class_tag for dispatch.
// "A class definition defines a class object."
codet python_convertert::convert_class_def(const jsont &stmt)
{
  std::string class_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // Analyze __init__ to determine instance attributes
  struct_typet::componentst components;
  const jsont &body = json_member(stmt, "body");

  const jsont *init_method = nullptr;
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(
        (is_node_type(item, "FunctionDef") ||
         is_node_type(item, "AsyncFunctionDef")) &&
        json_string(json_member(item, "name")) == "__init__")
      {
        init_method = &item;
        break;
      }
    }
  }

  // PLR §8.9: Inherit fields from base classes (supports multiple inheritance)
  const jsont &bases = json_member(stmt, "bases");
  std::set<std::string> inherited_fields;
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
      {
        std::string base_name = json_string(json_member(base, "id"));
        if(class_types.count(base_name))
        {
          const auto &base_type = class_types[base_name];
          for(const auto &comp : base_type.components())
          {
            std::string fname = id2string(comp.get_name());
            // Skip __class_tag and already-inherited fields (diamond)
            if(fname == "__class_tag" || inherited_fields.count(fname))
              continue;
            inherited_fields.insert(fname);
            components.push_back(comp);
          }
        }
      }
    }
  }

  // Scan class body for class-level attributes (AnnAssign outside methods)
  if(body.is_array())
  {
    for(const auto &item : as_array(body))
    {
      if(is_node_type(item, "AnnAssign"))
      {
        const jsont &target = json_member(item, "target");
        if(is_node_type(target, "Name"))
        {
          std::string attr_name = json_string(json_member(target, "id"));
          typet attr_type =
            convert_type_annotation(json_member(item, "annotation"));
          components.push_back(struct_typet::componentt{attr_name, attr_type});
        }
      }
      else if(is_node_type(item, "Assign"))
      {
        const jsont &targets = json_member(item, "targets");
        if(targets.is_array())
        {
          for(const auto &t : as_array(targets))
          {
            if(is_node_type(t, "Name"))
            {
              std::string attr_name = json_string(json_member(t, "id"));
              // Infer type from value
              const jsont &val = json_member(item, "value");
              typet attr_type = python_int_type();
              if(is_node_type(val, "Constant"))
              {
                const jsont &v = json_member(val, "value");
                if(v.is_string())
                  attr_type = python_string_type();
                else if(v.is_true() || v.is_false())
                  attr_type = python_int_type();
              }
              components.push_back(
                struct_typet::componentt{attr_name, attr_type});
            }
          }
        }
      }
    }
  }

  // Scan __init__ body for self.attr = ... assignments to determine fields
  if(init_method != nullptr)
  {
    const jsont &init_body = json_member(*init_method, "body");
    if(init_body.is_array())
    {
      for(const auto &s : as_array(init_body))
      {
        // Handle AnnAssign: self.attr: Type = value
        if(is_node_type(s, "AnnAssign"))
        {
          const jsont &target = json_member(s, "target");
          if(!is_node_type(target, "Attribute"))
            continue;
          const jsont &target_value = json_member(target, "value");
          if(
            !is_node_type(target_value, "Name") ||
            json_string(json_member(target_value, "id")) != "self")
            continue;

          std::string attr_name = json_string(json_member(target, "attr"));
          const jsont &annotation = json_member(s, "annotation");
          typet attr_type = annotation.is_null()
                              ? python_value_type()
                              : convert_type_annotation(annotation);
          components.push_back(struct_typet::componentt{attr_name, attr_type});
          continue;
        }

        if(!is_node_type(s, "Assign"))
          continue;
        const jsont &targets = json_member(s, "targets");
        if(!targets.is_array() || as_array(targets).empty())
          continue;
        const jsont &target = *as_array(targets).begin();
        if(!is_node_type(target, "Attribute"))
          continue;
        const jsont &target_value = json_member(target, "value");
        if(
          !is_node_type(target_value, "Name") ||
          json_string(json_member(target_value, "id")) != "self")
          continue;

        std::string attr_name = json_string(json_member(target, "attr"));

        // Determine type from the __init__ parameter annotation
        // Look up the parameter name in __init__'s args
        const jsont &rhs = json_member(s, "value");
        std::string rhs_name;
        if(is_node_type(rhs, "Name"))
          rhs_name = json_string(json_member(rhs, "id"));

        typet attr_type = python_value_type(); // tagged union for untyped

        // Check if RHS is a constructor call: self.inner = Inner(v)
        if(
          is_node_type(rhs, "Call") &&
          is_node_type(json_member(rhs, "func"), "Name"))
        {
          std::string ctor_name =
            json_string(json_member(json_member(rhs, "func"), "id"));
          if(class_types.count(ctor_name))
            attr_type = class_types[ctor_name];
        }
        else if(!rhs_name.empty())
        {
          // Find the parameter annotation
          const jsont &init_args = json_member(*init_method, "args");
          const jsont &params = json_member(init_args, "args");
          if(params.is_array())
          {
            for(const auto &p : as_array(params))
            {
              if(json_string(json_member(p, "arg")) == rhs_name)
              {
                const jsont &ann = json_member(p, "annotation");
                if(!ann.is_null())
                  attr_type = convert_type_annotation(ann);
                // else: stays as python_value_type (tagged union)
                break;
              }
            }
          }
        }

        components.push_back(struct_typet::componentt{attr_name, attr_type});
      }
    }
  }

  // Add __class_tag as first field for dynamic dispatch
  struct_typet::componentst tagged_components;
  tagged_components.push_back(
    struct_typet::componentt{"__class_tag", signedbv_typet{32}});
  // Inherit parent class attributes
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
      {
        std::string base_name = json_string(json_member(base, "id"));
        if(class_types.count(base_name))
        {
          const auto &parent = to_struct_type(class_types[base_name]);
          for(const auto &pc : parent.components())
          {
            if(pc.get_name() == "__class_tag")
              continue; // already added
            // Only add if not already defined in child
            bool found = false;
            for(const auto &c : components)
            {
              if(c.get_name() == pc.get_name())
              {
                found = true;
                break;
              }
            }
            if(!found)
              tagged_components.push_back(pc);
          }
        }
      }
    }
  }
  for(auto &c : components)
    tagged_components.push_back(std::move(c));

  struct_typet class_type{tagged_components};
  class_type.set_tag("python_class_" + class_name);
  class_types[class_name] = class_type;
  if(!class_tag_ids.count(class_name))
    class_tag_ids[class_name] = static_cast<int>(class_tag_ids.size()) + 1;

  // Record base classes for isinstance checks
  if(bases.is_array())
  {
    for(const auto &base : as_array(bases))
    {
      if(is_node_type(base, "Name"))
        class_bases[class_name].push_back(json_string(json_member(base, "id")));
    }
  }

  // PLR §3.3.2.1: compute C3 linearization MRO for this class.
  // MRO(C) = [C] + merge(MRO(B1), MRO(B2), ..., [B1, B2, ...])
  // where merge picks the head of the first list that isn't in
  // the tail of any other list; repeat until all lists empty.
  //
  // Idempotent: convert_class_def is invoked multiple times
  // across the passes (pre-register, full register, method
  // body convert); compute MRO exactly once per class to
  // avoid accumulating duplicates from re-runs over the
  // append-based class_bases map.
  if(class_mro.count(class_name) == 0)
  {
    // Dedup class_bases[class_name] in case previous passes
    // over the same ClassDef duplicated entries.
    auto &bv = class_bases[class_name];
    std::vector<std::string> dedup;
    std::set<std::string> seen;
    for(const auto &b : bv)
    {
      if(seen.insert(b).second)
        dedup.push_back(b);
    }
    bv = dedup;
    std::vector<std::string> mro{class_name};
    std::vector<std::vector<std::string>> seqs;
    for(const auto &b : class_bases[class_name])
    {
      auto it = class_mro.find(b);
      if(it != class_mro.end())
        seqs.push_back(it->second);
      else
        seqs.push_back({b}); // unknown base — assume just itself
    }
    if(!class_bases[class_name].empty())
      seqs.push_back(class_bases[class_name]);
    // Merge
    bool progress = true;
    while(progress)
    {
      progress = false;
      // Strip any now-empty lists.
      seqs.erase(
        std::remove_if(
          seqs.begin(),
          seqs.end(),
          [](const std::vector<std::string> &v) { return v.empty(); }),
        seqs.end());
      if(seqs.empty())
        break;
      for(std::size_t i = 0; i < seqs.size(); ++i)
      {
        const std::string &head = seqs[i].front();
        // head must not appear in the tail of any other list.
        bool in_tail = false;
        for(std::size_t j = 0; j < seqs.size() && !in_tail; ++j)
        {
          if(i == j)
            continue;
          for(std::size_t k = 1; k < seqs[j].size(); ++k)
          {
            if(seqs[j][k] == head)
            {
              in_tail = true;
              break;
            }
          }
        }
        if(!in_tail)
        {
          // Copy head by value — the following erase loop may
          // invalidate seqs[i]'s iterators/references, making
          // the `head` reference dangling and causing subsequent
          // seqs[j].front() != head mis-comparisons.
          std::string head_val = head;
          mro.push_back(head_val);
          // Remove head from the front of every sequence.
          for(auto &s : seqs)
          {
            if(!s.empty() && s.front() == head_val)
              s.erase(s.begin());
          }
          progress = true;
          break;
        }
      }
    }
    class_mro[class_name] = std::move(mro);
  }

  // Register the class type in the symbol table
  irep_idt type_symbol_id{"python::class::" + class_name};
  if(symbol_table.lookup(type_symbol_id) == nullptr)
  {
    symbolt type_sym{type_symbol_id, class_type, "python"};
    type_sym.base_name = class_name;
    type_sym.is_type = true;
    type_sym.location = loc;
    symbol_table.add(type_sym);
  }

  // Create a class object symbol for ClassName.attr access
  irep_idt class_obj_id{"python::" + class_name};
  if(symbol_table.lookup(class_obj_id) == nullptr)
  {
    symbolt class_obj{class_obj_id, class_type, "python"};
    class_obj.base_name = class_name;
    class_obj.is_lvalue = true;
    class_obj.is_state_var = true;
    class_obj.is_static_lifetime = true;
    // Initialize with class-level attribute values
    exprt::operandst field_values;
    for(const auto &comp : class_type.components())
    {
      // Set __class_tag to this class's unique ID
      if(id2string(comp.get_name()) == "__class_tag")
      {
        field_values.push_back(
          from_integer(class_tag_ids[class_name], signedbv_typet{32}));
        continue;
      }
      exprt val = safe_zero(comp.type());
      // Look for the value in the class body
      if(body.is_array())
      {
        for(const auto &item : as_array(body))
        {
          if(is_node_type(item, "AnnAssign"))
          {
            const jsont &tgt = json_member(item, "target");
            if(
              is_node_type(tgt, "Name") &&
              json_string(json_member(tgt, "id")) == id2string(comp.get_name()))
            {
              const jsont &v = json_member(item, "value");
              if(!v.is_null())
                val = safe_typecast(convert_expression(v), comp.type());
            }
          }
          else if(is_node_type(item, "Assign"))
          {
            const jsont &targets = json_member(item, "targets");
            if(targets.is_array())
            {
              for(const auto &tgt : as_array(targets))
              {
                if(
                  is_node_type(tgt, "Name") &&
                  json_string(json_member(tgt, "id")) ==
                    id2string(comp.get_name()))
                {
                  exprt rv = convert_expression(json_member(item, "value"));
                  val = safe_typecast(rv, comp.type());
                }
              }
            }
          }
        }
      }
      // If still zero and attribute is inherited, copy from parent
      if(val == safe_zero(comp.type()) && class_bases.count(class_name))
      {
        for(const auto &base_name : class_bases[class_name])
        {
          irep_idt parent_id{"python::" + base_name};
          const symbolt *parent_sym = symbol_table.lookup(parent_id);
          if(parent_sym != nullptr && parent_sym->value.id() == ID_struct)
          {
            const auto &parent_st = to_struct_type(parent_sym->value.type());
            if(parent_st.has_component(comp.get_name()))
            {
              auto idx = parent_st.component_number(comp.get_name());
              if(idx < parent_sym->value.operands().size())
              {
                val = parent_sym->value.operands()[idx];
                if(val.type() != comp.type())
                  val = safe_typecast(val, comp.type());
              }
            }
          }
        }
      }
      field_values.push_back(val);
    }
    class_obj.value = struct_exprt{std::move(field_values), class_type};
    symbol_table.add(class_obj);
  }

  // Generate class object initialization in the module body
  const symbolt *cls_sym = symbol_table.lookup(class_obj_id);
  if(cls_sym != nullptr && !cls_sym->value.is_nil())
  {
    // This will be included in the module body via convert_module_body
    // since class defs are processed in the first pass but the init
    // needs to run at module load time
  }

  // Now convert all methods
  if(body.is_array())
  {
    std::string saved_class = current_class;
    current_class = class_name;

    for(const auto &item : as_array(body))
    {
      if((is_node_type(item, "FunctionDef") ||
          is_node_type(item, "AsyncFunctionDef")))
      {
        std::string method_name = json_string(json_member(item, "name"));

        // PLR §8.7: Check for @classmethod/@staticmethod decorator
        bool is_classmethod = false;
        bool is_staticmethod = false;
        const jsont &decorators = json_member(item, "decorator_list");
        if(decorators.is_array())
        {
          for(const auto &dec : as_array(decorators))
          {
            if(is_node_type(dec, "Name"))
            {
              std::string dname = json_string(json_member(dec, "id"));
              if(dname == "classmethod")
                is_classmethod = true;
              if(dname == "staticmethod")
                is_staticmethod = true;
              if(dname == "property")
              {
                // Record this method as a property of the class.
                std::string m = json_string(json_member(item, "name"));
                class_property_methods[class_name].insert(m);
              }
            }
          }
        }

        // Build method with class-qualified name
        const jsont &args_node = json_member(item, "args");
        const jsont &params = json_member(args_node, "args");

        code_typet::parameterst parameters;

        // Helper: add a (pos-only or regular) parameter to this method.
        auto add_method_param = [&](const jsont &param)
        {
          std::string param_name = json_string(json_member(param, "arg"));
          // For @staticmethod, the first parameter is an ordinary
          // parameter, not 'self' — we still bind it.
          // For @classmethod, bind 'cls' so the body can reference it.
          // (Previously we skipped it, which caused 'Unknown variable:
          // cls' whenever the body actually used it.)
          typet param_type;
          if(param_name == "self" && !is_staticmethod)
            param_type = pointer_typet{class_type, config.ansi_c.pointer_width};
          else if(param_name == "cls" && is_classmethod)
            param_type = pointer_typet{class_type, config.ansi_c.pointer_width};
          else
          {
            const jsont &annotation = json_member(param, "annotation");
            param_type = annotation.is_null()
                           ? python_value_type()
                           : convert_type_annotation(annotation);
          }

          code_typet::parametert p{param_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + param_name);
          p.set_base_name(param_name);
          parameters.push_back(p);
        };

        // PLR §8.7: positional-only parameters.
        const jsont &posonlyargs_m = json_member(args_node, "posonlyargs");
        if(posonlyargs_m.is_array())
        {
          for(const auto &param : as_array(posonlyargs_m))
            add_method_param(param);
        }
        if(params.is_array())
        {
          for(const auto &param : as_array(params))
            add_method_param(param);
        }

        // *args for class methods
        const jsont &vararg_m = json_member(args_node, "vararg");
        if(!vararg_m.is_null())
        {
          std::string va_name = json_string(json_member(vararg_m, "arg"));
          typet va_type = python_list_type(python_value_type());
          code_typet::parametert p{va_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + va_name);
          p.set_base_name(va_name);
          parameters.push_back(p);
        }

        // kwonlyargs and **kwargs for class methods
        const jsont &kwonlyargs_m = json_member(args_node, "kwonlyargs");
        if(kwonlyargs_m.is_array())
        {
          for(const auto &param : as_array(kwonlyargs_m))
          {
            std::string pn = json_string(json_member(param, "arg"));
            const jsont &ann = json_member(param, "annotation");
            typet pt =
              ann.is_null() ? python_int_type() : convert_type_annotation(ann);
            code_typet::parametert p{pt};
            p.set_identifier(
              "python::" + class_name + "::" + method_name + "::" + pn);
            p.set_base_name(pn);
            parameters.push_back(p);
          }
        }
        const jsont &kwarg_m = json_member(args_node, "kwarg");
        if(!kwarg_m.is_null())
        {
          std::string kw_name = json_string(json_member(kwarg_m, "arg"));
          typet kw_type =
            python_dict_type(python_string_type(), python_value_type());
          code_typet::parametert p{kw_type};
          p.set_identifier(
            "python::" + class_name + "::" + method_name + "::" + kw_name);
          p.set_base_name(kw_name);
          parameters.push_back(p);
        }

        const jsont &returns = json_member(item, "returns");
        typet return_type = returns.is_null()
                              ? python_int_type()
                              : convert_type_annotation(returns);

        // NoneType → void
        if(
          !returns.is_null() &&
          json_string(json_member(returns, "id")) == "None")
          return_type = empty_typet{};

        // __init__ always returns void (it modifies self through pointer)
        if(method_name == "__init__")
          return_type = empty_typet{};

        code_typet func_type{parameters, return_type};
        irep_idt func_id{"python::" + class_name + "::" + method_name};

        if(symbol_table.lookup(func_id) == nullptr)
        {
          symbolt func_sym{func_id, func_type, "python"};
          func_sym.base_name = method_name;
          func_sym.location = get_location(item);
          func_sym.is_lvalue = true;
          symbol_table.add(func_sym);
        }

        // Create parameter symbols
        for(const auto &p : parameters)
        {
          if(symbol_table.lookup(p.get_identifier()) == nullptr)
          {
            symbolt param_sym{p.get_identifier(), p.type(), "python"};
            param_sym.base_name = p.get_base_name();
            param_sym.location = loc;
            param_sym.is_lvalue = true;
            param_sym.is_state_var = true;
            param_sym.is_parameter = true;
            symbol_table.add(param_sym);
          }
        }

        // Convert method body
        {
          std::string saved_func = current_function;
          if(!current_function.empty())
            enclosing_functions.push_back(current_function);
          current_function = class_name + "::" + method_name;

          code_blockt method_body;
          const jsont &method_body_json = json_member(item, "body");
          if(method_body_json.is_array())
          {
            for(const auto &s : as_array(method_body_json))
              method_body.add(convert_statement(s));
          }

          current_function = saved_func;
          if(
            !enclosing_functions.empty() &&
            enclosing_functions.back() == saved_func)
            enclosing_functions.pop_back();

          symbolt *sym_ptr = symbol_table.get_writeable(func_id);
          if(sym_ptr != nullptr)
            sym_ptr->value = method_body;
        }
      }
    }

    current_class = saved_class;
  }

  return code_skipt{};
}

codet python_convertert::convert_expr_stmt(const jsont &stmt)
{
  // Expression statement (e.g., function call as statement)
  const jsont &value = json_member(stmt, "value");

  // PLR §6.2.9: yield X → __gen_result.append(X) (eager evaluation)
  if(
    is_node_type(value, "Yield") && !current_function.empty() &&
    generator_functions.count(current_function))
  {
    const jsont &yield_val = json_member(value, "value");
    exprt val = yield_val.is_null() ? from_integer(0, python_int_type())
                                    : convert_expression(yield_val);

    std::string grn = "__gen_result_" + current_function;
    std::string grq = qualify_name(grn);
    irep_idt gri{grq};
    const symbolt *grs = symbol_table.lookup(gri);
    if(grs != nullptr)
    {
      const auto &list_st = to_struct_type(grs->type);
      const auto &data_type = to_array_type(list_st.components()[1].type());
      member_exprt data{grs->symbol_expr(), "data", data_type};
      member_exprt length{grs->symbol_expr(), "length", signedbv_typet{64}};
      code_blockt block;
      // data[length] = val
      exprt typed_val = val;
      if(typed_val.type() != data_type.element_type())
        typed_val = safe_typecast(typed_val, data_type.element_type());
      block.add(code_frontend_assignt{index_exprt{data, length}, typed_val});
      // length += 1
      block.add(code_frontend_assignt{
        length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
      return std::move(block);
    }
  }

  // Check for __CPROVER_assume calls
  if(is_node_type(value, "Call"))
  {
    const jsont &func = json_member(value, "func");
    std::string func_name;
    if(is_node_type(func, "Name"))
      func_name = json_string(json_member(func, "id"));

    if(func_name == "__CPROVER_assume" || func_name == "__ESBMC_assume")
    {
      const jsont &args = json_member(value, "args");
      if(args.is_array() && !as_array(args).empty())
      {
        exprt cond = convert_expression(*as_array(args).begin());
        if(cond.type() != bool_typet{})
          cond = safe_typecast(cond, bool_typet{});
        code_assumet assume{cond};
        assume.add_source_location() = get_location(stmt);
        return std::move(assume);
      }
    }

    // Handle list.append(val)
    if(is_node_type(func, "Attribute"))
    {
      std::string method = json_string(json_member(func, "attr"));
      if(method == "append")
      {
        exprt obj = convert_expression(json_member(func, "value"));
        const jsont &call_args = json_member(value, "args");
        if(
          !obj.is_nil() && is_python_list_type(obj.type()) &&
          call_args.is_array() && !as_array(call_args).empty())
        {
          exprt val = convert_expression(*as_array(call_args).begin());
          source_locationt loc = get_location(stmt);

          const auto &list_st = to_struct_type(obj.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());

          // lst.data[lst.length] = val
          member_exprt length{obj, "length", python_int_type()};
          member_exprt data{obj, "data", data_type};
          index_exprt slot{data, length};

          if(val.type() != data_type.element_type())
            val = typecast_exprt{val, data_type.element_type()};

          code_blockt block;
          code_frontend_assignt store{slot, val};
          store.add_source_location() = loc;
          block.add(std::move(store));

          // lst.length += 1
          code_frontend_assignt inc_len{
            length, plus_exprt{length, from_integer(1, python_int_type())}};
          inc_len.add_source_location() = loc;
          block.add(std::move(inc_len));

          return std::move(block);
        }
      }
      else if(method == "insert")
      {
        exprt obj = convert_expression(json_member(func, "value"));
        const jsont &call_args = json_member(value, "args");
        if(
          !obj.is_nil() && is_python_list_type(obj.type()) &&
          call_args.is_array() && as_array(call_args).size() >= 2)
        {
          auto arg_it = as_array(call_args).begin();
          exprt idx_expr = convert_expression(*arg_it);
          ++arg_it;
          exprt val = convert_expression(*arg_it);
          source_locationt loc = get_location(stmt);
          const auto &list_st = to_struct_type(obj.type());
          const auto &data_type = to_array_type(list_st.components()[1].type());
          member_exprt length{obj, "length", python_int_type()};
          member_exprt data{obj, "data", data_type};
          if(val.type() != data_type.element_type())
            val = safe_typecast(val, data_type.element_type());
          code_blockt block;
          for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 1; i--)
          {
            exprt ii = from_integer(i, python_int_type());
            exprt prev = from_integer(i - 1, python_int_type());
            block.add(code_ifthenelset{
              and_exprt{
                binary_relation_exprt{prev, ID_ge, idx_expr},
                binary_relation_exprt{prev, ID_lt, length}},
              code_frontend_assignt{
                index_exprt{data, ii}, index_exprt{data, prev}}});
          }
          block.add(code_frontend_assignt{index_exprt{data, idx_expr}, val});
          block.add(code_frontend_assignt{
            length, plus_exprt{length, from_integer(1, python_int_type())}});
          return std::move(block);
        }
      }
    }
  }

  exprt expr = convert_expression(value);

  if(expr.is_nil())
    return code_skipt{};

  code_expressiont code_expr{expr};
  code_expr.add_source_location() = get_location(stmt);
  return std::move(code_expr);
}

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

    // Check for uncaught exceptions after each module-level statement
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(
      exc_sym != nullptr && !is_node_type(stmt, "FunctionDef") &&
      !is_node_type(stmt, "AsyncFunctionDef") &&
      !is_node_type(stmt, "ClassDef") && !is_node_type(stmt, "Import") &&
      !is_node_type(stmt, "ImportFrom") && !is_node_type(stmt, "Try") &&
      !is_node_type(stmt, "If") && !is_node_type(stmt, "While") &&
      !is_node_type(stmt, "For"))
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
      // Module-level variable — register as a global symbol
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
