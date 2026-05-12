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
/// Convert a C++ double to a CBMC floatbv constant expression.
static constant_exprt double_to_floatbv(double d)
{
  uint64_t bits;
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64 bits");
  std::memcpy(&bits, &d, sizeof(bits));
  return constant_exprt{integer2bvrep(mp_integer{bits}, 64), double_type()};
}

static exprt build_string_struct(const std::string &s)
{
  // Create a refined_string_exprt for the literal.
  // The content is a pointer to a constant character array.
  //
  // IMPORTANT: the backing array's SIZE must match the string's
  // logical length. The refinement solver's
  // make_char_array_for_char_pointer short-circuits when the
  // pointer is an address-of of an inline array literal and
  // returns that array directly — ignoring the length field of
  // the struct. So if we append a trailing NUL (or pad) here,
  // functions like cprover_string_length_func see the padded
  // size, not the declared .length, and len("") returns 1.
  //
  // For C-interop (where a NUL-terminated buffer is required),
  // @c_intrinsic marshalling detects the struct shape and
  // emits a separate string_constantt-backed pointer — see the
  // str→char* handshake in convert_call.
  exprt::operandst chars;
  for(char c : s)
    chars.push_back(
      from_integer(static_cast<unsigned char>(c), unsignedbv_typet{8}));
  array_typet at(
    unsignedbv_typet{8}, from_integer(chars.size(), signedbv_typet{64}));
  array_exprt arr(std::move(chars), at);
  exprt content = address_of_exprt(
    index_exprt(arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
  exprt length =
    from_integer(static_cast<long long>(s.size()), signedbv_typet{64});
  return struct_exprt({length, content}, python_string_type());
}

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

/// Create a nondet refined string expression (length + content pointer).
/// Used as the result of string operations that the solver will constrain.
[[maybe_unused]] static exprt
make_nondet_string(symbol_table_baset &symbol_table)
{
  static unsigned str_ctr = 0;
  std::string len_name = "__string_len_" + std::to_string(str_ctr);
  std::string ptr_name = "__string_ptr_" + std::to_string(str_ctr);
  str_ctr++;

  // Length symbol
  irep_idt len_id{"python::" + len_name};
  if(symbol_table.lookup(len_id) == nullptr)
  {
    symbolt ls{len_id, signedbv_typet{64}, "python"};
    ls.base_name = len_name;
    ls.is_lvalue = true;
    ls.is_state_var = true;
    symbol_table.add(ls);
  }

  // Content pointer symbol
  irep_idt ptr_id{"python::" + ptr_name};
  if(symbol_table.lookup(ptr_id) == nullptr)
  {
    symbolt ps{ptr_id, pointer_typet(unsignedbv_typet{8}, 64), "python"};
    ps.base_name = ptr_name;
    ps.is_lvalue = true;
    ps.is_state_var = true;
    symbol_table.add(ps);
  }

  exprt len_expr = symbol_table.lookup_ref(len_id).symbol_expr();
  exprt ptr_expr = symbol_table.lookup_ref(ptr_id).symbol_expr();
  return struct_exprt({len_expr, ptr_expr}, python_string_type());
}

/// Emit a cprover_string_* function application.
/// Creates: return_code = func_id(result.length, result.content, args...)
/// Returns the result string expression.
[[maybe_unused]] static exprt emit_string_function(
  const irep_idt &func_id,
  const exprt::operandst &extra_args,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  exprt result = make_nondet_string(symbol_table);

  // Declare the function in the symbol table
  std::vector<typet> arg_types;
  arg_types.push_back(signedbv_typet{64}); // result length
  arg_types.push_back(pointer_typet(unsignedbv_typet{8}, 64)); // result content
  for(const auto &a : extra_args)
    arg_types.push_back(a.type());

  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), signedbv_typet{32}),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  // Build arguments: [result.length, result.content, extra_args...]
  exprt::operandst args;
  args.push_back(result.operands()[0]); // length
  args.push_back(result.operands()[1]); // content
  args.insert(args.end(), extra_args.begin(), extra_args.end());

  // Create function application
  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), args);
  app.type() = signedbv_typet{32};

  // Assign return code (we ignore it but the solver needs it)
  static unsigned rc_ctr = 0;
  std::string rc_name = "__str_rc_" + std::to_string(rc_ctr++);
  irep_idt rc_id{"python::" + rc_name};
  if(symbol_table.lookup(rc_id) == nullptr)
  {
    symbolt rs{rc_id, signedbv_typet{32}, "python"};
    rs.base_name = rc_name;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    symbol_table.add(rs);
  }
  pending_checks.push_back(
    code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});

  return result;
}

/// Register a string expression with the string solver's array_pool.
/// Emits ID_cprover_associate_array_to_pointer_func and
/// ID_cprover_associate_length_to_array_func so the solver knows
/// about this string's content and length.
[[maybe_unused]] static void register_string_with_solver(
  const exprt &str_expr,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  // Extract length and content pointer from the string
  exprt length =
    (str_expr.id() == ID_struct && str_expr.operands().size() == 2)
      ? str_expr.operands()[0]
      : exprt(member_exprt(str_expr, "length", signedbv_typet{64}));
  exprt content =
    (str_expr.id() == ID_struct && str_expr.operands().size() == 2)
      ? str_expr.operands()[1]
      : exprt(member_exprt(
          str_expr, "data", pointer_typet(unsignedbv_typet{8}, 64)));

  // Create a nondet infinite character array symbol
  static unsigned arr_ctr = 0;
  std::string arr_name = "__str_arr_" + std::to_string(arr_ctr++);
  irep_idt arr_id{"python::" + arr_name};
  array_typet inf_array_type(
    unsignedbv_typet{8}, infinity_exprt(signedbv_typet{64}));
  if(symbol_table.lookup(arr_id) == nullptr)
  {
    symbolt as{arr_id, inf_array_type, "python"};
    as.base_name = arr_name;
    as.is_lvalue = true;
    as.is_state_var = true;
    symbol_table.add(as);
  }
  exprt array_sym = symbol_table.lookup_ref(arr_id).symbol_expr();

  // Emit: rc = cprover_associate_array_to_pointer_func(array, pointer)
  {
    irep_idt fn_id{ID_cprover_associate_array_to_pointer_func};
    if(symbol_table.lookup(fn_id) == nullptr)
    {
      std::vector<typet> arg_types{inf_array_type, content.type()};
      symbolt fs{
        fn_id,
        mathematical_function_typet(std::move(arg_types), signedbv_typet{32}),
        "python"};
      fs.base_name = id2string(fn_id);
      symbol_table.add(fs);
    }
    function_application_exprt app(
      symbol_table.lookup_ref(fn_id).symbol_expr(), {array_sym, content});
    app.type() = signedbv_typet{32};

    static unsigned rc_ctr = 0;
    std::string rc_name = "__assoc_rc_" + std::to_string(rc_ctr++);
    irep_idt rc_id{"python::" + rc_name};
    if(symbol_table.lookup(rc_id) == nullptr)
    {
      symbolt rs{rc_id, signedbv_typet{32}, "python"};
      rs.base_name = rc_name;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    pending_checks.push_back(
      code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});
  }

  // Emit: rc = cprover_associate_length_to_array_func(array, length)
  {
    irep_idt fn_id{ID_cprover_associate_length_to_array_func};
    if(symbol_table.lookup(fn_id) == nullptr)
    {
      std::vector<typet> arg_types{inf_array_type, length.type()};
      symbolt fs{
        fn_id,
        mathematical_function_typet(std::move(arg_types), signedbv_typet{32}),
        "python"};
      fs.base_name = id2string(fn_id);
      symbol_table.add(fs);
    }
    function_application_exprt app(
      symbol_table.lookup_ref(fn_id).symbol_expr(), {array_sym, length});
    app.type() = signedbv_typet{32};

    static unsigned rc2_ctr = 0;
    std::string rc_name = "__assoc_len_rc_" + std::to_string(rc2_ctr++);
    irep_idt rc_id{"python::" + rc_name};
    if(symbol_table.lookup(rc_id) == nullptr)
    {
      symbolt rs{rc_id, signedbv_typet{32}, "python"};
      rs.base_name = rc_name;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    pending_checks.push_back(
      code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});
  }
}

/// Emit a boolean cprover_string_* function (equal, contains, etc.).
/// Returns a boolean expression. Definition shared with other
/// python_converter_*.cpp TUs via python_converter_helpers.h.
// emit_string_bool_function is defined in python_converter_helpers.h.

/// Emit a one-string-argument int-returning intrinsic call (e.g.,
/// cprover_string_length_func). Registers the function in the
/// symbol table, creates a fresh result symbol, assigns the
/// function-application into it via pending_checks, and returns
/// the result symbol. Follows the same pattern as
/// emit_string_bool_function for consistency with the rest of
/// the string-intrinsic path.
[[maybe_unused]] static exprt emit_string_int_function(
  const irep_idt &func_id,
  const exprt &str,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  const typet int_type = signedbv_typet{64};
  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    std::vector<typet> arg_types{str.type()};
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), int_type),
      "python"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), {str});
  app.type() = int_type;

  static unsigned int_ctr = 0;
  std::string rc_name = "__str_int_" + std::to_string(int_ctr++);
  irep_idt rc_id{"python::" + rc_name};
  if(symbol_table.lookup(rc_id) == nullptr)
  {
    symbolt rs{rc_id, int_type, "python"};
    rs.base_name = rc_name;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    symbol_table.add(rs);
  }
  pending_checks.push_back(
    code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});
  return symbol_table.lookup_ref(rc_id).symbol_expr();
}

/// Build a string literal and register it with the string solver.
/// Uses ID_cprover_string_literal_func so the solver knows the content.
[[maybe_unused]] static exprt build_solver_string_literal(
  const std::string &s,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_checks)
{
  // Create the literal value as a constant_exprt with string_typet
  constant_exprt lit_val{s, string_typet{}};
  // Use emit_string_function to create a nondet string constrained to equal the literal
  return emit_string_function(
    ID_cprover_string_literal_func, {lit_val}, symbol_table, pending_checks);
}

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

// PLR §6.2.2: Literals
// "Python supports string and bytes literals and various numeric literals."
exprt python_convertert::convert_constant(const jsont &expr)
{
  const jsont &value = json_member(expr, "value");
  source_locationt loc = get_location(expr);

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
    // the absence of a value." §6.10.3: "x is y is true if and only
    // if x and y are the same object." We use a sentinel value
    // distinct from 0 so that "0 is None" is correctly False. (not 0)
    return from_integer(mp_integer{-4611686018427387904LL}, python_int_type());
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
      ieee_val.from_double(std::stod(val_str));
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
      // Model as list of integers
      typet lt = python_list_type(python_int_type());
      const auto &data_type =
        to_array_type(to_struct_type(lt).components()[1].type());
      exprt::operandst elems;
      for(unsigned char b : bytes)
        elems.push_back(from_integer(b, python_int_type()));
      while(elems.size() < PYTHON_MAX_LIST_LENGTH)
        elems.push_back(from_integer(0, python_int_type()));
      return struct_exprt{
        {from_integer(static_cast<long long>(bytes.size()), python_int_type()),
         array_exprt{std::move(elems), data_type}},
        lt};
    }

    // Detect complex number literals (e.g., "2j", "(1+2j)")
    if(!str_val.empty() && str_val.back() == 'j')
    {
      // Parse imaginary part: "2j" → imag=2.0, real=0.0
      std::string imag_str = str_val.substr(0, str_val.size() - 1);
      double imag_val = imag_str.empty() ? 1.0 : std::stod(imag_str);
      ieee_floatt real_f{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      real_f.from_double(0.0);
      ieee_floatt imag_f{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      imag_f.from_double(imag_val);
      struct_typet::componentst comps;
      comps.push_back(struct_typet::componentt{"real", double_type()});
      comps.push_back(struct_typet::componentt{"imag", double_type()});
      struct_typet ct{comps};
      ct.set_tag("python_complex");
      return struct_exprt{{real_f.to_expr(), imag_f.to_expr()}, ct};
    }
    if(str_val.size() >= 4 && str_val.front() == '(' && str_val.back() == ')')
    {
      // "(1+2j)" format — parse as complex
      std::string inner = str_val.substr(1, str_val.size() - 2);
      if(!inner.empty() && inner.back() == 'j')
      {
        inner.pop_back(); // remove 'j'
        double real_val = 0.0, imag_val = 0.0;
        // Find the last + or - that separates real and imag
        size_t sep = inner.rfind('+');
        if(sep == std::string::npos || sep == 0)
          sep = inner.rfind('-');
        if(sep != std::string::npos && sep > 0)
        {
          real_val = std::stod(inner.substr(0, sep));
          std::string imag_s = inner.substr(sep);
          imag_val = imag_s.empty() ? 1.0 : std::stod(imag_s);
        }
        else
          imag_val = inner.empty() ? 1.0 : std::stod(inner);
        ieee_floatt rf{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        rf.from_double(real_val);
        ieee_floatt imf{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        imf.from_double(imag_val);
        struct_typet::componentst comps;
        comps.push_back(struct_typet::componentt{"real", double_type()});
        comps.push_back(struct_typet::componentt{"imag", double_type()});
        struct_typet ct{comps};
        ct.set_tag("python_complex");
        return struct_exprt{{rf.to_expr(), imf.to_expr()}, ct};
      }
    }

    // String literal
    return python_string_literal(str_val);
  }

    log.error() << "Unsupported constant value" << messaget::eom;
  return nil_exprt{};
}

// PLR §6.2.1: Identifiers (Names)
// "An identifier occurring as an atom is a name."
exprt python_convertert::convert_name(const jsont &expr)
{
  std::string id = json_string(json_member(expr, "id"));

  if(id == "True")
    return true_exprt{};
  else if(id == "False")
    return false_exprt{};
  else if(id == "None")
    return from_integer(mp_integer{-4611686018427387904LL}, python_int_type());
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

    log.error() << "Unknown variable: " << id << messaget::eom;
    return nil_exprt{};
  }

  return sym->symbol_expr();
}

// PLR §6.7: Binary arithmetic operations
// PLR §6.8: Shifting operations
// PLR §6.9: Binary bitwise operations
exprt python_convertert::convert_bin_op(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  exprt right = convert_expression(json_member(expr, "right"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(left.is_nil() || right.is_nil())
    return nil_exprt{};

  // PLR §6.7: type compatibility for binary operators. Python
  // raises TypeError at runtime for mismatched operand types
  // (e.g. int + list). CBMC's solver layers do not tolerate
  // mixed-type arithmetic in GOTO and abort with an invariant.
  // Return a nondet int for obviously incompatible operand
  // combinations so the symex graph stays well-typed; the
  // caller's reasoning continues with an over-approximation.
  {
    bool l_is_list = is_python_list_type(left.type());
    bool r_is_list = is_python_list_type(right.type());
    bool l_is_dict = is_python_dict_type(left.type());
    bool r_is_dict = is_python_dict_type(right.type());
    bool l_is_set = is_python_set_type(left.type());
    bool r_is_set = is_python_set_type(right.type());
    bool l_is_str = is_python_string_type(left.type());
    bool r_is_str = is_python_string_type(right.type());
    bool l_is_num =
      left.type().id() == ID_signedbv || left.type().id() == ID_integer ||
      left.type().id() == ID_floatbv || left.type().id() == ID_bool;
    bool r_is_num =
      right.type().id() == ID_signedbv || right.type().id() == ID_integer ||
      right.type().id() == ID_floatbv || right.type().id() == ID_bool;
    bool incompatible = false;
    // list OP non-list: only list * int (repeat) is valid.
    if(l_is_list && !r_is_list)
    {
      if(!(op == "Mult" && r_is_num))
        incompatible = true;
    }
    if(r_is_list && !l_is_list)
    {
      if(!(op == "Mult" && l_is_num))
        incompatible = true;
    }
    // dict / set with anything else is invalid.
    if(l_is_dict != r_is_dict)
      incompatible = true;
    if(l_is_set != r_is_set && (l_is_num || r_is_num))
      incompatible = true;
    // str OP non-str/num: invalid unless str * int (repeat).
    if(l_is_str && !r_is_str)
    {
      if(!(op == "Mult" && r_is_num))
        incompatible = true;
    }
    if(r_is_str && !l_is_str)
    {
      if(!(op == "Mult" && l_is_num))
        incompatible = true;
    }
    if(incompatible)
    {
      log_overapprox(
        "BinOp " + op + " on incompatible types — returning nondet");
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }
  }

  // PLib stdtypes: "str" type, §6.7: binary arithmetic
  // String concatenation: s1 + s2 produces a new string containing the
  // PLR §3.2: Complex number arithmetic
  auto is_complex = [](const typet &t)
  {
    return t.id() == ID_struct &&
           to_struct_type(t).get_tag() == "python_complex";
  };
  // PLR §3.2: Promote int/float to complex for mixed arithmetic
  if(is_complex(left.type()) && !is_complex(right.type()))
  {
    struct_typet ct = to_struct_type(left.type());
    exprt real_part = safe_typecast(right, double_type());
    right = struct_exprt{{real_part, safe_zero(double_type())}, ct};
  }
  if(!is_complex(left.type()) && is_complex(right.type()))
  {
    struct_typet ct = to_struct_type(right.type());
    exprt real_part = safe_typecast(left, double_type());
    left = struct_exprt{{real_part, safe_zero(double_type())}, ct};
  }
  if(is_complex(left.type()) && is_complex(right.type()))
  {
    struct_typet ct = to_struct_type(left.type());
    member_exprt lr{left, "real", double_type()};
    member_exprt li{left, "imag", double_type()};
    member_exprt rr{right, "real", double_type()};
    member_exprt ri{right, "imag", double_type()};
    if(op == "Add")
      return struct_exprt{{plus_exprt{lr, rr}, plus_exprt{li, ri}}, ct};
    if(op == "Sub")
      return struct_exprt{{minus_exprt{lr, rr}, minus_exprt{li, ri}}, ct};
    if(op == "Mult")
      return struct_exprt{
        {minus_exprt{mult_exprt{lr, rr}, mult_exprt{li, ri}},
         plus_exprt{mult_exprt{lr, ri}, mult_exprt{li, rr}}},
        ct};
    // PLR §6.7: FloorDiv and Mod on complex raise TypeError
    if(op == "FloorDiv" || op == "Mod")
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    if(op == "Div")
    {
      // (a+bi)/(c+di) = ((ac+bd) + (bc-ad)i) / (c²+d²)
      exprt denom = plus_exprt{mult_exprt{rr, rr}, mult_exprt{ri, ri}};
      exprt real_num = plus_exprt{mult_exprt{lr, rr}, mult_exprt{li, ri}};
      exprt imag_num = minus_exprt{mult_exprt{li, rr}, mult_exprt{lr, ri}};
      return struct_exprt{
        {div_exprt{real_num, denom}, div_exprt{imag_num, denom}}, ct};
    }
    // Other ops: return nondet complex
    return side_effect_expr_nondett{ct, source_locationt{}};
  }

  // PLib stdtypes: "str" type, §6.7: binary arithmetic
  // String concatenation: s1 + s2 produces a new string containing the
  // characters of s1 followed by s2. We track content by copying data
  // arrays element-by-element via pending_checks.
  if(
    is_python_string_type(left.type()) && is_python_string_type(right.type()) &&
    op == "Add")
  {
    // Constant-string optimization for concat
    {
      auto lv = extract_string_value(left);
      if(!lv.has_value() && left.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(left).get_identifier());
        if(it != string_constants.end())
          lv = it->second;
      }
      auto rv = extract_string_value(right);
      if(!rv.has_value() && right.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(right).get_identifier());
        if(it != string_constants.end())
          rv = it->second;
      }
      if(lv.has_value() && rv.has_value())
        return python_string_literal(lv.value() + rv.value());
    }
    // Fallback: use string solver for non-constant concat
    {
      // Decompose symbols into struct_exprt so the solver can process them.
      // The solver requires struct_exprt (not symbol_exprt) for source strings.
      auto to_string_struct = [](const exprt &s) -> exprt
      {
        if(s.id() == ID_struct && s.operands().size() == 2)
          return s; // already a struct
        return struct_exprt(
          {member_exprt(s, "length", signedbv_typet{64}),
           member_exprt(s, "data", pointer_typet(unsignedbv_typet{8}, 64))},
          s.type());
      };
      return emit_string_function(
        ID_cprover_string_concat_func,
        {to_string_struct(left), to_string_struct(right)},
        symbol_table,
        pending_checks);
    }
    struct_typet str_type = python_string_struct_def();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt left_len{left, "length", signedbv_typet{64}};
    member_exprt right_len{right, "length", signedbv_typet{64}};
    member_exprt left_data{left, "data", data_type};
    member_exprt right_data{right, "data", data_type};

    // Create temporary for result
    static unsigned str_concat_counter = 0;
    std::string tmp_name =
      "__str_concat_" + std::to_string(str_concat_counter++);
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

    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    symbol_exprt tmp = tmp_sym.symbol_expr();

    // Set length
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}},
      plus_exprt{left_len, right_len}});

    // Copy data: for each index i, tmp.data[i] =
    //   i < left.length ? left.data[i] : right.data[i - left.length]
    member_exprt tmp_data{tmp, "data", data_type};
    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt from_left = index_exprt{left_data, idx};
      exprt from_right = index_exprt{right_data, minus_exprt{idx, left_len}};
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, left_len}, from_left, from_right};
      pending_checks.push_back(
        code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }

    return std::move(tmp);
  }

  // PLR §6.7: Type-dispatched arithmetic on tagged unions
  // When both operands are tagged unions, dispatch on types:
  // if either is FLOAT, use float arithmetic; else use int
  if(
    is_python_value_type(left.type()) && is_python_value_type(right.type()) &&
    (op == "Add" || op == "Sub" || op == "Mult"))
  {
    exprt either_float = or_exprt{
      python_value_is(left, python_type_tagt::FLOAT),
      python_value_is(right, python_type_tagt::FLOAT)};
    exprt left_int = python_value_int(left);
    exprt right_int = python_value_int(right);
    exprt left_float = python_value_float(left);
    exprt right_float = python_value_float(right);

    exprt int_result, float_result;
    if(op == "Add")
    {
      int_result = plus_exprt{left_int, right_int};
      float_result = plus_exprt{left_float, right_float};
    }
    else if(op == "Sub")
    {
      int_result = minus_exprt{left_int, right_int};
      float_result = minus_exprt{left_float, right_float};
    }
    else
    {
      int_result = mult_exprt{left_int, right_int};
      float_result = mult_exprt{left_float, right_float};
    }

    // Return tagged union with appropriate type
    exprt int_wrapped = make_python_value(python_type_tagt::INT, int_result);
    exprt float_wrapped =
      make_python_value(python_type_tagt::FLOAT, float_result);
    return if_exprt{either_float, float_wrapped, int_wrapped};
  }

  // Unwrap tagged-union values to concrete types for operations
  if(is_python_value_type(left.type()))
    left = unwrap_value(
      left, right.type().id() != ID_struct ? right.type() : python_int_type());
  if(is_python_value_type(right.type()))
    right = unwrap_value(right, left.type());

  // Guard: if types are incompatible after unwrapping, cast to match
  if(
    left.type() != right.type() && op == "Add" &&
    (is_python_string_type(left.type()) ||
     is_python_string_type(right.type())) &&
    !(is_python_string_type(left.type()) &&
      is_python_string_type(right.type())))
  {
    // String + non-string: TypeError in Python
    const symbolt *exc_sym = symbol_table.lookup("python::__exception_active");
    if(exc_sym != nullptr)
      pending_checks.push_back(
        code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
    const symbolt *exc_type_sym =
      symbol_table.lookup("python::__exception_type");
    if(exc_type_sym != nullptr)
      pending_checks.push_back(code_frontend_assignt{
        exc_type_sym->symbol_expr(),
        from_integer(exception_type_hash("TypeError"), python_int_type())});
    return side_effect_expr_nondett{python_string_type(), source_locationt{}};
  }

  // PLR §6.7: String repetition: "ab" * 3 → "ababab"
  if(
    op == "Mult" &&
    (is_python_string_type(left.type()) || is_python_string_type(right.type())))
  {
    exprt str_op = is_python_string_type(left.type()) ? left : right;
    exprt num_op = is_python_string_type(left.type()) ? right : left;
    num_op = safe_typecast(num_op, signedbv_typet{64});

    // Constant-string optimization for repetition
    {
      auto sv = extract_string_value(str_op);
      if(!sv.has_value() && str_op.id() == ID_symbol)
      {
        auto it =
          string_constants.find(to_symbol_expr(str_op).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      auto nv = try_eval_double(num_op);
      if(sv.has_value() && nv.has_value() && nv.value() >= 0)
      {
        std::string result;
        for(int i = 0; i < static_cast<int>(nv.value()); i++)
          result += sv.value();
        return python_string_literal(result);
      }
    }
    return side_effect_expr_nondett{python_string_type(), source_locationt{}};
    struct_typet str_type = python_string_struct_def();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
    member_exprt old_len{str_op, "length", signedbv_typet{64}};
    member_exprt old_data{str_op, "data", data_type};

    static unsigned str_rep_counter = 0;
    std::string tmp_name = "__str_rep_" + std::to_string(str_rep_counter++);
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
    exprt new_len = mult_exprt{old_len, num_op};
    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}}, new_len});
    member_exprt tmp_data{tmp, "data", data_type};
    for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt src_idx = mod_exprt{idx, old_len};
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, new_len},
        index_exprt{old_data, src_idx},
        from_integer(0, unsignedbv_typet{8})};
      pending_checks.push_back(
        code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }
    return std::move(tmp);
  }

  // Set operations: bitmap-based
  if(is_python_set_type(left.type()) && is_python_set_type(right.type()))
  {
    member_exprt lb{left, "bitmap", unsignedbv_typet{64}};
    member_exprt rb{right, "bitmap", unsignedbv_typet{64}};
    member_exprt lo{left, "offset", signedbv_typet{64}};
    exprt result_bitmap;
    if(op == "Sub")
      result_bitmap = bitand_exprt{lb, bitnot_exprt{rb}};
    else if(op == "BitOr") // a | b (union)
      result_bitmap = bitor_exprt{lb, rb};
    else if(op == "BitAnd") // a & b (intersection)
      result_bitmap = bitand_exprt{lb, rb};
    else if(op == "BitXor") // a ^ b (symmetric difference)
      result_bitmap = bitxor_exprt{lb, rb};
    else
      return minus_exprt{left, right}; // fallback for non-set ops
    return struct_exprt{{result_bitmap, lo}, python_set_type()};
  }

  // List concatenation: [1,2] + [3,4] → [1,2,3,4]
  if(
    is_python_list_type(left.type()) && is_python_list_type(right.type()) &&
    op == "Add")
  {
    const auto &list_st = to_struct_type(left.type());
    const auto &data_type = to_array_type(list_st.components()[1].type());
    typet elem_type = data_type.element_type();

    member_exprt left_len{left, "length", signedbv_typet{64}};
    member_exprt right_len{right, "length", signedbv_typet{64}};
    member_exprt left_data{left, "data", data_type};
    member_exprt right_data{right, "data", data_type};

    exprt new_len = plus_exprt{left_len, right_len};
    exprt::operandst elems;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      // If i < left_len, take from left; else take from right at i-left_len
      elems.push_back(if_exprt{
        binary_relation_exprt{idx, ID_lt, left_len},
        index_exprt{left_data, idx},
        index_exprt{right_data, minus_exprt{idx, left_len}}});
    }
    array_exprt new_data{std::move(elems), data_type};
    return struct_exprt{{new_len, new_data}, left.type()};
  }

  // List repetition with content tracking: lst * n or n * lst
  if(
    op == "Mult" && is_python_list_type(right.type()) &&
    !is_python_list_type(left.type()))
  {
    std::swap(left, right); // normalize to lst * n
  }
  if(is_python_list_type(left.type()) && op == "Mult")
  {
    struct_typet list_type = to_struct_type(left.type());
    const auto &data_type = to_array_type(list_type.components()[1].type());
    member_exprt old_len{left, "length", signedbv_typet{64}};
    member_exprt old_data{left, "data", data_type};
    exprt n = safe_typecast(right, signedbv_typet{64});

    static unsigned list_repeat_counter = 0;
    std::string tmp_name =
      "__list_repeat_" + std::to_string(list_repeat_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};

    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, list_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }

    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    symbol_exprt tmp = tmp_sym.symbol_expr();

    pending_checks.push_back(code_frontend_assignt{
      member_exprt{tmp, "length", signedbv_typet{64}}, mult_exprt{old_len, n}});

    // Copy data: tmp.data[i] = i < new_len ? old.data[i % old.length] : 0
    member_exprt tmp_data{tmp, "data", data_type};
    exprt new_len = mult_exprt{old_len, n};
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      exprt src_idx = mod_exprt{idx, old_len};
      exprt val = if_exprt{
        binary_relation_exprt{idx, ID_lt, new_len},
        index_exprt{old_data, src_idx},
        safe_zero(data_type.element_type())};
      pending_checks.push_back(
        code_frontend_assignt{index_exprt{tmp_data, idx}, val});
    }

    return std::move(tmp);
  }

  // Type promotion: Python promotes bool → int → float. For
  // BinOp we widen to whichever side is more numerically
  // general.
  if(left.type() != right.type())
  {
    if(left.type().id() == ID_floatbv)
      right = safe_typecast(right, left.type());
    else if(right.type().id() == ID_floatbv)
      left = safe_typecast(left, right.type());
    else if(left.type().id() == ID_bool && right.type().id() == ID_signedbv)
      left = safe_typecast(left, right.type());
    else if(right.type().id() == ID_bool && left.type().id() == ID_signedbv)
      right = safe_typecast(right, left.type());
    else if(
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv &&
      to_signedbv_type(left.type()).get_width() !=
        to_signedbv_type(right.type()).get_width())
    {
      // Mixed-width int: widen to the larger.
      if(
        to_signedbv_type(left.type()).get_width() >
        to_signedbv_type(right.type()).get_width())
        right = safe_typecast(right, left.type());
      else
        left = safe_typecast(left, right.type());
    }
  }

  if(op == "Add")
  {
    return plus_exprt{left, right};
  }
  else if(op == "Sub")
  {
    return minus_exprt{left, right};
  }
  else if(op == "Mult")
  {
    return mult_exprt{left, right};
  }
  else if(op == "FloorDiv")
  {
    // PLR §6.7: Complex // anything raises TypeError
    if(is_complex(left.type()) || is_complex(right.type()))
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    // Python handles division by zero as ZeroDivisionError exception,
    // not as a property check. Set the exception flag instead.
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt is_zero = equal_exprt{right, safe_zero(right.type())};
        pending_checks.push_back(code_ifthenelset{
          is_zero,
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
        if(exc_type_sym != nullptr)
          pending_checks.push_back(code_ifthenelset{
            is_zero,
            code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(
                exception_type_hash("ZeroDivisionError"), python_int_type())}});
      }
    }
    // PLR §6.7: Floor division rounds toward negative infinity
    // Constant evaluation
    if(
      left.is_constant() && right.is_constant() &&
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
    {
      mp_integer a, b;
      if(
        !to_integer(to_constant_expr(left), a) &&
        !to_integer(to_constant_expr(right), b) && b != 0)
      {
        mp_integer q = a / b;
        mp_integer r = a % b;
        if(r != 0 && ((a < 0) != (b < 0)))
          q -= 1;
        return from_integer(q, left.type());
      }
    }
    // C division truncates toward zero. Adjust for negative results:
    // floor_div(a, b) = a/b - (1 if (a%b != 0 and sign(a) != sign(b)) else 0)
    if(left.type().id() == ID_signedbv || left.type().id() == ID_unsignedbv)
    {
      exprt quotient = div_exprt{left, right};
      exprt remainder = mod_exprt{left, right};
      exprt has_remainder =
        notequal_exprt{remainder, from_integer(0, left.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{left, right}, ID_lt, from_integer(0, left.type())};
      return minus_exprt{
        quotient,
        if_exprt{
          and_exprt{has_remainder, diff_sign},
          from_integer(1, left.type()),
          from_integer(0, left.type())}};
    }
    return div_exprt{left, right};
  }
  else if(op == "Mod")
  {
    // Constant evaluation: compute at conversion time
    if(
      left.is_constant() && right.is_constant() &&
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
    {
      mp_integer a, b;
      if(
        !to_integer(to_constant_expr(left), a) &&
        !to_integer(to_constant_expr(right), b) && b != 0)
      {
        // Python modulo: result has same sign as divisor
        mp_integer r = a % b;
        if(r != 0 && ((a < 0) != (b < 0)))
          r += b;
        return from_integer(r, left.type());
      }
    }
    // PLR §6.7: Complex % anything raises TypeError
    if(is_complex(left.type()) || is_complex(right.type()))
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    // Python handles modulo by zero as ZeroDivisionError exception
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt is_zero = equal_exprt{right, safe_zero(right.type())};
        pending_checks.push_back(code_ifthenelset{
          is_zero,
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
        if(exc_type_sym != nullptr)
          pending_checks.push_back(code_ifthenelset{
            is_zero,
            code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(
                exception_type_hash("ZeroDivisionError"), python_int_type())}});
      }
    }
    // PLR §6.7: Python modulo: result has same sign as divisor
    if(left.type().id() == ID_signedbv || left.type().id() == ID_unsignedbv)
    {
      exprt c_mod = mod_exprt{left, right};
      exprt has_rem = notequal_exprt{c_mod, from_integer(0, left.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{left, right}, ID_lt, from_integer(0, left.type())};
      return if_exprt{
        and_exprt{has_rem, diff_sign}, plus_exprt{c_mod, right}, c_mod};
    }
    // Float modulo: x % y = x - floor(x/y) * y
    if(left.type().id() == ID_floatbv || right.type().id() == ID_floatbv)
    {
      exprt fl = left, fr = right;
      if(fl.type().id() != ID_floatbv)
        fl = typecast_exprt{fl, double_type()};
      if(fr.type().id() != ID_floatbv)
        fr = typecast_exprt{fr, double_type()};
      // floor(x/y) * y
      exprt quotient = div_exprt{fl, fr};
      // Use if_exprt to implement floor for positive/negative
      exprt truncated = typecast_exprt{
        typecast_exprt{quotient, signedbv_typet{64}}, double_type()};
      exprt floored = if_exprt{
        binary_relation_exprt{quotient, ID_lt, truncated},
        minus_exprt{truncated, double_to_floatbv(1.0)},
        truncated};
      return minus_exprt{fl, mult_exprt{floored, fr}};
    }
    // Non-integer types: return nondet
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
  else if(op == "Pow")
  {
    // PLR §6.5: The power operator
    // For constant integer exponents, unroll the multiplication.
    // For negative exponents, return 1.0 / (base ** abs(exp)).
    mp_integer exp_val;
    bool exp_known = false;
    if(right.is_constant() && right.type().id() == ID_signedbv)
      exp_known = !to_integer(to_constant_expr(right), exp_val);
    // Handle -N (UnaryOp USub on constant)
    if(
      !exp_known && right.id() == ID_unary_minus &&
      right.operands().size() == 1 && right.operands()[0].is_constant())
    {
      mp_integer pos_val;
      if(!to_integer(to_constant_expr(right.operands()[0]), pos_val))
      {
        exp_val = -pos_val;
        exp_known = true;
      }
    }
    // PLR §6.5: Complex power — not supported as exact expression
    if(is_complex(left.type()))
      return side_effect_expr_nondett{left.type(), source_locationt{}};

    // Constant base and exponent: compute pow() at conversion time
    // Handle typecast(constant) as constant (from int→float promotion)
    auto ev_base = try_eval_double(left);
    auto ev_exp = try_eval_double(right);
    if(ev_base.has_value() && ev_exp.has_value())
    {
      double base_d = ev_base.value(), exp_d = ev_exp.value();
      double result_d = std::pow(base_d, exp_d);
      if(
        right.type().id() == ID_floatbv || exp_d < 0 ||
        exp_d != std::floor(exp_d))
      {
        return double_to_floatbv(result_d);
      }
      return from_integer(
        mp_integer{static_cast<long long>(result_d)}, left.type());
    }

    if(exp_known)
    {
      bool negative = exp_val < 0;
      if(negative)
        exp_val = -exp_val;

      if(exp_val == 0)
        return from_integer(1, left.type());

      // Constant base and exponent: compute at conversion time
      if(left.is_constant() && left.type().id() == ID_signedbv)
      {
        mp_integer base_val;
        if(!to_integer(to_constant_expr(left), base_val))
        {
          mp_integer result_val{1};
          for(mp_integer i = 0; i < exp_val; ++i)
            result_val *= base_val;
          if(negative)
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_integer(result_val);
            ieee_floatt one{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            one.from_integer(1);
            return div_exprt{one.to_expr(), fv.to_expr()};
          }
          return from_integer(result_val, left.type());
        }
      }

      // Unroll: base * base * ... (up to reasonable limit)
      if(exp_val <= 16)
      {
        exprt result = left;
        for(mp_integer i = 1; i < exp_val; ++i)
          result = mult_exprt{result, left};
        if(negative)
        {
          typet ft = double_type();
          ieee_floatt one{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          one.from_integer(1);
          // Use ieee_floatt for exact base conversion
          exprt float_result = result;
          if(result.is_constant() && result.type().id() == ID_signedbv)
          {
            mp_integer rv;
            if(!to_integer(to_constant_expr(result), rv))
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_integer(rv);
              float_result = fv.to_expr();
            }
            else
              float_result = typecast_exprt{result, ft};
          }
          else
            float_result = typecast_exprt{result, ft};
          return div_exprt{one.to_expr(), float_result};
        }
        return result;
      }
    }
    // Variable exponent: build if-then-else chain for b=0..16
    // (only for scalar types — complex handled above)
    {
      exprt result = from_integer(1, left.type()); // x**0 == 1
      for(int i = 16; i >= 1; i--)
      {
        exprt power = left;
        for(int j = 1; j < i; j++)
          power = mult_exprt{power, left};
        result = if_exprt{
          equal_exprt{right, from_integer(i, right.type())}, power, result};
      }
      return result;
    }
  }
  else if(op == "Div")
  {
    // PLR §6.7: True division always returns float.
    // For constant integer operands, compute exactly with ieee_floatt
    if(
      left.is_constant() && right.is_constant() &&
      left.type().id() == ID_signedbv && right.type().id() == ID_signedbv)
    {
      mp_integer lv, rv;
      if(
        !to_integer(to_constant_expr(left), lv) &&
        !to_integer(to_constant_expr(right), rv) && rv != 0)
      {
        ieee_floatt fl{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fl.from_integer(lv);
        ieee_floatt fr{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fr.from_integer(rv);
        fl /= fr;
        return fl.to_expr();
      }
    }
    typet float_type = double_type();
    // Set ZeroDivisionError for division by zero
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
      {
        exprt is_zero = equal_exprt{right, safe_zero(right.type())};
        pending_checks.push_back(code_ifthenelset{
          is_zero,
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
        if(exc_type_sym != nullptr)
          pending_checks.push_back(code_ifthenelset{
            is_zero,
            code_frontend_assignt{
              exc_type_sym->symbol_expr(),
              from_integer(
                exception_type_hash("ZeroDivisionError"), python_int_type())}});
      }
    }
    exprt fl = safe_typecast(left, float_type);
    exprt fr = safe_typecast(right, float_type);
    return if_exprt{
      equal_exprt{right, safe_zero(right.type())},
      safe_zero(float_type),
      div_exprt{fl, fr}};
  }
  else if(op == "BitOr" || op == "BitAnd" || op == "BitXor")
  {
    // PLR §6.9: bitwise ops require integer-like operands. For
    // set/list-of-string operands (and mixed-type operands that the
    // set-handling block above did not pick up), a bitvector bit-op
    // is ill-typed and would trip solver invariants; return a typed
    // nondet instead. Visible with --python-strict-warnings.
    const bool left_int =
      left.type().id() == ID_signedbv || left.type().id() == ID_unsignedbv ||
      left.type().id() == ID_integer || left.type().id() == ID_bool;
    const bool right_int =
      right.type().id() == ID_signedbv || right.type().id() == ID_unsignedbv ||
      right.type().id() == ID_integer || right.type().id() == ID_bool;
    if(!left_int || !right_int)
    {
      log_overapprox(
        std::string{"bitwise "} + op +
        " on non-integer operand: using nondet over-approximation");
      // Pick a result type: prefer left's type if it's a struct,
      // else python_int_type().
      const typet &rt =
        (left.type().id() == ID_struct || left.type().id() == ID_struct_tag)
          ? left.type()
          : python_int_type();
      return side_effect_expr_nondett{rt, source_locationt{}};
    }
    // Bitwise ops require bitvectors — cast if using unbounded ints
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    if(op == "BitOr")
      return bitor_exprt{left, right};
    if(op == "BitAnd")
      return bitand_exprt{left, right};
    return bitxor_exprt{left, right};
  }
  else if(op == "LShift")
  {
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    // Negative shift raises ValueError
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      if(exc_sym != nullptr)
      {
        exprt neg =
          binary_relation_exprt{right, ID_lt, from_integer(0, right.type())};
        pending_checks.push_back(code_ifthenelset{
          neg, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
      }
    }
    return shl_exprt{left, right};
  }
  else if(op == "RShift")
  {
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    // Negative shift raises ValueError
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      if(exc_sym != nullptr)
      {
        exprt neg =
          binary_relation_exprt{right, ID_lt, from_integer(0, right.type())};
        pending_checks.push_back(code_ifthenelset{
          neg, code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}}});
      }
    }
    return ashr_exprt{left, right};
  }
  else if(op == "MatMult")
  {
    // PLR §6.7: matrix multiplication '@'. The Python semantics are
    // defined only in terms of the operands' __matmul__ methods; for
    // primitive types 'x @ y' is not defined. We do not model matrix
    // objects, so we route MatMult through Mult (scalar product),
    // which gives the correct result for scalar operands and a sound
    // over-approximation for anything else.
    if(left.type() == right.type() && left.type().id() != ID_struct)
      return mult_exprt{left, right};
    return side_effect_expr_nondett{left.type(), source_locationt{}};
  }
  else
  {
    log.warning() << "Unsupported binary operator: " << op << messaget::eom;
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
}

// PLR §6.6: Unary arithmetic and bitwise operations
// "All unary arithmetic and bitwise operations have the same priority."
exprt python_convertert::convert_unary_op(const jsont &expr)
{
  exprt operand = convert_expression(json_member(expr, "operand"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(operand.is_nil())
    return nil_exprt{};

  // Unwrap tagged-union values
  if(is_python_value_type(operand.type()))
    operand = unwrap_value(operand, python_int_type());

  if(op == "USub")
  {
    if(
      operand.type().id() == ID_struct &&
      to_struct_type(operand.type()).get_tag() == "python_complex")
    {
      return struct_exprt{
        {unary_minus_exprt{member_exprt{operand, "real", double_type()}},
         unary_minus_exprt{member_exprt{operand, "imag", double_type()}}},
        operand.type()};
    }
    return unary_minus_exprt{operand};
  }
  else if(op == "UAdd")
    return operand;
  else if(op == "Not")
    return not_exprt{safe_typecast(operand, bool_typet{})};
  else if(op == "Invert")
  {
    // PLR §6.7: bitwise ~x. Promote bool to int, then bitnot.
    exprt cast_operand = operand.type().id() == ID_bool
                           ? safe_typecast(operand, python_int_type())
                           : operand;
    return bitnot_exprt{cast_operand};
  }
  else
  {
    log.warning() << "Unsupported unary operator: " << op << messaget::eom;
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }
}

// PLR §6.11: Boolean operations
// "x or y: if x is true, then x, else y"
// "x and y: if x is false, then x, else y"
exprt python_convertert::convert_bool_op(const jsont &expr)
{
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));
  const jsont &values = json_member(expr, "values");

  if(!values.is_array() || as_array(values).size() < 2)
  {
    log.error() << "BoolOp requires at least 2 operands" << messaget::eom;
    return nil_exprt{};
  }

  exprt result = convert_expression(*as_array(values).begin());
  auto it = std::next(as_array(values).begin());
  for(; it != as_array(values).end(); ++it)
  {
    exprt next = convert_expression(*it);
    if(result.is_nil() || next.is_nil())
      return nil_exprt{};

    // PLR §6.11: "x or y" returns x if x is truthy, else y
    // "x and y" returns x if x is falsy, else y
    if(op == "And")
    {
      exprt cond = safe_typecast(result, bool_typet{});
      // If result is falsy, return result; else return next
      if(result.type() == next.type())
        result = if_exprt{cond, next, result};
      else
        result = and_exprt{cond, safe_typecast(next, bool_typet{})};
    }
    else if(op == "Or")
    {
      exprt cond = safe_typecast(result, bool_typet{});
      // If result is truthy, return result; else return next
      if(result.type() == next.type())
        result = if_exprt{cond, result, next};
      else
        result = or_exprt{cond, safe_typecast(next, bool_typet{})};
    }
    else
    {
      log.warning() << "Unsupported bool operator: " << op << messaget::eom;
      return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
    }
  }

  return result;
}

// PLR §6.10: Comparisons
// "Comparisons can be chained arbitrarily, e.g., x < y <= z is equivalent
// to x < y and y <= z."

// PLR §6.3.4: Calls
// "A call calls a callable object (e.g., a function) with a possibly
// empty series of arguments."
exprt python_convertert::convert_call(const jsont &expr)
{
  const jsont &func = json_member(expr, "func");
  const jsont &args = json_member(expr, "args");

  std::string func_name;
  if(is_node_type(func, "Name"))
    func_name = json_string(json_member(func, "id"));
  else if(is_node_type(func, "Attribute"))
  {
    std::string method_name = json_string(json_member(func, "attr"));

    // PLR §6.3.4: super() — resolve to parent class
    const jsont &obj_node = json_member(func, "value");
    if(
      is_node_type(obj_node, "Call") &&
      is_node_type(json_member(obj_node, "func"), "Name") &&
      json_string(json_member(json_member(obj_node, "func"), "id")) == "super")
    {
      // PLR §3.3.2.1 C3 linearization for super() dispatch.
      // class_mro[root_class] is computed at ClassDef time.
      // mro_root_class tracks the dispatch root (the class
      // whose method invocation started the current super
      // chain) — preserved across nested super() inlining so
      // every hop consults the same MRO.
      if(!current_class.empty())
      {
        // Diagnostic recursion guard (stack-overflow safety).
        static thread_local std::size_t super_depth = 0;
        struct guardt
        {
          ~guardt()
          {
            super_depth--;
          }
        } gd;
        super_depth++;
        if(super_depth > 64)
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        // Compute the next class in the MRO chain.
        std::string root =
          mro_root_class.empty() ? current_class : mro_root_class;
        std::string base_class;
        auto mit = class_mro.find(root);
        if(mit != class_mro.end())
        {
          const auto &mro = mit->second;
          for(std::size_t i = 0; i + 1 < mro.size(); ++i)
          {
            if(mro[i] == current_class)
            {
              base_class = mro[i + 1];
              break;
            }
          }
        }
        // Fallback: first declared base (single-inheritance
        // heuristic) when MRO lookup fails.
        if(
          base_class.empty() && class_bases.count(current_class) &&
          !class_bases[current_class].empty())
          base_class = class_bases[current_class][0];
        if(!base_class.empty())
        {
          // Inline super().__init__() by re-converting the base class's
          // __init__ body with the current self pointer. This avoids
          // pointer type mismatches (Derived* vs Base*).
          // Find the base class __init__ AST
          const jsont &module_body = json_member(parse_tree.ast_json, "body");
          if(module_body.is_array())
          {
            for(const auto &top_stmt : as_array(module_body))
            {
              if(
                is_node_type(top_stmt, "ClassDef") &&
                json_string(json_member(top_stmt, "name")) == base_class)
              {
                const jsont &cls_body = json_member(top_stmt, "body");
                if(cls_body.is_array())
                {
                  for(const auto &item : as_array(cls_body))
                  {
                    if(
                      (is_node_type(item, "FunctionDef") ||
                       is_node_type(item, "AsyncFunctionDef")) &&
                      json_string(json_member(item, "name")) == method_name)
                    {
                      // Convert the base __init__ body statements
                      // in the current scope (so self refers to Derived).
                      // Collect into a local buffer first, because
                      // convert_statement() clears the shared
                      // pending_checks on entry — if we push directly
                      // into pending_checks, each statement clobbers
                      // whatever the previous statement added.
                      const jsont &init_body = json_member(item, "body");
                      if(init_body.is_array())
                      {
                        std::string saved_class = current_class;
                        std::string saved_mro_root = mro_root_class;
                        if(mro_root_class.empty())
                          mro_root_class = saved_class;
                        current_class = base_class;
                        std::vector<codet> inlined;
                        for(const auto &s : as_array(init_body))
                          inlined.push_back(convert_statement(s));
                        current_class = saved_class;
                        mro_root_class = saved_mro_root;
                        for(auto &st : inlined)
                          pending_checks.push_back(std::move(st));
                      }
                      // Return a no-op value (the side effects are in
                      // pending_checks)
                      return from_integer(0, python_int_type());
                    }
                  }
                }
                break;
              }
            }
          }
        }
      }
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }

    // Check if this is a module function call: math.sqrt(x)
    if(is_node_type(obj_node, "Name"))
    {
      std::string obj_name = json_string(json_member(obj_node, "id"));
      if(imported_modules.count(obj_name))
      {
        // Resolve module.func to the function symbol. If the
        // resolved function is decorated with @c_intrinsic,
        // fall through to the main convert_call path so the
        // decorator's fold/domain/range semantics apply. A
        // direct function-call emission would bypass those.
        irep_idt func_id{"python::" + method_name};
        const symbolt *sym = symbol_table.lookup(func_id);
        if(
          sym != nullptr && sym->type.id() == ID_code &&
          c_intrinsic_map.count(func_id) == 0)
        {
          const code_typet &ft = to_code_type(sym->type);
          exprt::operandst arguments;
          if(args.is_array())
          {
            for(const auto &arg : as_array(args))
              arguments.push_back(convert_expression(arg));
          }
          for(std::size_t i = 0;
              i < arguments.size() && i < ft.parameters().size();
              i++)
          {
            if(arguments[i].type() != ft.parameters()[i].type())
              arguments[i] =
                safe_typecast(arguments[i], ft.parameters()[i].type());
          }
          side_effect_expr_function_callt call{
            sym->symbol_expr(),
            std::move(arguments),
            ft.return_type(),
            get_location(expr)};
          return std::move(call);
        }
        // PLR stdlib: math module functions — the attribute-style
        // math.X(...) form has its own inline handler below. The
        // bare-name 'from math import X; X(...)' form is handled
        // via the @c_intrinsic decorators in library/math.py.
        if(obj_name == "math")
        {
          // Math constants
          if(
            method_name == "pi" || method_name == "e" || method_name == "inf" ||
            method_name == "nan" || method_name == "tau")
          {
            if(method_name == "pi")
              return double_to_floatbv(M_PI);
            if(method_name == "e")
              return double_to_floatbv(M_E);
            if(method_name == "tau")
              return double_to_floatbv(2.0 * M_PI);
            if(method_name == "inf")
            {
              ieee_floatt val{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              val.make_plus_infinity();
              return val.to_expr();
            }
            // nan
            {
              ieee_floatt val{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              val.make_NaN();
              return val.to_expr();
            }
          }
          // Dispatch with the math function name. The attribute
          // form uses the following inline block (exact models
          // for ceil/floor/fabs/isclose; domain-checked folding;
          // nondet-with-constraints for symbolic args). A future
          // cleanup will migrate this to the decorator path for
          // parity with the bare-name form.
          std::string saved_func = func_name;
          func_name = method_name;
          exprt math_arg =
            args.is_array() && !as_array(args).empty()
              ? convert_expression(*as_array(args).begin())
              : side_effect_expr_nondett{double_type(), get_location(expr)};
          if(math_arg.type().id() != ID_floatbv)
          {
            if(math_arg.is_constant() && math_arg.type().id() == ID_signedbv)
            {
              mp_integer iv;
              if(!to_integer(to_constant_expr(math_arg), iv))
              {
                ieee_floatt fv{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                fv.from_integer(iv);
                math_arg = fv.to_expr();
              }
              else
                math_arg = safe_typecast(math_arg, double_type());
            }
            else
              math_arg = safe_typecast(math_arg, double_type());
          }

          // Exact models
          if(func_name == "ceil")
            return plus_exprt{
              typecast_exprt{math_arg, python_int_type()},
              if_exprt{
                binary_relation_exprt{
                  typecast_exprt{
                    typecast_exprt{math_arg, python_int_type()}, double_type()},
                  ID_lt,
                  math_arg},
                from_integer(1, python_int_type()),
                from_integer(0, python_int_type())}};
          if(func_name == "floor")
            return minus_exprt{
              typecast_exprt{math_arg, python_int_type()},
              if_exprt{
                binary_relation_exprt{
                  typecast_exprt{
                    typecast_exprt{math_arg, python_int_type()}, double_type()},
                  ID_gt,
                  math_arg},
                from_integer(1, python_int_type()),
                from_integer(0, python_int_type())}};
          if(func_name == "fabs")
            return if_exprt{
              binary_relation_exprt{math_arg, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{math_arg},
              math_arg};
          if(func_name == "trunc")
            return typecast_exprt{math_arg, python_int_type()};
          if(func_name == "isnan")
            return isnan_exprt{math_arg};
          if(func_name == "isinf")
            return isinf_exprt{math_arg};
          if(func_name == "isfinite")
            return and_exprt{
              not_exprt{isnan_exprt{math_arg}},
              not_exprt{isinf_exprt{math_arg}}};
          if(func_name == "copysign")
          {
            exprt arg2 =
              as_array(args).size() >= 2
                ? convert_expression(*std::next(as_array(args).begin()))
                : math_arg;
            if(arg2.type().id() != ID_floatbv)
              arg2 = safe_typecast(arg2, double_type());
            exprt abs_x = if_exprt{
              binary_relation_exprt{math_arg, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{math_arg},
              math_arg};
            return if_exprt{
              binary_relation_exprt{arg2, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{abs_x},
              abs_x};
          }
          if(func_name == "isclose")
          {
            exprt arg2 =
              as_array(args).size() >= 2
                ? convert_expression(*std::next(as_array(args).begin()))
                : math_arg;
            if(arg2.type().id() != ID_floatbv)
              arg2 = safe_typecast(arg2, double_type());
            ieee_floatt tol{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            tol.from_double(1e-9);
            exprt diff = if_exprt{
              binary_relation_exprt{
                minus_exprt{math_arg, arg2}, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{minus_exprt{math_arg, arg2}},
              minus_exprt{math_arg, arg2}};
            return binary_relation_exprt{diff, ID_le, tol.to_expr()};
          }
          // Option-4 domain check: raise ValueError for known-bad
          // constant arguments, and emit a guarded ValueError for
          // non-constant arguments. See math_function_domain() /
          // emit_value_error() and doc/architectural/
          // python-module-support-plan.md.
          {
            auto domain_opt = math_function_domain(func_name, math_arg);
            auto pre_eval = try_eval_double(math_arg);
            if(pre_eval.has_value())
            {
              double val = pre_eval.value();
              bool in_domain = true;
              if(domain_opt.has_value())
              {
                if(func_name == "sqrt")
                  in_domain = val >= 0;
                else if(
                  func_name == "log" || func_name == "log2" ||
                  func_name == "log10")
                  in_domain = val > 0;
                else if(func_name == "log1p")
                  in_domain = val > -1;
                else if(func_name == "asin" || func_name == "acos")
                  in_domain = val >= -1 && val <= 1;
                else if(func_name == "atanh")
                  in_domain = val > -1 && val < 1;
                else if(func_name == "acosh")
                  in_domain = val >= 1;
              }
              if(!in_domain)
              {
                emit_value_error(false_exprt{});
                return side_effect_expr_nondett{
                  double_type(), get_location(expr)};
              }
              // In-domain constant — fall through to the constant
              // folding block below.
            }
            else if(domain_opt.has_value())
            {
              // Non-constant argument: emit a guarded ValueError
              // and fall through to the nondet-with-constraints
              // path below.
              emit_value_error(domain_opt.value());
            }
          }

          // Constant evaluation (handle typecast from int→float)
          {
            auto eval_result = try_eval_double(math_arg);
            if(eval_result.has_value())
            {
              double val = eval_result.value();
              double res = 0;
              bool computed = true;
              if(func_name == "sqrt" && val >= 0)
                res = std::sqrt(val);
              else if(func_name == "sin")
                res = std::sin(val);
              else if(func_name == "cos")
                res = std::cos(val);
              else if(func_name == "tan")
                res = std::tan(val);
              else if(func_name == "asin" && val >= -1 && val <= 1)
                res = std::asin(val);
              else if(func_name == "acos" && val >= -1 && val <= 1)
                res = std::acos(val);
              else if(func_name == "atan")
                res = std::atan(val);
              else if(func_name == "log" && val > 0)
                res = std::log(val);
              else if(func_name == "log2" && val > 0)
                res = std::log2(val);
              else if(func_name == "log10" && val > 0)
                res = std::log10(val);
              else if(func_name == "exp")
                res = std::exp(val);
              else if(func_name == "exp2")
                res = std::exp2(val);
              else if(func_name == "floor")
                res = std::floor(val);
              else if(func_name == "ceil")
                res = std::ceil(val);
              else if(func_name == "fabs")
                res = std::fabs(val);
              else if(func_name == "degrees")
                res = val * 180.0 / M_PI;
              else if(func_name == "radians")
                res = val * M_PI / 180.0;
              else if(func_name == "sinh")
                res = std::sinh(val);
              else if(func_name == "cosh")
                res = std::cosh(val);
              else if(func_name == "tanh")
                res = std::tanh(val);
              else if(func_name == "asinh")
                res = std::asinh(val);
              else if(func_name == "acosh" && val >= 1)
                res = std::acosh(val);
              else if(func_name == "atanh" && val > -1 && val < 1)
                res = std::atanh(val);
              else if(func_name == "erf")
                res = std::erf(val);
              else if(func_name == "erfc")
                res = std::erfc(val);
              else if(func_name == "gamma" || func_name == "lgamma")
                res = std::lgamma(val);
              else if(func_name == "cbrt")
                res = std::cbrt(val);
              else if(func_name == "expm1")
                res = std::expm1(val);
              else if(func_name == "log1p" && val > -1)
                res = std::log1p(val);
              else
                computed = false;
              if(computed)
                return double_to_floatbv(res);
            }
          }
          // Two-arg constant evaluation. Delegates to the
          // same fold table the @c_intrinsic decorator uses
          // for bare-name calls, so math.X(a, b) and X(a, b)
          // (after `from math import X`) share semantics.
          //
          // log(x, base) is the one attribute-only case not
          // in the decorator's fold map (the bare-name log
          // routes through its own single-arg fold): keep its
          // inline handling below.
          if(as_array(args).size() >= 2)
          {
            exprt arg2 = convert_expression(*std::next(as_array(args).begin()));
            if(arg2.type().id() != ID_floatbv)
              arg2 = safe_typecast(arg2, double_type());
            auto ev1 = try_eval_double(math_arg);
            auto ev2 = try_eval_double(arg2);
            if(ev1.has_value() && ev2.has_value())
            {
              double v1 = ev1.value(), v2 = ev2.value();
              if(func_name == "log" && v2 > 0 && v1 > 0)
                return double_to_floatbv(std::log(v1) / std::log(v2));
              irep_idt math_id{"python::" + func_name};
              auto fi = c_intrinsic_fold_map.find(math_id);
              if(fi != c_intrinsic_fold_map.end())
              {
                const std::string &op = fi->second;
                double r = 0;
                bool ok = true;
                if(op == "pow")
                  r = std::pow(v1, v2);
                else if(op == "atan2")
                  r = std::atan2(v1, v2);
                else if(op == "fmod")
                  r = std::fmod(v1, v2);
                else if(op == "hypot")
                  r = std::hypot(v1, v2);
                else if(op == "copysign")
                  r = std::copysign(v1, v2);
                else if(op == "remainder")
                  r = std::remainder(v1, v2);
                else
                  ok = false;
                if(ok && std::isfinite(r))
                  return double_to_floatbv(r);
              }
            }
          }
          // Nondet with constraints. Look up domain= / range=
          // from the decorator map (populated by the library's
          // @c_intrinsic annotations in math.py) so the
          // attribute-style form shares semantics with the
          // bare-name form. Previously this had its own inline
          // constraint logic duplicating the decorator path.
          {
            irep_idt math_id{"python::" + func_name};
            auto di = c_intrinsic_domain_map.find(math_id);
            auto ri = c_intrinsic_range_map.find(math_id);
            std::string dom =
              di != c_intrinsic_domain_map.end() ? di->second : std::string{};
            std::string rng =
              ri != c_intrinsic_range_map.end() ? ri->second : std::string{};
            // The sqrt-specific extra axiom (result*result == arg)
            // was a past improvement that the decorator path
            // doesn't emit; preserve it here for now.
            exprt tv = emit_math_intrinsic_nondet(
              dom, rng, math_arg, get_location(expr));
            if(func_name == "sqrt")
            {
              ieee_floatt fz{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fz.from_double(0.0);
              exprt float_arg = math_arg;
              if(math_arg.type().id() != ID_floatbv)
                float_arg = typecast_exprt(math_arg, double_type());
              exprt arg_nonneg =
                binary_relation_exprt{float_arg, ID_ge, fz.to_expr()};
              pending_checks.push_back(code_assumet{implies_exprt{
                arg_nonneg,
                ieee_float_equal_exprt{mult_exprt{tv, tv}, float_arg}}});
            }
            return tv;
          }
        }
        // PLR stdlib: re module — return nondet for all methods
        if(obj_name == "re")
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        // PLib: random module — constrained nondet for randint
        if(obj_name == "random")
        {
          if(
            method_name == "randint" && args.is_array() &&
            as_array(args).size() >= 2)
          {
            auto it = as_array(args).begin();
            exprt lo = convert_expression(*it);
            ++it;
            exprt hi = convert_expression(*it);
            // Return nondet int with assume(lo <= result <= hi)
            side_effect_expr_nondett nondet{
              python_int_type(), get_location(expr)};
            static unsigned rand_ctr = 0;
            std::string tmp = "__rand_" + std::to_string(rand_ctr++);
            std::string tq = qualify_name(tmp);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, python_int_type(), "python"};
              ts.base_name = tmp;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{tv, nondet});
            pending_checks.push_back(code_assumet{and_exprt{
              binary_relation_exprt{tv, ID_ge, lo},
              binary_relation_exprt{tv, ID_le, hi}}});
            return std::move(tv);
          }
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        return side_effect_expr_nondett{python_int_type(), get_location(expr)};
      }
    }

    // Method call: obj.method(args)
    exprt obj = convert_expression(json_member(func, "value"));
    if(obj.is_nil())
      return nil_exprt{};

    // Resolve class name from object's type (struct or pointer-to-struct)
    typet obj_base_type = obj.type();
    if(obj_base_type.id() == ID_pointer)
      obj_base_type = to_pointer_type(obj_base_type).base_type();

    if(obj_base_type.id() == ID_struct || obj_base_type.id() == ID_struct_tag)
    {
      // PLR §3.2: Complex number methods
      if(
        obj_base_type.id() == ID_struct &&
        to_struct_type(obj_base_type).get_tag() == "python_complex")
      {
        if(method_name == "conjugate")
        {
          member_exprt r{obj, "real", double_type()};
          member_exprt i{obj, "imag", double_type()};
          return struct_exprt{
            {r, unary_minus_exprt{i}}, to_struct_type(obj_base_type)};
        }
        // Other complex methods: return nondet
        return side_effect_expr_nondett{obj_base_type, get_location(expr)};
      }

      // PLib stdtypes: String methods
      if(is_python_string_type(obj_base_type))
      {
        if(method_name == "split")
        {
          // PLib stdtypes: str.split(sep) — constant optimization
          if(args.is_array() && !as_array(args).empty())
          {
            exprt delim_expr = convert_expression(*as_array(args).begin());
            auto obj_sv = extract_string_value(obj);
            auto delim_sv = extract_string_value(delim_expr);
            if(obj_sv.has_value() && delim_sv.has_value())
            {
              std::string s = obj_sv.value();
              std::string d = delim_sv.value();
              std::vector<std::string> parts;
              if(!d.empty())
              {
                std::size_t pos = 0;
                while(pos <= s.size())
                {
                  auto found = s.find(d, pos);
                  if(found == std::string::npos)
                  {
                    parts.push_back(s.substr(pos));
                    break;
                  }
                  parts.push_back(s.substr(pos, found - pos));
                  pos = found + d.size();
                }
              }
              else
                parts.push_back(s);

              struct_typet str_type = python_string_struct_def();
              typet list_type = python_list_type(python_string_type());
              const auto &data_type =
                to_array_type(to_struct_type(list_type).components()[1].type());
              exprt::operandst list_elems;
              for(const auto &p : parts)
                list_elems.push_back(python_string_literal(p));
              while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
                list_elems.push_back(safe_zero(python_string_type()));
              return struct_exprt{
                {from_integer(
                   static_cast<long long>(parts.size()), python_int_type()),
                 array_exprt{std::move(list_elems), data_type}},
                list_type};
            }
          }
          // Fallback: original struct-literal handler
          if(
            obj.id() == ID_struct && args.is_array() && !as_array(args).empty())
          {
            exprt delim_expr = convert_expression(*as_array(args).begin());
            // Extract constant string bytes from obj
            if(
              obj.operands().size() == 2 && obj.operands()[0].is_constant() &&
              delim_expr.id() == ID_struct &&
              delim_expr.operands().size() == 2 &&
              delim_expr.operands()[0].is_constant())
            {
              mp_integer slen, dlen;
              if(
                !to_integer(to_constant_expr(obj.operands()[0]), slen) &&
                !to_integer(to_constant_expr(delim_expr.operands()[0]), dlen) &&
                dlen == 1 && slen <= PYTHON_MAX_STRING_LENGTH)
              {
                // Get delimiter byte
                const auto &delim_data = delim_expr.operands()[1];
                mp_integer delim_byte;
                if(
                  delim_data.operands().size() > 0 &&
                  delim_data.operands()[0].is_constant() &&
                  !to_integer(
                    to_constant_expr(delim_data.operands()[0]), delim_byte))
                {
                  // Scan string for delimiter, build parts
                  const auto &str_data = obj.operands()[1];
                  std::vector<std::string> parts;
                  std::string current;
                  for(mp_integer i = 0; i < slen; ++i)
                  {
                    std::size_t idx = i.to_ulong();
                    if(
                      idx < str_data.operands().size() &&
                      str_data.operands()[idx].is_constant())
                    {
                      mp_integer ch;
                      if(!to_integer(
                           to_constant_expr(str_data.operands()[idx]), ch))
                      {
                        if(ch == delim_byte)
                        {
                          parts.push_back(current);
                          current.clear();
                        }
                        else
                          current += static_cast<char>(ch.to_ulong());
                      }
                    }
                  }
                  parts.push_back(current);

                  // Build list of string structs
                  struct_typet str_type = python_string_struct_def();
                  const auto &data_type =
                    array_typet(unsignedbv_typet{8}, from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
                  typet list_type = python_list_type(python_string_type());
                  const auto &list_data_type = to_array_type(
                    to_struct_type(list_type).components()[1].type());

                  exprt::operandst list_elems;
                  for(const auto &part : parts)
                  {
                    list_elems.push_back(python_string_literal(part));
                  }
                  while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
                    list_elems.push_back(safe_zero(python_string_type()));
                  exprt len_expr = from_integer(
                    static_cast<long long>(parts.size()), python_int_type());
                  return struct_exprt{
                    {len_expr,
                     array_exprt{std::move(list_elems), list_data_type}},
                    list_type};
                }
              }
            }
          }
          // Fallback: nondet list of strings
          return side_effect_expr_nondett{
            python_list_type(python_string_type()), get_location(expr)};
        }
        // PLib stdtypes: upper/lower — exact byte transformation
        if(method_name == "upper" || method_name == "lower")
        {
          auto sv = extract_string_value(obj);
          if(!sv.has_value() && obj.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(obj).get_identifier());
            if(it != string_constants.end())
              sv = it->second;
          }
          if(sv.has_value())
          {
            std::string result = sv.value();
            for(auto &c : result)
              c = (method_name == "upper") ? toupper(c) : tolower(c);
            return python_string_literal(result);
          }
          // Use string solver for non-constant upper/lower
          {
            // Decompose obj into struct for the solver
            exprt src =
              (obj.id() == ID_struct && obj.operands().size() == 2)
                ? obj
                : exprt(struct_exprt(
                    {member_exprt(obj, "length", signedbv_typet{64}),
                     member_exprt(
                       obj, "data", pointer_typet(unsignedbv_typet{8}, 64))},
                    obj.type()));
            return emit_string_function(
              method_name == "upper" ? ID_cprover_string_to_upper_case_func
                                     : ID_cprover_string_to_lower_case_func,
              {src},
              symbol_table,
              pending_checks);
          }
          const auto &data_type = array_typet(
            unsignedbv_typet{8},
            from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
          member_exprt src_data{obj, "data", data_type};
          member_exprt src_len{obj, "length", signedbv_typet{64}};

          static unsigned case_ctr = 0;
          std::string tn = "__case_" + std::to_string(case_ctr++);
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
          symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            member_exprt{tmp, "length", signedbv_typet{64}}, src_len});
          member_exprt dst_data{tmp, "data", data_type};

          exprt lo = from_integer(
            method_name == "upper" ? 'a' : 'A', unsignedbv_typet{8});
          exprt hi = from_integer(
            method_name == "upper" ? 'z' : 'Z', unsignedbv_typet{8});
          exprt offset =
            from_integer(method_name == "upper" ? -32 : 32, signedbv_typet{8});

          for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt ch = index_exprt{src_data, idx};
            exprt in_range = and_exprt{
              binary_relation_exprt{ch, ID_ge, lo},
              binary_relation_exprt{ch, ID_le, hi}};
            exprt converted =
              plus_exprt{ch, typecast_exprt{offset, unsignedbv_typet{8}}};
            pending_checks.push_back(code_frontend_assignt{
              index_exprt{dst_data, idx}, if_exprt{in_range, converted, ch}});
          }
          return std::move(tmp);
        }
        if(
          method_name == "strip" || method_name == "lstrip" ||
          method_name == "rstrip" || method_name == "title" ||
          method_name == "capitalize" || method_name == "swapcase" ||
          method_name == "zfill" || method_name == "casefold" ||
          method_name == "center" || method_name == "ljust" ||
          method_name == "rjust" || method_name == "expandtabs" ||
          method_name == "zfill" || method_name == "encode" ||
          method_name == "decode" || method_name == "removeprefix" ||
          method_name == "removesuffix")
        {
          // Try exact computation for constant strings
          auto str_val = extract_string_value(obj);
          if(str_val.has_value())
          {
            std::string s = str_val.value();
            std::string result;
            if(
              method_name == "strip" || method_name == "lstrip" ||
              method_name == "rstrip")
            {
              std::string chars_to_strip = " \t\n\r\f\v";
              if(args.is_array() && !as_array(args).empty())
              {
                auto cv = extract_string_value(
                  convert_expression(*as_array(args).begin()));
                if(cv.has_value())
                  chars_to_strip = cv.value();
              }
              if(method_name == "strip")
              {
                size_t start = s.find_first_not_of(chars_to_strip);
                size_t end = s.find_last_not_of(chars_to_strip);
                result = (start == std::string::npos)
                           ? ""
                           : s.substr(start, end - start + 1);
              }
              else if(method_name == "lstrip")
              {
                size_t start = s.find_first_not_of(chars_to_strip);
                result = (start == std::string::npos) ? "" : s.substr(start);
              }
              else // rstrip
              {
                size_t end = s.find_last_not_of(chars_to_strip);
                result = (end == std::string::npos) ? "" : s.substr(0, end + 1);
              }
            }
            else if(method_name == "capitalize")
            {
              result = s;
              if(!result.empty())
              {
                result[0] = static_cast<char>(
                  std::toupper(static_cast<unsigned char>(result[0])));
                for(size_t i = 1; i < result.size(); i++)
                  result[i] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(result[i])));
              }
            }
            else if(method_name == "title")
            {
              result = s;
              bool next_upper = true;
              for(size_t i = 0; i < result.size(); i++)
              {
                if(std::isalpha(static_cast<unsigned char>(result[i])))
                {
                  result[i] =
                    next_upper
                      ? static_cast<char>(
                          std::toupper(static_cast<unsigned char>(result[i])))
                      : static_cast<char>(
                          std::tolower(static_cast<unsigned char>(result[i])));
                  next_upper = false;
                }
                else
                  next_upper = true;
              }
            }
            else if(method_name == "swapcase")
            {
              result = s;
              for(auto &c : result)
              {
                if(std::isupper(static_cast<unsigned char>(c)))
                  c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
                else if(std::islower(static_cast<unsigned char>(c)))
                  c = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
              }
            }
            else if(method_name == "casefold")
            {
              result = s;
              for(auto &c : result)
                c = static_cast<char>(
                  std::tolower(static_cast<unsigned char>(c)));
            }
            else if(
              method_name == "center" || method_name == "ljust" ||
              method_name == "rjust")
            {
              int width = 0;
              char fill = ' ';
              if(args.is_array() && !as_array(args).empty())
              {
                auto ait = as_array(args).begin();
                auto wv = try_eval_double(convert_expression(*ait));
                if(wv.has_value())
                  width = static_cast<int>(wv.value());
                ++ait;
                if(ait != as_array(args).end())
                {
                  auto fv = extract_string_value(convert_expression(*ait));
                  if(fv.has_value() && !fv.value().empty())
                    fill = fv.value()[0];
                }
              }
              if(static_cast<int>(s.size()) >= width)
                result = s;
              else
              {
                int pad = width - static_cast<int>(s.size());
                if(method_name == "center")
                {
                  int left_pad = pad / 2;
                  int right_pad = pad - left_pad;
                  result = std::string(left_pad, fill) + s +
                           std::string(right_pad, fill);
                }
                else if(method_name == "ljust")
                  result = s + std::string(pad, fill);
                else
                  result = std::string(pad, fill) + s;
              }
            }
            else if(method_name == "expandtabs")
            {
              int tabsize = 8;
              if(args.is_array() && !as_array(args).empty())
              {
                auto tv =
                  try_eval_double(convert_expression(*as_array(args).begin()));
                if(tv.has_value())
                  tabsize = static_cast<int>(tv.value());
              }
              result.clear();
              int col = 0;
              for(char c : s)
              {
                if(c == '\t')
                {
                  int spaces = tabsize - (col % tabsize);
                  result += std::string(spaces, ' ');
                  col += spaces;
                }
                else if(c == '\n' || c == '\r')
                {
                  result += c;
                  col = 0;
                }
                else
                {
                  result += c;
                  col++;
                }
              }
            }
            else if(method_name == "removeprefix")
            {
              std::string prefix;
              if(args.is_array() && !as_array(args).empty())
              {
                auto pv = extract_string_value(
                  convert_expression(*as_array(args).begin()));
                if(pv.has_value())
                  prefix = pv.value();
              }
              if(!prefix.empty() && s.find(prefix) == 0)
                result = s.substr(prefix.size());
              else
                result = s;
            }
            else if(method_name == "removesuffix")
            {
              std::string suffix;
              if(args.is_array() && !as_array(args).empty())
              {
                auto sv2 = extract_string_value(
                  convert_expression(*as_array(args).begin()));
                if(sv2.has_value())
                  suffix = sv2.value();
              }
              if(
                !suffix.empty() && s.size() >= suffix.size() &&
                s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
                result = s.substr(0, s.size() - suffix.size());
              else
                result = s;
            }
            else if(method_name == "zfill")
            {
              int width = 0;
              if(args.is_array() && !as_array(args).empty())
              {
                auto wv =
                  try_eval_double(convert_expression(*as_array(args).begin()));
                if(wv.has_value())
                  width = static_cast<int>(wv.value());
              }
              if(static_cast<int>(s.size()) >= width)
                result = s;
              else
              {
                int pad = width - static_cast<int>(s.size());
                if(!s.empty() && (s[0] == '+' || s[0] == '-'))
                  result = s[0] + std::string(pad, '0') + s.substr(1);
                else
                  result = std::string(pad, '0') + s;
              }
            }
            else
              result = s; // fallback for unhandled methods
            if(result.size() <= PYTHON_MAX_STRING_LENGTH)
              return python_string_literal(result);
          }
          // Return nondet string with constraints for symbolic strings
          {
            static unsigned str_method_ctr = 0;
            std::string tn = "__str_meth_" + std::to_string(str_method_ctr++);
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
            // Both ASSUMEs use cprover_string_length_func so the
            // len()-consumer (which also goes through the intrinsic)
            // sees a value coordinated with the input's length.
            // The .length member-access path remains valid too
            // because refine-strings' array_pool.find() respects
            // the struct's .length field for non-constant
            // pointers.
            exprt result_len_intr = emit_string_int_function(
              ID_cprover_string_length_func, tv, symbol_table, pending_checks);
            exprt input_len_intr = emit_string_int_function(
              ID_cprover_string_length_func, obj, symbol_table, pending_checks);
            // strip/lstrip/rstrip: result length <= input length
            if(
              method_name == "strip" || method_name == "lstrip" ||
              method_name == "rstrip")
            {
              pending_checks.push_back(code_assumet{and_exprt{
                binary_relation_exprt{
                  result_len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
                binary_relation_exprt{
                  result_len_intr, ID_le, input_len_intr}}});
            }
            // capitalize/title/swapcase/casefold: same length
            else if(
              method_name == "capitalize" || method_name == "title" ||
              method_name == "swapcase" || method_name == "casefold")
            {
              pending_checks.push_back(
                code_assumet{equal_exprt{result_len_intr, input_len_intr}});
            }
            return std::move(tv);
          }
        }
        if(method_name == "replace" || method_name == "format")
        {
          // PLib stdtypes: str.replace(old, new) for constant strings
          if(
            method_name == "replace" && args.is_array() &&
            as_array(args).size() >= 2)
          {
            auto ait = as_array(args).begin();
            exprt old_expr = convert_expression(*ait);
            ++ait;
            exprt new_expr = convert_expression(*ait);
            // Extract all three as constant strings
            auto extract_str = [&](const exprt &e) -> std::string
            {
              auto sv = extract_string_value(e);
              if(sv.has_value())
                return sv.value();
              return "";
            };
            std::string src = extract_str(obj);
            std::string old_s = extract_str(old_expr);
            std::string new_s = extract_str(new_expr);
            if(!src.empty() && !old_s.empty())
            {
              // Perform replacement
              std::string result;
              std::size_t pos = 0;
              while(pos < src.size())
              {
                auto found = src.find(old_s, pos);
                if(found == std::string::npos)
                {
                  result += src.substr(pos);
                  break;
                }
                result += src.substr(pos, found - pos) + new_s;
                pos = found + old_s.size();
              }
              // Build string literal
              return python_string_literal(result);
            }
            // Symbolic-string replace not wired: the solver's
            // cprover_string_replace_func handles char-to-char
            // replacement only (5 args, chars not strings).
            // str.replace(old, new) on general strings needs
            // a new multi-char solver intrinsic. Falls through
            // to nondet for now.
          }
          // PLib stdtypes: str.format() — substitute {} placeholders
          if(method_name == "format")
          {
            auto fmt_sv = extract_string_value(obj);
            if(fmt_sv.has_value())
            {
              std::string fmt = fmt_sv.value();
              // Simple {} substitution with string args
              std::string result;
              std::size_t arg_idx = 0;
              exprt::operandst arg_exprs;
              if(args.is_array())
              {
                for(const auto &a : as_array(args))
                  arg_exprs.push_back(convert_expression(a));
              }
              bool all_const = true;
              for(std::size_t i = 0; i < fmt.size(); i++)
              {
                if(fmt[i] == '{' && i + 1 < fmt.size())
                {
                  // Find closing }
                  auto close = fmt.find('}', i + 1);
                  if(close == std::string::npos)
                  {
                    result += fmt[i];
                    continue;
                  }
                  std::string spec = fmt.substr(i + 1, close - i - 1);
                  i = close; // skip to }

                  // Determine which arg to use
                  std::size_t use_idx = arg_idx;
                  std::string fmt_spec;
                  if(spec.empty())
                  {
                    // {} — use next arg
                    use_idx = arg_idx++;
                  }
                  else if(std::isdigit(spec[0]))
                  {
                    // {0}, {1}, {0:d}, etc.
                    auto colon = spec.find(':');
                    use_idx = std::stoul(spec.substr(
                      0, colon != std::string::npos ? colon : spec.size()));
                    if(colon != std::string::npos)
                      fmt_spec = spec.substr(colon + 1);
                  }
                  else if(spec[0] == ':')
                  {
                    // {:d}, {:.2f}, etc.
                    fmt_spec = spec.substr(1);
                    use_idx = arg_idx++;
                  }
                  else
                  {
                    all_const = false;
                    continue;
                  }

                  if(use_idx >= arg_exprs.size())
                  {
                    all_const = false;
                    continue;
                  }

                  // Extract value
                  auto sv = extract_string_value(arg_exprs[use_idx]);
                  if(sv.has_value())
                  {
                    result += sv.value();
                  }
                  else
                  {
                    auto fv = try_eval_double(arg_exprs[use_idx]);
                    if(fv.has_value())
                    {
                      double d = fv.value();
                      if(fmt_spec.empty() || fmt_spec == "d" || fmt_spec == "n")
                      {
                        if(d == std::floor(d) && std::abs(d) < 1e15)
                          result += std::to_string(static_cast<long long>(d));
                        else
                        {
                          std::ostringstream oss;
                          oss << d;
                          result += oss.str();
                        }
                      }
                      else if(fmt_spec[0] == '.')
                      {
                        // .Nf format
                        int prec = std::stoi(fmt_spec.substr(1));
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(prec) << d;
                        result += oss.str();
                      }
                      else
                        all_const = false;
                    }
                    else
                      all_const = false;
                  }
                }
                else
                  result += fmt[i];
              }
              if(all_const)
              {
                return python_string_literal(result);
              }
              // Symbolic int fast path: template is just "{}"
              // or "{0}" with no format spec, single int arg.
              // Emit cprover_string_of_int_func.
              if(
                (fmt == "{}" || fmt == "{0}") && arg_exprs.size() == 1 &&
                (arg_exprs[0].type().id() == ID_signedbv ||
                 arg_exprs[0].type().id() == ID_integer))
              {
                exprt as_i64 =
                  arg_exprs[0].type() == signedbv_typet{64}
                    ? arg_exprs[0]
                    : safe_typecast(arg_exprs[0], signedbv_typet{64});
                exprt r = emit_string_function(
                  ID_cprover_string_of_int_func,
                  {as_i64},
                  symbol_table,
                  pending_checks);
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
                      mathematical_function_typet(
                        std::move(at), signedbv_typet{32}),
                      "python"};
                    fs.base_name = id2string(fid);
                    symbol_table.add(fs);
                  }
                };
                ensure_fn(ID_cprover_associate_array_to_pointer_func);
                ensure_fn(ID_cprover_associate_length_to_array_func);
                return r;
              }
            }
          }
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
        // PLib stdtypes: startswith/endswith — exact byte comparison
        if(
          (method_name == "startswith" || method_name == "endswith") &&
          args.is_array() && !as_array(args).empty())
        {
          exprt prefix = convert_expression(*as_array(args).begin());
          // Constant-string optimization
          auto obj_sv = extract_string_value(obj);
          auto pre_sv = extract_string_value(prefix);
          if(obj_sv.has_value() && pre_sv.has_value())
          {
            bool result;
            if(method_name == "startswith")
              result = obj_sv.value().substr(0, pre_sv.value().size()) ==
                       pre_sv.value();
            else
              result = obj_sv.value().size() >= pre_sv.value().size() &&
                       obj_sv.value().substr(
                         obj_sv.value().size() - pre_sv.value().size()) ==
                         pre_sv.value();
            return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
          }
          if(is_python_string_type(prefix.type()))
          {
            const auto &data_type =
              array_typet(unsignedbv_typet{8}, from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
            member_exprt obj_data{obj, "data", data_type};
            member_exprt obj_len{obj, "length", signedbv_typet{64}};
            member_exprt pre_data{prefix, "data", data_type};
            member_exprt pre_len{prefix, "length", signedbv_typet{64}};

            // Build conjunction: all prefix bytes match
            exprt result = binary_relation_exprt{pre_len, ID_le, obj_len};
            for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_prefix = binary_relation_exprt{idx, ID_lt, pre_len};
              exprt obj_idx =
                (method_name == "endswith")
                  ? minus_exprt{minus_exprt{obj_len, pre_len}, from_integer(-static_cast<long long>(i), signedbv_typet{64})}
                  : idx;
              if(method_name == "endswith")
                obj_idx = plus_exprt{minus_exprt{obj_len, pre_len}, idx};
              exprt match = equal_exprt{
                index_exprt{obj_data, obj_idx}, index_exprt{pre_data, idx}};
              result = and_exprt{result, or_exprt{not_exprt{in_prefix}, match}};
            }
            return result;
          }
        }
        // PLib stdtypes: exact string predicates
        if(
          method_name == "isdigit" || method_name == "isalpha" ||
          method_name == "isalnum" || method_name == "isupper" ||
          method_name == "islower" || method_name == "isspace" ||
          method_name == "isascii")
        {
          // Constant-string optimization
          auto sv = extract_string_value(obj);
          if(sv.has_value())
          {
            const std::string &s = sv.value();
            bool result = !s.empty();
            for(char c : s)
            {
              unsigned char uc = static_cast<unsigned char>(c);
              if(
                method_name == "isdigit" || method_name == "isdecimal" ||
                method_name == "isnumeric")
                result = result && std::isdigit(uc);
              else if(method_name == "isalpha")
                result = result && (std::isalpha(uc) || uc >= 0x80);
              else if(method_name == "isalnum")
                result = result && (std::isalnum(uc) || uc >= 0x80);
              else if(method_name == "isupper")
                result = result && (!std::isalpha(uc) || std::isupper(uc));
              else if(method_name == "islower")
                result = result && (!std::isalpha(uc) || std::islower(uc));
              else if(method_name == "isspace")
                result = result && std::isspace(uc);
              else if(method_name == "isascii")
                result = result && (uc < 128);
            }
            if(method_name == "isascii" && s.empty())
              result = true; // empty string is ASCII
            return result ? exprt{true_exprt{}} : exprt{false_exprt{}};
          }
          const auto &data_type = array_typet(unsignedbv_typet{8}, from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
          // Pointer-based string: return nondet for non-constant
          return side_effect_expr_nondett{python_string_type(), source_locationt{}};
          member_exprt data{obj, "data", data_type};
          member_exprt length{obj, "length", signedbv_typet{64}};

          // length > 0 AND for all i < length: char_predicate(data[i])
          exprt result = binary_relation_exprt{
            length, ID_gt, from_integer(0, signedbv_typet{64})};
          for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt ch = index_exprt{data, idx};
            exprt pred;
            if(
              method_name == "isdigit" || method_name == "isdecimal" ||
              method_name == "isnumeric")
              pred = and_exprt{
                binary_relation_exprt{
                  ch, ID_ge, from_integer('0', unsignedbv_typet{8})},
                binary_relation_exprt{
                  ch, ID_le, from_integer('9', unsignedbv_typet{8})}};
            else if(method_name == "isalpha")
              pred = or_exprt{
                or_exprt{
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('a', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('z', unsignedbv_typet{8})}},
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('A', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('Z', unsignedbv_typet{8})}}},
                // UTF-8: first byte >= 0xC0 or continuation byte 0x80-0xBF
                binary_relation_exprt{
                  ch, ID_ge, from_integer(0x80, unsignedbv_typet{8})}};
            else if(method_name == "isalnum")
              pred = or_exprt{
                and_exprt{
                  binary_relation_exprt{
                    ch, ID_ge, from_integer('0', unsignedbv_typet{8})},
                  binary_relation_exprt{
                    ch, ID_le, from_integer('9', unsignedbv_typet{8})}},
                or_exprt{
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('a', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('z', unsignedbv_typet{8})}},
                  and_exprt{
                    binary_relation_exprt{
                      ch, ID_ge, from_integer('A', unsignedbv_typet{8})},
                    binary_relation_exprt{
                      ch, ID_le, from_integer('Z', unsignedbv_typet{8})}}}};
            else if(method_name == "isupper")
              pred = and_exprt{
                binary_relation_exprt{
                  ch, ID_ge, from_integer('A', unsignedbv_typet{8})},
                binary_relation_exprt{
                  ch, ID_le, from_integer('Z', unsignedbv_typet{8})}};
            else if(method_name == "islower")
              pred = and_exprt{
                binary_relation_exprt{
                  ch, ID_ge, from_integer('a', unsignedbv_typet{8})},
                binary_relation_exprt{
                  ch, ID_le, from_integer('z', unsignedbv_typet{8})}};
            else if(method_name == "isspace")
              pred = or_exprt{
                equal_exprt{ch, from_integer(' ', unsignedbv_typet{8})},
                or_exprt{
                  equal_exprt{ch, from_integer('\t', unsignedbv_typet{8})},
                  equal_exprt{ch, from_integer('\n', unsignedbv_typet{8})}}};
            else // isascii
              pred = binary_relation_exprt{
                ch, ID_le, from_integer(127, unsignedbv_typet{8})};
            result = and_exprt{result, or_exprt{not_exprt{in_range}, pred}};
          }
          return result;
        }
        if(
          method_name == "startswith" || method_name == "endswith" ||
          method_name == "istitle" || method_name == "isidentifier" ||
          method_name == "isprintable")
        {
          // Use solver for startswith/endswith on non-constant strings
          if(
            (method_name == "startswith" || method_name == "endswith") &&
            args.is_array() && !as_array(args).empty())
          {
            exprt prefix = convert_expression(*as_array(args).begin());
            if(is_python_string_type(prefix.type()))
            {
              return emit_string_bool_function(
                method_name == "startswith" ? ID_cprover_string_is_prefix_func
                                            : ID_cprover_string_is_suffix_func,
                prefix,
                obj,
                symbol_table,
                pending_checks);
            }
          }
          return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
        }
        if(
          method_name == "find" || method_name == "index" ||
          method_name == "rfind" || method_name == "rindex" ||
          method_name == "count")
        {
          auto str_val = extract_string_value(obj);
          if(!str_val.has_value() && obj.id() == ID_symbol)
          {
            auto it =
              string_constants.find(to_symbol_expr(obj).get_identifier());
            if(it != string_constants.end())
              str_val = it->second;
          }
          if(str_val.has_value() && args.is_array() && !as_array(args).empty())
          {
            auto ait = as_array(args).begin();
            exprt arg_expr = convert_expression(*ait);
            auto arg_val = extract_string_value(arg_expr);
            if(!arg_val.has_value() && arg_expr.id() == ID_symbol)
            {
              auto it2 = string_constants.find(
                to_symbol_expr(arg_expr).get_identifier());
              if(it2 != string_constants.end())
                arg_val = it2->second;
            }
            // Parse optional start/end range
            int start = 0, end = -1;
            ++ait;
            if(ait != as_array(args).end())
            {
              auto sv2 = try_eval_double(convert_expression(*ait));
              if(sv2.has_value())
                start = static_cast<int>(sv2.value());
              ++ait;
              if(ait != as_array(args).end())
              {
                auto ev = try_eval_double(convert_expression(*ait));
                if(ev.has_value())
                  end = static_cast<int>(ev.value());
              }
            }
            if(arg_val.has_value())
            {
              std::string s = str_val.value();
              int slen = static_cast<int>(s.size());
              if(start < 0)
                start += slen;
              if(start < 0)
                start = 0;
              if(end < 0)
                end = slen;
              if(end > slen)
                end = slen;
              std::string slice =
                (start < end) ? s.substr(start, end - start) : "";
              const std::string &sub = arg_val.value();
              if(method_name == "find")
              {
                auto pos = slice.find(sub);
                return from_integer(
                  pos == std::string::npos
                    ? -1
                    : static_cast<long long>(pos + start),
                  python_int_type());
              }
              if(method_name == "rfind")
              {
                auto pos = slice.rfind(sub);
                return from_integer(
                  pos == std::string::npos
                    ? -1
                    : static_cast<long long>(pos + start),
                  python_int_type());
              }
              if(method_name == "index" || method_name == "rindex")
              {
                auto pos =
                  (method_name == "index") ? s.find(sub) : s.rfind(sub);
                if(pos == std::string::npos)
                {
                  // Raise ValueError
                  const symbolt *exc_sym =
                    symbol_table.lookup("python::__exception_active");
                  if(exc_sym)
                    pending_checks.push_back(code_frontend_assignt{
                      exc_sym->symbol_expr(), true_exprt{}});
                  return from_integer(-1, python_int_type());
                }
                return from_integer(
                  static_cast<long long>(pos), python_int_type());
              }
              if(method_name == "count")
              {
                long long cnt = 0;
                size_t pos = 0;
                while((pos = slice.find(sub, pos)) != std::string::npos)
                {
                  cnt++;
                  pos += sub.empty() ? 1 : sub.size();
                }
                if(sub.empty())
                  cnt = static_cast<long long>(slice.size() + 1);
                return from_integer(cnt, python_int_type());
              }
            }
          }
          // Constrained nondet for symbolic find/count
          {
            static unsigned find_ctr = 0;
            std::string tn = "__find_" + std::to_string(find_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, python_int_type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              tv,
              side_effect_expr_nondett{python_int_type(), get_location(expr)}});
            member_exprt slen{obj, "length", signedbv_typet{64}};
            if(method_name == "count")
              pending_checks.push_back(code_assumet{and_exprt{
                binary_relation_exprt{
                  tv, ID_ge, from_integer(0, python_int_type())},
                binary_relation_exprt{tv, ID_le, slen}}});
            else // find, rfind, index, rindex
              pending_checks.push_back(code_assumet{and_exprt{
                binary_relation_exprt{
                  tv, ID_ge, from_integer(-1, python_int_type())},
                binary_relation_exprt{tv, ID_lt, slen}}});
            return std::move(tv);
          }
        }
        if(method_name == "join")
        {
          // sep.join(lst) — for constant sep and list of constant strings
          if(args.is_array() && !as_array(args).empty())
          {
            exprt list_arg = convert_expression(*as_array(args).begin());
            auto sep_val = extract_string_value(obj);
            if(sep_val.has_value() && is_python_list_type(list_arg.type()))
            {
              // Look up list literal for symbols
              const exprt *list_val = &list_arg;
              if(list_arg.id() == ID_symbol)
              {
                auto it =
                  list_literals.find(to_symbol_expr(list_arg).get_identifier());
                if(it != list_literals.end())
                  list_val = &it->second;
              }
              const auto &list_st = to_struct_type(list_arg.type());
              to_array_type(list_st.components()[1].type());
              if(
                list_val->id() == ID_struct &&
                list_val->operands().size() >= 2 &&
                list_val->operands()[0].is_constant())
              {
                mp_integer len;
                if(!to_integer(to_constant_expr(list_val->operands()[0]), len))
                {
                  std::string result;
                  bool all_const = true;
                  const exprt &data_arr = list_val->operands()[1];
                  for(mp_integer i = 0; i < len; ++i)
                  {
                    auto idx = i.to_ulong();
                    if(idx < data_arr.operands().size())
                    {
                      auto sv = extract_string_value(data_arr.operands()[idx]);
                      if(sv.has_value())
                      {
                        if(i > 0)
                          result += sep_val.value();
                        result += sv.value();
                      }
                      else
                        all_const = false;
                    }
                  }
                  if(all_const)
                    return python_string_literal(result);
                }
              }
            }
          }
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
        if(method_name == "partition" || method_name == "rpartition")
        {
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
      }

      // PLib stdtypes: Dict methods (array-based model)
      if(is_python_dict_type(obj_base_type))
      {
        if(method_name == "get")
        {
          // d.get(key, default) — scan keys array
          if(args.is_array() && !as_array(args).empty())
          {
            auto arg_it = as_array(args).begin();
            exprt key_expr = convert_expression(*arg_it);
            const auto &dict_st = to_struct_type(obj_base_type);
            const auto &keys_type =
              to_array_type(dict_st.components()[1].type());
            const auto &vals_type =
              to_array_type(dict_st.components()[2].type());
            member_exprt length{obj, "length", signedbv_typet{64}};
            member_exprt keys{obj, "keys", keys_type};
            member_exprt vals{obj, "values", vals_type};

            if(key_expr.type() != keys_type.element_type())
              key_expr = safe_typecast(key_expr, keys_type.element_type());

            // Default value: None if not specified, else second arg
            exprt default_val = from_integer(
              mp_integer{-4611686018427387904LL}, vals_type.element_type());
            ++arg_it;
            if(arg_it != as_array(args).end())
              default_val = safe_typecast(
                convert_expression(*arg_it), vals_type.element_type());

            // Constant-key fast path: same logic as subscript read.
            // When the dict value is a literal struct or a tracked
            // dict_literal symbol, and the key is a string constant,
            // resolve at conversion time.
            auto key_str = extract_string_value(key_expr);
            if(key_str.has_value())
            {
              const exprt *dict_val = nullptr;
              if(obj.id() == ID_struct)
                dict_val = &obj;
              else if(obj.id() == ID_symbol)
              {
                auto it =
                  dict_literals.find(to_symbol_expr(obj).get_identifier());
                if(it != dict_literals.end())
                  dict_val = &it->second;
              }
              if(
                dict_val != nullptr && dict_val->operands().size() >= 3 &&
                dict_val->operands()[0].is_constant())
              {
                mp_integer len_val;
                if(!to_integer(
                     to_constant_expr(dict_val->operands()[0]), len_val))
                {
                  const exprt &keys_arr = dict_val->operands()[1];
                  const exprt &vals_arr = dict_val->operands()[2];
                  for(mp_integer i = 0; i < len_val; ++i)
                  {
                    auto idx = i.to_ulong();
                    if(idx < keys_arr.operands().size())
                    {
                      auto kv = extract_string_value(keys_arr.operands()[idx]);
                      if(kv.has_value() && kv.value() == key_str.value())
                        return vals_arr.operands()[idx];
                    }
                  }
                  // Key not present in the literal: default path.
                  return default_val;
                }
              }
            }

            exprt result = default_val;
            for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt match = equal_exprt{index_exprt{keys, idx}, key_expr};
              result = if_exprt{
                and_exprt{in_range, match}, index_exprt{vals, idx}, result};
            }
            return result;
          }
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(method_name == "keys")
        {
          // Constant fast path: when obj is a literal struct
          // or a tracked dict_literal symbol, build the keys
          // list directly from the stored operands — no
          // member_exprt indirection.
          const exprt *dict_val = nullptr;
          if(obj.id() == ID_struct)
            dict_val = &obj;
          else if(obj.id() == ID_symbol)
          {
            auto it = dict_literals.find(to_symbol_expr(obj).get_identifier());
            if(it != dict_literals.end())
              dict_val = &it->second;
          }
          const auto &dict_st = to_struct_type(obj_base_type);
          const auto &keys_type = to_array_type(dict_st.components()[1].type());
          typet key_type = keys_type.element_type();
          struct_typet list_type = python_list_type(key_type);
          const auto &list_data_type =
            to_array_type(list_type.components()[1].type());
          if(
            dict_val != nullptr && dict_val->operands().size() >= 3 &&
            dict_val->operands()[0].is_constant())
          {
            exprt::operandst elems;
            for(const auto &k : dict_val->operands()[1].operands())
              elems.push_back(k);
            while(elems.size() < PYTHON_MAX_LIST_LENGTH)
              elems.push_back(safe_zero(key_type));
            return struct_exprt{
              {dict_val->operands()[0],
               array_exprt{std::move(elems), list_data_type}},
              list_type};
          }
          // d.keys() → list of d.keys[0..d.length-1]
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt keys{obj, "keys", keys_type};
          exprt::operandst elems;
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            elems.push_back(
              index_exprt{keys, from_integer(i, signedbv_typet{64})});
          while(elems.size() < PYTHON_MAX_LIST_LENGTH)
            elems.push_back(safe_zero(key_type));
          return struct_exprt{
            {length, array_exprt{std::move(elems), list_data_type}}, list_type};
        }
        if(method_name == "values")
        {
          const exprt *dict_val = nullptr;
          if(obj.id() == ID_struct)
            dict_val = &obj;
          else if(obj.id() == ID_symbol)
          {
            auto it = dict_literals.find(to_symbol_expr(obj).get_identifier());
            if(it != dict_literals.end())
              dict_val = &it->second;
          }
          const auto &dict_st = to_struct_type(obj_base_type);
          const auto &vals_type = to_array_type(dict_st.components()[2].type());
          typet val_type = vals_type.element_type();
          struct_typet list_type = python_list_type(val_type);
          const auto &list_data_type =
            to_array_type(list_type.components()[1].type());
          if(
            dict_val != nullptr && dict_val->operands().size() >= 3 &&
            dict_val->operands()[0].is_constant())
          {
            exprt::operandst elems;
            for(const auto &v : dict_val->operands()[2].operands())
              elems.push_back(v);
            while(elems.size() < PYTHON_MAX_LIST_LENGTH)
              elems.push_back(safe_zero(val_type));
            return struct_exprt{
              {dict_val->operands()[0],
               array_exprt{std::move(elems), list_data_type}},
              list_type};
          }
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt vals{obj, "values", vals_type};
          exprt::operandst elems;
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            elems.push_back(
              index_exprt{vals, from_integer(i, signedbv_typet{64})});
          while(elems.size() < PYTHON_MAX_LIST_LENGTH)
            elems.push_back(safe_zero(val_type));
          return struct_exprt{
            {length, array_exprt{std::move(elems), list_data_type}}, list_type};
        }
        if(method_name == "items")
        {
          // PLR dict.items(): view of (key, value) pairs. We
          // materialise as a python_list of python_tuple(key,
          // value) — precise for dict literals, falls through
          // to nondet otherwise.
          const exprt *dict_val = nullptr;
          if(obj.id() == ID_struct)
            dict_val = &obj;
          else if(obj.id() == ID_symbol)
          {
            auto it = dict_literals.find(to_symbol_expr(obj).get_identifier());
            if(it != dict_literals.end())
              dict_val = &it->second;
          }
          if(
            dict_val != nullptr && dict_val->operands().size() >= 3 &&
            dict_val->operands()[0].is_constant())
          {
            mp_integer lv;
            if(!to_integer(to_constant_expr(dict_val->operands()[0]), lv))
            {
              const auto &dict_st = to_struct_type(obj_base_type);
              const auto &keys_type =
                to_array_type(dict_st.components()[1].type());
              const auto &vals_type =
                to_array_type(dict_st.components()[2].type());
              const typet key_t = keys_type.element_type();
              const typet val_t = vals_type.element_type();
              struct_typet tuple_t = python_tuple_type({key_t, val_t});
              tuple_t.set_tag("python_tuple");
              struct_typet list_t = python_list_type(tuple_t);
              const auto &list_data_type =
                to_array_type(list_t.components()[1].type());
              const exprt &src_keys = dict_val->operands()[1];
              const exprt &src_vals = dict_val->operands()[2];
              exprt::operandst elems;
              for(mp_integer i = 0; i < lv; ++i)
              {
                auto idx = i.to_ulong();
                if(
                  idx >= src_keys.operands().size() ||
                  idx >= src_vals.operands().size())
                  break;
                elems.push_back(struct_exprt{
                  {src_keys.operands()[idx], src_vals.operands()[idx]},
                  tuple_t});
              }
              while(elems.size() < PYTHON_MAX_LIST_LENGTH)
                elems.push_back(safe_zero(tuple_t));
              return struct_exprt{
                {dict_val->operands()[0],
                 array_exprt{std::move(elems), list_data_type}},
                list_t};
            }
          }
          // Returns list of tuples — simplified to nondet for now
          return side_effect_expr_nondett{
            python_list_type(python_int_type()), get_location(expr)};
        }
        if(method_name == "clear")
        {
          // d.clear() → set d.length = 0
          if(obj.id() == ID_symbol)
          {
            member_exprt length{obj, "length", signedbv_typet{64}};
            pending_checks.push_back(code_frontend_assignt{
              length, from_integer(0, signedbv_typet{64})});
          }
          return from_integer(0, python_int_type()); // returns None
        }
        if(method_name == "copy")
        {
          return obj; // shallow copy = same struct
        }
        if(method_name == "update")
        {
          // PLR dict.update(other): merge entries from other
          // into d. If other is a dict literal (struct_exprt
          // with constant length + keys + values), emit one
          // dict-subscript-assign statement per entry to
          // preserve dict_literals tracking on both sides.
          // Non-literal 'other' or non-dict — fall through to
          // returning None (no mutation) as an
          // over-approximation.
          if(args.is_array() && !as_array(args).empty())
          {
            exprt other = convert_expression(*as_array(args).begin());
            const exprt *olit = nullptr;
            if(other.id() == ID_struct)
              olit = &other;
            else if(other.id() == ID_symbol)
            {
              auto it =
                dict_literals.find(to_symbol_expr(other).get_identifier());
              if(it != dict_literals.end())
                olit = &it->second;
            }
            if(
              olit != nullptr && olit->operands().size() >= 3 &&
              olit->operands()[0].is_constant() &&
              is_python_dict_type(obj.type()))
            {
              mp_integer olen;
              if(!to_integer(to_constant_expr(olit->operands()[0]), olen))
              {
                // Invalidate dict_literals tracking on obj so
                // subsequent d["key"] lookups read from the
                // mutated struct, not the stale literal.
                if(obj.id() == ID_symbol)
                  dict_literals.erase(to_symbol_expr(obj).get_identifier());
                const auto &dst_st = to_struct_type(obj.type());
                const auto &keys_type =
                  to_array_type(dst_st.components()[1].type());
                const auto &vals_type =
                  to_array_type(dst_st.components()[2].type());
                member_exprt dst_len{obj, "length", signedbv_typet{64}};
                member_exprt dst_keys{obj, "keys", keys_type};
                member_exprt dst_vals{obj, "values", vals_type};
                const exprt &src_keys = olit->operands()[1];
                const exprt &src_vals = olit->operands()[2];
                for(mp_integer i = 0; i < olen; ++i)
                {
                  auto idx = i.to_ulong();
                  if(
                    idx >= src_keys.operands().size() ||
                    idx >= src_vals.operands().size())
                    break;
                  exprt k = src_keys.operands()[idx];
                  exprt v = src_vals.operands()[idx];
                  if(k.type() != keys_type.element_type())
                    k = safe_typecast(k, keys_type.element_type());
                  if(v.type() != vals_type.element_type())
                    v = safe_typecast(v, vals_type.element_type());
                  // Scan existing keys, replace or append.
                  // Matches the Assign-to-subscript handler
                  // pattern used for d[k] = v statements.
                  static unsigned upd_ctr = 0;
                  std::string fn = "__upd_found_" + std::to_string(upd_ctr++);
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
                  symbol_exprt found =
                    symbol_table.lookup_ref(fi).symbol_expr();
                  pending_checks.push_back(
                    code_frontend_assignt{found, false_exprt{}});
                  for(std::size_t si = 0; si < PYTHON_MAX_DICT_SIZE; si++)
                  {
                    exprt sidx = from_integer(si, signedbv_typet{64});
                    exprt in_range =
                      binary_relation_exprt{sidx, ID_lt, dst_len};
                    exprt match = equal_exprt{index_exprt{dst_keys, sidx}, k};
                    code_blockt upd;
                    upd.add(
                      code_frontend_assignt{index_exprt{dst_vals, sidx}, v});
                    upd.add(code_frontend_assignt{found, true_exprt{}});
                    pending_checks.push_back(code_ifthenelset{
                      and_exprt{in_range, match}, std::move(upd)});
                  }
                  code_blockt append;
                  append.add(
                    code_frontend_assignt{index_exprt{dst_keys, dst_len}, k});
                  append.add(
                    code_frontend_assignt{index_exprt{dst_vals, dst_len}, v});
                  append.add(code_frontend_assignt{
                    dst_len,
                    plus_exprt{dst_len, from_integer(1, signedbv_typet{64})}});
                  pending_checks.push_back(
                    code_ifthenelset{not_exprt{found}, std::move(append)});
                }
              }
            }
          }
          return from_integer(0, python_int_type());
        }
        if(
          method_name == "setdefault" || method_name == "pop" ||
          method_name == "popitem")
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
      }

      // Python set methods: operate on the 64-bit bitmap so
      // they remain precise for int elements in [0, 64).
      // Falls through to nondet for element values outside
      // that range or for set-typed structs whose elements
      // are not int.
      if(is_python_set_type(obj_base_type))
      {
        member_exprt bm{obj, "bitmap", unsignedbv_typet{64}};
        member_exprt off{obj, "offset", signedbv_typet{64}};

        auto arg_bitmap = [&](const exprt &s) {
          return member_exprt{s, "bitmap", unsignedbv_typet{64}};
        };

        // Read a single int arg; cast to unsigned{64} for shifts.
        auto single_arg_bit_shift = [&]() -> std::optional<exprt>
        {
          if(!args.is_array() || as_array(args).empty())
            return std::nullopt;
          exprt val = convert_expression(*as_array(args).begin());
          if(val.type() != signedbv_typet{64})
            val = safe_typecast(val, signedbv_typet{64});
          exprt shamt = typecast_exprt{val, unsignedbv_typet{64}};
          return shamt;
        };

        if(method_name == "add" || method_name == "discard")
        {
          auto shamt = single_arg_bit_shift();
          if(!shamt.has_value())
            return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
          exprt bit = shl_exprt{from_integer(1, unsignedbv_typet{64}), *shamt};
          exprt new_bm = method_name == "add"
                           ? exprt{bitor_exprt{bm, bit}}
                           : exprt{bitand_exprt{bm, bitnot_exprt{bit}}};
          pending_checks.push_back(code_frontend_assignt{bm, new_bm});
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(method_name == "remove")
        {
          // Like discard, but raise KeyError if the element is
          // not present.
          auto shamt = single_arg_bit_shift();
          if(!shamt.has_value())
            return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
          exprt bit = shl_exprt{from_integer(1, unsignedbv_typet{64}), *shamt};
          exprt present = notequal_exprt{
            bitand_exprt{bm, bit}, from_integer(0, unsignedbv_typet{64})};
          add_check(
            present,
            "exception",
            "KeyError: element not in set",
            get_location(expr));
          exprt new_bm = bitand_exprt{bm, bitnot_exprt{bit}};
          pending_checks.push_back(code_frontend_assignt{bm, new_bm});
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(
          method_name == "union" || method_name == "intersection" ||
          method_name == "difference" || method_name == "symmetric_difference")
        {
          if(!args.is_array() || as_array(args).empty())
            return obj;
          exprt other = convert_expression(*as_array(args).begin());
          if(!is_python_set_type(other.type()))
            return side_effect_expr_nondett{
              python_set_type(), get_location(expr)};
          exprt rbm = arg_bitmap(other);
          exprt new_bm;
          if(method_name == "union")
            new_bm = bitor_exprt{bm, rbm};
          else if(method_name == "intersection")
            new_bm = bitand_exprt{bm, rbm};
          else if(method_name == "difference")
            new_bm = bitand_exprt{bm, bitnot_exprt{rbm}};
          else // symmetric_difference
            new_bm = bitxor_exprt{bm, rbm};
          return struct_exprt{{std::move(new_bm), off}, python_set_type()};
        }
        if(
          method_name == "issubset" || method_name == "issuperset" ||
          method_name == "isdisjoint")
        {
          if(!args.is_array() || as_array(args).empty())
            return true_exprt{};
          exprt other = convert_expression(*as_array(args).begin());
          if(!is_python_set_type(other.type()))
            return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
          exprt rbm = arg_bitmap(other);
          exprt zero = from_integer(0, unsignedbv_typet{64});
          if(method_name == "issubset")
            // A⊆B iff A & ~B == 0
            return equal_exprt{bitand_exprt{bm, bitnot_exprt{rbm}}, zero};
          if(method_name == "issuperset")
            return equal_exprt{bitand_exprt{rbm, bitnot_exprt{bm}}, zero};
          // isdisjoint: A & B == 0
          return equal_exprt{bitand_exprt{bm, rbm}, zero};
        }
        if(method_name == "copy")
          return obj;
        if(method_name == "clear")
        {
          pending_checks.push_back(
            code_frontend_assignt{bm, from_integer(0, unsignedbv_typet{64})});
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(method_name == "pop")
        {
          // Return nondet int and clear a nondet bit — over-approximation
          // that reflects pop removing an arbitrary element.
          pending_checks.push_back(code_frontend_assignt{
            bm,
            side_effect_expr_nondett{
              unsignedbv_typet{64}, get_location(expr)}});
          return side_effect_expr_nondett{
            signedbv_typet{64}, get_location(expr)};
        }
      }

      // PLib stdtypes: List methods (append, sort, reverse, pop, etc.)
      if(is_python_list_type(obj_base_type))
      {
        const auto &list_st = to_struct_type(obj_base_type);
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt length{obj, "length", signedbv_typet{64}};
        member_exprt data{obj, "data", data_type};

        if(method_name == "reverse")
        {
          // Reverse in place: swap data[i] with data[len-1-i]
          code_blockt block;
          for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH / 2; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt mirror = minus_exprt{
              minus_exprt{length, from_integer(1, signedbv_typet{64})}, idx};
            exprt cond = binary_relation_exprt{idx, ID_lt, mirror};
            // Swap via temp
            static unsigned rev_counter = 0;
            std::string tmp_name = "__rev_tmp_" + std::to_string(rev_counter++);
            std::string tmp_qname = qualify_name(tmp_name);
            irep_idt tmp_id{tmp_qname};
            if(symbol_table.lookup(tmp_id) == nullptr)
            {
              symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
              tmp_sym.base_name = tmp_name;
              tmp_sym.is_lvalue = true;
              tmp_sym.is_state_var = true;
              symbol_table.add(tmp_sym);
            }
            symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
            code_blockt swap;
            swap.add(code_frontend_assignt{tmp, index_exprt{data, idx}});
            swap.add(code_frontend_assignt{
              index_exprt{data, idx}, index_exprt{data, mirror}});
            swap.add(code_frontend_assignt{index_exprt{data, mirror}, tmp});
            pending_checks.push_back(code_ifthenelset{cond, std::move(swap)});
          }
          return from_integer(0, python_int_type()); // None
        }

        if(method_name == "sort")
        {
          // Bubble sort via pending_checks (correct for bounded lists)
          for(std::size_t pass = 0; pass < PYTHON_MAX_LIST_LENGTH; pass++)
          {
            for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt next = from_integer(i + 1, signedbv_typet{64});
              exprt in_bounds = binary_relation_exprt{next, ID_lt, length};
              exprt should_swap = binary_relation_exprt{
                index_exprt{data, idx}, ID_gt, index_exprt{data, next}};
              // Conditional swap
              static unsigned sort_counter = 0;
              std::string tmp_name =
                "__sort_tmp_" + std::to_string(sort_counter++);
              std::string tmp_qname = qualify_name(tmp_name);
              irep_idt tmp_id{tmp_qname};
              if(symbol_table.lookup(tmp_id) == nullptr)
              {
                symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
                tmp_sym.base_name = tmp_name;
                tmp_sym.is_lvalue = true;
                tmp_sym.is_state_var = true;
                symbol_table.add(tmp_sym);
              }
              symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
              code_blockt swap;
              swap.add(code_frontend_assignt{tmp, index_exprt{data, idx}});
              swap.add(code_frontend_assignt{
                index_exprt{data, idx}, index_exprt{data, next}});
              swap.add(code_frontend_assignt{index_exprt{data, next}, tmp});
              pending_checks.push_back(code_ifthenelset{
                and_exprt{in_bounds, should_swap}, std::move(swap)});
            }
          }
          return from_integer(0, python_int_type()); // None
        }

        if(method_name == "pop")
        {
          // PLib stdtypes: pop(i) or pop() — remove and return element
          exprt pop_idx;
          if(args.is_array() && !as_array(args).empty())
            pop_idx = safe_typecast(
              convert_expression(*as_array(args).begin()), signedbv_typet{64});
          else
            pop_idx = minus_exprt{length, from_integer(1, signedbv_typet{64})};

          static unsigned pop_counter = 0;
          std::string tmp_name = "__pop_tmp_" + std::to_string(pop_counter++);
          std::string tmp_qname = qualify_name(tmp_name);
          irep_idt tmp_id{tmp_qname};
          if(symbol_table.lookup(tmp_id) == nullptr)
          {
            symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
            tmp_sym.base_name = tmp_name;
            tmp_sym.is_lvalue = true;
            tmp_sym.is_state_var = true;
            symbol_table.add(tmp_sym);
          }
          symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
          // Save element at index
          pending_checks.push_back(
            code_frontend_assignt{tmp, index_exprt{data, pop_idx}});
          // Shift elements left from pop_idx
          for(std::size_t j = 0; j + 1 < PYTHON_MAX_LIST_LENGTH; j++)
          {
            exprt jexpr = from_integer(j, signedbv_typet{64});
            exprt guard = and_exprt{
              binary_relation_exprt{jexpr, ID_ge, pop_idx},
              binary_relation_exprt{
                jexpr,
                ID_lt,
                minus_exprt{length, from_integer(1, signedbv_typet{64})}}};
            pending_checks.push_back(code_ifthenelset{
              guard,
              code_frontend_assignt{
                index_exprt{data, jexpr},
                index_exprt{
                  data,
                  plus_exprt{jexpr, from_integer(1, signedbv_typet{64})}}}});
          }
          // Decrement length
          pending_checks.push_back(code_frontend_assignt{
            member_exprt{obj, "length", signedbv_typet{64}},
            minus_exprt{length, from_integer(1, signedbv_typet{64})}});
          return std::move(tmp);
        }

        // PLib stdtypes: list.index(value) — return index of first occurrence
        if(method_name == "index")
        {
          if(args.is_array() && !as_array(args).empty())
          {
            exprt search = convert_expression(*as_array(args).begin());
            if(search.type() != data_type.element_type())
              search = safe_typecast(search, data_type.element_type());
            // Build if-then-else chain: check from end to start
            exprt result = from_integer(-1, python_int_type()); // not found
            for(int i = PYTHON_MAX_LIST_LENGTH - 1; i >= 0; i--)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt match = equal_exprt{index_exprt{data, idx}, search};
              result = if_exprt{and_exprt{in_range, match}, idx, result};
            }
            return result;
          }
        }

        // PLib stdtypes: list.extend(iterable)
        if(method_name == "extend")
        {
          if(args.is_array() && !as_array(args).empty())
          {
            exprt arg = convert_expression(*as_array(args).begin());
            if(is_python_list_type(arg.type()))
            {
              member_exprt arg_len{arg, "length", signedbv_typet{64}};
              const auto &arg_data_type = to_array_type(
                to_struct_type(arg.type()).components()[1].type());
              member_exprt arg_data{arg, "data", arg_data_type};
              // Copy elements: obj.data[obj.length + i] = arg.data[i]
              for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt dst = plus_exprt{length, idx};
                pending_checks.push_back(code_ifthenelset{
                  binary_relation_exprt{idx, ID_lt, arg_len},
                  code_frontend_assignt{
                    index_exprt{data, dst}, index_exprt{arg_data, idx}}});
              }
              pending_checks.push_back(code_frontend_assignt{
                member_exprt{obj, "length", signedbv_typet{64}},
                plus_exprt{length, arg_len}});
            }
            else if(is_python_string_type(arg.type()))
            {
              // extend with string: iterate characters
              auto sv = extract_string_value(arg);
              if(!sv.has_value() && arg.id() == ID_symbol)
              {
                auto it =
                  string_constants.find(to_symbol_expr(arg).get_identifier());
                if(it != string_constants.end())
                  sv = it->second;
              }
              if(sv.has_value())
              {
                // Constant string: add each char as a single-char string
                for(std::size_t i = 0; i < sv.value().size(); i++)
                {
                  exprt dst =
                    plus_exprt{length, from_integer(i, signedbv_typet{64})};
                  exprt ch_str =
                    python_string_literal(std::string(1, sv.value()[i]));
                  if(ch_str.type() != data_type.element_type())
                    ch_str = safe_typecast(ch_str, data_type.element_type());
                  pending_checks.push_back(
                    code_frontend_assignt{index_exprt{data, dst}, ch_str});
                }
                pending_checks.push_back(code_frontend_assignt{
                  member_exprt{obj, "length", signedbv_typet{64}},
                  plus_exprt{
                    length,
                    from_integer(
                      static_cast<long long>(sv.value().size()),
                      signedbv_typet{64})}});
              }
            }
          }
          return from_integer(0, python_int_type());
        }

        // PLib stdtypes: list.remove(value)
        if(method_name == "remove")
        {
          if(args.is_array() && !as_array(args).empty())
          {
            exprt val = convert_expression(*as_array(args).begin());
            if(val.type() != data_type.element_type())
              val = safe_typecast(val, data_type.element_type());
            // Find first occurrence and shift left
            // Use a found flag to track if we've found the element
            static unsigned rm_counter = 0;
            std::string flag_name =
              "__rm_found_" + std::to_string(rm_counter++);
            std::string flag_qname = qualify_name(flag_name);
            irep_idt flag_id{flag_qname};
            if(symbol_table.lookup(flag_id) == nullptr)
            {
              symbolt flag_sym{flag_id, bool_typet{}, "python"};
              flag_sym.base_name = flag_name;
              flag_sym.is_lvalue = true;
              flag_sym.is_state_var = true;
              symbol_table.add(flag_sym);
            }
            symbol_exprt found = symbol_table.lookup_ref(flag_id).symbol_expr();
            pending_checks.push_back(
              code_frontend_assignt{found, false_exprt{}});
            for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt next = from_integer(i + 1, signedbv_typet{64});
              exprt in_bounds = binary_relation_exprt{idx, ID_lt, length};
              exprt is_match = equal_exprt{index_exprt{data, idx}, val};
              // If not found yet and matches, set found
              code_blockt on_match;
              on_match.add(code_frontend_assignt{found, true_exprt{}});
              pending_checks.push_back(code_ifthenelset{
                and_exprt{in_bounds, and_exprt{not_exprt{found}, is_match}},
                std::move(on_match)});
              // If found, shift left
              pending_checks.push_back(code_ifthenelset{
                and_exprt{in_bounds, found},
                code_frontend_assignt{
                  index_exprt{data, idx}, index_exprt{data, next}}});
            }
            pending_checks.push_back(code_ifthenelset{
              found,
              code_frontend_assignt{
                member_exprt{obj, "length", signedbv_typet{64}},
                minus_exprt{length, from_integer(1, signedbv_typet{64})}}});
          }
          return from_integer(0, python_int_type());
        }

        // PLib stdtypes: list.copy()
        if(method_name == "copy")
          return obj; // struct copy
      }

      if(obj_base_type.id() != ID_struct)
      {
        // Tagged-union (python_value_type) values: route the
        // method call through __class_ptr with virtual
        // dispatch on __class_tag.
        //
        // PLR §3.3.2 Method resolution order: Python picks the
        // override along the MRO. Our CLASS tag stores the
        // concrete class tag at wrap time; we emit an if-chain
        // comparing *(__class_ptr as int32*) against the tag
        // of every class that both (a) defines method_name and
        // (b) is a compatible entry point (defines or inherits
        // it). The first matching class wins when multiple
        // classes share the method name but aren't
        // subclass-related — matches Python semantics when the
        // caller has not narrowed via isinstance.
        if(
          obj_base_type.id() == ID_struct_tag &&
          id2string(to_struct_tag_type(obj_base_type).get_identifier()) ==
            std::string{PYTHON_VALUE_TAG})
        {
          // Collect classes that define method_name directly.
          std::vector<std::string> method_owners;
          for(const auto &[cls_name, cls_type] : class_types)
          {
            irep_idt mid{"python::" + cls_name + "::" + method_name};
            const symbolt *msym = symbol_table.lookup(mid);
            if(msym != nullptr && msym->type.id() == ID_code)
              method_owners.push_back(cls_name);
          }
          if(method_owners.empty())
            return side_effect_expr_nondett{obj.type(), get_location(expr)};

          // Build argument list (shared across all branches).
          exprt::operandst call_args;
          if(args.is_array())
          {
            for(const auto &a : as_array(args))
              call_args.push_back(convert_expression(a));
          }

          // Single owner: simple dispatch (common case).
          if(method_owners.size() == 1)
          {
            const std::string &cls_name = method_owners.front();
            const auto &cls_type = class_types.at(cls_name);
            irep_idt mid{"python::" + cls_name + "::" + method_name};
            const symbolt *msym = symbol_table.lookup_ref(mid).name.empty()
                                    ? nullptr
                                    : &symbol_table.lookup_ref(mid);
            const code_typet &mty = to_code_type(msym->type);
            exprt::operandst mcall_args;
            pointer_typet cls_ptr_type{cls_type, 64};
            exprt self_ptr =
              typecast_exprt{python_value_class_ptr(obj), cls_ptr_type};
            bool has_self = !mty.parameters().empty() &&
                            mty.parameters()[0].type().id() == ID_pointer;
            if(has_self)
              mcall_args.push_back(self_ptr);
            for(std::size_t i = 0; i < call_args.size(); i++)
            {
              exprt av = call_args[i];
              std::size_t pidx = mcall_args.size();
              if(
                pidx < mty.parameters().size() &&
                av.type() != mty.parameters()[pidx].type())
                av = safe_typecast(av, mty.parameters()[pidx].type());
              mcall_args.push_back(std::move(av));
            }
            return side_effect_expr_function_callt{
              msym->symbol_expr(),
              std::move(mcall_args),
              mty.return_type(),
              get_location(expr)};
          }

          // Multi-owner: virtual dispatch via __class_tag.
          // Assign the matching branch's call result to a
          // shared tmp symbol.
          //
          // Return type: use the first owner's return type;
          // the code_typet contract across overrides is
          // conventionally the same.
          irep_idt first_mid{
            "python::" + method_owners.front() + "::" + method_name};
          const symbolt *first_sym = symbol_table.lookup(first_mid);
          typet ret_type = to_code_type(first_sym->type).return_type();

          static unsigned vdisp_ctr = 0;
          std::string tn = "__vdisp_" + std::to_string(vdisp_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, ret_type, "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            symbol_table.add(ts);
          }
          symbol_exprt result_sym = symbol_table.lookup_ref(ti).symbol_expr();

          // Read __class_tag from the wrapped struct.
          pointer_typet i32_ptr{signedbv_typet{32}, 64};
          dereference_exprt class_tag{
            typecast_exprt{python_value_class_ptr(obj), i32_ptr},
            signedbv_typet{32}};

          for(const auto &cls_name : method_owners)
          {
            const auto &cls_type = class_types.at(cls_name);
            irep_idt mid{"python::" + cls_name + "::" + method_name};
            const symbolt *msym = symbol_table.lookup(mid);
            const code_typet &mty = to_code_type(msym->type);

            exprt::operandst mcall_args;
            pointer_typet cls_ptr_type{cls_type, 64};
            exprt self_ptr =
              typecast_exprt{python_value_class_ptr(obj), cls_ptr_type};
            bool has_self = !mty.parameters().empty() &&
                            mty.parameters()[0].type().id() == ID_pointer;
            if(has_self)
              mcall_args.push_back(self_ptr);
            for(std::size_t i = 0; i < call_args.size(); i++)
            {
              exprt av = call_args[i];
              std::size_t pidx = mcall_args.size();
              if(
                pidx < mty.parameters().size() &&
                av.type() != mty.parameters()[pidx].type())
                av = safe_typecast(av, mty.parameters()[pidx].type());
              mcall_args.push_back(std::move(av));
            }

            auto tit = class_tag_ids.find(cls_name);
            if(tit == class_tag_ids.end())
              continue;
            exprt tag_match = equal_exprt{
              class_tag, from_integer(tit->second, signedbv_typet{32})};

            code_blockt branch;
            side_effect_expr_function_callt call{
              msym->symbol_expr(),
              std::move(mcall_args),
              mty.return_type(),
              get_location(expr)};
            exprt call_result = call;
            if(call_result.type() != ret_type)
              call_result = safe_typecast(call_result, ret_type);
            branch.add(code_frontend_assignt{result_sym, call_result});
            pending_checks.push_back(
              code_ifthenelset{std::move(tag_match), std::move(branch)});
          }
          return std::move(result_sym);
        }
        // Non-struct type (e.g., struct_tag_typet for strings) — return nondet
        return side_effect_expr_nondett{obj.type(), get_location(expr)};
      }
      if(obj_base_type.id() != ID_struct)
        return side_effect_expr_nondett{obj.type(), get_location(expr)};
      const auto &st = to_struct_type(obj_base_type);
      std::string tag = id2string(st.get_tag());
      // tag is "python_class_ClassName"
      if(tag.substr(0, 13) == "python_class_")
      {
        std::string class_name = tag.substr(13);
        irep_idt method_id{"python::" + class_name + "::" + method_name};
        const symbolt *method_sym = symbol_table.lookup(method_id);
        if(method_sym != nullptr)
        {
          const code_typet &method_type = to_code_type(method_sym->type);
          exprt::operandst arguments;
          // Pass address of object as self (pointer-based model)
          // Skip for @staticmethod (no self/cls parameter)
          bool has_self = !method_type.parameters().empty() &&
                          method_type.parameters()[0].type().id() == ID_pointer;
          if(has_self)
          {
            if(obj.type().id() == ID_pointer)
              arguments.push_back(obj);
            else if(obj.id() == ID_side_effect)
              arguments.push_back(side_effect_expr_nondett{
                pointer_typet{obj.type(), config.ansi_c.pointer_width},
                get_location(expr)});
            else
              arguments.push_back(address_of_exprt{obj});
          }
          if(args.is_array())
          {
            for(const auto &arg : as_array(args))
              arguments.push_back(convert_expression(arg));
          }

          // Handle keyword arguments for method calls
          const jsont &method_keywords = json_member(expr, "keywords");
          if(method_keywords.is_array() && !as_array(method_keywords).empty())
          {
            const auto &mparams = method_type.parameters();
            arguments.resize(mparams.size(), nil_exprt{});
            std::vector<std::pair<std::string, exprt>> unmatched;
            for(const auto &kw : as_array(method_keywords))
            {
              std::string kw_name = json_string(json_member(kw, "arg"));
              exprt kw_val = convert_expression(json_member(kw, "value"));
              bool matched = false;
              for(std::size_t i = 0; i < mparams.size(); i++)
              {
                if(id2string(mparams[i].get_base_name()) == kw_name)
                {
                  arguments[i] = kw_val;
                  matched = true;
                  break;
                }
              }
              if(!matched)
                unmatched.push_back({kw_name, kw_val});
            }
            if(
              !unmatched.empty() && !mparams.empty() &&
              is_python_dict_type(mparams.back().type()))
            {
              std::size_t ki = mparams.size() - 1;
              typet dt = mparams.back().type();
              const auto &dst = to_struct_type(dt);
              const auto &kat = to_array_type(dst.components()[1].type());
              const auto &vat = to_array_type(dst.components()[2].type());
              exprt::operandst ks, vs;
              for(const auto &[n, v] : unmatched)
              {
                ks.push_back(python_string_literal(n));
                if(is_python_value_type(vat.element_type()))
                  vs.push_back(wrap_value(v));
                else
                  vs.push_back(safe_typecast(v, vat.element_type()));
              }
              while(ks.size() < PYTHON_MAX_DICT_SIZE)
              {
                ks.push_back(safe_zero(kat.element_type()));
                vs.push_back(safe_zero(vat.element_type()));
              }
              arguments[ki] = struct_exprt{
                {from_integer(
                   static_cast<long long>(unmatched.size()),
                   signedbv_typet{64}),
                 array_exprt{std::move(ks), kat},
                 array_exprt{std::move(vs), vat}},
                dt};
            }
            for(std::size_t i = 0; i < arguments.size() && i < mparams.size();
                i++)
            {
              if(arguments[i].is_nil())
                arguments[i] = safe_zero(mparams[i].type());
              else if(arguments[i].type() != mparams[i].type())
                arguments[i] = safe_typecast(arguments[i], mparams[i].type());
            }
          }

          // Check for dynamic dispatch: if subclasses override this method,
          // dispatch based on __class_tag
          std::vector<std::pair<std::string, irep_idt>> dispatch_targets;
          for(const auto &[sub_name, sub_bases] : class_bases)
          {
            for(const auto &base : sub_bases)
            {
              if(base == class_name)
              {
                irep_idt sub_method_id{
                  "python::" + sub_name + "::" + method_name};
                if(symbol_table.lookup(sub_method_id) != nullptr)
                  dispatch_targets.emplace_back(sub_name, sub_method_id);
                break;
              }
            }
          }

          if(!dispatch_targets.empty())
          {
            // Read __class_tag from the object
            exprt deref_obj =
              obj.type().id() == ID_pointer ? dereference_exprt{obj} : obj;
            exprt tag_field =
              member_exprt{deref_obj, "__class_tag", signedbv_typet{32}};

            // Build if-then-else chain: check subclass tags first
            exprt result_call = side_effect_expr_function_callt{
              method_sym->symbol_expr(),
              arguments,
              method_type.return_type(),
              get_location(expr)};

            for(auto it = dispatch_targets.rbegin();
                it != dispatch_targets.rend();
                ++it)
            {
              const auto &[sub_name, sub_method_id] = *it;
              const symbolt &sub_sym = symbol_table.lookup_ref(sub_method_id);
              exprt sub_call = side_effect_expr_function_callt{
                sub_sym.symbol_expr(),
                arguments,
                method_type.return_type(),
                get_location(expr)};
              exprt tag_check = equal_exprt{
                tag_field,
                from_integer(class_tag_ids[sub_name], signedbv_typet{32})};
              result_call = if_exprt{tag_check, sub_call, result_call};
            }
            return result_call;
          }

          side_effect_expr_function_callt call{
            method_sym->symbol_expr(),
            std::move(arguments),
            method_type.return_type(),
            get_location(expr)};
          return std::move(call);
        }
      }
    }
    // Suppress warnings for known regex/common methods on nondet objects
    if(
      method_name != "search" && method_name != "match" &&
      method_name != "group" && method_name != "groups" &&
      method_name != "span" && method_name != "findall" &&
      method_name != "sub" && method_name != "split" &&
      method_name != "compile" && method_name != "pattern" &&
      // Common container/string methods whose effect is opaque at
      // the Python-front-end level — they already fall through to a
      // nondet result. Silencing them reduces log noise for stdlib
      // ingestion (Step 1 of module support plan).
      method_name != "__class__" && method_name != "__new__" &&
      method_name != "__init__" && method_name != "__del__" &&
      method_name != "__cast" && method_name != "items" &&
      method_name != "keys" && method_name != "values" &&
      method_name != "get" && method_name != "pop" && method_name != "update" &&
      method_name != "clear" && method_name != "add" &&
      method_name != "discard" && method_name != "remove" &&
      method_name != "insert" && method_name != "append" &&
      method_name != "extend" && method_name != "copy" &&
      method_name != "read" && method_name != "write" &&
      method_name != "close" && method_name != "flush" &&
      method_name != "seek" && method_name != "tell" && method_name != "join" &&
      method_name != "encode" && method_name != "decode" &&
      method_name != "startswith" && method_name != "endswith" &&
      method_name != "strip" && method_name != "rstrip" &&
      method_name != "lstrip" && method_name != "replace" &&
      method_name != "format" && method_name != "split" &&
      method_name != "rsplit" && method_name != "rpartition" &&
      method_name != "partition" && method_name != "find" &&
      method_name != "rfind" && method_name != "index" &&
      method_name != "rindex" && method_name != "count" &&
      method_name != "lower" && method_name != "upper" &&
      method_name != "title" && method_name != "capitalize" &&
      method_name != "swapcase" && method_name != "isdigit" &&
      method_name != "isalpha" && method_name != "isalnum" &&
      method_name != "isspace" && method_name != "islower" &&
      method_name != "isupper" && method_name != "translate" &&
      method_name != "maketrans" && method_name != "zfill" &&
      method_name != "center" && method_name != "ljust" &&
      method_name != "rjust" && method_name != "expandtabs")
      log_overapprox(
        "method '" + method_name +
        "': no resolution, returning nondet over-approximation");
    // Stub-context fallback for regex method names. When code
    // like ``compile(r).search(v)`` appears in an imported stub
    // (e.g. third-party library stubs under PYTHONPATH), neither
    // ``compile`` nor ``search`` resolves to our library's re
    // model — the stub's import happens in a context where
    // module resolution doesn't reach cbmc-python.git's built-in
    // library directory. The caller's typical check pattern is
    // ``search(v) is not None``; to keep that provable we
    // constrain the nondet result to be non-negative, which
    // excludes the None sentinel (-2^62). This mirrors the
    // pre-Wave-1 ad-hoc behaviour that we'd retired once the
    // library stub took over — for USER code the library still
    // provides the real model; this fallback only helps stub-
    // resident calls.
    if(
      method_name == "search" || method_name == "match" ||
      method_name == "fullmatch" || method_name == "compile" ||
      method_name == "findall" || method_name == "finditer" ||
      method_name == "sub" || method_name == "subn" || method_name == "split")
    {
      side_effect_expr_nondett nd{python_int_type(), get_location(expr)};
      static unsigned re_stub_ctr = 0;
      std::string tn = "__re_stub_result_" + std::to_string(re_stub_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_int_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      const symbolt &ts = symbol_table.lookup_ref(ti);
      pending_checks.push_back(code_frontend_assignt{ts.symbol_expr(), nd});
      pending_checks.push_back(code_assumet{binary_relation_exprt{
        ts.symbol_expr(), ID_ge, from_integer(0, python_int_type())}});
      return ts.symbol_expr();
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }

  // Handle nondet functions
  if(func_name == "nondet_int" || func_name == "__VERIFIER_nondet_int")
  {
    side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
    return std::move(nondet);
  }
  // Frontend hooks for the Python library's re stub.
  // ``__cbmc_re_{match,search,fullmatch}(pattern, subject)`` are the
  // three entry points the library calls when both arguments are
  // Python strings. The front-end lowers each to the corresponding
  // ``cprover_string_{match,search,fullmatch}_func`` intrinsic; the
  // SMT backend (in particular ``--cvc5``) intercepts the intrinsic
  // and compiles the pattern to an SMT-LIB 2.6 regex term. When
  // the pattern cannot be translated (back-refs, lookaround, ...)
  // the intrinsic degrades to a sound nondet result.
  if(
    func_name == "__cbmc_re_match" || func_name == "__cbmc_re_search" ||
    func_name == "__cbmc_re_fullmatch")
  {
    if(args.is_array() && as_array(args).size() == 2)
    {
      auto it = as_array(args).begin();
      exprt pattern = convert_expression(*it++);
      exprt subject = convert_expression(*it);
      if(
        is_python_string_type(pattern.type()) &&
        is_python_string_type(subject.type()))
      {
        auto to_str = [](const exprt &s) -> exprt
        {
          if(s.id() == ID_struct && s.operands().size() == 2)
            return s;
          return struct_exprt{
            {member_exprt{s, "length", signedbv_typet{64}},
             member_exprt{s, "data", pointer_typet{unsignedbv_typet{8}, 64}}},
            s.type()};
        };
        irep_idt intrinsic_id = ID_cprover_string_match_func;
        if(func_name == "__cbmc_re_search")
          intrinsic_id = ID_cprover_string_search_func;
        else if(func_name == "__cbmc_re_fullmatch")
          intrinsic_id = ID_cprover_string_fullmatch_func;
        return emit_string_bool_function(
          intrinsic_id,
          to_str(pattern),
          to_str(subject),
          symbol_table,
          pending_checks);
      }
    }
    // Fallback: nondet bool.
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  else if(func_name == "nondet_float" || func_name == "__VERIFIER_nondet_float")
  {
    side_effect_expr_nondett nondet{double_type(), get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "nondet_bool" || func_name == "__VERIFIER_nondet_bool")
  {
    side_effect_expr_nondett nondet{bool_typet{}, get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "randint")
  {
    // from random import randint — constrained nondet
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt lo = convert_expression(*it);
      ++it;
      exprt hi = convert_expression(*it);
      side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
      static unsigned ri_ctr = 0;
      std::string tn = "__randint_" + std::to_string(ri_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, python_int_type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{tv, nondet});
      pending_checks.push_back(
        code_assumet{binary_relation_exprt{tv, ID_ge, lo}});
      pending_checks.push_back(
        code_assumet{binary_relation_exprt{tv, ID_le, hi}});
      return tv;
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "nondet_str" || func_name == "nondet_string")
  {
    static unsigned ns_ctr = 0;
    std::string tn = "__nondet_str_" + std::to_string(ns_ctr++);
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
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{python_string_type(), get_location(expr)}});
    // Emit the length constraint through the intrinsic so
    // downstream len() consumers (which route through
    // cprover_string_length_func) see the same value.
    exprt len_intr = emit_string_int_function(
      ID_cprover_string_length_func, tmp, symbol_table, pending_checks);
    // If size argument provided, constrain length == size
    if(args.is_array() && !as_array(args).empty())
    {
      exprt size = convert_expression(*as_array(args).begin());
      exprt size_i64 = safe_typecast(size, signedbv_typet{64});
      pending_checks.push_back(code_assumet{equal_exprt{len_intr, size_i64}});
    }
    else
    {
      pending_checks.push_back(code_assumet{and_exprt{
        binary_relation_exprt{
          len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
        binary_relation_exprt{
          len_intr,
          ID_le,
          from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}}});
    }
    return std::move(tmp);
  }
  else if(func_name == "nondet_list")
  {
    // nondet_list(n) — constrain length to [0, n]
    static unsigned nl_ctr = 0;
    exprt max_len = from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64});
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(arg.type().id() != ID_signedbv)
        arg = safe_typecast(arg, signedbv_typet{64});
      max_len = arg;
    }
    std::string tn = "__nondet_list_" + std::to_string(nl_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    typet lt = python_list_type(python_int_type());
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, lt, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{lt, get_location(expr)}});
    member_exprt len{tmp, "length", signedbv_typet{64}};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{len, ID_le, max_len}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_dict")
  {
    typet dt = python_dict_type(python_string_type(), python_int_type());
    static unsigned nd_ctr = 0;
    exprt max_len = from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64});
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(arg.type().id() != ID_signedbv)
        arg = safe_typecast(arg, signedbv_typet{64});
      max_len = arg;
    }
    std::string tn = "__nondet_dict_" + std::to_string(nd_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, dt, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{dt, get_location(expr)}});
    member_exprt len{tmp, "length", signedbv_typet{64}};
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{len, ID_le, max_len}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_complex")
  {
    struct_typet::componentst comps;
    comps.push_back(struct_typet::componentt{"real", double_type()});
    comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet ct{comps};
    ct.set_tag("python_complex");
    return side_effect_expr_nondett{ct, get_location(expr)};
  }
  // PLR: assume() constrains nondet values (ESBMC/CBMC verification primitive)
  else if(
    func_name == "assume" || func_name == "__VERIFIER_assume" ||
    func_name == "__CPROVER_assume" || func_name == "__ESBMC_assume")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt cond = convert_expression(*as_array(args).begin());
      if(cond.type().id() != ID_bool)
        cond = typecast_exprt{cond, bool_typet{}};
      pending_checks.push_back(code_assumet{cond});
    }
    return from_integer(0, python_int_type());
  }
  // PLib builtins: map(func, iterable)
  else if(func_name == "map")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt func_arg = convert_expression(*it);
      ++it;
      exprt list_arg = convert_expression(*it);
      if(
        is_python_list_type(list_arg.type()) && func_arg.id() == ID_symbol &&
        func_arg.type().id() == ID_code)
      {
        // Unroll: result[i] = func(input[i]) for i in 0..length
        const auto &list_st = to_struct_type(list_arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        const code_typet &ft = to_code_type(func_arg.type());
        typet ret_type = ft.return_type();
        typet result_list_type = python_list_type(ret_type);

        static unsigned map_ctr = 0;
        std::string tn = "__map_" + std::to_string(map_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt ti{tq};
        if(symbol_table.lookup(ti) == nullptr)
        {
          symbolt ts{ti, result_list_type, "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
        member_exprt src_data{list_arg, "data", data_type};
        member_exprt src_len{list_arg, "length", signedbv_typet{64}};
        const auto &res_data_type = to_array_type(
          to_struct_type(result_list_type).components()[1].type());
        member_exprt dst_data{tmp, "data", res_data_type};

        pending_checks.push_back(code_frontend_assignt{
          member_exprt{tmp, "length", signedbv_typet{64}}, src_len});

        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt guard = binary_relation_exprt{idx, ID_lt, src_len};
          exprt elem = index_exprt{src_data, idx};
          if(elem.type() != ft.parameters()[0].type())
            elem = safe_typecast(elem, ft.parameters()[0].type());
          side_effect_expr_function_callt call{
            func_arg, {elem}, ret_type, get_location(expr)};
          pending_checks.push_back(code_ifthenelset{
            guard, code_frontend_assignt{index_exprt{dst_data, idx}, call}});
        }
        return std::move(tmp);
      }
      if(is_python_list_type(list_arg.type()))
        return side_effect_expr_nondett{list_arg.type(), get_location(expr)};
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: zip(*iterables)
  else if(func_name == "zip")
  {
    // Return nondet list of tuples
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: filter(func, iterable)
  else if(func_name == "filter")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt func_arg = convert_expression(*it);
      ++it;
      exprt list_arg = convert_expression(*it);
      if(
        is_python_list_type(list_arg.type()) && func_arg.id() == ID_symbol &&
        func_arg.type().id() == ID_code)
      {
        const auto &list_st = to_struct_type(list_arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        const code_typet &ft = to_code_type(func_arg.type());
        typet result_list_type = list_arg.type();
        member_exprt src_data{list_arg, "data", data_type};
        member_exprt src_len{list_arg, "length", signedbv_typet{64}};

        static unsigned filter_ctr = 0;
        std::string tn = "__filter_" + std::to_string(filter_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt ti{tq};
        if(symbol_table.lookup(ti) == nullptr)
        {
          symbolt ts{ti, result_list_type, "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
        const auto &res_data_type = to_array_type(
          to_struct_type(result_list_type).components()[1].type());
        member_exprt dst_data{tmp, "data", res_data_type};

        // Counter for result length
        std::string cn = tn + "_len";
        std::string cq = qualify_name(cn);
        irep_idt ci{cq};
        if(symbol_table.lookup(ci) == nullptr)
        {
          symbolt cs{ci, python_int_type(), "python"};
          cs.base_name = cn;
          cs.is_lvalue = true;
          cs.is_state_var = true;
          symbol_table.add(cs);
        }
        symbol_exprt cnt = symbol_table.lookup_ref(ci).symbol_expr();
        pending_checks.push_back(
          code_frontend_assignt{cnt, from_integer(0, python_int_type())});

        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt guard = binary_relation_exprt{idx, ID_lt, src_len};
          exprt elem = index_exprt{src_data, idx};
          if(elem.type() != ft.parameters()[0].type())
            elem = safe_typecast(elem, ft.parameters()[0].type());
          side_effect_expr_function_callt call{
            func_arg, {elem}, ft.return_type(), get_location(expr)};
          // If filter returns truthy, add to result
          exprt truthy = safe_typecast(call, bool_typet{});
          exprt include = and_exprt{guard, truthy};
          pending_checks.push_back(code_ifthenelset{
            include,
            code_blockt{
              {code_frontend_assignt{index_exprt{dst_data, cnt}, elem},
               code_frontend_assignt{
                 cnt, plus_exprt{cnt, from_integer(1, python_int_type())}}}}});
        }
        pending_checks.push_back(code_frontend_assignt{
          member_exprt{tmp, "length", python_int_type()}, cnt});
        return std::move(tmp);
      }
      if(is_python_list_type(list_arg.type()))
        return side_effect_expr_nondett{list_arg.type(), get_location(expr)};
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // iter(x) returns x (for lists, iteration is by index)
  // next(it) returns first element (simplified model)
  else if(func_name == "iter")
  {
    if(args.is_array() && !as_array(args).empty())
      return convert_expression(*as_array(args).begin());
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "next")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && is_python_list_type(arg.type()))
      {
        const auto &data_type =
          to_array_type(to_struct_type(arg.type()).components()[1].type());
        return index_exprt{
          member_exprt{arg, "data", data_type},
          from_integer(0, signedbv_typet{64})};
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "len")
  {
    // PLib builtins: len(s) returns the length of s
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Python string: route through the refinement intrinsic
        // so the back-end (refine-strings or future SMT strings)
        // controls the semantics.
        if(is_python_string_type(arg.type()))
        {
          exprt result = emit_string_int_function(
            ID_cprover_string_length_func, arg, symbol_table, pending_checks);
          if(result.type() != python_int_type())
            result = safe_typecast(result, python_int_type());
          return result;
        }
        if(is_python_list_type(arg.type()) || is_python_dict_type(arg.type()))
          return member_exprt{arg, "length", python_int_type()};

        // Tuple: number of components
        if(is_python_tuple_type(arg.type()))
        {
          const auto &st = to_struct_type(arg.type());
          return from_integer(st.components().size(), python_int_type());
        }

        // Set (bitmap): popcount
        if(is_python_set_type(arg.type()))
        {
          // Approximate: count bits in bitmap
          member_exprt bm{arg, "bitmap", unsignedbv_typet{64}};
          return popcount_exprt{bm, python_int_type()};
        }

        // Tagged union: dispatch on tag
        if(is_python_value_type(arg.type()))
        {
          exprt str_len =
            member_exprt{python_value_str(arg), "length", python_int_type()};
          exprt list_len =
            member_exprt{python_value_list(arg), "length", python_int_type()};
          return if_exprt{
            python_value_is(arg, python_type_tagt::STR),
            str_len,
            if_exprt{
              python_value_is(arg, python_type_tagt::LIST),
              list_len,
              from_integer(0, python_int_type())}};
        }
      }
    }
    // Check for __len__ dunder method on class instances
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && arg.type().id() == ID_struct)
      {
        std::string tag = id2string(to_struct_type(arg.type()).get_tag());
        // Try python_class_X::__len__ or just X::__len__
        for(const auto &prefix :
            {"python::" + tag + "::__len__",
             "python::" +
               (tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag) +
               "::__len__"})
        {
          const symbolt *len_sym = symbol_table.lookup(irep_idt{prefix});
          if(len_sym != nullptr)
            return side_effect_expr_function_callt{
              len_sym->symbol_expr(),
              {address_of_exprt{arg}},
              python_int_type(),
              get_location(expr)};
        }
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // Built-in type constructors: int(), float(), bool()
  else if(func_name == "int")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Tagged union: dispatch on tag
        if(is_python_value_type(arg.type()))
          return unwrap_value(arg, python_int_type());
        // int("60") — parse constant string to int.
        // PLR builtins: int(x, base=10) parses the string
        // representation, accepting leading/trailing
        // whitespace and an optional sign.
        if(is_python_string_type(arg.type()))
        {
          auto sv = extract_string_value(arg);
          if(sv.has_value())
          {
            try
            {
              long long val = std::stoll(sv.value());
              return from_integer(val, python_int_type());
            }
            catch(...)
            {
            }
          }
          // Symbolic string: emit cprover_string_parse_int_func
          // so the solver knows the int's relationship to the
          // string's characters. Inverse of str(n) / f"{n}" /
          // "{}".format(n) which emit cprover_string_of_int_func.
          exprt parsed = emit_string_int_function(
            ID_cprover_string_parse_int_func,
            arg,
            symbol_table,
            pending_checks);
          if(parsed.type() != python_int_type())
            parsed = safe_typecast(parsed, python_int_type());
          return parsed;
        }
        return safe_typecast(arg, python_int_type());
      }
    }
    return from_integer(0, python_int_type());
  }
  else if(func_name == "float")
  {
    // PLR §2.4.8: float(x) converts x to floating-point
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // For constant integers, use ieee_floatt for exact conversion
        if(
          arg.is_constant() &&
          (arg.type().id() == ID_signedbv || arg.type().id() == ID_unsignedbv))
        {
          mp_integer iv;
          if(!to_integer(to_constant_expr(arg), iv))
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_integer(iv);
            return fv.to_expr();
          }
        }
        // float("60") — parse constant string to float
        if(is_python_string_type(arg.type()))
        {
          auto sv = extract_string_value(arg);
          if(sv.has_value())
          {
            try
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_double(std::stod(sv.value()));
              return fv.to_expr();
            }
            catch(...)
            {
            }
          }
        }
        return typecast_exprt{arg, double_type()};
      }
    }
    ieee_floatt zero{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    return zero.to_expr();
  }
  else if(func_name == "bool")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Check for __bool__ dunder
        if(arg.type().id() == ID_struct)
        {
          std::string tag = id2string(to_struct_type(arg.type()).get_tag());
          std::string cls =
            tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
          irep_idt bid{"python::" + cls + "::__bool__"};
          const symbolt *bsym = symbol_table.lookup(bid);
          if(bsym != nullptr)
            return side_effect_expr_function_callt{
              bsym->symbol_expr(),
              {address_of_exprt{arg}},
              bool_typet{},
              get_location(expr)};
        }
        return safe_typecast(arg, bool_typet{});
      }
    }
    return false_exprt{};
  }
  // print() — no-op, return None (modeled as 0)
  else if(func_name == "print")
  {
    mp_integer none_val = mp_integer(1) << 62;
    none_val = -none_val;
    return from_integer(none_val, python_int_type());
  }
  // PLib builtins: input() reads from stdin — model as nondet string
  else if(func_name == "input")
  {
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // PLib builtins: hex/oct/bin — compute for constants, nondet otherwise
  else if(func_name == "hex" || func_name == "oct" || func_name == "bin")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      // Evaluate unary minus on constants
      if(
        arg.id() == ID_unary_minus && arg.operands().size() == 1 &&
        arg.operands()[0].is_constant())
      {
        mp_integer v;
        if(!to_integer(to_constant_expr(arg.operands()[0]), v))
          arg = from_integer(-v, arg.type());
      }
      if(arg.is_constant() && arg.type().id() == ID_signedbv)
      {
        mp_integer val;
        if(!to_integer(to_constant_expr(arg), val))
        {
          std::string result;
          bool negative = val < 0;
          mp_integer abs_val = negative ? -val : val;
          if(func_name == "hex")
          {
            std::string digits;
            if(abs_val == 0)
              digits = "0";
            else
            {
              mp_integer tmp = abs_val;
              while(tmp > 0)
              {
                int d = (tmp % 16).to_long();
                digits = std::string(1, "0123456789abcdef"[d]) + digits;
                tmp /= 16;
              }
            }
            result = (negative ? "-0x" : "0x") + digits;
          }
          else if(func_name == "oct")
          {
            std::string digits;
            if(abs_val == 0)
              digits = "0";
            else
            {
              mp_integer tmp = abs_val;
              while(tmp > 0)
              {
                digits = std::to_string((tmp % 8).to_long()) + digits;
                tmp /= 8;
              }
            }
            result = (negative ? "-0o" : "0o") + digits;
          }
          else // bin
          {
            std::string digits;
            if(abs_val == 0)
              digits = "0";
            else
            {
              mp_integer tmp = abs_val;
              while(tmp > 0)
              {
                digits = ((tmp % 2) == 1 ? "1" : "0") + digits;
                tmp /= 2;
              }
            }
            result = (negative ? "-0b" : "0b") + digits;
          }
          return python_string_literal(result);
        }
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  else if(func_name == "repr" || func_name == "ascii")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && arg.type().id() == ID_struct)
      {
        std::string tag = id2string(to_struct_type(arg.type()).get_tag());
        std::string cls =
          tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
        irep_idt rid{"python::" + cls + "::__repr__"};
        const symbolt *rsym = symbol_table.lookup(rid);
        if(rsym != nullptr)
          return side_effect_expr_function_callt{
            rsym->symbol_expr(),
            {address_of_exprt{arg}},
            python_string_type(),
            get_location(expr)};
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // PLib builtins: hash/id — return nondet int
  else if(func_name == "hash")
  {
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // chr(n) → single-character string
  else if(func_name == "chr")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt code_point = convert_expression(*as_array(args).begin());
      // Range check: chr() requires 0 <= arg <= 0x10ffff
      add_check(
        and_exprt{
          binary_relation_exprt{
            code_point, ID_ge, from_integer(0, code_point.type())},
          binary_relation_exprt{
            code_point, ID_le, from_integer(0x10ffff, code_point.type())}},
        "value-error",
        "chr() arg not in range(0x110000)",
        get_location(expr));
      struct_typet str_type = python_string_struct_def();
      const auto &data_type = array_typet(unsignedbv_typet{8}, from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
      exprt::operandst chars;

      // For constant code points, encode as UTF-8
      mp_integer cp_val;
      // Try to resolve variable code points via try_eval_double
      auto cp_dbl = try_eval_double(code_point);
      if(
        (code_point.is_constant() &&
         !to_integer(to_constant_expr(code_point), cp_val)) ||
        (cp_dbl.has_value() &&
         (cp_val = static_cast<long long>(cp_dbl.value()), true)))
      {
        long cp = cp_val.to_long();
        if(cp < 0x80)
        {
          chars.push_back(from_integer(cp, unsignedbv_typet{8}));
        }
        else if(cp < 0x800)
        {
          chars.push_back(from_integer(0xC0 | (cp >> 6), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | (cp & 0x3F), unsignedbv_typet{8}));
        }
        else if(cp < 0x10000)
        {
          chars.push_back(from_integer(0xE0 | (cp >> 12), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | ((cp >> 6) & 0x3F), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | (cp & 0x3F), unsignedbv_typet{8}));
        }
        else
        {
          chars.push_back(from_integer(0xF0 | (cp >> 18), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | ((cp >> 12) & 0x3F), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | ((cp >> 6) & 0x3F), unsignedbv_typet{8}));
          chars.push_back(
            from_integer(0x80 | (cp & 0x3F), unsignedbv_typet{8}));
        }
      }
      else
      {
        // Non-constant: single byte (truncated)
        chars.push_back(safe_typecast(code_point, unsignedbv_typet{8}));
      }

      // For constant code points, use python_string_literal
      if(code_point.is_constant())
      {
        std::string s;
        for(const auto &c : chars)
        {
          mp_integer v;
          if(!to_integer(to_constant_expr(c), v))
            s += static_cast<char>(v.to_long());
        }
        return python_string_literal(s);
      }
      // Non-constant: build pointer-based string
      array_typet at(
        unsignedbv_typet{8},
        from_integer(static_cast<long long>(chars.size()), signedbv_typet{64}));
      array_exprt arr(std::move(chars), at);
      exprt ptr = address_of_exprt(index_exprt(
        arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      exprt length = from_integer(1LL, signedbv_typet{64});
      return struct_exprt{{length, ptr}, str_type};
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  // ord(c) → integer code point of single character
  else if(func_name == "ord")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_string_type(arg.type()))
      {
        // For constant strings, compute Unicode code point
        auto sv = extract_string_value(arg);
        if(sv.has_value() && !sv.value().empty())
        {
          const std::string &s = sv.value();
          unsigned char c0 = static_cast<unsigned char>(s[0]);
          long cp = c0;
          if(c0 >= 0xC0 && c0 < 0xE0 && s.size() >= 2)
            cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[1]) & 0x3F);
          else if(c0 >= 0xE0 && c0 < 0xF0 && s.size() >= 3)
            cp = ((c0 & 0x0F) << 12) |
                 ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[2]) & 0x3F);
          else if(c0 >= 0xF0 && s.size() >= 4)
            cp = ((c0 & 0x07) << 18) |
                 ((static_cast<unsigned char>(s[1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(s[2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[3]) & 0x3F);
          return from_integer(cp, python_int_type());
        }
        // Symbolic: return first byte
        const auto &data_type = array_typet(
          unsignedbv_typet{8},
          from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
        member_exprt data{arg, "data", data_type};
        return safe_typecast(
          index_exprt{data, from_integer(0, signedbv_typet{64})},
          python_int_type());
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // complex(real, imag) — return struct with real/imag fields
  else if(func_name == "complex")
  {
    struct_typet::componentst comps;
    comps.push_back(struct_typet::componentt{"real", double_type()});
    comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet complex_type{comps};
    complex_type.set_tag("python_complex");

    exprt real_val = safe_zero(double_type());
    exprt imag_val = safe_zero(double_type());
    if(args.is_array())
    {
      auto it = as_array(args).begin();
      if(it != as_array(args).end())
      {
        exprt arg = convert_expression(*it);
        if(arg.type().id() == ID_floatbv)
          real_val = arg;
        else
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          if(arg.is_constant())
          {
            mp_integer iv;
            if(!to_integer(to_constant_expr(arg), iv))
              fv.from_integer(iv);
          }
          real_val = fv.to_expr();
        }
        ++it;
      }
      if(it != as_array(args).end())
      {
        exprt arg = convert_expression(*it);
        if(arg.type().id() == ID_floatbv)
          imag_val = arg;
        else
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          if(arg.is_constant())
          {
            mp_integer iv;
            if(!to_integer(to_constant_expr(arg), iv))
              fv.from_integer(iv);
          }
          imag_val = fv.to_expr();
        }
      }
    }
    return struct_exprt{{real_val, imag_val}, complex_type};
  }
  // PLib stdtypes: set(iterable) — deduplicate elements
  else if(func_name == "dict")
  {
    // dict(a=1, b=2) — construct from keyword arguments
    const jsont &keywords = json_member(expr, "keywords");
    if(keywords.is_array() && !as_array(keywords).empty())
    {
      typet dict_type =
        python_dict_type(python_string_type(), python_int_type());
      // Determine value type from first keyword
      exprt first_val =
        convert_expression(json_member(*as_array(keywords).begin(), "value"));
      dict_type = python_dict_type(python_string_type(), first_val.type());
      const auto &dict_st = to_struct_type(dict_type);
      const auto &keys_arr_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_arr_type = to_array_type(dict_st.components()[2].type());

      exprt::operandst keys, vals;
      for(const auto &kw : as_array(keywords))
      {
        std::string kname = json_string(json_member(kw, "arg"));
        exprt kval = convert_expression(json_member(kw, "value"));
        keys.push_back(python_string_literal(kname));
        if(kval.type() != vals_arr_type.element_type())
          kval = safe_typecast(kval, vals_arr_type.element_type());
        vals.push_back(kval);
      }
      while(keys.size() < PYTHON_MAX_DICT_SIZE)
      {
        keys.push_back(safe_zero(keys_arr_type.element_type()));
        vals.push_back(safe_zero(vals_arr_type.element_type()));
      }
      return struct_exprt{
        {from_integer(
           static_cast<long long>(as_array(keywords).size()),
           python_int_type()),
         array_exprt{std::move(keys), keys_arr_type},
         array_exprt{std::move(vals), vals_arr_type}},
        dict_type};
    }
    // dict() with no args — empty dict
    if(!args.is_array() || as_array(args).empty())
    {
      typet dict_type =
        python_dict_type(python_string_type(), python_int_type());
      return safe_zero(dict_type);
    }
    return side_effect_expr_nondett{
      python_dict_type(python_string_type(), python_int_type()),
      get_location(expr)};
  }
  else if(func_name == "set")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
      {
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt src_data{arg, "data", data_type};
        member_exprt src_len{arg, "length", signedbv_typet{64}};

        // Create result list
        static unsigned set_counter = 0;
        std::string tmp_name = "__set_" + std::to_string(set_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, arg.type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        member_exprt dst_data{tmp, "data", data_type};
        member_exprt dst_len{tmp, "length", signedbv_typet{64}};

        // Initialize result length to 0
        pending_checks.push_back(
          code_frontend_assignt{dst_len, from_integer(0, signedbv_typet{64})});

        // For each input element, check if already in result
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt in_bounds = binary_relation_exprt{idx, ID_lt, src_len};
          exprt elem = index_exprt{src_data, idx};

          // Check if elem is already in result[0..dst_len)
          // Build: found = result[0]==elem || result[1]==elem || ...
          static unsigned found_ctr = 0;
          std::string fn = "__set_found_" + std::to_string(found_ctr++);
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
          pending_checks.push_back(code_frontend_assignt{found, false_exprt{}});

          for(std::size_t j = 0; j < PYTHON_MAX_LIST_LENGTH; j++)
          {
            exprt jdx = from_integer(j, signedbv_typet{64});
            exprt j_in_result = binary_relation_exprt{jdx, ID_lt, dst_len};
            exprt match = equal_exprt{index_exprt{dst_data, jdx}, elem};
            pending_checks.push_back(code_ifthenelset{
              and_exprt{j_in_result, match},
              code_frontend_assignt{found, true_exprt{}}});
          }

          // If not found and in bounds, add to result
          code_blockt add_block;
          add_block.add(
            code_frontend_assignt{index_exprt{dst_data, dst_len}, elem});
          add_block.add(code_frontend_assignt{
            dst_len, plus_exprt{dst_len, from_integer(1, signedbv_typet{64})}});
          pending_checks.push_back(code_ifthenelset{
            and_exprt{in_bounds, not_exprt{found}}, std::move(add_block)});
        }

        return std::move(tmp);
      }
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // list() / reversed() — return copy or nondet
  else if(func_name == "list" || func_name == "reversed")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
        return arg;
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // enumerate(iterable) → list of (index, element) tuples
  else if(func_name == "enumerate")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
      {
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        typet elem_type = data_type.element_type();
        member_exprt length{arg, "length", signedbv_typet{64}};
        member_exprt data{arg, "data", data_type};
        struct_typet::componentst comps;
        comps.push_back(struct_typet::componentt{"_0", python_int_type()});
        comps.push_back(struct_typet::componentt{"_1", elem_type});
        struct_typet tuple_type{comps};
        tuple_type.set_tag("python_tuple");
        struct_typet result_list_type = python_list_type(tuple_type);
        const auto &result_data_type =
          to_array_type(result_list_type.components()[1].type());
        exprt::operandst elems;
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          elems.push_back(struct_exprt{
            {from_integer(i, python_int_type()), index_exprt{data, idx}},
            tuple_type});
        }
        return struct_exprt{
          {length, array_exprt{std::move(elems), result_data_type}},
          result_list_type};
      }
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: sorted(iterable) — return sorted copy
  else if(func_name == "sorted")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      // PLR builtins: sorted(iterable, /, *, key=None, reverse=False).
      // Pick up the reverse=... keyword; key= is not yet supported.
      bool sorted_reverse = false;
      // key=lambda is not yet modelled — the lambda body
      // would need per-element evaluation. We accept the
      // argument silently but ignore it. The caller gets
      // a list with the same elements as the input but in
      // the default ordering (which may not match Python
      // semantics when key= is supplied — documented as
      // limitation).
      const jsont &sorted_kw = json_member(expr, "keywords");
      if(sorted_kw.is_array())
      {
        for(const auto &k : as_array(sorted_kw))
        {
          std::string kn = json_string(json_member(k, "arg"));
          if(kn == "reverse")
          {
            exprt kv = convert_expression(json_member(k, "value"));
            if(kv.is_true())
              sorted_reverse = true;
          }
        }
      }
      if(is_python_list_type(arg.type()))
      {
        // Parse-time fast path: literal or tracked list sorted
        // in C++; returns a pre-sorted list_exprt.
        const exprt *lit = nullptr;
        if(arg.id() == ID_struct)
          lit = &arg;
        else if(arg.id() == ID_symbol)
        {
          auto it = list_literals.find(to_symbol_expr(arg).get_identifier());
          if(it != list_literals.end())
            lit = &it->second;
        }
        if(
          lit != nullptr && lit->operands().size() >= 2 &&
          lit->operands()[0].is_constant())
        {
          mp_integer lv;
          if(!to_integer(to_constant_expr(lit->operands()[0]), lv))
          {
            const exprt &data_arr = lit->operands()[1];
            std::vector<std::pair<mp_integer, exprt>> pairs;
            bool all_const = true;
            for(mp_integer i = 0; i < lv; ++i)
            {
              auto idx = i.to_ulong();
              if(idx >= data_arr.operands().size())
              {
                all_const = false;
                break;
              }
              const exprt &e = data_arr.operands()[idx];
              if(!e.is_constant() || e.type().id() != ID_signedbv)
              {
                all_const = false;
                break;
              }
              mp_integer val;
              if(to_integer(to_constant_expr(e), val))
              {
                all_const = false;
                break;
              }
              pairs.emplace_back(val, e);
            }
            if(all_const)
            {
              if(sorted_reverse)
                std::sort(
                  pairs.begin(),
                  pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first > b.first; });
              else
                std::sort(
                  pairs.begin(),
                  pairs.end(),
                  [](const auto &a, const auto &b)
                  { return a.first < b.first; });
              const auto &list_st = to_struct_type(arg.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              exprt::operandst sorted_elems;
              for(const auto &p : pairs)
                sorted_elems.push_back(p.second);
              while(sorted_elems.size() < PYTHON_MAX_LIST_LENGTH)
                sorted_elems.push_back(safe_zero(data_type.element_type()));
              return struct_exprt{
                {lit->operands()[0],
                 array_exprt{std::move(sorted_elems), data_type}},
                arg.type()};
            }
          }
        }
        // Create a copy and sort it
        static unsigned sorted_counter = 0;
        std::string tmp_name = "__sorted_" + std::to_string(sorted_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, arg.type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{tmp, arg});
        // Bubble sort the copy
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{tmp, "data", data_type};
        member_exprt length{tmp, "length", signedbv_typet{64}};
        for(std::size_t pass = 0; pass < PYTHON_MAX_LIST_LENGTH; pass++)
        {
          for(std::size_t i = 0; i + 1 < PYTHON_MAX_LIST_LENGTH; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt next = from_integer(i + 1, signedbv_typet{64});
            exprt guard = and_exprt{
              binary_relation_exprt{next, ID_lt, length},
              binary_relation_exprt{
                index_exprt{data, idx},
                sorted_reverse ? ID_lt : ID_gt,
                index_exprt{data, next}}};
            static unsigned stmp = 0;
            std::string sn = "__stmp_" + std::to_string(stmp++);
            std::string sq = qualify_name(sn);
            irep_idt si{sq};
            if(symbol_table.lookup(si) == nullptr)
            {
              symbolt ss{si, data_type.element_type(), "python"};
              ss.base_name = sn;
              ss.is_lvalue = true;
              ss.is_state_var = true;
              symbol_table.add(ss);
            }
            symbol_exprt sv = symbol_table.lookup_ref(si).symbol_expr();
            code_blockt swap;
            swap.add(code_frontend_assignt{sv, index_exprt{data, idx}});
            swap.add(code_frontend_assignt{
              index_exprt{data, idx}, index_exprt{data, next}});
            swap.add(code_frontend_assignt{index_exprt{data, next}, sv});
            pending_checks.push_back(code_ifthenelset{guard, std::move(swap)});
          }
        }
        return std::move(tmp);
      }
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: sum(iterable) — sum of elements
  else if(func_name == "sum")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil() && is_python_list_type(arg.type()))
      {
        const auto &list_st = to_struct_type(arg.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt length{arg, "length", signedbv_typet{64}};
        member_exprt data{arg, "data", data_type};

        // Unrolled accumulation: result = sum of data[0..length-1]
        static unsigned sum_counter = 0;
        std::string tmp_name = "__sum_" + std::to_string(sum_counter++);
        std::string tmp_qname = qualify_name(tmp_name);
        irep_idt tmp_id{tmp_qname};
        if(symbol_table.lookup(tmp_id) == nullptr)
        {
          symbolt tmp_sym{tmp_id, data_type.element_type(), "python"};
          tmp_sym.base_name = tmp_name;
          tmp_sym.is_lvalue = true;
          tmp_sym.is_state_var = true;
          symbol_table.add(tmp_sym);
        }
        symbol_exprt tmp = symbol_table.lookup_ref(tmp_id).symbol_expr();
        pending_checks.push_back(code_frontend_assignt{
          tmp, from_integer(0, data_type.element_type())});
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          pending_checks.push_back(code_ifthenelset{
            binary_relation_exprt{idx, ID_lt, length},
            code_frontend_assignt{
              tmp, plus_exprt{tmp, index_exprt{data, idx}}}});
        }
        return std::move(tmp);
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // PLib builtins: range() as expression → list
  else if(func_name == "range")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt start, stop, step;
      auto it = as_array(args).begin();
      if(as_array(args).size() == 1)
      {
        start = from_integer(0, python_int_type());
        stop = convert_expression(*it);
        step = from_integer(1, python_int_type());
      }
      else
      {
        start = convert_expression(*it);
        ++it;
        stop = convert_expression(*it);
        step = from_integer(1, python_int_type());
        if(as_array(args).size() >= 3)
        {
          ++it;
          step = convert_expression(*it);
        }
      }
      start = safe_typecast(start, python_int_type());
      stop = safe_typecast(stop, python_int_type());
      step = safe_typecast(step, python_int_type());

      typet lt = python_list_type(python_int_type());
      static unsigned range_ctr = 0;
      std::string tn = "__range_" + std::to_string(range_ctr++);
      std::string tq = qualify_name(tn);
      irep_idt ti{tq};
      if(symbol_table.lookup(ti) == nullptr)
      {
        symbolt ts{ti, lt, "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        symbol_table.add(ts);
      }
      symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
      const auto &data_type =
        to_array_type(to_struct_type(lt).components()[1].type());
      member_exprt data{tmp, "data", data_type};
      member_exprt length{tmp, "length", signedbv_typet{64}};

      // Fill: data[i] = start + i * step for i in 0..MAX
      pending_checks.push_back(
        code_frontend_assignt{length, from_integer(0, signedbv_typet{64})});
      for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt val = plus_exprt{start, mult_exprt{idx, step}};
        exprt in_range = binary_relation_exprt{val, ID_lt, stop};
        code_blockt add;
        add.add(code_frontend_assignt{index_exprt{data, idx}, val});
        add.add(code_frontend_assignt{
          length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
        pending_checks.push_back(code_ifthenelset{in_range, std::move(add)});
      }
      return std::move(tmp);
    }
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
  }
  // PLib builtins: round(number) → nearest integer
  else if(func_name == "round")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      auto ait = as_array(args).begin();
      exprt arg = convert_expression(*ait);
      auto eval_val = try_eval_double(arg);
      if(eval_val.has_value())
      {
        double val = eval_val.value();
        int ndigits = 0;
        ++ait;
        if(ait != as_array(args).end())
        {
          exprt nd = convert_expression(*ait);
          auto nev = try_eval_double(nd);
          if(nev.has_value())
            ndigits = static_cast<int>(nev.value());
        }
        double factor = std::pow(10.0, ndigits);
        double rounded = std::round(val * factor) / factor;
        if(ndigits > 0)
          return double_to_floatbv(rounded);
        return from_integer(static_cast<long long>(rounded), python_int_type());
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // PLib builtins: divmod(a, b) returns (a // b, a % b)
  else if(func_name == "divmod")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt a = convert_expression(*it);
      ++it;
      exprt b = convert_expression(*it);
      a = safe_typecast(a, python_int_type());
      b = safe_typecast(b, python_int_type());
      // Use float type if either arg is float
      typet result_type = python_int_type();
      if(a.type().id() == ID_floatbv || b.type().id() == ID_floatbv)
      {
        result_type = double_type();
        a = safe_typecast(a, double_type());
        b = safe_typecast(b, double_type());
      }
      struct_typet::componentst comps;
      comps.push_back(struct_typet::componentt{"_0", result_type});
      comps.push_back(struct_typet::componentt{"_1", result_type});
      struct_typet tuple_type{comps};
      tuple_type.set_tag("python_tuple");
      // PLR §6.7: divmod uses floor division and Python modulo
      if(result_type.id() == ID_floatbv)
      {
        // Float divmod: quotient = floor(a/b), remainder = a - quotient*b
        // Use typecast to int for floor
        exprt q_raw = div_exprt{a, b};
        exprt q_int = typecast_exprt{q_raw, python_int_type()};
        exprt q_float = typecast_exprt{q_int, double_type()};
        // Adjust for negative: if q_raw < q_float, subtract 1
        exprt floor_q = minus_exprt{
          q_float,
          if_exprt{
            binary_relation_exprt{q_float, ID_gt, q_raw},
            typecast_exprt{from_integer(1, python_int_type()), double_type()},
            typecast_exprt{from_integer(0, python_int_type()), double_type()}}};
        exprt py_mod = minus_exprt{a, mult_exprt{floor_q, b}};
        return struct_exprt{{floor_q, py_mod}, tuple_type};
      }
      exprt quotient = div_exprt{a, b};
      exprt remainder = mod_exprt{a, b};
      exprt has_remainder =
        notequal_exprt{remainder, from_integer(0, a.type())};
      exprt diff_sign = binary_relation_exprt{
        bitxor_exprt{a, b}, ID_lt, from_integer(0, a.type())};
      exprt floor_q = minus_exprt{
        quotient,
        if_exprt{
          and_exprt{has_remainder, diff_sign},
          from_integer(1, a.type()),
          from_integer(0, a.type())}};
      exprt py_mod = if_exprt{
        and_exprt{has_remainder, diff_sign},
        plus_exprt{remainder, b},
        remainder};
      return struct_exprt{{floor_q, py_mod}, tuple_type};
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // str() — return nondet string
  else if(func_name == "str")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      // str(string) — return as-is
      if(is_python_string_type(arg.type()))
        return arg;
      // str(bool) — "True" or "False"
      if(arg.type().id() == ID_bool || arg.type().id() == ID_c_bool)
      {
        if(arg.is_true())
          return python_string_literal("True");
        if(arg.is_false())
          return python_string_literal("False");
      }
      // str(class_instance) — call __str__ if available
      if(arg.type().id() == ID_struct)
      {
        const auto &tag = to_struct_type(arg.type()).get_tag();
        if(id2string(tag).substr(0, 13) == "python_class_")
        {
          std::string cls = id2string(tag).substr(13); // strip "python_class_"
          irep_idt str_id{"python::" + cls + "::__str__"};
          const symbolt *str_sym = symbol_table.lookup(str_id);
          if(str_sym != nullptr)
          {
            exprt self_ptr = address_of_exprt{arg};
            side_effect_expr_function_callt call{
              str_sym->symbol_expr(),
              {self_ptr},
              python_string_type(),
              get_location(expr)};
            return std::move(call);
          }
        }
      }
      // str(int/float) — convert at conversion time using try_eval_double
      {
        auto ev = try_eval_double(arg);
        if(ev.has_value())
        {
          double d = ev.value();
          std::string s;
          if(
            d == std::floor(d) && std::abs(d) < 1e15 &&
            (arg.type().id() == ID_signedbv || arg.type().id() == ID_integer ||
             (arg.id() == ID_symbol && arg.type().id() != ID_floatbv)))
            s = std::to_string(static_cast<long long>(d));
          else
          {
            std::ostringstream oss;
            oss << d;
            s = oss.str();
            // Python-style: remove trailing zeros after decimal
            if(s.find('.') != std::string::npos)
            {
              while(s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.')
                s.pop_back();
            }
          }
          return python_string_literal(s);
        }
      }
      // str(int_constant) — legacy path
      if(arg.is_constant() && arg.type().id() == ID_signedbv)
      {
        mp_integer iv;
        if(!to_integer(to_constant_expr(arg), iv))
        {
          std::string s = integer2string(iv);
          return python_string_literal(s);
        }
      }
      // str(float_constant)
      if(arg.is_constant() && arg.type().id() == ID_floatbv)
      {
        ieee_floatt fv{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        fv.from_expr(to_constant_expr(arg));
        std::string s = fv.to_ansi_c_string();
        if(s.find(".") != std::string::npos)
        {
          while(s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.')
            s.pop_back();
        }
        return python_string_literal(s);
      }
      // Symbolic int: emit cprover_string_of_int_func so the
      // solver knows the result's content and length
      // precisely.
      if(arg.type().id() == ID_signedbv || arg.type().id() == ID_integer)
      {
        exprt as_i64 = arg.type() == signedbv_typet{64}
                         ? arg
                         : safe_typecast(arg, signedbv_typet{64});
        exprt result = emit_string_function(
          ID_cprover_string_of_int_func,
          {as_i64},
          symbol_table,
          pending_checks);
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
        ensure_fn(ID_cprover_associate_array_to_pointer_func);
        ensure_fn(ID_cprover_associate_length_to_array_func);
        return result;
      }
      // Symbolic float: emit cprover_string_of_double_func
      // (Python floats are double-precision) so the solver
      // knows the result's content precisely.
      if(arg.type().id() == ID_floatbv)
      {
        exprt result = emit_string_function(
          ID_cprover_string_of_double_func,
          {arg},
          symbol_table,
          pending_checks);
        auto ensure_fn2 = [&](const irep_idt &fid)
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
        ensure_fn2(ID_cprover_associate_array_to_pointer_func);
        ensure_fn2(ID_cprover_associate_length_to_array_func);
        return result;
      }
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }
    // str() with no arguments → empty string
    return python_string_literal("");
  }
  // all(genexp) / any(genexp) — unroll for literal iterables
  else if(func_name == "all" || func_name == "any")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      const jsont &arg = *as_array(args).begin();
      if(is_node_type(arg, "GeneratorExp"))
      {
        const jsont &elt = json_member(arg, "elt");
        const jsont &generators = json_member(arg, "generators");
        if(generators.is_array() && !as_array(generators).empty())
        {
          const jsont &gen = *as_array(generators).begin();
          const jsont &gen_iter = json_member(gen, "iter");
          const jsont &gen_target = json_member(gen, "target");
          std::string iter_var = json_string(json_member(gen_target, "id"));

          if(is_node_type(gen_iter, "List") || is_node_type(gen_iter, "Name"))
          {
            // Get the iterable elements
            exprt iterable = convert_expression(gen_iter);
            const jsont *elts_json = nullptr;
            if(is_node_type(gen_iter, "List"))
              elts_json = &json_member(gen_iter, "elts");

            if(elts_json != nullptr && elts_json->is_array())
            {
              // Literal list: unroll
              std::string qname = qualify_name(iter_var);
              irep_idt iter_sym_id{qname};
              if(symbol_table.lookup(iter_sym_id) == nullptr)
              {
                symbolt sym{iter_sym_id, python_int_type(), "python"};
                sym.base_name = iter_var;
                sym.is_lvalue = true;
                sym.is_state_var = true;
                symbol_table.add(sym);
              }

              exprt result = (func_name == "all") ? exprt{true_exprt{}}
                                                  : exprt{false_exprt{}};

              for(const auto &val_json : as_array(*elts_json))
              {
                exprt val = convert_expression(val_json);
                exprt elt_expr = convert_expression(elt);
                // Substitute iter_var with concrete value
                std::function<void(exprt &)> subst = [&](exprt &e)
                {
                  if(
                    e.id() == ID_symbol &&
                    to_symbol_expr(e).get_identifier() == iter_sym_id)
                    e = val;
                  else
                    for(auto &op : e.operands())
                      subst(op);
                };
                subst(elt_expr);

                if(elt_expr.type() != bool_typet{})
                  elt_expr = typecast_exprt{elt_expr, bool_typet{}};

                if(func_name == "all")
                  result = and_exprt{result, elt_expr};
                else
                  result = or_exprt{result, elt_expr};
              }
              return result;
            }

            // Variable iterable: iterate over list data array
            if(!iterable.is_nil() && is_python_list_type(iterable.type()))
            {
              const auto &list_st = to_struct_type(iterable.type());
              const auto &data_type =
                to_array_type(list_st.components()[1].type());
              member_exprt data{iterable, "data", data_type};
              member_exprt length{iterable, "length", signedbv_typet{64}};

              std::string qname = qualify_name(iter_var);
              irep_idt iter_sym_id{qname};
              if(symbol_table.lookup(iter_sym_id) == nullptr)
              {
                symbolt sym{iter_sym_id, data_type.element_type(), "python"};
                sym.base_name = iter_var;
                sym.is_lvalue = true;
                sym.is_state_var = true;
                symbol_table.add(sym);
              }

              exprt result = (func_name == "all") ? exprt{true_exprt{}}
                                                  : exprt{false_exprt{}};
              for(std::size_t i = 0;
                  i < std::min(
                        static_cast<std::size_t>(16),
                        static_cast<std::size_t>(PYTHON_MAX_LIST_LENGTH));
                  i++)
              {
                exprt idx = from_integer(i, signedbv_typet{64});
                exprt in_range = binary_relation_exprt{idx, ID_lt, length};
                exprt elem = index_exprt{data, idx};

                exprt elt_expr = convert_expression(elt);
                std::function<void(exprt &)> subst = [&](exprt &e)
                {
                  if(
                    e.id() == ID_symbol &&
                    to_symbol_expr(e).get_identifier() == iter_sym_id)
                    e = elem;
                  else
                    for(auto &op : e.operands())
                      subst(op);
                };
                subst(elt_expr);

                if(elt_expr.type() != bool_typet{})
                  elt_expr = safe_typecast(elt_expr, bool_typet{});

                if(func_name == "all")
                  result =
                    and_exprt{result, or_exprt{not_exprt{in_range}, elt_expr}};
                else
                  result = or_exprt{result, and_exprt{in_range, elt_expr}};
              }
              return result;
            }
          }
        }
      }
      // Non-generator argument: all([x, y]) / any([x, y])
      exprt arg_expr = convert_expression(arg);
      if(!arg_expr.is_nil() && is_python_list_type(arg_expr.type()))
      {
        // Iterate over list elements
        const auto &list_st = to_struct_type(arg_expr.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{arg_expr, "data", data_type};
        member_exprt length{arg_expr, "length", signedbv_typet{64}};

        exprt result =
          (func_name == "all") ? exprt{true_exprt{}} : exprt{false_exprt{}};
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt in_range = binary_relation_exprt{idx, ID_lt, length};
          exprt elem = index_exprt{data, idx};
          exprt truthy = safe_typecast(elem, bool_typet{});

          if(func_name == "all")
            result = and_exprt{result, or_exprt{not_exprt{in_range}, truthy}};
          else
            result = or_exprt{result, and_exprt{in_range, truthy}};
        }
        return result;
      }
      if(!arg_expr.is_nil())
        return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    }
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  // PLib builtins: type(obj) — return the type of an object
  // We model this as a static type tag for comparison with type names.
  else if(func_name == "hasattr" || func_name == "callable")
  {
    return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
  }
  else if(
    func_name == "TypeVar" || func_name == "NewType" ||
    func_name == "overload" || func_name == "dataclass" || func_name == "field")
  {
    // typing/dataclass decorators and constructors — return nondet
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  else if(func_name == "type")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // Return a type-tag constant based on static type
        int tag = 0; // unknown
        if(arg.type().id() == ID_signedbv || arg.type().id() == ID_integer)
          tag = 1;
        else if(arg.type().id() == ID_floatbv)
          tag = 2;
        else if(arg.type().id() == ID_bool)
          tag = 3;
        else if(is_python_string_type(arg.type()))
          tag = 4;
        else if(is_python_list_type(arg.type()))
          tag = 5;
        else if(is_python_tuple_type(arg.type()))
          tag = 6;
        else if(is_python_dict_type(arg.type()))
          tag = 7;
        return from_integer(tag, python_int_type());
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // PLR §6.10.2: isinstance(obj, classinfo)
  // "Return True if the object argument is an instance of the classinfo
  // argument, or of a (direct, indirect, or virtual) subclass thereof."
  else if(func_name == "isinstance")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt obj = convert_expression(*it);
      ++it;
      std::string cls_name;
      if(is_node_type(*it, "Name"))
        cls_name = json_string(json_member(*it, "id"));

      // PLR §6.10.2: isinstance(x, (A, B)) — tuple of types
      if(is_node_type(*it, "Tuple"))
      {
        const jsont &elts = json_member(*it, "elts");
        if(elts.is_array() && !obj.is_nil())
        {
          // Tagged union: OR the per-tag checks.
          if(is_python_value_type(obj.type()))
          {
            exprt any_match = false_exprt{};
            for(const auto &elt : as_array(elts))
            {
              if(!is_node_type(elt, "Name"))
                continue;
              std::string tname = json_string(json_member(elt, "id"));
              exprt m;
              if(tname == "int")
                m = python_value_is(obj, python_type_tagt::INT);
              else if(tname == "float")
                m = python_value_is(obj, python_type_tagt::FLOAT);
              else if(tname == "bool")
                m = python_value_is(obj, python_type_tagt::BOOL);
              else if(tname == "str")
                m = python_value_is(obj, python_type_tagt::STR);
              else if(tname == "list")
                m = python_value_is(obj, python_type_tagt::LIST);
              else if(class_types.count(tname) > 0)
              {
                // Precise per-class dispatch on __class_tag.
                // Same logic as the single-name path below.
                std::set<std::string> matching;
                matching.insert(tname);
                bool grew = true;
                while(grew)
                {
                  grew = false;
                  for(const auto &[cand, cand_bases] : class_bases)
                  {
                    if(matching.count(cand) > 0)
                      continue;
                    for(const auto &b : cand_bases)
                    {
                      if(matching.count(b) > 0)
                      {
                        matching.insert(cand);
                        grew = true;
                        break;
                      }
                    }
                  }
                }
                exprt class_ptr = python_value_class_ptr(obj);
                pointer_typet i32_ptr{signedbv_typet{32}, 64};
                dereference_exprt class_tag{
                  typecast_exprt{class_ptr, i32_ptr}, signedbv_typet{32}};
                exprt tag_check = false_exprt{};
                for(const auto &mm : matching)
                {
                  auto ti = class_tag_ids.find(mm);
                  if(ti == class_tag_ids.end())
                    continue;
                  exprt eq = equal_exprt{
                    class_tag, from_integer(ti->second, signedbv_typet{32})};
                  if(tag_check.id() == ID_false)
                    tag_check = std::move(eq);
                  else
                    tag_check = or_exprt{std::move(tag_check), std::move(eq)};
                }
                m = and_exprt{
                  python_value_is(obj, python_type_tagt::CLASS),
                  std::move(tag_check)};
              }
              else
                continue;
              any_match = or_exprt{any_match, m};
            }
            return any_match;
          }
          exprt result = false_exprt{};
          for(const auto &elt : as_array(elts))
          {
            if(is_node_type(elt, "Name"))
            {
              std::string tname = json_string(json_member(elt, "id"));
              bool match = false;
              if(
                tname == "int" && (obj.type().id() == ID_signedbv ||
                                   obj.type().id() == ID_integer))
                match = true;
              else if(tname == "float" && obj.type().id() == ID_floatbv)
                match = true;
              else if(tname == "bool" && obj.type().id() == ID_bool)
                match = true;
              else if(tname == "str" && is_python_string_type(obj.type()))
                match = true;
              else if(tname == "list" && is_python_list_type(obj.type()))
                match = true;
              else if(tname == "set" && is_python_set_type(obj.type()))
                match = true;
              else if(tname == "dict" && is_python_dict_type(obj.type()))
                match = true;
              else if(
                tname == "complex" && obj.type().id() == ID_struct &&
                to_struct_type(obj.type()).get_tag() == "python_complex")
                match = true;
              if(match)
                return true_exprt{};
            }
          }
          return false_exprt{};
        }
      }

      // Handle isinstance(x, type(None)) — check for NoneType
      if(
        !obj.is_nil() && is_node_type(*it, "Call") &&
        is_node_type(json_member(*it, "func"), "Name") &&
        json_string(json_member(json_member(*it, "func"), "id")) == "type")
      {
        const jsont &type_args = json_member(*it, "args");
        if(
          type_args.is_array() && !as_array(type_args).empty() &&
          is_node_type(*as_array(type_args).begin(), "Constant") &&
          json_member(*as_array(type_args).begin(), "value").is_null())
        {
          // isinstance(x, type(None)) — check if x is None
          if(is_python_value_type(obj.type()))
            return python_value_is(obj, python_type_tagt::NONE);
          if(obj.type().id() == ID_signedbv)
          {
            // None sentinel check
            return equal_exprt{
              obj,
              from_integer(mp_integer{-4611686018427387904LL}, obj.type())};
          }
          return false_exprt{};
        }
      }

      if(!obj.is_nil() && !cls_name.empty())
      {
        // Tagged union: isinstance checks the tag field
        if(is_python_value_type(obj.type()))
        {
          if(cls_name == "int")
            return python_value_is(obj, python_type_tagt::INT);
          if(cls_name == "float")
            return python_value_is(obj, python_type_tagt::FLOAT);
          if(cls_name == "bool")
            return python_value_is(obj, python_type_tagt::BOOL);
          if(cls_name == "str")
            return python_value_is(obj, python_type_tagt::STR);
          if(cls_name == "list")
            return python_value_is(obj, python_type_tagt::LIST);
          // User-defined class: dispatch precisely on the
          // __class_tag read through __class_ptr.
          //
          // The pointer is typed as opaque (void*); we cast
          // it to pointer-to-int32 to read __class_tag, which
          // sits at offset 0 in every class struct. Then we
          // OR-compare against the tag of cls_name plus the
          // tags of all classes that inherit from cls_name
          // (forward BFS through class_bases to find
          // subclasses).
          if(class_types.count(cls_name) > 0)
          {
            // Collect cls_name + all known subclasses of cls_name
            // (any class whose ancestor chain includes cls_name).
            std::set<std::string> matching;
            matching.insert(cls_name);
            bool grew = true;
            while(grew)
            {
              grew = false;
              for(const auto &[cand, cand_bases] : class_bases)
              {
                if(matching.count(cand) > 0)
                  continue;
                for(const auto &b : cand_bases)
                {
                  if(matching.count(b) > 0)
                  {
                    matching.insert(cand);
                    grew = true;
                    break;
                  }
                }
              }
            }
            // Build OR of class_tag equality checks.
            exprt class_ptr = python_value_class_ptr(obj);
            pointer_typet i32_ptr{signedbv_typet{32}, 64};
            dereference_exprt class_tag{
              typecast_exprt{class_ptr, i32_ptr}, signedbv_typet{32}};
            exprt tag_check = false_exprt{};
            for(const auto &m : matching)
            {
              auto ti = class_tag_ids.find(m);
              if(ti == class_tag_ids.end())
                continue;
              exprt eq = equal_exprt{
                class_tag, from_integer(ti->second, signedbv_typet{32})};
              if(tag_check.id() == ID_false)
                tag_check = std::move(eq);
              else
                tag_check = or_exprt{std::move(tag_check), std::move(eq)};
            }
            // Guard with the outer CLASS tag check — the precise
            // dispatch only applies when tag == CLASS.
            return and_exprt{
              python_value_is(obj, python_type_tagt::CLASS),
              std::move(tag_check)};
          }
          return false_exprt{}; // not a known type
        }

        // Check built-in types first
        if(
          cls_name == "int" &&
          (obj.type().id() == ID_signedbv || obj.type().id() == ID_integer))
          return true_exprt{};
        if(cls_name == "float" && obj.type().id() == ID_floatbv)
          return true_exprt{};
        if(cls_name == "bool" && obj.type().id() == ID_bool)
          return true_exprt{};
        if(cls_name == "str" && is_python_string_type(obj.type()))
          return true_exprt{};
        if(cls_name == "list" && is_python_list_type(obj.type()))
          return true_exprt{};
        if(cls_name == "tuple" && is_python_tuple_type(obj.type()))
          return true_exprt{};
        if(cls_name == "dict" && is_python_dict_type(obj.type()))
          return true_exprt{};
        if(cls_name == "set" && is_python_set_type(obj.type()))
          return true_exprt{};
        if(
          cls_name == "complex" && obj.type().id() == ID_struct &&
          to_struct_type(obj.type()).get_tag() == "python_complex")
          return true_exprt{};

        // If checking against a built-in type and obj is a different
        // built-in type, return false (no cross-type isinstance)
        if(
          cls_name == "int" || cls_name == "float" || cls_name == "bool" ||
          cls_name == "str" || cls_name == "list" || cls_name == "tuple" ||
          cls_name == "dict")
        {
          // obj is not the requested built-in type
          if(
            obj.type().id() == ID_signedbv || obj.type().id() == ID_integer ||
            obj.type().id() == ID_floatbv || obj.type().id() == ID_bool ||
            is_python_string_type(obj.type()) ||
            is_python_list_type(obj.type()) ||
            is_python_tuple_type(obj.type()) || is_python_dict_type(obj.type()))
            return false_exprt{};
        }

        // Check user-defined classes
        if(obj.type().id() == ID_struct)
        {
          const auto &st = to_struct_type(obj.type());
          std::string tag = id2string(st.get_tag());
          std::string obj_class;
          if(tag.substr(0, 13) == "python_class_")
            obj_class = tag.substr(13);

          if(!obj_class.empty())
          {
            // BFS through inheritance hierarchy (supports multiple inheritance)
            std::vector<std::string> queue = {obj_class};
            std::set<std::string> visited;
            while(!queue.empty())
            {
              std::string check = queue.back();
              queue.pop_back();
              if(visited.count(check))
                continue;
              visited.insert(check);
              if(check == cls_name)
                return true_exprt{};
              auto base_it = class_bases.find(check);
              if(base_it != class_bases.end())
              {
                for(const auto &b : base_it->second)
                  queue.push_back(b);
              }
            }
            return false_exprt{};
          }
        }
      }
      return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
    }
    return false_exprt{};
  }
  // abs()
  else if(func_name == "abs")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
      {
        // PLib builtins: abs(complex) = sqrt(real² + imag²)
        if(
          arg.type().id() == ID_struct &&
          to_struct_type(arg.type()).get_tag() == "python_complex")
        {
          // For struct_exprt (literal complex), compute at conversion time
          if(arg.id() == ID_struct && arg.operands().size() == 2)
          {
            const exprt &re = arg.operands()[0];
            const exprt &im = arg.operands()[1];
            if(re.is_constant() && im.is_constant())
            {
              ieee_floatt rv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              rv.from_expr(to_constant_expr(re));
              ieee_floatt iv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              iv.from_expr(to_constant_expr(im));
              double rd = std::stod(rv.to_ansi_c_string());
              double id = std::stod(iv.to_ansi_c_string());
              return double_to_floatbv(std::sqrt(rd * rd + id * id));
            }
          }
          // Variable complex: sqrt(real² + imag²)
          {
            member_exprt re{arg, "real", double_type()};
            member_exprt im{arg, "imag", double_type()};
            exprt sum = plus_exprt{mult_exprt{re, re}, mult_exprt{im, im}};
            // Return nondet >= 0 (sound overapproximation of sqrt)
            static unsigned abs_ctr = 0;
            std::string tn = "__abs_" + std::to_string(abs_ctr++);
            std::string tq = qualify_name(tn);
            irep_idt ti{tq};
            if(symbol_table.lookup(ti) == nullptr)
            {
              symbolt ts{ti, double_type(), "python"};
              ts.base_name = tn;
              ts.is_lvalue = true;
              ts.is_state_var = true;
              symbol_table.add(ts);
            }
            symbol_exprt tmp = symbol_table.lookup_ref(ti).symbol_expr();
            pending_checks.push_back(code_frontend_assignt{
              tmp,
              side_effect_expr_nondett{double_type(), source_locationt{}}});
            pending_checks.push_back(code_assumet{
              binary_relation_exprt{tmp, ID_ge, safe_zero(double_type())}});
            // Constrain: result² == real² + imag²
            pending_checks.push_back(
              code_assumet{ieee_float_equal_exprt{mult_exprt{tmp, tmp}, sum}});
            return std::move(tmp);
          }
        }
        // abs(x) = x >= 0 ? x : -x
        if(arg.type().id() == ID_signedbv || arg.type().id() == ID_floatbv)
        {
          return if_exprt{
            binary_relation_exprt{arg, ID_ge, safe_zero(arg.type())},
            arg,
            unary_minus_exprt{arg}};
        }
        // TypeError for non-numeric types
        if(
          is_python_string_type(arg.type()) ||
          is_python_list_type(arg.type()) || is_python_dict_type(arg.type()))
        {
          add_check(
            false_exprt{},
            "exception",
            "TypeError: bad operand type for abs()",
            get_location(expr));
        }
        return side_effect_expr_nondett{arg.type(), get_location(expr)};
      }
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // min(), max()
  else if(func_name == "min" || func_name == "max")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt a = convert_expression(*it);
      ++it;
      exprt b = convert_expression(*it);
      if(!a.is_nil() && !b.is_nil())
      {
        // Promote to same type (e.g., min(3, 2.5) → float)
        if(a.type() != b.type())
        {
          if(a.type().id() == ID_floatbv)
            b = safe_typecast(b, a.type());
          else if(b.type().id() == ID_floatbv)
            a = safe_typecast(a, b.type());
          else
            b = safe_typecast(b, a.type());
        }
        irep_idt op = (func_name == "min") ? ID_lt : ID_gt;
        return if_exprt{binary_relation_exprt{a, op, b}, a, b};
      }
    }
    return nil_exprt{};
  }

  // Regular function call — check if it's a class constructor
  if(class_types.count(func_name))
  {
    // Constructor call as expression: create temp, call __init__, return temp
    const struct_typet &cls_type = class_types[func_name];
    static unsigned ctor_tmp_counter = 0;
    std::string tmp_name =
      "__ctor_expr_" + func_name + "_" + std::to_string(ctor_tmp_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};

    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, cls_type, "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }

    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);

    // Copy class-level default values from the class object
    irep_idt class_obj_id{"python::" + func_name};
    const symbolt *class_obj = symbol_table.lookup(class_obj_id);
    if(class_obj != nullptr && !class_obj->value.is_nil())
    {
      pending_checks.push_back(
        code_frontend_assignt{tmp_sym.symbol_expr(), class_obj->symbol_expr()});
    }

    irep_idt init_id{"python::" + func_name + "::__init__"};
    const symbolt *init_sym = symbol_table.lookup(init_id);
    if(init_sym != nullptr)
    {
      exprt::operandst init_args;
      init_args.push_back(address_of_exprt{tmp_sym.symbol_expr()});
      if(args.is_array())
      {
        for(const auto &arg : as_array(args))
          init_args.push_back(convert_expression(arg));
      }
      // Handle keyword arguments
      const code_typet &init_type = to_code_type(init_sym->type);
      const jsont &keywords = json_member(expr, "keywords");
      if(keywords.is_array())
      {
        for(const auto &kw : as_array(keywords))
        {
          std::string kw_name = json_string(json_member(kw, "arg"));
          exprt kw_val = convert_expression(json_member(kw, "value"));
          // Find the parameter index for this keyword
          for(std::size_t pi = 0; pi < init_type.parameters().size(); pi++)
          {
            if(id2string(init_type.parameters()[pi].get_base_name()) == kw_name)
            {
              while(init_args.size() <= pi)
                init_args.push_back(nil_exprt{});
              init_args[pi] = kw_val;
              break;
            }
          }
        }
      }
      // Pad missing args with defaults
      while(init_args.size() < init_type.parameters().size())
        init_args.push_back(
          safe_zero(init_type.parameters()[init_args.size()].type()));
      for(std::size_t i = 0;
          i < init_args.size() && i < init_type.parameters().size();
          i++)
      {
        if(init_args[i].type() != init_type.parameters()[i].type())
          init_args[i] =
            safe_typecast(init_args[i], init_type.parameters()[i].type());
      }

      side_effect_expr_function_callt call{
        init_sym->symbol_expr(),
        std::move(init_args),
        empty_typet{},
        get_location(expr)};
      // Inject the __init__ call before the current statement
      pending_checks.push_back(code_expressiont{call});
    }

    return tmp_sym.symbol_expr();
  }

  irep_idt symbol_id{"python::" + func_name};
  const symbolt *sym = symbol_table.lookup(symbol_id);
  // The former ad-hoc math-function block has been retired.
  // math.py declares each function with @c_intrinsic('name',
  // fold='op', domain='kind', range='kind').
  // The decorator path a few hundred lines below handles the
  // complete semantics: parse-time fold for constants, guarded
  // ValueError for domain violations, and range-constrained
  // nondet return for symbolic arguments.

  // Check function aliases (lambda assignments: double = lambda x: x*2)
  if(sym == nullptr || sym->type.id() != ID_code)
  {
    auto alias_it = function_aliases.find(qualify_name(func_name));
    if(alias_it != function_aliases.end())
      sym = symbol_table.lookup(alias_it->second);
  }

  if(sym == nullptr || sym->type.id() != ID_code)
  {
    // Try module scope (forward references from inside functions)
    if(!current_function.empty())
    {
      sym = symbol_table.lookup(irep_idt{"python::" + func_name});
      if(sym != nullptr && sym->type.id() != ID_code)
        sym = nullptr;
    }
  }

  if(sym == nullptr || sym->type.id() != ID_code)
  {
    // Recognised-but-unmodelled Python built-ins. Returning a sound
    // nondet value is safe for these because (i) they are pure or
    // have no side effects that affect verification targets, and
    // (ii) any further reasoning about them would require a proper
    // model (tracked by Step 2 of the module support plan). By
    // whitelisting them here we avoid the spurious "no-body" false
    // positive that would otherwise fail verification of any
    // program that merely mentions them.
    //
    // Each entry maps the Python name to the type of the nondet
    // value returned. A nil typet means: use python_int_type().
    static const std::map<std::string, typet> known_nondet_builtins = {
      {"getattr", typet{}},
      {"setattr", typet{}},
      {"hasattr", bool_typet{}},
      {"callable", bool_typet{}},
      {"issubclass", bool_typet{}},
      {"id", typet{}},
      {"hash", typet{}},
      {"iter", typet{}},
      {"next", typet{}},
      {"tuple", typet{}},
      {"list", typet{}},
      {"set", typet{}},
      {"frozenset", typet{}},
      {"dict", typet{}},
      {"bytes", typet{}},
      {"bytearray", typet{}},
      {"memoryview", typet{}},
      {"super", typet{}},
      {"object", typet{}},
      {"classmethod", typet{}},
      {"staticmethod", typet{}},
      {"property", typet{}},
      {"vars", typet{}},
      {"dir", typet{}},
      {"globals", typet{}},
      {"locals", typet{}},
      {"open", typet{}},
      {"type", typet{}},
    };
    auto builtin_it = known_nondet_builtins.find(func_name);
    if(builtin_it != known_nondet_builtins.end())
    {
      // A default-constructed typet in the table means "use the
      // generic python_int_type()"; otherwise use the explicit type.
      // typet{}.is_nil() is false (the default id is empty, not
      // ID_nil), so check for an empty id instead.
      const typet t = builtin_it->second.id().empty() ? python_int_type()
                                                      : builtin_it->second;
      side_effect_expr_nondett nondet{t, get_location(expr)};
      return std::move(nondet);
    }

    // Unknown function — return nondet value (sound overapproximation).
    // Quiet by default because stdlib ingestion routinely hits
    // hundreds of these and the fallback is already correct (nondet
    // + optional no-body assertion). Visible with --verbosity 9 or
    // --python-strict-warnings.
    log_overapprox(
      "function '" + func_name +
      "': no body known, returning nondet over-approximation");
    if(!no_body_check)
    {
      add_check(
        false_exprt{},
        "no-body",
        "no body for callee " + func_name,
        get_location(expr));
    }
    side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
    return std::move(nondet);
  }

  const code_typet &func_type = to_code_type(sym->type);
  const auto &params = func_type.parameters();

  // Build argument list: start with positional args
  exprt::operandst arguments;
  if(args.is_array())
  {
    for(const auto &arg : as_array(args))
      arguments.push_back(convert_expression(arg));
  }

  // Handle keyword arguments: match by parameter name
  const jsont &keywords = json_member(expr, "keywords");
  if(keywords.is_array() && !as_array(keywords).empty())
  {
    arguments.resize(params.size(), nil_exprt{});

    // Collect unmatched keywords for **kwargs
    std::vector<std::pair<std::string, exprt>> unmatched_kw;

    for(const auto &kw : as_array(keywords))
    {
      std::string kw_name = json_string(json_member(kw, "arg"));
      exprt kw_val = convert_expression(json_member(kw, "value"));

      bool matched = false;
      for(std::size_t i = 0; i < params.size(); i++)
      {
        if(id2string(params[i].get_base_name()) == kw_name)
        {
          arguments[i] = kw_val;
          matched = true;
          break;
        }
      }
      if(!matched)
        unmatched_kw.push_back({kw_name, kw_val});
    }

    // Pack unmatched keywords into a dict for the last param if it's a dict
    if(
      !unmatched_kw.empty() && !params.empty() &&
      is_python_dict_type(params.back().type()))
    {
      std::size_t kwargs_idx = params.size() - 1;
      typet dict_type = params.back().type();
      const auto &dict_st = to_struct_type(dict_type);
      const auto &keys_arr_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_arr_type = to_array_type(dict_st.components()[2].type());

      exprt::operandst key_elems, val_elems;
      for(const auto &[name, val] : unmatched_kw)
      {
        key_elems.push_back(python_string_literal(name));
        if(is_python_value_type(vals_arr_type.element_type()))
          val_elems.push_back(wrap_value(val));
        else
          val_elems.push_back(safe_typecast(val, vals_arr_type.element_type()));
      }
      while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
      {
        key_elems.push_back(safe_zero(keys_arr_type.element_type()));
        val_elems.push_back(safe_zero(vals_arr_type.element_type()));
      }
      exprt length = from_integer(
        static_cast<long long>(unmatched_kw.size()), signedbv_typet{64});
      arguments[kwargs_idx] = struct_exprt{
        {length,
         array_exprt{std::move(key_elems), keys_arr_type},
         array_exprt{std::move(val_elems), vals_arr_type}},
        dict_type};
    }
  }

  // Prepend bound self for bound method calls
  {
    auto bm_it = bound_methods.find(qualify_name(func_name));
    if(bm_it != bound_methods.end())
    {
      arguments.insert(arguments.begin(), bm_it->second.second);
    }
  }

  // Add closure captures as extra arguments BEFORE padding
  {
    auto cap_it2 = closure_captures.find(id2string(sym->name));
    if(cap_it2 != closure_captures.end())
    {
      for(const auto &[outer_id, name, type] : cap_it2->second)
      {
        const symbolt *outer_sym = symbol_table.lookup(irep_idt{outer_id});
        if(outer_sym != nullptr)
          arguments.push_back(outer_sym->symbol_expr());
        else
          arguments.push_back(
            side_effect_expr_nondett{type, get_location(expr)});
      }
    }
  }

  // Fill in defaults for any remaining nil arguments.
  // Defaults are stored in the FunctionDef AST; look up the function's
  // definition to find them.
  if(arguments.size() < params.size())
    arguments.resize(params.size(), nil_exprt{});

  // Use pre-evaluated default values (evaluated at definition time)
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(arguments[i].is_nil())
    {
      auto def_it = default_values.find({func_name, i});
      if(def_it != default_values.end())
      {
        arguments[i] = def_it->second;
        // Class reference: struct default → pointer param
        if(
          params[i].type().id() == ID_pointer &&
          arguments[i].type().id() == ID_struct &&
          to_pointer_type(params[i].type()).base_type() == arguments[i].type())
        {
          arguments[i] = address_of_exprt{arguments[i]};
        }
      }
    }
  }

  // Fallback: look up the function's AST to get defaults
  const jsont &body = json_member(parse_tree.ast_json, "body");
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if(
        (is_node_type(stmt, "FunctionDef") ||
         is_node_type(stmt, "AsyncFunctionDef")) &&
        json_string(json_member(stmt, "name")) == func_name)
      {
        const jsont &func_args = json_member(stmt, "args");
        const jsont &defaults = json_member(func_args, "defaults");
        if(defaults.is_array())
        {
          std::size_t n_defaults = as_array(defaults).size();
          std::size_t first_default = params.size() - n_defaults;
          auto def_it = as_array(defaults).begin();
          for(std::size_t i = first_default; i < params.size(); i++, ++def_it)
          {
            if(arguments[i].is_nil())
            {
              arguments[i] = convert_expression(*def_it);
              // Class reference: struct default → pointer param
              if(
                params[i].type().id() == ID_pointer &&
                arguments[i].type().id() == ID_struct &&
                to_pointer_type(params[i].type()).base_type() ==
                  arguments[i].type())
              {
                arguments[i] = address_of_exprt{arguments[i]};
              }
            }
          }
        }
        break;
      }
    }
  }

  // Replace any remaining nil arguments with safe_zero of param type
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(arguments[i].is_nil())
    {
      if(params[i].type().id() == ID_pointer)
      {
        // For pointer params (class references), create a temp object
        static unsigned ref_tmp_ctr = 0;
        std::string tn = "__ref_tmp_" + std::to_string(ref_tmp_ctr++);
        std::string tq = qualify_name(tn);
        irep_idt ti{tq};
        typet base = to_pointer_type(params[i].type()).base_type();
        if(symbol_table.lookup(ti) == nullptr)
        {
          symbolt ts{ti, base, "python"};
          ts.base_name = tn;
          ts.is_lvalue = true;
          ts.is_state_var = true;
          symbol_table.add(ts);
        }
        arguments[i] =
          address_of_exprt{symbol_table.lookup_ref(ti).symbol_expr()};
      }
      else
        arguments[i] = safe_zero(params[i].type());
    }
  }

  // Remove trailing nil arguments beyond param count
  while(!arguments.empty() && arguments.back().is_nil())
    arguments.pop_back();

  // Typecast arguments to match parameter types
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(!arguments[i].is_nil() && arguments[i].type() != params[i].type())
    {
      // Class reference: struct arg → pointer param → take address_of
      if(
        params[i].type().id() == ID_pointer &&
        arguments[i].type().id() == ID_struct &&
        to_pointer_type(params[i].type()).base_type() == arguments[i].type())
      {
        arguments[i] = address_of_exprt{arguments[i]};
      }
      else
        arguments[i] = safe_typecast(arguments[i], params[i].type());
    }
  }

  // @c_intrinsic: redirect the call to the named C function. The
  // Python function's declared signature is used as-is for the C
  // intrinsic, with one exception — Python str parameters (and
  // str returns) are marshalled to/from C ``char *`` so the C
  // library's view of string arguments is consistent with its
  // usual conventions. See the 'str marshalling' comments below.
  auto intrinsic_it = c_intrinsic_map.find(sym->name);
  if(intrinsic_it != c_intrinsic_map.end())
  {
    const std::string &c_name = intrinsic_it->second;

    // Parse-time constant folding. If a ``fold=`` keyword was
    // set on the decorator *and* every argument at this call
    // site is a float (or int-that-converts-to-float) constant,
    // evaluate the named host-side op (from <cmath>) and return
    // the result as a constant expression. Falls through to the
    // C-call path for non-constant arguments or unrecognised
    // fold names.
    //
    // If ``domain=`` is also set, a constant argument that fails
    // the named domain predicate raises Python ValueError
    // (matching CPython's math-domain semantics). A nondet
    // argument leaves the ad-hoc math path (in
    // imported_math_funcs) to emit the guarded ValueError and
    // the nondet+constraints return. This duplication will be
    // retired in a follow-up once the decorator supports the
    // nondet+constraints case too.
    auto fold_it = c_intrinsic_fold_map.find(sym->name);
    auto domain_it = c_intrinsic_domain_map.find(sym->name);
    if(fold_it != c_intrinsic_fold_map.end() && arguments.size() == 1)
    {
      auto cv = try_eval_double(arguments[0]);
      if(cv.has_value())
      {
        double x = cv.value();

        // Domain check for constant args. If out of domain, raise
        // ValueError (matching CPython) and return a nondet
        // sentinel; the exception handler intercepts before the
        // caller observes the value.
        if(domain_it != c_intrinsic_domain_map.end())
        {
          const std::string &dom = domain_it->second;
          bool in_domain = true;
          if(dom == "nonneg")
            in_domain = x >= 0;
          else if(dom == "positive")
            in_domain = x > 0;
          else if(dom == "gt_neg_one")
            in_domain = x > -1;
          else if(dom == "abs_le_1")
            in_domain = x >= -1 && x <= 1;
          else if(dom == "abs_lt_1")
            in_domain = x > -1 && x < 1;
          else if(dom == "ge_1")
            in_domain = x >= 1;
          if(!in_domain)
          {
            emit_value_error(false_exprt{});
            return side_effect_expr_nondett{double_type(), get_location(expr)};
          }
        }

        const std::string &op = fold_it->second;
        double r = 0;
        bool computed = true;
        if(op == "sqrt")
          r = std::sqrt(x);
        else if(op == "cbrt")
          r = std::cbrt(x);
        else if(op == "exp")
          r = std::exp(x);
        else if(op == "exp2")
          r = std::exp2(x);
        else if(op == "expm1")
          r = std::expm1(x);
        else if(op == "log")
          r = std::log(x);
        else if(op == "log2")
          r = std::log2(x);
        else if(op == "log10")
          r = std::log10(x);
        else if(op == "log1p")
          r = std::log1p(x);
        else if(op == "sin")
          r = std::sin(x);
        else if(op == "cos")
          r = std::cos(x);
        else if(op == "tan")
          r = std::tan(x);
        else if(op == "asin")
          r = std::asin(x);
        else if(op == "acos")
          r = std::acos(x);
        else if(op == "atan")
          r = std::atan(x);
        else if(op == "sinh")
          r = std::sinh(x);
        else if(op == "cosh")
          r = std::cosh(x);
        else if(op == "tanh")
          r = std::tanh(x);
        else if(op == "asinh")
          r = std::asinh(x);
        else if(op == "acosh")
          r = std::acosh(x);
        else if(op == "atanh")
          r = std::atanh(x);
        else if(op == "ceil")
          r = std::ceil(x);
        else if(op == "floor")
          r = std::floor(x);
        else if(op == "trunc")
          r = std::trunc(x);
        else if(op == "fabs")
          r = std::fabs(x);
        else if(op == "erf")
          r = std::erf(x);
        else if(op == "erfc")
          r = std::erfc(x);
        else if(op == "gamma" || op == "tgamma")
          r = std::tgamma(x);
        else if(op == "lgamma")
          r = std::lgamma(x);
        else
          computed = false;
        if(computed && std::isfinite(r))
          return double_to_floatbv(r);
      }
    }

    // Two-arg fold: math.pow(x, y), atan2(y, x), hypot(x, y),
    // fmod(x, y), copysign(x, y), remainder(x, y). All args
    // must be float/int constants; when both are, evaluate
    // via std::<op>.
    if(fold_it != c_intrinsic_fold_map.end() && arguments.size() == 2)
    {
      auto cv1 = try_eval_double(arguments[0]);
      auto cv2 = try_eval_double(arguments[1]);
      if(cv1.has_value() && cv2.has_value())
      {
        const std::string &op = fold_it->second;
        double a = cv1.value(), b = cv2.value();
        double r = 0;
        bool computed = true;
        if(op == "pow")
          r = std::pow(a, b);
        else if(op == "atan2")
          r = std::atan2(a, b);
        else if(op == "hypot")
          r = std::hypot(a, b);
        else if(op == "fmod")
          r = std::fmod(a, b);
        else if(op == "copysign")
          r = std::copysign(a, b);
        else if(op == "remainder")
          r = std::remainder(a, b);
        else
          computed = false;
        if(computed && std::isfinite(r))
          return double_to_floatbv(r);
      }
    }

    // Symbolic-argument handling for decorator-driven math.
    //
    // When fold= is set but the argument is symbolic (not a
    // compile-time constant), we model the call as a nondet
    // return constrained by the optional domain= and range=
    // keywords — matching the semantics of CPython's math
    // functions plus CBMC's C math-library model:
    //
    //   * domain= named predicate: emit guarded ValueError if
    //     the predicate rejects the argument at runtime.
    //   * range= named predicate: constrain the nondet return
    //     accordingly.
    //
    // Return type follows func_type (the Python declaration).
    // For most math functions this is double_type().
    auto range_it = c_intrinsic_range_map.find(sym->name);
    bool is_math_float_fn =
      fold_it != c_intrinsic_fold_map.end() && arguments.size() == 1 &&
      to_code_type(func_type).return_type().id() == ID_floatbv;
    if(is_math_float_fn)
    {
      std::string dom = domain_it != c_intrinsic_domain_map.end()
                          ? domain_it->second
                          : std::string{};
      std::string rng = range_it != c_intrinsic_range_map.end()
                          ? range_it->second
                          : std::string{};
      return emit_math_intrinsic_nondet(
        dom, rng, arguments[0], get_location(expr));
    }

    const pointer_typet c_char_ptr{char_type(), config.ansi_c.pointer_width};

    // Helper: is this type a Python refined string type?
    auto is_py_str = [](const typet &t) { return is_python_string_type(t); };

    // Build the C function signature by projecting each Python
    // parameter onto a C equivalent. Python str → C char*.
    // Python int → C signed int of the width declared by the
    // @c_intrinsic('name', int_width=N) annotation (default 64).
    auto iw_it = c_intrinsic_int_width_map.find(sym->name);
    int int_width =
      iw_it != c_intrinsic_int_width_map.end() ? iw_it->second : 64;
    auto maybe_narrow_int = [&](typet &t)
    {
      if(t.id() == ID_signedbv)
      {
        const auto sz = to_signedbv_type(t).get_width();
        if(
          static_cast<int>(sz) != int_width &&
          (int_width == 32 || int_width == 64))
          t = signedbv_typet{static_cast<std::size_t>(int_width)};
      }
    };
    code_typet c_func_type = to_code_type(func_type);
    for(auto &p : c_func_type.parameters())
    {
      if(is_py_str(p.type()))
        p.type() = c_char_ptr;
      else
        maybe_narrow_int(p.type());
      p.set_identifier(irep_idt{});
    }
    typet c_return_type = c_func_type.return_type();
    const bool return_is_py_str = is_py_str(c_return_type);
    if(return_is_py_str)
      c_func_type.return_type() = c_char_ptr;
    else
      maybe_narrow_int(c_func_type.return_type());

    irep_idt c_id{c_name};
    if(symbol_table.lookup(c_id) == nullptr)
    {
      symbolt c_sym{c_id, c_func_type, ID_C};
      c_sym.base_name = c_name;
      c_sym.location = get_location(expr);
      c_sym.is_lvalue = true;
      c_sym.is_extern = true;
      symbol_table.add(c_sym);
    }

    // Marshal arguments: for each parameter typed as Python str,
    // extract the struct's 'data' pointer and hand that to C.
    // For string-literal arguments (produced by
    // build_string_struct), the backing array is a *temporary*
    // and taking its address produces a pointer CBMC treats as a
    // "dead object" when dereferenced later. build_string_literal
    // wraps the same bytes into a static-lifetime symbol and
    // returns a new struct with the safe pointer; we detect the
    // inline literal shape and substitute, then extract the raw
    // pointer without going through a struct temporary (that
    // temporary loses track of the persistent storage).
    const auto &c_params = c_func_type.parameters();
    for(std::size_t i = 0; i < arguments.size() && i < c_params.size(); i++)
    {
      const typet &py_param_type =
        to_code_type(func_type).parameters()[i].type();
      if(is_py_str(py_param_type))
      {
        // Try to recognise the build_string_struct shape:
        //   { length-constant, address_of(array-literal[0]) }
        // If matched, hand C the address_of directly — that's a
        // genuine pointer into persistent storage, not a
        // member-access into a temporary struct.
        if(
          arguments[i].id() == ID_struct && arguments[i].operands().size() == 2)
        {
          const exprt &len_op = arguments[i].operands()[0];
          const exprt &data_op = arguments[i].operands()[1];
          if(
            len_op.is_constant() && data_op.id() == ID_address_of &&
            to_address_of_expr(data_op).object().id() == ID_index)
          {
            const exprt &arr =
              to_index_expr(to_address_of_expr(data_op).object()).array();
            if(arr.id() == ID_array)
            {
              std::string bytes;
              bytes.reserve(arr.operands().size());
              bool all_bytes = true;
              for(const auto &op : arr.operands())
              {
                if(!op.is_constant())
                {
                  all_bytes = false;
                  break;
                }
                mp_integer v;
                if(to_integer(to_constant_expr(op), v))
                {
                  all_bytes = false;
                  break;
                }
                bytes.push_back(static_cast<char>(v.to_long()));
              }
              // (build_string_struct doesn't append a trailing
              // NUL — string_constantt below handles that.)
              if(all_bytes)
              {
                // Emit a C string_constantt — CBMC recognises
                // these as persistent and exempt from dead-object
                // checks, exactly like C code's "literal"
                // constructs. Pass address_of(str[0]) as the
                // C callee's char*.
                string_constantt sc{irep_idt{bytes}};
                arguments[i] = typecast_exprt{
                  address_of_exprt{index_exprt{
                    sc, from_integer(0, signedbv_typet{64}), char_type()}},
                  c_char_ptr};
                continue;
              }
            }
          }
        }

        exprt data = member_exprt{
          arguments[i],
          "data",
          pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width}};
        arguments[i] = typecast_exprt{std::move(data), c_char_ptr};
      }
      else if(
        arguments[i].type().id() == ID_signedbv &&
        c_params[i].type().id() == ID_signedbv &&
        arguments[i].type() != c_params[i].type())
      {
        // int width narrowing / widening for int_width= callees.
        arguments[i] = typecast_exprt{arguments[i], c_params[i].type()};
      }
    }

    symbol_exprt callee_expr = symbol_table.lookup_ref(c_id).symbol_expr();
    callee_expr.add_source_location() = get_location(expr);
    exprt call = side_effect_expr_function_callt{
      std::move(callee_expr),
      std::move(arguments),
      c_func_type.return_type(),
      get_location(expr)};

    if(!return_is_py_str)
    {
      // If int_width= narrowed the return type, widen back to
      // the Python-declared int so downstream code gets the
      // expected signedbv width.
      const typet &py_return = to_code_type(func_type).return_type();
      if(
        call.type().id() == ID_signedbv && py_return.id() == ID_signedbv &&
        call.type() != py_return)
      {
        return typecast_exprt{std::move(call), py_return};
      }
      return call;
    }

    // Marshal the return: wrap the returned char* into a Python
    // refined-string struct. The length is nondet (we can't
    // compute strlen precisely without a separate intrinsic), but
    // for a sound verification over-approximation we constrain it
    // to be in [0, PYTHON_MAX_STRING_LENGTH].
    // We store the call's result in a fresh temp first because
    // we want to read it multiple times (for the data and the
    // fake length) without duplicating side effects.
    static unsigned cstr_ret_ctr = 0;
    std::string rn = "__cstr_ret_" + std::to_string(cstr_ret_ctr++);
    std::string rq = qualify_name(rn);
    irep_idt rid{rq};
    if(symbol_table.lookup(rid) == nullptr)
    {
      symbolt rs{rid, c_char_ptr, "python"};
      rs.base_name = rn;
      rs.is_lvalue = true;
      rs.is_state_var = true;
      symbol_table.add(rs);
    }
    symbol_exprt ret_ptr = symbol_table.lookup_ref(rid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{ret_ptr, call});

    // Nondet length, constrained to [0, PYTHON_MAX_STRING_LENGTH].
    std::string ln = "__cstr_len_" + std::to_string(cstr_ret_ctr - 1);
    std::string lq = qualify_name(ln);
    irep_idt lid{lq};
    if(symbol_table.lookup(lid) == nullptr)
    {
      symbolt ls{lid, signedbv_typet{64}, "python"};
      ls.base_name = ln;
      ls.is_lvalue = true;
      ls.is_state_var = true;
      symbol_table.add(ls);
    }
    symbol_exprt ret_len = symbol_table.lookup_ref(lid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      ret_len,
      side_effect_expr_nondett{signedbv_typet{64}, source_locationt{}}});
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      ret_len, ID_ge, from_integer(0, signedbv_typet{64})}});
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      ret_len,
      ID_le,
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}});

    // Build the Python refined-string struct {length, data} where
    // data is the C pointer we just saved.
    return struct_exprt{
      {ret_len,
       typecast_exprt{
         ret_ptr,
         pointer_typet{unsignedbv_typet{8}, config.ansi_c.pointer_width}}},
      python_string_type()};
  }

  side_effect_expr_function_callt call{
    sym->symbol_expr(),
    std::move(arguments),
    func_type.return_type(),
    get_location(expr)};

  return std::move(call);
}

// PLR §6.13: Conditional expressions
// "x if C else y — first C is evaluated; if true, x is evaluated; else y."
exprt python_convertert::convert_if_exp(const jsont &expr)
{
  exprt test = convert_expression(json_member(expr, "test"));
  exprt body = convert_expression(json_member(expr, "body"));
  exprt orelse = convert_expression(json_member(expr, "orelse"));

  if(test.is_nil() || body.is_nil() || orelse.is_nil())
    return nil_exprt{};

  // Ensure both branches have the same type
  if(body.type() != orelse.type())
    orelse = safe_typecast(orelse, body.type());

  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  return if_exprt{test, body, orelse};
}

// PLR §6.3.2: Subscriptions
// "The primary must evaluate to an object that supports subscription."
exprt python_convertert::convert_subscript(const jsont &expr)
{
  exprt value = convert_expression(json_member(expr, "value"));

  if(value.is_nil())
    return nil_exprt{};

  // Dict subscript: d["key"] → scan keys array for match
  if(is_python_dict_type(value.type()))
  {
    exprt slice = convert_expression(json_member(expr, "slice"));
    if(!slice.is_nil())
    {
      // Constant-key optimization: resolve at conversion time
      auto key_str = extract_string_value(slice);
      if(key_str.has_value())
      {
        const exprt *dict_val = nullptr;
        if(value.id() == ID_struct)
          dict_val = &value;
        else if(value.id() == ID_symbol)
        {
          auto it = dict_literals.find(to_symbol_expr(value).get_identifier());
          if(it != dict_literals.end())
            dict_val = &it->second;
        }
        if(
          dict_val != nullptr && dict_val->operands().size() >= 3 &&
          dict_val->operands()[0].is_constant())
        {
          mp_integer len_val;
          if(!to_integer(to_constant_expr(dict_val->operands()[0]), len_val))
          {
            const exprt &keys_arr = dict_val->operands()[1];
            const exprt &vals_arr = dict_val->operands()[2];
            for(mp_integer i = 0; i < len_val; ++i)
            {
              auto idx = i.to_ulong();
              if(idx < keys_arr.operands().size())
              {
                auto kv = extract_string_value(keys_arr.operands()[idx]);
                if(kv.has_value() && kv.value() == key_str.value())
                  return vals_arr.operands()[idx];
              }
            }
          }
        }
      }

      const auto &dict_st = to_struct_type(value.type());
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      member_exprt length{value, "length", signedbv_typet{64}};
      member_exprt keys{value, "keys", keys_type};
      member_exprt vals{value, "values", vals_type};

      // Scan: result = values[i] where keys[i] == slice
      exprt result =
        safe_zero(vals_type.element_type()); // default if not found
      exprt found = false_exprt{};
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt key_i = index_exprt{keys, idx};
        if(key_i.type() != slice.type())
          key_i = safe_typecast(key_i, slice.type());
        exprt match = equal_exprt{key_i, slice};
        exprt cond = and_exprt{in_range, match};
        result = if_exprt{cond, index_exprt{vals, idx}, result};
        found = or_exprt{found, cond};
      }
      // KeyError if key not found
      add_check(
        found,
        "exception",
        "KeyError: key not found in dict",
        get_location(expr));
      return result;
    }
  }

  // List/string slicing: lst[1:4] or s[::-1]
  const jsont &slice_json = json_member(expr, "slice");
  if(
    is_node_type(slice_json, "Slice") &&
    (is_python_list_type(value.type()) || is_python_string_type(value.type())))
  {
    const jsont &lower_json = json_member(slice_json, "lower");
    const jsont &upper_json = json_member(slice_json, "upper");
    const jsont &step_json = json_member(slice_json, "step");

    member_exprt length{value, "length", signedbv_typet{64}};

    // Check for step=-1 (reverse)
    bool is_reverse = false;
    if(!step_json.is_null())
    {
      exprt step = convert_expression(step_json);
      if(step.is_constant())
      {
        mp_integer step_val;
        if(!to_integer(to_constant_expr(step), step_val) && step_val == -1)
          is_reverse = true;
      }
    }

    // Constant-string optimization for slicing
    if(is_python_string_type(value.type()))
    {
      auto sv = extract_string_value(value);
      if(!sv.has_value() && value.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(value).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
      {
        std::string s = sv.value();
        int len = static_cast<int>(s.size());
        if(is_reverse)
        {
          std::string rev(s.rbegin(), s.rend());
          return python_string_literal(rev);
        }
        else
        {
          int lo =
            lower_json.is_null()
              ? 0
              : static_cast<int>(
                  try_eval_double(convert_expression(lower_json)).value_or(0));
          int hi =
            upper_json.is_null()
              ? len
              : static_cast<int>(try_eval_double(convert_expression(upper_json))
                                   .value_or(len));
          if(lo < 0)
            lo += len;
          if(hi < 0)
            hi += len;
          if(lo < 0)
            lo = 0;
          if(hi > len)
            hi = len;
          if(lo >= hi)
            return python_string_literal("");
          return python_string_literal(s.substr(lo, hi - lo));
        }
      }
      // Non-constant string: return nondet
      return side_effect_expr_nondett{python_string_type(), source_locationt{}};
    }

    const auto &st = to_struct_type(value.type());
    const auto &data_type = to_array_type(st.components()[1].type());
    typet elem_type = data_type.element_type();
    member_exprt src_data{value, "data", data_type};

    exprt lower, upper;
    if(is_reverse)
    {
      lower = from_integer(0, signedbv_typet{64});
      upper = length;
    }
    else
    {
      lower = lower_json.is_null() ? from_integer(0, signedbv_typet{64})
                                   : convert_expression(lower_json);
      upper = upper_json.is_null() ? length : convert_expression(upper_json);
    }

    exprt new_length =
      is_reverse ? exprt{length} : exprt{minus_exprt{upper, lower}};

    // Determine result type (same as source: list or string)
    std::size_t max_len = is_python_string_type(value.type())
                            ? PYTHON_MAX_STRING_LENGTH
                            : PYTHON_MAX_LIST_LENGTH;
    array_typet result_data_type{
      elem_type, from_integer(max_len, signedbv_typet{64})};

    exprt::operandst result_elems;
    for(std::size_t i = 0; i < max_len; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      if(is_reverse)
      {
        // result[i] = src[length - 1 - i]
        result_elems.push_back(index_exprt{
          src_data,
          minus_exprt{
            minus_exprt{length, from_integer(1, signedbv_typet{64})}, idx}});
      }
      else
      {
        result_elems.push_back(index_exprt{src_data, plus_exprt{lower, idx}});
      }
    }

    array_exprt result_data{std::move(result_elems), result_data_type};
    return struct_exprt{{new_length, result_data}, st};
  }

  exprt slice = convert_expression(json_member(expr, "slice"));

  if(value.is_nil() || slice.is_nil())
    return nil_exprt{};

  // String indexing: s[i] → s.data[i] as a single-char string struct
  if(is_python_string_type(value.type()))
  {
    member_exprt length{value, "length", python_int_type()};
    // PLR §6.3.3: negative indices count from the end
    exprt adjusted_idx = if_exprt{
      binary_relation_exprt{slice, ID_lt, from_integer(0, slice.type())},
      plus_exprt{length, slice},
      slice};
    add_check(
      and_exprt{
        binary_relation_exprt{adjusted_idx, ID_ge, safe_zero(slice.type())},
        binary_relation_exprt{adjusted_idx, ID_lt, length}},
      "index-out-of-bounds",
      "string index out of range",
      get_location(expr));
    // Constant-string optimization for indexing
    {
      auto sv = extract_string_value(value);
      if(!sv.has_value() && value.id() == ID_symbol)
      {
        auto it = string_constants.find(to_symbol_expr(value).get_identifier());
        if(it != string_constants.end())
          sv = it->second;
      }
      if(sv.has_value())
      {
        auto nv = try_eval_double(adjusted_idx);
        if(nv.has_value())
        {
          int i = static_cast<int>(nv.value());
          int len = static_cast<int>(sv.value().size());
          if(i >= 0 && i < len)
            return python_string_literal(std::string(1, sv.value()[i]));
        }
      }
    }
    // Non-constant indexing: read char via pointer arithmetic
    {
      member_exprt data_ptr(
        value, "data", pointer_typet(unsignedbv_typet{8}, 64));
      dereference_exprt ch(plus_exprt(data_ptr, adjusted_idx));
      // Build a single-char string from the character
      exprt::operandst chars;
      chars.push_back(ch);
      array_typet at(unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
      array_exprt arr(std::move(chars), at);
      exprt ptr = address_of_exprt(index_exprt(
        arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      exprt len = from_integer(1, signedbv_typet{64});
      return struct_exprt({len, ptr}, python_string_type());
    }
    struct_typet str_type = python_string_struct_def();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));

    member_exprt data{value, "data", data_type};
    index_exprt char_val{data, adjusted_idx};

    // Build a single-character string struct
    exprt::operandst chars;
    chars.push_back(char_val);
    while(chars.size() < PYTHON_MAX_STRING_LENGTH)
      chars.push_back(from_integer(0, unsignedbv_typet{8}));

    array_exprt data_expr{std::move(chars), data_type};
    exprt length_expr = from_integer(1, python_int_type());

    struct_exprt result{{length_expr, data_expr}, str_type};
    return std::move(result);
  }

  // Array/list indexing
  if(is_python_list_type(value.type()))
  {
    // PLR §6.3.2: Non-integer index → TypeError
    if(
      slice.type().id() != ID_signedbv && slice.type().id() != ID_unsignedbv &&
      slice.type().id() != ID_integer && slice.type().id() != ID_bool)
    {
      const symbolt *exc_sym =
        symbol_table.lookup("python::__exception_active");
      const symbolt *exc_type_sym =
        symbol_table.lookup("python::__exception_type");
      if(exc_sym != nullptr)
        pending_checks.push_back(
          code_frontend_assignt{exc_sym->symbol_expr(), true_exprt{}});
      if(exc_type_sym != nullptr)
      {
        long type_hash = exception_type_hash("TypeError");
        pending_checks.push_back(code_frontend_assignt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())});
      }
      return from_integer(0, python_int_type());
    }
    member_exprt length{value, "length", python_int_type()};

    // PLR §6.3.2: Subscriptions — negative indices count from the end.
    // Handle negative indices: lst[-1] → lst[len-1]
    exprt effective_idx = slice;
    if(slice.is_constant())
    {
      mp_integer idx_val;
      if(!to_integer(to_constant_expr(slice), idx_val) && idx_val < 0)
        effective_idx = plus_exprt{length, slice};
    }
    else
    {
      // Runtime: if idx < 0 then idx + length else idx
      effective_idx = if_exprt{
        binary_relation_exprt{slice, ID_lt, safe_zero(slice.type())},
        plus_exprt{length, slice},
        slice};
    }

    add_check(
      and_exprt{
        binary_relation_exprt{
          effective_idx, ID_ge, safe_zero(effective_idx.type())},
        binary_relation_exprt{effective_idx, ID_lt, length}},
      "index-out-of-bounds",
      "list index out of range",
      get_location(expr));

    const auto &st = to_struct_type(value.type());
    const auto &data_type = to_array_type(st.components()[1].type());
    member_exprt data{value, "data", data_type};
    return index_exprt{data, effective_idx};
  }

  // Tuple indexing with constant index
  if(is_python_tuple_type(value.type()))
  {
    if(slice.is_constant())
    {
      mp_integer idx;
      if(!to_integer(to_constant_expr(slice), idx))
      {
        const auto &st = to_struct_type(value.type());
        std::string field = "_" + integer2string(idx);
        if(st.has_component(field))
          return member_exprt{value, field, st.get_component(field).type()};
      }
    }
    log.error() << "Tuple indexing requires a constant index" << messaget::eom;
    return nil_exprt{};
  }

  // Tagged union subscript: unwrap to list and index
  if(is_python_value_type(value.type()))
  {
    exprt list_val = python_value_list(value);
    if(is_python_list_type(list_val.type()))
    {
      const auto &list_st = to_struct_type(list_val.type());
      const auto &data_type = to_array_type(list_st.components()[1].type());
      member_exprt data{list_val, "data", data_type};
      return index_exprt{data, slice};
    }
  }

  log_overapprox("Subscript: unsupported operand type, using nondet");
  return nil_exprt{};
}

// PLR §6.2.5: List, set and tuple displays
// "A tuple display yields a new tuple object."
exprt python_convertert::convert_tuple(const jsont &expr)
{
  const jsont &elts = json_member(expr, "elts");
  if(!elts.is_array())
    return nil_exprt{};

  exprt::operandst elements;
  std::vector<typet> element_types;
  for(const auto &elt : as_array(elts))
  {
    exprt e = convert_expression(elt);
    if(e.is_nil())
      return nil_exprt{};
    element_types.push_back(e.type());
    elements.push_back(e);
  }

  struct_typet tuple_type = python_tuple_type(element_types);
  return struct_exprt{std::move(elements), tuple_type};
}

// PLR §6.2.5: List displays
// "A list display yields a new list object, the contents being specified
// by either a list of expressions or a comprehension."
exprt python_convertert::convert_list(const jsont &expr)
{
  const jsont &elts = json_member(expr, "elts");
  if(!elts.is_array())
    return nil_exprt{};

  // Collect elements and determine element type from first element
  exprt::operandst elements;
  for(const auto &elt : as_array(elts))
  {
    exprt e = convert_expression(elt);
    if(e.is_nil())
      return nil_exprt{};
    elements.push_back(e);
  }

  if(elements.empty())
  {
    // Empty list — default to int element type
    struct_typet list_type = python_list_type(python_int_type());
    exprt length = from_integer(0, python_int_type());
    array_typet data_type{
      python_int_type(),
      from_integer(PYTHON_MAX_LIST_LENGTH, python_int_type())};
    exprt::operandst zeros;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
      zeros.push_back(from_integer(0, python_int_type()));
    array_exprt data{std::move(zeros), data_type};
    return struct_exprt{{length, data}, list_type};
  }

  typet elem_type = elements[0].type();
  // Check for mixed types — use tagged union for heterogeneous lists
  bool is_heterogeneous = false;
  for(const auto &e : elements)
  {
    if(e.type() != elem_type)
    {
      is_heterogeneous = true;
      elem_type = python_value_type();
      break;
    }
  }
  // The backing array has a fixed maximum size (PYTHON_MAX_LIST_LENGTH);
  // however, list literals in stdlib code can exceed it (e.g.
  // typing.py's 100+-entry '__all__'). Grow the array size to the
  // number of elements whenever that exceeds the default so the
  // resulting IR is self-consistent (operands count == array size).
  const std::size_t list_array_size =
    std::max<std::size_t>(PYTHON_MAX_LIST_LENGTH, elements.size());
  struct_typet list_type = python_list_type(elem_type);
  // Rebuild the struct's array-typed 'data' component to match the
  // actual literal size. python_list_type always returns the struct
  // with the default max length, so override the data component's
  // array type here.
  {
    auto &comps = list_type.components();
    if(comps.size() == 2)
      comps[1].type() = array_typet{
        elem_type, from_integer(list_array_size, python_int_type())};
  }
  array_typet data_type{
    elem_type, from_integer(list_array_size, python_int_type())};

  // Build data array: elements followed by zeros
  exprt::operandst data_elems;
  for(auto &e : elements)
  {
    if(is_heterogeneous)
      e = wrap_value(e);
    else if(e.type() != elem_type)
      e = typecast_exprt{e, elem_type};
    data_elems.push_back(e);
  }
  while(data_elems.size() < list_array_size)
    data_elems.push_back(safe_zero(elem_type));

  array_exprt data{std::move(data_elems), data_type};
  exprt length =
    from_integer(static_cast<long long>(elements.size()), python_int_type());

  return struct_exprt{{length, data}, list_type};
}

// PLR §6.3.1: Attribute references
// "An attribute reference is a primary followed by a period and a name."
exprt python_convertert::convert_attribute(const jsont &expr)
{
  std::string attr = json_string(json_member(expr, "attr"));

  // Math module constants: math.pi, math.e, etc.
  // Check BEFORE converting value (which would fail for module names)
  if(
    json_member(expr, "value").is_object() &&
    is_node_type(json_member(expr, "value"), "Name"))
  {
    std::string obj_name =
      json_string(json_member(json_member(expr, "value"), "id"));
    if(obj_name == "math")
    {
      if(attr == "pi")
        return double_to_floatbv(M_PI);
      if(attr == "e")
        return double_to_floatbv(M_E);
      if(attr == "tau")
        return double_to_floatbv(2.0 * M_PI);
      if(attr == "inf")
      {
        ieee_floatt v{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        v.make_plus_infinity();
        return v.to_expr();
      }
      if(attr == "nan")
      {
        ieee_floatt v{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        v.make_NaN();
        return v.to_expr();
      }
    }
  }

  exprt value = convert_expression(json_member(expr, "value"));

  if(value.is_nil())
    return nil_exprt{};

  // If value is a pointer (self in a method), dereference first
  if(value.type().id() == ID_pointer)
  {
    const auto &base = to_pointer_type(value.type()).base_type();
    if(base.id() == ID_struct)
    {
      const auto &st = to_struct_type(base);
      if(st.has_component(attr))
        return member_exprt{
          dereference_exprt{value}, attr, st.get_component(attr).type()};
    }
  }

  // For struct types (classes), access the member directly.
  // PLR §3.3.2: an @property-decorated method is accessed
  // like a field — emit the call with self as sole arg.
  if(value.type().id() == ID_struct)
  {
    const auto &st = to_struct_type(value.type());
    std::string stag = id2string(st.get_tag());
    if(stag.substr(0, 13) == "python_class_")
    {
      std::string cls = stag.substr(13);
      auto pit = class_property_methods.find(cls);
      if(pit != class_property_methods.end() && pit->second.count(attr))
      {
        irep_idt mid{"python::" + cls + "::" + attr};
        const symbolt *msym = symbol_table.lookup(mid);
        if(msym != nullptr && msym->type.id() == ID_code)
        {
          const code_typet &mty = to_code_type(msym->type);
          // Materialise into a tmp so the ASSIGN (via
          // pending_checks) is visible to subsequent
          // statements. A bare side_effect_expr_function_callt
          // return was getting dropped in some assignment
          // paths.
          static unsigned prop_ctr = 0;
          std::string tn = "__prop_" + std::to_string(prop_ctr++);
          std::string tq = qualify_name(tn);
          irep_idt ti{tq};
          if(symbol_table.lookup(ti) == nullptr)
          {
            symbolt ts{ti, mty.return_type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            symbol_table.add(ts);
          }
          symbol_exprt tv = symbol_table.lookup_ref(ti).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            tv,
            side_effect_expr_function_callt{
              msym->symbol_expr(),
              {address_of_exprt{value}},
              mty.return_type(),
              get_location(expr)}});
          return std::move(tv);
        }
      }
    }
    if(st.has_component(attr))
      return member_exprt{value, attr, st.get_component(attr).type()};
  }

  // Attribute accesses on an already-opaque tagged python_value
  // base are the common case for imported module symbols and
  // unannotated parameters. The only information we can return is
  // a fresh nondet, so log at debug level (or warning level if
  // --python-strict-warnings is set) rather than falling through
  // to the louder 'attribute ...' path.
  const bool base_is_module_value =
    (value.type().id() == ID_struct_tag &&
     id2string(to_struct_tag_type(value.type()).get_identifier()) ==
       std::string{PYTHON_VALUE_TAG});
  if(base_is_module_value)
  {
    // If any known user class has this attribute, dereference
    // __class_ptr through its struct type and read the field.
    // We pick the first matching class; when multiple classes
    // share the attribute name the result's type comes from
    // the first. Callers that need precision should narrow
    // with isinstance first.
    for(const auto &[cls_name, cls_type] : class_types)
    {
      if(!cls_type.has_component(attr))
        continue;
      const typet &field_type = cls_type.get_component(attr).type();
      exprt class_ptr = python_value_class_ptr(value);
      pointer_typet cls_ptr_type{cls_type, 64};
      dereference_exprt deref{
        typecast_exprt{class_ptr, cls_ptr_type}, cls_type};
      return member_exprt{std::move(deref), attr, field_type};
    }
    log_overapprox("attribute '" + attr + "': using nondet over-approximation");
    return side_effect_expr_nondett{python_int_type(), source_locationt{}};
  }

  log_overapprox("attribute '" + attr + "': using nondet over-approximation");
  return side_effect_expr_nondett{python_int_type(), source_locationt{}};
}

// PLR §6.2.7: Dictionary displays
// "A dictionary display yields a new dictionary object."
exprt python_convertert::convert_dict(const jsont &expr)
{
  const jsont &keys = json_member(expr, "keys");
  const jsont &values = json_member(expr, "values");

  if(!keys.is_array() || !values.is_array())
    return nil_exprt{};

  // Collect key-value pairs
  std::vector<std::pair<exprt, exprt>> pairs;
  auto key_it = as_array(keys).begin();
  auto val_it = as_array(values).begin();
  for(; key_it != as_array(keys).end(); ++key_it, ++val_it)
  {
    exprt k = convert_expression(*key_it);
    exprt v = convert_expression(*val_it);
    if(k.is_nil() || v.is_nil())
      continue;
    pairs.emplace_back(k, v);
  }

  // Determine key/value types from first pair
  typet key_type = pairs.empty() ? python_string_type() : pairs[0].first.type();
  typet val_type = pairs.empty() ? python_int_type() : pairs[0].second.type();

  struct_typet dict_type = python_dict_type(key_type, val_type);
  const auto &keys_arr_type = to_array_type(dict_type.components()[1].type());
  const auto &vals_arr_type = to_array_type(dict_type.components()[2].type());

  // Build keys array
  exprt::operandst key_elems;
  for(const auto &p : pairs)
  {
    exprt k = p.first;
    if(k.type() != key_type)
      k = safe_typecast(k, key_type);
    key_elems.push_back(k);
  }
  while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
    key_elems.push_back(safe_zero(key_type));

  // Build values array
  exprt::operandst val_elems;
  for(const auto &p : pairs)
  {
    exprt v = p.second;
    if(v.type() != val_type)
      v = safe_typecast(v, val_type);
    val_elems.push_back(v);
  }
  while(val_elems.size() < PYTHON_MAX_DICT_SIZE)
    val_elems.push_back(safe_zero(val_type));

  exprt length =
    from_integer(static_cast<long long>(pairs.size()), signedbv_typet{64});

  return struct_exprt{
    {length,
     array_exprt{std::move(key_elems), keys_arr_type},
     array_exprt{std::move(val_elems), vals_arr_type}},
    dict_type};
}

// PLR §6.2.5: List displays
// "A list display yields a new list object, the contents being specified
// by either a list of expressions or a comprehension."
// PLR §6.2.8: Comprehension displays
// "A comprehension consists of a single expression followed by at least
// one for clause and zero or more for or if clauses."
exprt python_convertert::convert_list_comp(const jsont &expr)
{
  const jsont &elt = json_member(expr, "elt");
  const jsont &generators = json_member(expr, "generators");

  if(!generators.is_array() || as_array(generators).empty())
    return nil_exprt{};

  // Collect all generators (support nested for)
  struct gen_info
  {
    std::string var_name;
    std::vector<const jsont *> values;
    std::vector<exprt> const_values;
  };
  std::vector<gen_info> gens;

  for(const auto &gen : as_array(generators))
  {
    const jsont &gen_iter = json_member(gen, "iter");
    gen_info gi;
    gi.var_name = json_string(json_member(json_member(gen, "target"), "id"));
    // Literal-list form: [f(x) for x in [1, 2, 3]]
    if(is_node_type(gen_iter, "List"))
    {
      const jsont &elts = json_member(gen_iter, "elts");
      if(elts.is_array())
      {
        for(const auto &e : as_array(elts))
          gi.values.push_back(&e);
      }
    }
    // Name form: xs = [1, 2, 3]; [f(x) for x in xs]
    // Resolve via list_literals if the name points to a
    // tracked literal list.
    else if(is_node_type(gen_iter, "Name"))
    {
      std::string iter_name = json_string(json_member(gen_iter, "id"));
      auto it = list_literals.find(irep_idt{qualify_name(iter_name)});
      if(it != list_literals.end())
      {
        // The list_literal is a struct_exprt with [length,
        // data]. data is an array_exprt whose operands are
        // the values. But we need JSON pointers for the same
        // values — we can't plug non-JSON exprt values into
        // the gens structure.
        // Build synthetic JSON Constant nodes? Too complex.
        // Instead, just take the values directly into
        // a secondary 'const_values' stream.
        const exprt &list_struct = it->second;
        if(list_struct.operands().size() >= 2)
        {
          const exprt &length_expr = list_struct.operands()[0];
          if(length_expr.is_constant())
          {
            mp_integer len;
            if(!to_integer(to_constant_expr(length_expr), len))
            {
              const exprt &data_arr = list_struct.operands()[1];
              for(mp_integer i = 0; i < len; ++i)
              {
                auto idx = i.to_ulong();
                if(idx < data_arr.operands().size())
                  gi.const_values.push_back(data_arr.operands()[idx]);
              }
            }
          }
        }
      }
      if(gi.values.empty() && gi.const_values.empty())
      {
        log.warning() << "List comprehension iterable '" << iter_name
                      << "' is not a known literal list" << messaget::eom;
        return nil_exprt{};
      }
    }
    else
    {
      log.warning() << "List comprehension only supports literal list iterables"
                    << messaget::eom;
      return nil_exprt{};
    }
    gens.push_back(std::move(gi));
  }

  if(gens.empty())
    return nil_exprt{};

  // Create symbols for all iteration variables
  for(auto &gi : gens)
  {
    std::string qname = qualify_name(gi.var_name);
    irep_idt sym_id{qname};
    if(symbol_table.lookup(sym_id) == nullptr)
    {
      symbolt sym{sym_id, python_int_type(), "python"};
      sym.base_name = gi.var_name;
      sym.is_lvalue = true;
      sym.is_state_var = true;
      symbol_table.add(sym);
    }
  }

  // Unroll all combinations
  // For single generator: iterate values
  // For nested: iterate cartesian product
  std::vector<std::vector<std::size_t>> combos;
  combos.push_back({});
  for(const auto &gi : gens)
  {
    std::vector<std::vector<std::size_t>> new_combos;
    // Use whichever source of values is populated.
    std::size_t n =
      gi.values.empty() ? gi.const_values.size() : gi.values.size();
    for(const auto &combo : combos)
    {
      for(std::size_t i = 0; i < n; i++)
      {
        auto new_combo = combo;
        new_combo.push_back(i);
        new_combos.push_back(std::move(new_combo));
      }
    }
    combos = std::move(new_combos);
  }

  // Evaluate elt for each combination
  exprt::operandst elements;
  for(const auto &combo : combos)
  {
    // Convert each iteration variable's value
    std::vector<std::pair<irep_idt, exprt>> bindings;
    for(std::size_t g = 0; g < gens.size(); g++)
    {
      exprt val;
      if(!gens[g].values.empty())
        val = convert_expression(*gens[g].values[combo[g]]);
      else
        val = gens[g].const_values[combo[g]];
      bindings.push_back({irep_idt{qualify_name(gens[g].var_name)}, val});
    }

    // Evaluate elt and substitute
    exprt elt_expr = convert_expression(elt);
    for(const auto &[sym_id, val] : bindings)
    {
      std::function<void(exprt &)> subst = [&](exprt &e)
      {
        if(e.id() == ID_symbol && to_symbol_expr(e).get_identifier() == sym_id)
          e = val;
        else
          for(auto &op : e.operands())
            subst(op);
      };
      subst(elt_expr);
    }
    // Check if conditions (filters) for this combination
    bool passes_filter = true;
    for(std::size_t g = 0; g < gens.size() && passes_filter; g++)
    {
      const jsont &gen = *std::next(as_array(generators).begin(), g);
      const jsont &ifs = json_member(gen, "ifs");
      if(ifs.is_array())
      {
        for(const auto &cond_json : as_array(ifs))
        {
          exprt cond_expr = convert_expression(cond_json);
          for(const auto &[sym_id, val] : bindings)
          {
            std::function<void(exprt &)> subst2 = [&](exprt &e)
            {
              if(
                e.id() == ID_symbol &&
                to_symbol_expr(e).get_identifier() == sym_id)
                e = val;
              else
                for(auto &op : e.operands())
                  subst2(op);
            };
            subst2(cond_expr);
          }
          // Try constant evaluation: check if all operands are constant
          // and the result can be computed
          // Try constant evaluation after substitution
          // Recursively simplify the expression
          std::function<exprt(const exprt &)> try_eval =
            [&](const exprt &e) -> exprt
          {
            if(e.is_constant())
              return e;
            // Simplify binary ops with constant operands
            if(e.operands().size() == 2)
            {
              exprt l = try_eval(e.operands()[0]);
              exprt r = try_eval(e.operands()[1]);
              if(
                l.is_constant() && r.is_constant() &&
                l.type().id() == ID_signedbv)
              {
                mp_integer a, b;
                if(
                  !to_integer(to_constant_expr(l), a) &&
                  !to_integer(to_constant_expr(r), b))
                {
                  if(e.id() == ID_mod && b != 0)
                    return from_integer(a % b, l.type());
                  if(e.id() == ID_plus)
                    return from_integer(a + b, l.type());
                  if(e.id() == ID_minus)
                    return from_integer(a - b, l.type());
                  if(e.id() == ID_mult)
                    return from_integer(a * b, l.type());
                  if(e.id() == ID_bitxor)
                  {
                    long long av = a.to_long(), bv = b.to_long();
                    return from_integer(av ^ bv, l.type());
                  }
                  if(e.id() == ID_div && b != 0)
                    return from_integer(a / b, l.type());
                  if(e.id() == ID_equal)
                    return a == b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_notequal)
                    return a != b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_lt)
                    return a < b ? static_cast<exprt>(true_exprt{})
                                 : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_gt)
                    return a > b ? static_cast<exprt>(true_exprt{})
                                 : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_le)
                    return a <= b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                  if(e.id() == ID_ge)
                    return a >= b ? static_cast<exprt>(true_exprt{})
                                  : static_cast<exprt>(false_exprt{});
                }
              }
            }
            // if_exprt: evaluate condition
            if(e.id() == ID_if && e.operands().size() == 3)
            {
              exprt c = try_eval(e.operands()[0]);
              if(c.is_true())
                return try_eval(e.operands()[1]);
              if(c.is_false())
                return try_eval(e.operands()[2]);
            }
            // and/or
            if(e.id() == ID_and && e.operands().size() == 2)
            {
              exprt l = try_eval(e.operands()[0]);
              exprt r = try_eval(e.operands()[1]);
              if(l.is_false() || r.is_false())
                return false_exprt{};
              if(l.is_true() && r.is_true())
                return true_exprt{};
            }
            if(e.id() == ID_not && e.operands().size() == 1)
            {
              exprt o = try_eval(e.operands()[0]);
              if(o.is_true())
                return false_exprt{};
              if(o.is_false())
                return true_exprt{};
            }
            return e;
          };
          exprt simplified = try_eval(cond_expr);
          if(simplified.is_false())
            passes_filter = false;
        }
      }
    }
    if(!passes_filter)
      continue;
    elements.push_back(elt_expr);
  }

  if(elements.empty())
    return nil_exprt{};

  // Build the result list
  typet elem_type = elements[0].type();
  struct_typet list_type = python_list_type(elem_type);
  array_typet data_type{
    elem_type, from_integer(PYTHON_MAX_LIST_LENGTH, python_int_type())};

  exprt::operandst data_elems;
  for(auto &e : elements)
  {
    if(e.type() != elem_type)
      e = typecast_exprt{e, elem_type};
    data_elems.push_back(e);
  }
  while(data_elems.size() < PYTHON_MAX_LIST_LENGTH)
    data_elems.push_back(safe_zero(elem_type));

  array_exprt data{std::move(data_elems), data_type};
  exprt length =
    from_integer(static_cast<long long>(elements.size()), python_int_type());

  return struct_exprt{{length, data}, list_type};
}

// PLR §6.2.7: dict comprehension '{k: v for x in xs}'. Shares the
// generator-unrolling logic with convert_list_comp; builds a
// python_dict struct (length, keys array, values array) at the end.
exprt python_convertert::convert_dict_comp(const jsont &expr)
{
  const jsont &key_expr_json = json_member(expr, "key");
  const jsont &val_expr_json = json_member(expr, "value");
  const jsont &generators = json_member(expr, "generators");

  if(!generators.is_array() || as_array(generators).empty())
    return nil_exprt{};

  // Collect generators: supported iterables are literal lists and
  // range() calls with constant-integer arguments. Anything else
  // falls back to a nondet dict over-approximation.
  struct gen_info
  {
    std::string var_name;
    // Either a list of jsont pointers from a literal [ ... ] or a
    // list of pre-computed integer values from a range().
    std::vector<const jsont *> json_values;
    std::vector<mp_integer> int_values;
    bool is_range = false;
  };
  std::vector<gen_info> gens;

  // Helper: try to evaluate range(a[, b[, c]]) with constant ints.
  auto try_range = [&](const jsont &call, std::vector<mp_integer> &out) -> bool
  {
    if(!is_node_type(call, "Call"))
      return false;
    const jsont &func = json_member(call, "func");
    if(!is_node_type(func, "Name"))
      return false;
    if(json_string(json_member(func, "id")) != "range")
      return false;
    const jsont &args_n = json_member(call, "args");
    if(!args_n.is_array() || as_array(args_n).empty())
      return false;
    std::vector<mp_integer> ints;
    for(const auto &a : as_array(args_n))
    {
      exprt av = convert_expression(a);
      if(!av.is_constant() || av.type().id() != ID_signedbv)
        return false;
      mp_integer v;
      if(to_integer(to_constant_expr(av), v))
        return false;
      ints.push_back(v);
    }
    mp_integer start{0}, stop, step{1};
    if(ints.size() == 1)
      stop = ints[0];
    else if(ints.size() == 2)
    {
      start = ints[0];
      stop = ints[1];
    }
    else if(ints.size() == 3)
    {
      start = ints[0];
      stop = ints[1];
      step = ints[2];
    }
    else
      return false;
    if(step == 0)
      return false;
    out.clear();
    if(step > 0)
    {
      for(mp_integer i = start; i < stop; i += step)
        out.push_back(i);
    }
    else
    {
      for(mp_integer i = start; i > stop; i += step)
        out.push_back(i);
    }
    return true;
  };

  for(const auto &gen : as_array(generators))
  {
    const jsont &gen_iter = json_member(gen, "iter");
    gen_info gi;
    gi.var_name = json_string(json_member(json_member(gen, "target"), "id"));
    std::vector<mp_integer> r_values;
    if(is_node_type(gen_iter, "List"))
    {
      const jsont &elts = json_member(gen_iter, "elts");
      if(elts.is_array())
      {
        for(const auto &e : as_array(elts))
          gi.json_values.push_back(&e);
      }
    }
    else if(try_range(gen_iter, r_values))
    {
      gi.is_range = true;
      gi.int_values = std::move(r_values);
    }
    else
    {
      log_overapprox(
        "dict comprehension with non-literal iterable: using nondet dict");
      return side_effect_expr_nondett{
        python_dict_type(python_value_type(), python_value_type()),
        source_locationt{}};
    }
    gens.push_back(std::move(gi));
  }

  // Register iteration-variable symbols.
  for(auto &gi : gens)
  {
    std::string qname = qualify_name(gi.var_name);
    irep_idt sym_id{qname};
    if(symbol_table.lookup(sym_id) == nullptr)
    {
      symbolt sym{sym_id, python_int_type(), "python"};
      sym.base_name = gi.var_name;
      sym.is_lvalue = true;
      sym.is_state_var = true;
      symbol_table.add(sym);
    }
  }

  // Cartesian-product unroll.
  std::vector<std::vector<std::size_t>> combos{{}};
  for(const auto &gi : gens)
  {
    const std::size_t n =
      gi.is_range ? gi.int_values.size() : gi.json_values.size();
    std::vector<std::vector<std::size_t>> next;
    for(const auto &combo : combos)
      for(std::size_t i = 0; i < n; i++)
      {
        auto c = combo;
        c.push_back(i);
        next.push_back(std::move(c));
      }
    combos = std::move(next);
  }

  // Evaluate (key, value) pairs for each combination, applying ifs.
  std::vector<std::pair<exprt, exprt>> pairs;
  for(const auto &combo : combos)
  {
    std::vector<std::pair<irep_idt, exprt>> bindings;
    for(std::size_t g = 0; g < gens.size(); g++)
    {
      exprt v;
      if(gens[g].is_range)
        v = from_integer(gens[g].int_values[combo[g]], python_int_type());
      else
        v = convert_expression(*gens[g].json_values[combo[g]]);
      bindings.push_back({irep_idt{qualify_name(gens[g].var_name)}, v});
    }

    std::function<void(exprt &)> subst = [&](exprt &e)
    {
      for(const auto &[sym_id, val] : bindings)
        if(e.id() == ID_symbol && to_symbol_expr(e).get_identifier() == sym_id)
        {
          e = val;
          return;
        }
      for(auto &op : e.operands())
        subst(op);
    };

    // Check filter 'if' clauses.
    bool passes = true;
    for(std::size_t g = 0; g < gens.size() && passes; g++)
    {
      const jsont &gen = *std::next(as_array(generators).begin(), g);
      const jsont &ifs = json_member(gen, "ifs");
      if(ifs.is_array())
      {
        for(const auto &cond_json : as_array(ifs))
        {
          exprt cond = convert_expression(cond_json);
          subst(cond);
          // Constant-fold obvious cases.
          if(cond.is_false())
          {
            passes = false;
            break;
          }
        }
      }
    }
    if(!passes)
      continue;

    exprt k = convert_expression(key_expr_json);
    exprt v = convert_expression(val_expr_json);
    subst(k);
    subst(v);
    pairs.emplace_back(std::move(k), std::move(v));
  }

  // Choose key/value types from the first pair (fall back to generic).
  typet key_type =
    pairs.empty() ? python_string_type() : pairs.front().first.type();
  typet val_type =
    pairs.empty() ? python_int_type() : pairs.front().second.type();
  struct_typet dict_type = python_dict_type(key_type, val_type);
  const auto &keys_arr_type = to_array_type(dict_type.components()[1].type());
  const auto &vals_arr_type = to_array_type(dict_type.components()[2].type());

  exprt::operandst key_elems, val_elems;
  for(auto &p : pairs)
  {
    if(p.first.type() != key_type)
      p.first = safe_typecast(p.first, key_type);
    if(p.second.type() != val_type)
      p.second = safe_typecast(p.second, val_type);
    key_elems.push_back(p.first);
    val_elems.push_back(p.second);
  }
  while(key_elems.size() < PYTHON_MAX_DICT_SIZE)
    key_elems.push_back(safe_zero(key_type));
  while(val_elems.size() < PYTHON_MAX_DICT_SIZE)
    val_elems.push_back(safe_zero(val_type));

  exprt length =
    from_integer(static_cast<long long>(pairs.size()), signedbv_typet{64});

  return struct_exprt{
    {length,
     array_exprt{std::move(key_elems), keys_arr_type},
     array_exprt{std::move(val_elems), vals_arr_type}},
    dict_type};
}

// --- Statement conversion ---

codet python_convertert::convert_statement(const jsont &stmt)
{
  std::string node_type = json_string(json_member(stmt, "_type"));

  // Clear pending checks before converting this statement
  pending_checks.clear();

  codet result = code_skipt{};

  if(node_type == "AnnAssign")
    result = convert_ann_assign(stmt);
  else if(node_type == "Assign")
    result = convert_assign(stmt);
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

          // Register known math functions
          if(module == "math")
          {
            // library/math.py is loaded via the ImportFrom
            // library-resolve path (see Pass 0.2 at the top of
            // this function). Here we only handle the module
            // constants (pi, e) as static double symbols; the
            // function entries are populated by the library's
            // @c_intrinsic decorators.
            // Constants: register as global variables
            if(name == "pi" || name == "e")
            {
              irep_idt sym_id{"python::" + asname};
              if(symbol_table.lookup(sym_id) == nullptr)
              {
                symbolt sym{sym_id, double_type(), "python"};
                sym.base_name = asname;
                sym.is_lvalue = true;
                sym.is_state_var = true;
                sym.is_static_lifetime = true;
                // pi ≈ 3.14159, e ≈ 2.71828 — use nondet with constraints
                sym.value =
                  side_effect_expr_nondett{double_type(), source_locationt{}};
                symbol_table.add(sym);
              }
              continue;
            }
            // Other math functions: handled by the library's
            // @c_intrinsic decorators; no registration here.
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
            module == "numpy" || module == "pandas" ||
            module == "sqlalchemy" || module == "click" || module == "pytest")
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
          }
        }
      }
    }
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
                key = safe_typecast(key, keys_type.element_type());

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
                exprt match = equal_exprt{index_exprt{keys_arr, idx}, key};
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
    std::function<std::pair<exprt, code_blockt>(const jsont &, const exprt &)>
      compile_pattern = [&](const jsont &pat, const exprt &subj)
      -> std::pair<exprt, code_blockt>
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
            length, from_integer((long long)pat_arr.size(), signedbv_typet{64})};
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
            }
          }
          // Match suffix against the last `suffix` elements.
          for(std::size_t j = 0; j < suffix; j++)
          {
            exprt idx = minus_exprt{
              length,
              from_integer(
                (long long)(suffix - j), signedbv_typet{64})};
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
            key_expr = safe_typecast(key_expr, keys_type.element_type());
          // Search for the key in dict.keys. Build condition
          // "key is present AND its value matches pattern".
          exprt key_found = false_exprt{};
          exprt val_match = true_exprt{};
          for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
          {
            exprt idx = from_integer(i, signedbv_typet{64});
            exprt in_range = binary_relation_exprt{idx, ID_lt, length};
            exprt key_eq = equal_exprt{index_exprt{dkeys, idx}, key_expr};
            exprt slot_match = and_exprt{in_range, std::move(key_eq)};
            key_found = or_exprt{std::move(key_found), slot_match};
            // Value pattern match: if key is at this slot, val
            // must match. OR together.
          }
          // For the binding part, recursive compile_pattern on
          // the slot value. Approximate by picking slot 0 when
          // key_found — precise matching would need per-slot
          // dispatch.
          exprt val_expr = index_exprt{dvals, from_integer(0, signedbv_typet{64})};
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

    for(auto it = case_list.rbegin(); it != case_list.rend(); ++it)
    {
      const jsont &match_case = **it;
      const jsont &pattern = json_member(match_case, "pattern");
      const jsont &guard = json_member(match_case, "guard");
      const jsont &body = json_member(match_case, "body");

      auto [cond, binds] = compile_pattern(pattern, subject);
      code_blockt body_block;
      if(body.is_array())
      {
        for(const auto &s : as_array(body))
          body_block.add(convert_statement(s));
      }
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
    auto pending_sets_exception = [](const code_blockt &b) {
      std::function<bool(const exprt &)> has_exc_assign =
        [&](const exprt &e) -> bool {
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
    return std::move(block);
  }

  return result;
}

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

    typet obj_type = obj.type();
    if(obj_type.id() == ID_pointer)
    {
      const auto &base = to_pointer_type(obj_type).base_type();
      if(base.id() == ID_struct)
      {
        const auto &st = to_struct_type(base);
        if(st.has_component(attr))
        {
          member_exprt lhs{
            dereference_exprt{obj}, attr, st.get_component(attr).type()};
          if(rhs.type() != lhs.type())
            rhs = safe_typecast(rhs, lhs.type());
          code_frontend_assignt assign{lhs, rhs};
          assign.add_source_location() = loc;
          return std::move(assign);
        }
      }
    }
    return code_skipt{};
  }

  std::string var_name = json_string(json_member(target, "id"));
  typet var_type = convert_type_annotation(annotation);

  std::string qualified_name = qualify_name(var_name);
  irep_idt symbol_id{qualified_name};

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

  if(value.is_null())
    return code_skipt{};

  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  // Function alias: g: Callable = double → record alias
  if(rhs.id() == ID_symbol && rhs.type().id() == ID_code)
  {
    function_aliases[qualified_name] = to_symbol_expr(rhs).get_identifier();
    return code_skipt{};
  }

  // If annotation gave a placeholder type (e.g., dict→int) but the RHS
  // has a concrete struct type, use the RHS type instead.
  const symbolt &sym = symbol_table.lookup_ref(symbol_id);
  if(
    sym.type != rhs.type() && rhs.type().id() == ID_struct &&
    (sym.type == python_int_type() || sym.type.id() != ID_struct ||
     (is_python_list_type(sym.type) && is_python_list_type(rhs.type())) ||
     (is_python_dict_type(sym.type) && is_python_dict_type(rhs.type()))))
  {
    symbol_table.get_writeable_ref(symbol_id).type = rhs.type();
  }

  const symbolt &sym2 = symbol_table.lookup_ref(symbol_id);

  // Type cast if needed
  if(rhs.type() != sym2.type)
    rhs = safe_typecast(rhs, sym2.type);

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
    if(rhs.id() == ID_struct)
      dict_literals[symbol_id] = rhs;
    else
      dict_literals.erase(symbol_id);
  }
  if(is_python_list_type(rhs.type()))
  {
    if(rhs.id() == ID_struct)
      list_literals[symbol_id] = rhs;
    else
      list_literals.erase(symbol_id);
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

            irep_idt init_id{"python::" + call_name + "::__init__"};
            const symbolt *init_sym = symbol_table.lookup(init_id);
            if(init_sym != nullptr)
            {
              exprt::operandst init_args;
              init_args.push_back(address_of_exprt{tmp_sym.symbol_expr()});
              const jsont &call_args = json_member(value, "args");
              if(call_args.is_array())
              {
                for(const auto &a : as_array(call_args))
                  init_args.push_back(convert_expression(a));
              }
              // Match argument types to parameter types
              const auto &init_params =
                to_code_type(init_sym->type).parameters();
              for(std::size_t ai = 0;
                  ai < init_args.size() && ai < init_params.size();
                  ai++)
              {
                if(init_args[ai].type() != init_params[ai].type())
                  init_args[ai] =
                    safe_typecast(init_args[ai], init_params[ai].type());
              }
              side_effect_expr_function_callt call{
                init_sym->symbol_expr(),
                std::move(init_args),
                empty_typet{},
                loc};
              result.add(code_expressiont{call});
            }

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

        const symbolt &var_sym = symbol_table.lookup_ref(symbol_id);
        code_blockt result;

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

        // Call __init__(&var, args...)
        irep_idt init_id{"python::" + call_name + "::__init__"};
        const symbolt *init_sym = symbol_table.lookup(init_id);
        if(init_sym != nullptr)
        {
          exprt::operandst arguments;
          arguments.push_back(address_of_exprt{var_sym.symbol_expr()});

          const jsont &call_args = json_member(value, "args");
          if(call_args.is_array())
          {
            for(const auto &arg : as_array(call_args))
              arguments.push_back(convert_expression(arg));
          }

          // Handle keyword arguments
          const code_typet &init_type = to_code_type(init_sym->type);
          const jsont &kw_args = json_member(value, "keywords");
          if(kw_args.is_array())
          {
            for(const auto &kw : as_array(kw_args))
            {
              std::string kw_name = json_string(json_member(kw, "arg"));
              exprt kw_val = convert_expression(json_member(kw, "value"));
              for(std::size_t pi = 0; pi < init_type.parameters().size(); pi++)
              {
                if(
                  id2string(init_type.parameters()[pi].get_base_name()) ==
                  kw_name)
                {
                  while(arguments.size() <= pi)
                    arguments.push_back(nil_exprt{});
                  arguments[pi] = kw_val;
                  break;
                }
              }
            }
          }

          // Pad missing arguments with defaults (safe_zero for each param type)
          while(arguments.size() < init_type.parameters().size())
          {
            std::size_t idx = arguments.size();
            arguments.push_back(safe_zero(init_type.parameters()[idx].type()));
          }
          // Replace nil entries (gaps from keyword matching) with defaults
          for(std::size_t i = 0; i < arguments.size(); i++)
          {
            if(arguments[i].is_nil() && i < init_type.parameters().size())
              arguments[i] = safe_zero(init_type.parameters()[i].type());
          }

          // Typecast arguments to match parameter types
          for(std::size_t i = 0;
              i < arguments.size() && i < init_type.parameters().size();
              i++)
          {
            if(arguments[i].type() != init_type.parameters()[i].type())
              arguments[i] =
                safe_typecast(arguments[i], init_type.parameters()[i].type());
          }

          side_effect_expr_function_callt call{
            init_sym->symbol_expr(), std::move(arguments), empty_typet{}, loc};
          code_expressiont call_stmt{call};
          call_stmt.add_source_location() = loc;
          result.add(std::move(call_stmt));
        }

        if(result.statements().size() == 1)
          return result.statements().front();
        return std::move(result);
      }
    }
  }

  exprt rhs = convert_expression(value);
  if(rhs.is_nil())
    return code_skipt{};

  // Lambda/function assignment: record alias instead of creating variable
  if(rhs.id() == ID_symbol && rhs.type().id() == ID_code)
  {
    for(const auto &target : as_array(targets))
    {
      if(is_node_type(target, "Name"))
      {
        std::string var_name = json_string(json_member(target, "id"));
        function_aliases[qualify_name(var_name)] =
          to_symbol_expr(rhs).get_identifier();
      }
    }
    return code_skipt{};
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
          // Bind call arguments to closure captures
          const jsont &call_args =
            json_member(json_member(stmt, "value"), "args");
          if(call_args.is_array())
          {
            irep_idt fid{"python::" + called};
            const symbolt *fsym = symbol_table.lookup(fid);
            if(fsym != nullptr && fsym->type.id() == ID_code)
            {
              const auto &fp = to_code_type(fsym->type).parameters();
              auto ai = as_array(call_args).begin();
              for(std::size_t i = 0;
                  i < fp.size() && ai != as_array(call_args).end();
                  i++, ++ai)
              {
                exprt av = convert_expression(*ai);
                std::string pid = id2string(fp[i].get_identifier());
                auto ci = closure_captures.find(id2string(it->second));
                if(ci != closure_captures.end())
                {
                  for(auto &cap : ci->second)
                  {
                    if(std::get<0>(cap) == pid)
                    {
                      static unsigned lb = 0;
                      std::string tn = "__lam_bind_" + std::to_string(lb++);
                      std::string tq = qualify_name(tn);
                      irep_idt ti{tq};
                      if(symbol_table.lookup(ti) == nullptr)
                      {
                        symbolt ts{ti, av.type(), "python"};
                        ts.base_name = tn;
                        ts.is_lvalue = true;
                        ts.is_state_var = true;
                        ts.is_static_lifetime = true;
                        symbol_table.add(ts);
                      }
                      lam_block.add(code_frontend_assignt{
                        symbol_table.lookup_ref(ti).symbol_expr(), av});
                      std::get<0>(cap) = id2string(ti);
                    }
                  }
                }
              }
            }
          }
          for(const auto &target : as_array(targets))
          {
            if(is_node_type(target, "Name"))
            {
              std::string var_name = json_string(json_member(target, "id"));
              function_aliases[qualify_name(var_name)] = it->second;
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
            for(const auto &target : as_array(targets))
            {
              if(is_node_type(target, "Name"))
              {
                std::string var_name = json_string(json_member(target, "id"));
                std::string qname = qualify_name(var_name);
                function_aliases[qname] = method_id;
                bound_methods[qname] = {method_id, address_of_exprt{obj_expr}};
              }
            }
            return code_skipt{};
          }
        }
      }
    }
  }

  code_blockt block;

  for(const auto &target : as_array(targets))
  {
    // Handle tuple unpacking: a, b, c = expr
    if(is_node_type(target, "Tuple") || is_node_type(target, "List"))
    {
      const jsont &elts = json_member(target, "elts");
      if(elts.is_array() && is_python_tuple_type(rhs.type()))
      {
        const auto &tuple_st = to_struct_type(rhs.type());
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
    }

    // Handle subscript assignment: lst[i] = value
    // PLR §3.2: "Tuples are immutable sequences"
    if(is_node_type(target, "Subscript"))
    {
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
                typed_rhs = safe_typecast(typed_rhs, data_type.element_type());
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
                typed_key = safe_typecast(typed_key, keys_type.element_type());
              exprt typed_rhs = rhs;
              if(typed_rhs.type() != vals_type.element_type())
                typed_rhs = safe_typecast(typed_rhs, vals_type.element_type());
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
                exprt match =
                  equal_exprt{index_exprt{keys_arr, idx}, typed_key};
                code_blockt upd;
                upd.add(
                  code_frontend_assignt{index_exprt{vals_arr, idx}, typed_rhs});
                upd.add(code_frontend_assignt{found, true_exprt{}});
                block.add(
                  code_ifthenelset{and_exprt{in_range, match}, std::move(upd)});
              }
              code_blockt append;
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
                safe_typecast(outer_typed_key, keys_type.element_type());
            exprt outer_rhs = tmp_sym;
            if(outer_rhs.type() != vals_type.element_type())
              outer_rhs = safe_typecast(outer_rhs, vals_type.element_type());
            for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
            {
              exprt idx = from_integer(i, signedbv_typet{64});
              exprt in_range = binary_relation_exprt{idx, ID_lt, length};
              exprt match =
                equal_exprt{index_exprt{keys_arr, idx}, outer_typed_key};
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
              outer_rhs = safe_typecast(outer_rhs, data_type.element_type());
            block.add(code_frontend_assignt{
              index_exprt{data, outer_key}, std::move(outer_rhs)});
          }
          continue;
        }
      }
      exprt obj = convert_expression(json_member(target, "value"));

      // Tuple assignment → raise TypeError
      if(!obj.is_nil() && is_python_tuple_type(obj.type()))
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
          const auto &dict_st = to_struct_type(obj.type());
          const auto &keys_type = to_array_type(dict_st.components()[1].type());
          const auto &vals_type = to_array_type(dict_st.components()[2].type());
          member_exprt length{obj, "length", signedbv_typet{64}};
          member_exprt keys_arr{obj, "keys", keys_type};
          member_exprt vals_arr{obj, "values", vals_type};
          exprt typed_key = key;
          if(typed_key.type() != keys_type.element_type())
            typed_key = safe_typecast(typed_key, keys_type.element_type());
          exprt typed_val = rhs;
          if(typed_val.type() != vals_type.element_type())
            typed_val = safe_typecast(typed_val, vals_type.element_type());
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
            exprt match = equal_exprt{index_exprt{keys_arr, idx}, typed_key};
            code_blockt update;
            update.add(
              code_frontend_assignt{index_exprt{vals_arr, idx}, typed_val});
            update.add(code_frontend_assignt{found, true_exprt{}});
            block.add(
              code_ifthenelset{and_exprt{in_range, match}, std::move(update)});
          }
          code_blockt append;
          append.add(
            code_frontend_assignt{index_exprt{keys_arr, length}, typed_key});
          append.add(
            code_frontend_assignt{index_exprt{vals_arr, length}, typed_val});
          append.add(code_frontend_assignt{
            length, plus_exprt{length, from_integer(1, signedbv_typet{64})}});
          block.add(code_ifthenelset{not_exprt{found}, std::move(append)});
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
        index_exprt lhs{data, idx};
        exprt typed_rhs = rhs;
        if(typed_rhs.type() != data_type.element_type())
          typed_rhs = typecast_exprt{typed_rhs, data_type.element_type()};
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
        if(obj_type.id() == ID_pointer)
        {
          const auto &base = to_pointer_type(obj_type).base_type();
          if(base.id() == ID_struct)
          {
            const auto &st = to_struct_type(base);
            if(st.has_component(attr))
            {
              member_exprt lhs{
                dereference_exprt{obj}, attr, st.get_component(attr).type()};
              exprt typed_rhs = rhs;
              if(typed_rhs.type() != lhs.type())
                typed_rhs = safe_typecast(typed_rhs, lhs.type());
              code_frontend_assignt assign{lhs, typed_rhs};
              assign.add_source_location() = loc;
              block.add(std::move(assign));
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
            if(typed_rhs.type() != lhs.type())
              typed_rhs = safe_typecast(typed_rhs, lhs.type());
            code_frontend_assignt assign{lhs, typed_rhs};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
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
            member_exprt lhs{std::move(deref), attr, field_type};
            exprt typed_rhs = rhs;
            if(typed_rhs.type() != lhs.type())
              typed_rhs = safe_typecast(typed_rhs, lhs.type());
            code_frontend_assignt assign{lhs, typed_rhs};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
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

    // Check if variable exists and has a different type (type change)
    const symbolt *existing = symbol_table.lookup(symbol_id);
    // Also check the versioned symbol
    auto ver_it = variable_versions.find(qualified_name);
    if(ver_it != variable_versions.end())
      existing = symbol_table.lookup(ver_it->second);

    bool rhs_has_side_effect = rhs.id() == ID_side_effect;

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
    }
    if(is_python_dict_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
        dict_literals[sym.name] = typed_rhs;
      else
        dict_literals.erase(sym.name);
    }
    if(is_python_list_type(typed_rhs.type()))
    {
      if(typed_rhs.id() == ID_struct)
        list_literals[sym.name] = typed_rhs;
      else
        list_literals.erase(sym.name);
    }
    {
      auto ev = try_eval_double(typed_rhs);
      if(ev.has_value())
        float_constants[sym.name] = ev.value();
      else
        float_constants.erase(sym.name);
    }
    block.add(std::move(assign));
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

  // Determine the LHS expression based on target type
  exprt lhs;
  if(have_subscript_rewrite)
    lhs = subscript_lhs;
  else if(is_node_type(target, "Name"))
    lhs = convert_name(target);
  else if(is_node_type(target, "Subscript"))
    lhs = convert_subscript(target);
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
    // Non-constant: assign nondet
    return code_frontend_assignt{
      lhs, side_effect_expr_nondett{python_string_type(), source_locationt{}}};
    struct_typet str_type = python_string_struct_def();
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
      struct_typet str_type = python_string_struct_def();
      const auto &data_type = array_typet(unsignedbv_typet{8}, from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));
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
      // For now, return nondet (proper repetition needs unrolling)
      new_rhs = side_effect_expr_nondett{lhs.type(), loc};
    }
  }
  else if(op == "Add")
    new_rhs = plus_exprt{arith_lhs, rhs};
  else if(op == "Sub")
    new_rhs = minus_exprt{arith_lhs, rhs};
  else if(op == "Mult")
    new_rhs = mult_exprt{arith_lhs, rhs};
  else if(op == "FloorDiv")
    new_rhs = div_exprt{arith_lhs, rhs};
  else if(op == "Div")
  {
    // True division: result is float
    exprt fl = arith_lhs, fr = rhs;
    if(fl.type().id() != ID_floatbv)
      fl = typecast_exprt{fl, double_type()};
    if(fr.type().id() != ID_floatbv)
      fr = typecast_exprt{fr, double_type()};
    new_rhs = div_exprt{fl, fr};
  }
  else if(op == "Mod")
    new_rhs = mod_exprt{arith_lhs, rhs};
  else if(op == "BitOr")
    new_rhs = bitor_exprt{arith_lhs, rhs};
  else if(op == "BitAnd")
    new_rhs = bitand_exprt{arith_lhs, rhs};
  else if(op == "BitXor")
    new_rhs = bitxor_exprt{arith_lhs, rhs};
  else if(op == "LShift")
    new_rhs = shl_exprt{arith_lhs, rhs};
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
  code_frontend_assignt assign{lhs, new_rhs};
  assign.add_source_location() = loc;
  return std::move(assign);
}

// PLR §7.3: The assert statement
// "Assert statements are a convenient way to insert debugging assertions
// into a program."
codet python_convertert::convert_assert(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  // Ensure the test is boolean
  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  source_locationt loc = get_location(stmt);
  loc.set_property_class("assertion");
  loc.set_comment("Python assertion");

  code_assertt assertion{test};
  assertion.add_source_location() = loc;
  return std::move(assertion);
}

// PLR §8.1: The if statement
// "The if statement is used for conditional execution."
codet python_convertert::convert_if(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  // Flush pending checks from test expression evaluation
  code_blockt pre_checks;
  for(auto &check : pending_checks)
    pre_checks.add(std::move(check));
  pending_checks.clear();

  // Save version state before branches
  auto saved_versions = variable_versions;
  if_else_depth++;

  // Convert body
  code_blockt then_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      then_block.add(convert_statement(s));
  }

  // Save then-branch versions, restore for else branch
  auto then_versions = variable_versions;
  variable_versions = saved_versions;

  // Convert orelse (may be empty, elif chain, or else block)
  const jsont &orelse = json_member(stmt, "orelse");
  if(orelse.is_array() && !as_array(orelse).empty())
  {
    code_blockt else_block;
    for(const auto &s : as_array(orelse))
      else_block.add(convert_statement(s));

    // Merge: if both branches modified the same variable, keep the
    // then-branch version (the else branch's version is only live on
    // the else path, which CBMC handles via the if-then-else structure)
    variable_versions = then_versions;

    if_else_depth--;

    code_ifthenelset if_stmt{
      test, std::move(then_block), std::move(else_block)};
    if_stmt.add_source_location() = get_location(stmt);
    if(!pre_checks.statements().empty())
    {
      pre_checks.add(std::move(if_stmt));
      return std::move(pre_checks);
    }
    return std::move(if_stmt);
  }
  else
  {
    variable_versions = then_versions;
    if_else_depth--;

    code_ifthenelset if_stmt{test, std::move(then_block)};
    if_stmt.add_source_location() = get_location(stmt);
    if(!pre_checks.statements().empty())
    {
      pre_checks.add(std::move(if_stmt));
      return std::move(pre_checks);
    }
    return std::move(if_stmt);
  }
}

// PLR §8.2: The while statement
// "The while statement is used for repeated execution as long as an
// expression is true."
codet python_convertert::convert_while(const jsont &stmt)
{
  // Convert the test. Walrus (NamedExpr) inside the test
  // generates pending_checks that bind variables used by
  // the body; those bindings must re-execute every
  // iteration, not only at loop entry. We rewrite
  //
  //     while cond: body
  //
  // as
  //
  //     while true:
  //         <test-side-effects>
  //         if not cond: break
  //         body
  //
  // to put the pending_checks inside the loop body. This
  // is a no-op when the test has no side effects.
  std::vector<codet> test_side_effects;
  pending_checks.swap(test_side_effects);
  exprt test = convert_expression(json_member(stmt, "test"));
  std::vector<codet> test_pending;
  pending_checks.swap(test_pending);
  pending_checks.swap(test_side_effects); // restore outer state
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = safe_typecast(test, bool_typet{});

  code_blockt body_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  if(!test_pending.empty())
  {
    // Wrap: while(true) { side_effects; if(!test) break; body; }
    code_blockt loop_body;
    for(auto &c : test_pending)
      loop_body.add(std::move(c));
    loop_body.add(code_ifthenelset{not_exprt{test}, code_breakt{}});
    for(const auto &s : body_block.statements())
      loop_body.add(s);
    code_whilet while_stmt{true_exprt{}, std::move(loop_body)};
    while_stmt.add_source_location() = get_location(stmt);
    return std::move(while_stmt);
  }

  code_whilet while_stmt{test, std::move(body_block)};
  while_stmt.add_source_location() = get_location(stmt);
  return std::move(while_stmt);
}

// PLR §8.3: The for statement
// "The for statement is used to iterate over the elements of a
// sequence (such as a string, tuple or list) or other iterable."
// We desugar to while loops with explicit index variables.
// "The for statement is used to iterate over the elements of a sequence
// (such as a string, tuple or list) or other iterable object."
codet python_convertert::convert_for(const jsont &stmt)
{
  const jsont &target = json_member(stmt, "target");
  const jsont &iter = json_member(stmt, "iter");
  source_locationt loc = get_location(stmt);
  typet int_type = python_int_type();

  std::string var_name = json_string(json_member(target, "id"));
  // For tuple targets (for a, b in ...), use a synthetic name
  if(var_name.empty() && is_node_type(target, "Tuple"))
  {
    static unsigned tuple_iter_ctr = 0;
    var_name = "__tuple_iter_" + std::to_string(tuple_iter_ctr++);
  }
  std::string qualified_name = qualify_name(var_name);

  // Check for range() call
  if(
    is_node_type(iter, "Call") &&
    json_string(json_member(json_member(iter, "func"), "id")) == "range")
  {
    const jsont &range_args = json_member(iter, "args");
    if(!range_args.is_array() || as_array(range_args).empty())
      return code_skipt{};

    // PLib stdtypes: range(start, stop[, step])
    exprt start, stop, step;
    if(as_array(range_args).size() == 1)
    {
      start = from_integer(0, int_type);
      stop = convert_expression(*as_array(range_args).begin());
      step = from_integer(1, int_type);
    }
    else if(as_array(range_args).size() >= 2)
    {
      start = convert_expression(*as_array(range_args).begin());
      stop = convert_expression(*std::next(as_array(range_args).begin(), 1));
      if(as_array(range_args).size() >= 3)
        step = convert_expression(*std::next(as_array(range_args).begin(), 2));
      else
        step = from_integer(1, int_type);
    }
    else
    {
      start = from_integer(0, int_type);
      stop = from_integer(0, int_type);
      step = from_integer(1, int_type);
    }

    start = safe_typecast(start, int_type);
    stop = safe_typecast(stop, int_type);
    step = safe_typecast(step, int_type);

    irep_idt symbol_id{qualified_name};
    if(symbol_table.lookup(symbol_id) == nullptr)
    {
      symbolt new_symbol{symbol_id, int_type, "python"};
      new_symbol.base_name = var_name;
      new_symbol.location = loc;
      new_symbol.is_lvalue = true;
      new_symbol.is_state_var = true;
      symbol_table.add(new_symbol);
    }

    symbol_exprt loop_sym = symbol_table.lookup_ref(symbol_id).symbol_expr();

    code_frontend_assignt init{loop_sym, start};
    init.add_source_location() = loc;

    code_blockt body_block;
    const jsont &body = json_member(stmt, "body");
    if(body.is_array())
    {
      for(const auto &s : as_array(body))
        body_block.add(convert_statement(s));
    }
    body_block.add(code_frontend_assignt{loop_sym, plus_exprt{loop_sym, step}});

    // Condition: step > 0 ? i < stop : i > stop
    exprt cond;
    if(step.is_constant())
    {
      mp_integer sv;
      if(!to_integer(to_constant_expr(step), sv) && sv < 0)
        cond = binary_relation_exprt{loop_sym, ID_gt, stop};
      else
        cond = binary_relation_exprt{loop_sym, ID_lt, stop};
    }
    else
    {
      // Dynamic step: if(step > 0) then i < stop else i > stop
      cond = if_exprt{
        binary_relation_exprt{step, ID_gt, from_integer(0, int_type)},
        binary_relation_exprt{loop_sym, ID_lt, stop},
        binary_relation_exprt{loop_sym, ID_gt, stop}};
    }

    code_whilet while_stmt{cond, std::move(body_block)};
    while_stmt.add_source_location() = loc;

    code_blockt result;
    result.add(std::move(init));
    result.add(std::move(while_stmt));
    return std::move(result);
  }

  // for x in iterable (list or string)
  // Desugar to: __idx = 0; while(__idx < iterable.length) {
  //   x = iterable.data[__idx]; body; __idx += 1; }
  exprt iterable = convert_expression(iter);
  if(iterable.is_nil())
    return code_skipt{};

  // If the iterable is a complex expression (e.g., enumerate() result),
  // store it in a temp symbol so it doesn't get simplified away.
  code_blockt pre_loop;
  if(iterable.id() != ID_symbol && iterable.type().id() == ID_struct)
  {
    static unsigned iter_tmp_ctr = 0;
    std::string tn = "__iter_tmp_" + std::to_string(iter_tmp_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt ti{tq};
    if(symbol_table.lookup(ti) == nullptr)
    {
      symbolt ts{ti, iterable.type(), "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    // Flush pending checks (e.g., from enumerate's wrap_value)
    for(auto &pc : pending_checks)
      pre_loop.add(std::move(pc));
    pending_checks.clear();
    pre_loop.add(code_frontend_assignt{
      symbol_table.lookup_ref(ti).symbol_expr(), iterable});
    iterable = symbol_table.lookup_ref(ti).symbol_expr();
  }

  bool is_list = is_python_list_type(iterable.type());
  bool is_string = is_python_string_type(iterable.type());
  bool is_dict = is_python_dict_type(iterable.type());

  // PLR §8.3: "for k in dict" iterates over keys (array-based)
  if(is_dict)
  {
    const auto &dict_st = to_struct_type(iterable.type());
    const auto &keys_type = to_array_type(dict_st.components()[1].type());
    typet key_type = keys_type.element_type();

    irep_idt var_id{qualified_name};
    if(symbol_table.lookup(var_id) == nullptr)
    {
      symbolt new_sym{var_id, key_type, "python"};
      new_sym.base_name = var_name;
      new_sym.location = loc;
      new_sym.is_lvalue = true;
      new_sym.is_state_var = true;
      symbol_table.add(new_sym);
    }
    symbol_exprt loop_var = symbol_table.lookup_ref(var_id).symbol_expr();

    // Desugar to: idx=0; while(idx < d.length) { k = d.keys[idx]; body; idx++ }
    static unsigned dict_iter_ctr = 0;
    std::string idx_name = "__dict_idx_" + std::to_string(dict_iter_ctr++);
    std::string idx_qname = qualify_name(idx_name);
    irep_idt idx_id{idx_qname};
    if(symbol_table.lookup(idx_id) == nullptr)
    {
      symbolt idx_sym{idx_id, signedbv_typet{64}, "python"};
      idx_sym.base_name = idx_name;
      idx_sym.is_lvalue = true;
      idx_sym.is_state_var = true;
      symbol_table.add(idx_sym);
    }
    symbol_exprt idx_var = symbol_table.lookup_ref(idx_id).symbol_expr();
    member_exprt length{iterable, "length", signedbv_typet{64}};
    member_exprt keys{iterable, "keys", keys_type};

    code_blockt result;
    result.add(
      code_frontend_assignt{idx_var, from_integer(0, signedbv_typet{64})});

    code_blockt body_block;
    exprt key_val = index_exprt{keys, idx_var};
    if(key_val.type() != loop_var.type())
      key_val = safe_typecast(key_val, loop_var.type());
    body_block.add(code_frontend_assignt{loop_var, key_val});

    const jsont &body_stmts = json_member(stmt, "body");
    if(body_stmts.is_array())
    {
      for(const auto &s : as_array(body_stmts))
        body_block.add(convert_statement(s));
    }
    body_block.add(code_frontend_assignt{
      idx_var, plus_exprt{idx_var, from_integer(1, signedbv_typet{64})}});

    code_whilet while_stmt{
      binary_relation_exprt{idx_var, ID_lt, length}, std::move(body_block)};
    result.add(std::move(while_stmt));
    return std::move(result);
  }

  if(!is_list && !is_string)
  {
    log_overapprox(
      "for-in iteration: unsupported iterable type, skipping body");
    return code_skipt{};
  }

  // Determine element type
  typet elem_type;
  if(is_list)
  {
    const auto &data_type =
      to_array_type(to_struct_type(iterable.type()).components()[1].type());
    elem_type = data_type.element_type();
  }
  else
    elem_type =
      python_string_type(); // string iteration yields single-char strings

  // Create loop variable
  irep_idt var_id{qualified_name};
  if(symbol_table.lookup(var_id) == nullptr)
  {
    symbolt new_sym{var_id, elem_type, "python"};
    new_sym.base_name = var_name;
    new_sym.location = loc;
    new_sym.is_lvalue = true;
    new_sym.is_state_var = true;
    symbol_table.add(new_sym);
  }
  else if(symbol_table.lookup_ref(var_id).type != elem_type)
  {
    // Update type to match iterable element type (e.g., tuple from enumerate)
    symbol_table.get_writeable_ref(var_id).type = elem_type;
  }
  symbol_exprt loop_var = symbol_table.lookup_ref(var_id).symbol_expr();

  // Create index variable
  std::string idx_name = "__for_idx_" + var_name;
  std::string idx_qname = qualify_name(idx_name);
  irep_idt idx_id{idx_qname};
  if(symbol_table.lookup(idx_id) == nullptr)
  {
    symbolt idx_sym{idx_id, int_type, "python"};
    idx_sym.base_name = idx_name;
    idx_sym.location = loc;
    idx_sym.is_lvalue = true;
    idx_sym.is_state_var = true;
    symbol_table.add(idx_sym);
  }
  symbol_exprt idx_var = symbol_table.lookup_ref(idx_id).symbol_expr();

  member_exprt length{iterable, "length", int_type};
  typet data_field_type;
  if(is_python_string_type(iterable.type()))
    data_field_type = pointer_typet(unsignedbv_typet{8}, 64);
  else if(iterable.type().id() == ID_struct)
    data_field_type = to_struct_type(iterable.type()).components()[1].type();
  else
    data_field_type = signedbv_typet{64}; // fallback
  member_exprt data{iterable, "data", data_field_type};

  code_blockt result;

  // __idx = 0
  result.add(code_frontend_assignt{idx_var, from_integer(0, int_type)});

  // while(__idx < iterable.length)
  code_blockt body_block;

  // x = iterable.data[__idx] (typecast if needed)
  exprt elem_val = is_string
                     ? exprt(dereference_exprt{plus_exprt{data, idx_var}})
                     : exprt(index_exprt{data, idx_var});

  // Handle tuple unpacking: for a, b in list_of_tuples
  if(is_node_type(target, "Tuple") && is_list)
  {
    const jsont &elts = json_member(target, "elts");
    if(elts.is_array())
    {
      std::size_t tidx = 0;
      for(const auto &elt : as_array(elts))
      {
        if(is_node_type(elt, "Name"))
        {
          std::string elt_name = json_string(json_member(elt, "id"));
          std::string elt_qname = qualify_name(elt_name);
          irep_idt elt_id{elt_qname};
          if(symbol_table.lookup(elt_id) == nullptr)
          {
            symbolt elt_sym{elt_id, python_int_type(), "python"};
            elt_sym.base_name = elt_name;
            elt_sym.is_lvalue = true;
            elt_sym.is_state_var = true;
            symbol_table.add(elt_sym);
          }
          symbol_exprt elt_var = symbol_table.lookup_ref(elt_id).symbol_expr();
          // Access tuple field: elem._0, elem._1, etc.
          std::string field = "_" + std::to_string(tidx);
          if(
            elem_val.type().id() == ID_struct &&
            to_struct_type(elem_val.type()).has_component(field))
          {
            exprt field_val = member_exprt{
              elem_val,
              field,
              to_struct_type(elem_val.type()).get_component(field).type()};
            if(field_val.type() != elt_var.type())
              field_val = safe_typecast(field_val, elt_var.type());
            body_block.add(code_frontend_assignt{elt_var, field_val});
          }
          else
          {
            // Fallback: use nondet
            body_block.add(code_frontend_assignt{
              elt_var, side_effect_expr_nondett{elt_var.type(), loc}});
          }
        }
        tidx++;
      }
    }
  }
  else
  {
    // For string iteration, wrap the char byte in a single-char string struct
    if(is_string && is_python_string_type(loop_var.type()))
    {
      // Build pointer-based single-char string: {length=1, data=&[char]}
      exprt::operandst chars;
      chars.push_back(elem_val);
      array_typet at(unsignedbv_typet{8}, from_integer(1, signedbv_typet{64}));
      array_exprt arr(std::move(chars), at);
      exprt ptr = address_of_exprt(index_exprt(
        arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{8}));
      exprt len_one = from_integer(1, signedbv_typet{64});
      elem_val = struct_exprt{{len_one, ptr}, python_string_type()};
    }
    else if(elem_val.type() != loop_var.type())
      elem_val = safe_typecast(elem_val, loop_var.type());
    body_block.add(code_frontend_assignt{loop_var, elem_val});
  } // end else (non-tuple target)

  // user body
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  // __idx += 1
  body_block.add(code_frontend_assignt{
    idx_var, plus_exprt{idx_var, from_integer(1, int_type)}});

  code_whilet while_stmt{
    binary_relation_exprt{idx_var, ID_lt, length}, std::move(body_block)};
  while_stmt.add_source_location() = loc;
  result.add(std::move(while_stmt));

  // Prepend pre-loop setup (temp for complex iterables)
  if(!pre_loop.statements().empty())
  {
    for(auto &s : result.statements())
      pre_loop.add(std::move(s));
    return std::move(pre_loop);
  }
  return std::move(result);
}

// PLR §7.6: The return statement
// "return may only occur syntactically nested in a function definition."
codet python_convertert::convert_return(const jsont &stmt)
{
  // PLR §6.2.9: return in generator → return __gen_result
  if(!current_function.empty() && generator_functions.count(current_function))
  {
    std::string grn = "__gen_result_" + current_function;
    std::string grq = qualify_name(grn);
    const symbolt *grs = symbol_table.lookup(irep_idt{grq});
    if(grs != nullptr)
      return code_frontend_returnt{grs->symbol_expr()};
  }

  const jsont &value = json_member(stmt, "value");

  if(value.is_null())
  {
    // Bare return — check if function expects a return value
    if(!current_function.empty())
    {
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(
        func_sym != nullptr && func_sym->type.id() == ID_code &&
        to_code_type(func_sym->type).return_type().id() != ID_empty)
      {
        return code_frontend_returnt{
          from_integer(0, to_code_type(func_sym->type).return_type())};
      }
    }
    return code_frontend_returnt{};
  }

  // Check if returning a constructor call: return Foo(args)
  if(
    is_node_type(value, "Call") &&
    is_node_type(json_member(value, "func"), "Name"))
  {
    std::string call_name =
      json_string(json_member(json_member(value, "func"), "id"));
    if(class_types.count(call_name))
    {
      // Create a temporary, call __init__, return the temporary
      const struct_typet &cls_type = class_types[call_name];
      source_locationt loc = get_location(stmt);

      std::string tmp_name = "__ret_tmp_" + call_name;
      std::string tmp_qname = qualify_name(tmp_name);
      irep_idt tmp_id{tmp_qname};

      if(symbol_table.lookup(tmp_id) == nullptr)
      {
        symbolt tmp_sym{tmp_id, cls_type, "python"};
        tmp_sym.base_name = tmp_name;
        tmp_sym.is_lvalue = true;
        tmp_sym.is_state_var = true;
        symbol_table.add(tmp_sym);
      }

      const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
      code_blockt block;

      irep_idt init_id{"python::" + call_name + "::__init__"};
      const symbolt *init_sym = symbol_table.lookup(init_id);
      if(init_sym != nullptr)
      {
        exprt::operandst args;
        args.push_back(address_of_exprt{tmp_sym.symbol_expr()});
        const jsont &call_args = json_member(value, "args");
        if(call_args.is_array())
        {
          for(const auto &a : as_array(call_args))
            args.push_back(convert_expression(a));
        }
        // Match argument types to parameter types
        const auto &init_params = to_code_type(init_sym->type).parameters();
        for(std::size_t ai = 0; ai < args.size() && ai < init_params.size();
            ai++)
        {
          if(args[ai].type() != init_params[ai].type())
            args[ai] = safe_typecast(args[ai], init_params[ai].type());
        }
        side_effect_expr_function_callt call{
          init_sym->symbol_expr(), std::move(args), empty_typet{}, loc};
        block.add(code_expressiont{call});
      }

      // Typecast to function's return type if needed
      exprt ret_expr = tmp_sym.symbol_expr();
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(func_sym != nullptr && func_sym->type.id() == ID_code)
      {
        typet ret_type = to_code_type(func_sym->type).return_type();
        if(ret_expr.type() != ret_type)
          ret_expr = safe_typecast(ret_expr, ret_type);
      }
      block.add(code_frontend_returnt{ret_expr});
      return std::move(block);
    }
  }

  exprt ret_val = convert_expression(value);
  if(ret_val.is_nil())
  {
    // Expression conversion failed — return default value
    if(!current_function.empty())
    {
      irep_idt func_id{"python::" + current_function};
      const symbolt *func_sym = symbol_table.lookup(func_id);
      if(
        func_sym != nullptr && func_sym->type.id() == ID_code &&
        to_code_type(func_sym->type).return_type().id() != ID_empty)
      {
        return code_frontend_returnt{
          from_integer(0, to_code_type(func_sym->type).return_type())};
      }
    }
    return code_frontend_returnt{};
  }

  // Typecast return value to match function's return type
  if(!current_function.empty())
  {
    irep_idt func_id{"python::" + current_function};
    const symbolt *func_sym = symbol_table.lookup(func_id);
    if(
      func_sym != nullptr && func_sym->type.id() == ID_code &&
      ret_val.type() != to_code_type(func_sym->type).return_type())
    {
      typet ret_type = to_code_type(func_sym->type).return_type();

      // Track functions that return lambdas (before type update)
      if(
        ret_val.id() == ID_symbol && ret_val.type().id() == ID_code &&
        !current_function.empty())
      {
        lambda_returning_functions[current_function] =
          to_symbol_expr(ret_val).get_identifier();
        ret_val = from_integer(0, python_int_type());
      }

      // If declared type doesn't match actual return, update function type.
      // This handles: int→float, int→string, int→list, and
      // list[X]→list[Y] (same tag, different element types).
      if(
        (ret_type == python_int_type() &&
         (ret_val.type().id() == ID_floatbv ||
          is_python_value_type(ret_val.type()) ||
          is_python_string_type(ret_val.type()) ||
          is_python_list_type(ret_val.type()))) ||
        (ret_val.type().id() == ID_struct && ret_type.id() == ID_struct &&
         ret_val.type() != ret_type &&
         to_struct_type(ret_val.type()).get_tag() ==
           to_struct_type(ret_type).get_tag()))
      {
        code_typet new_type = to_code_type(func_sym->type);
        new_type.return_type() = ret_val.type();
        symbol_table.get_writeable_ref(func_id).type = new_type;
      }
      else
      {
        // Dereference pointer returns for class reference params
        if(
          ret_val.type().id() == ID_pointer && ret_type.id() == ID_struct &&
          to_pointer_type(ret_val.type()).base_type() == ret_type)
          ret_val = dereference_exprt{ret_val};
        else if(
          ret_val.type().id() == ID_struct && ret_type.id() == ID_struct &&
          to_struct_type(ret_val.type()).get_tag() ==
            to_struct_type(ret_type).get_tag())
        {
          // Same struct tag (e.g., both python_list) but different
          // component types — treat as compatible (Python is dynamically typed)
        }
        else
          ret_val = safe_typecast(ret_val, ret_type);
      }
    }
  }

  code_frontend_returnt ret{ret_val};
  ret.add_source_location() = get_location(stmt);
  return std::move(ret);
}

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

// PLR §7.9: The break statement
// "break may only occur syntactically nested in a for or while loop."
codet python_convertert::convert_break()
{
  return code_breakt{};
}

// PLR §7.10: The continue statement
// "continue may only occur syntactically nested in a for or while loop."
codet python_convertert::convert_continue()
{
  return code_continuet{};
}

// PLR §7.1: Expression statements / pass
// "pass is a null operation — when it is executed, nothing happens."
codet python_convertert::convert_pass()
{
  return code_skipt{};
}

// PLR §7.8: The raise statement
// "raise evaluates the first expression as the exception object. It must
// be either a subclass or an instance of BaseException."
codet python_convertert::convert_raise(const jsont &stmt)
{
  source_locationt loc = get_location(stmt);

  // Extract exception type name for the error message
  std::string exc_type = "Exception";
  const jsont &exc = json_member(stmt, "exc");
  if(!exc.is_null())
  {
    if(is_node_type(exc, "Call"))
    {
      const jsont &func = json_member(exc, "func");
      if(is_node_type(func, "Name"))
        exc_type = json_string(json_member(func, "id"));
      // PEP 654: if the raised value is ExceptionGroup(msg, [e]),
      // and the exception list is a single-element literal, treat
      // it as raising that one exception's type. Multi-element
      // groups are over-approximated as raising the first element.
      if(exc_type == "ExceptionGroup" || exc_type == "BaseExceptionGroup")
      {
        const jsont &args = json_member(exc, "args");
        if(args.is_array() && as_array(args).size() >= 2)
        {
          auto args_it = as_array(args).begin();
          ++args_it;
          const jsont &excs = *args_it;
          if(is_node_type(excs, "List") || is_node_type(excs, "Tuple"))
          {
            const jsont &elts = json_member(excs, "elts");
            if(elts.is_array() && !as_array(elts).empty())
            {
              const jsont &first = *as_array(elts).begin();
              if(is_node_type(first, "Call"))
              {
                const jsont &f_func = json_member(first, "func");
                if(is_node_type(f_func, "Name"))
                  exc_type = json_string(json_member(f_func, "id"));
              }
              else if(is_node_type(first, "Name"))
              {
                exc_type = json_string(json_member(first, "id"));
              }
              if(as_array(elts).size() > 1)
                log_overapprox(
                  "ExceptionGroup with >1 element — raising first only");
            }
          }
        }
      }
    }
    else if(is_node_type(exc, "Name"))
      exc_type = json_string(json_member(exc, "id"));
  }

  code_blockt block;

  // Set the exception flag and type
  irep_idt exc_id{"python::__exception_active"};
  const symbolt *exc_sym = symbol_table.lookup(exc_id);
  if(exc_sym != nullptr)
  {
    code_frontend_assignt set_flag{exc_sym->symbol_expr(), true_exprt{}};
    set_flag.add_source_location() = loc;
    block.add(std::move(set_flag));
  }

  // Set exception type (hash of type name for matching)
  irep_idt exc_type_sym_id{"python::__exception_type"};
  const symbolt *exc_type_sym = symbol_table.lookup(exc_type_sym_id);
  if(exc_type_sym != nullptr)
  {
    // Use a simple hash: sum of character values
    long type_hash = exception_type_hash(exc_type);
    code_frontend_assignt set_type{
      exc_type_sym->symbol_expr(), from_integer(type_hash, python_int_type())};
    set_type.add_source_location() = loc;
    block.add(std::move(set_type));
  }

  // Exception payload: first positional arg (typically a message
  // string). Register a global '__exception_payload' string symbol
  // on first use so 'except T as e: str(e)' can read it back.
  if(is_node_type(exc, "Call"))
  {
    const jsont &exc_args = json_member(exc, "args");
    if(exc_args.is_array() && !as_array(exc_args).empty())
    {
      irep_idt payload_id{"python::__exception_payload"};
      if(symbol_table.lookup(payload_id) == nullptr)
      {
        symbolt ps{payload_id, python_string_type(), "python"};
        ps.base_name = "__exception_payload";
        ps.is_lvalue = true;
        ps.is_state_var = true;
        symbol_table.add(ps);
      }
      exprt arg0 = convert_expression(*as_array(exc_args).begin());
      if(is_python_string_type(arg0.type()))
      {
        code_frontend_assignt set_payload{
          symbol_table.lookup_ref(payload_id).symbol_expr(), arg0};
        set_payload.add_source_location() = loc;
        block.add(std::move(set_payload));
      }
    }
  }

  // Add a failing assertion for uncaught exceptions only at top level
  // outside of try blocks
  if(current_function.empty() && try_depth == 0)
  {
    loc.set_property_class("exception");
    loc.set_comment("raise " + exc_type);
    code_assertt assertion{false_exprt{}};
    assertion.add_source_location() = loc;
    block.add(std::move(assertion));

    code_assumet assume{false_exprt{}};
    assume.add_source_location() = loc;
    block.add(std::move(assume));
  }
  else if(try_depth > 0)
  {
    // PLR §8.4: raise inside a try body. Do NOT return here —
    // the enclosing convert_try body loop guards each
    // subsequent statement with !__exception_active so the
    // raise effectively skips the rest of the try body, and
    // the except/finally machinery takes over. Emitting a
    // return here would bypass the handler.
  }
  else
  {
    // Inside a function but outside any try block: the
    // exception flag is set and we must unwind to the
    // caller. Return a value of the correct type.
    irep_idt func_id{"python::" + current_function};
    const symbolt *func_sym = symbol_table.lookup(func_id);
    if(
      func_sym != nullptr &&
      to_code_type(func_sym->type).return_type().id() == ID_empty)
    {
      block.add(code_frontend_returnt{});
    }
    else if(func_sym != nullptr)
    {
      // Return nondet of the declared type. If the type is later
      // updated by a return statement, CBMC's GOTO conversion will
      // handle the typecast.
      typet ret_type = to_code_type(func_sym->type).return_type();
      block.add(code_frontend_returnt{
        side_effect_expr_nondett{ret_type, source_locationt{}}});
    }
  }

  return std::move(block);
}

// PLR §8.5: The with statement
// "The with statement is used to wrap the execution of a block with
// methods defined by a context manager."
codet python_convertert::convert_with(const jsont &stmt)
{
  // Simplified: execute the body, ignoring __enter__/__exit__ protocol.
  // If there's an 'as' variable, assign the context_expr to it.
  code_blockt block;
  source_locationt loc = get_location(stmt);

  const jsont &items = json_member(stmt, "items");
  if(items.is_array())
  {
    for(const auto &item : as_array(items))
    {
      const jsont &optional_vars = json_member(item, "optional_vars");
      if(!optional_vars.is_null() && is_node_type(optional_vars, "Name"))
      {
        std::string var_name = json_string(json_member(optional_vars, "id"));
        std::string qname = qualify_name(var_name);
        irep_idt sym_id{qname};

        const jsont &ctx_expr = json_member(item, "context_expr");

        // Check if context_expr is a constructor call
        if(
          is_node_type(ctx_expr, "Call") &&
          is_node_type(json_member(ctx_expr, "func"), "Name") &&
          class_types.count(
            json_string(json_member(json_member(ctx_expr, "func"), "id"))))
        {
          std::string cls_name =
            json_string(json_member(json_member(ctx_expr, "func"), "id"));
          const struct_typet &cls_type = class_types[cls_name];

          if(symbol_table.lookup(sym_id) == nullptr)
          {
            symbolt new_sym{sym_id, cls_type, "python"};
            new_sym.base_name = var_name;
            new_sym.is_lvalue = true;
            new_sym.is_state_var = true;
            symbol_table.add(new_sym);
          }

          // Call __init__
          irep_idt init_id{"python::" + cls_name + "::__init__"};
          const symbolt *init_sym = symbol_table.lookup(init_id);
          if(init_sym != nullptr)
          {
            const symbolt &var_sym = symbol_table.lookup_ref(sym_id);
            exprt::operandst args;
            args.push_back(address_of_exprt{var_sym.symbol_expr()});
            const jsont &call_args = json_member(ctx_expr, "args");
            if(call_args.is_array())
            {
              for(const auto &a : as_array(call_args))
                args.push_back(convert_expression(a));
            }
            // Match argument types to parameter types
            const auto &init_params = to_code_type(init_sym->type).parameters();
            for(std::size_t ai = 0; ai < args.size() && ai < init_params.size();
                ai++)
            {
              if(args[ai].type() != init_params[ai].type())
                args[ai] = safe_typecast(args[ai], init_params[ai].type());
            }
            side_effect_expr_function_callt call{
              init_sym->symbol_expr(), std::move(args), empty_typet{}, loc};
            block.add(code_expressiont{call});
          }
        }
        else
        {
          // Non-constructor: with expr as x → x = expr
          exprt ctx = convert_expression(ctx_expr);
          if(!ctx.is_nil())
          {
            if(symbol_table.lookup(sym_id) == nullptr)
            {
              symbolt new_sym{sym_id, ctx.type(), "python"};
              new_sym.base_name = var_name;
              new_sym.is_lvalue = true;
              new_sym.is_state_var = true;
              symbol_table.add(new_sym);
            }
            const symbolt &sym = symbol_table.lookup_ref(sym_id);
            code_frontend_assignt assign{sym.symbol_expr(), ctx};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
          }
        }
      }
    }
  }

  // Convert the body
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      block.add(convert_statement(s));
  }

  return std::move(block);
}

// --- Module body conversion ---

// PLR §8.4: The try statement
// PLR §3.2: "Exceptions are identified by class instances."
// We model exceptions via __exception_active (bool) and
// __exception_type (hash of exception class name).
// "The try statement specifies exception handlers and/or cleanup code
// for a group of statements."
codet python_convertert::convert_try(const jsont &stmt)
{
  code_blockt block;
  source_locationt loc = get_location(stmt);

  irep_idt exc_id{"python::__exception_active"};
  const symbolt *exc_sym = symbol_table.lookup(exc_id);

  // PLR §8.4: Execute the try body.
  // After each statement, if an exception was raised, skip remaining
  // statements (they are guarded by !__exception_active).
  const jsont &body = json_member(stmt, "body");
  try_depth++;
  if(body.is_array())
  {
    bool first = true;
    for(const auto &s : as_array(body))
    {
      codet stmt_code = convert_statement(s);
      // First statement runs unconditionally; subsequent ones are
      // guarded so that a raise in an earlier statement skips them.
      if(!first && exc_sym != nullptr)
      {
        code_ifthenelset guarded{
          not_exprt{exc_sym->symbol_expr()}, std::move(stmt_code)};
        block.add(std::move(guarded));
      }
      else
        block.add(std::move(stmt_code));
      first = false;
    }
  }
  try_depth--;

  // Check for except handlers
  const jsont &handlers = json_member(stmt, "handlers");

  // PLR §8.4: the else clause runs iff no exception was raised
  // in the try body. Snapshot __exception_active BEFORE the
  // handler_chain runs (the handler clears it when catching),
  // so we can test the pre-handler state.
  const jsont &orelse = json_member(stmt, "orelse");
  bool has_else = orelse.is_array() && !as_array(orelse).empty();
  exprt exc_before =
    exc_sym != nullptr ? exprt{exc_sym->symbol_expr()} : exprt{};
  if(has_else && exc_sym != nullptr)
  {
    static unsigned try_else_ctr = 0;
    std::string tn = "__try_exc_before_" + std::to_string(try_else_ctr++);
    std::string tq = qualify_name(tn);
    irep_idt tid{tq};
    if(symbol_table.lookup(tid) == nullptr)
    {
      symbolt ts{tid, bool_typet{}, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      symbol_table.add(ts);
    }
    exc_before = symbol_table.lookup_ref(tid).symbol_expr();
    block.add(code_frontend_assignt{exc_before, exc_sym->symbol_expr()});
  }

  if(handlers.is_array() && !as_array(handlers).empty() && exc_sym != nullptr)
  {
    // PLR §8.4: iterate all except handlers sequentially
    const symbolt *exc_type_sym =
      symbol_table.lookup("python::__exception_type");

    // Build chained if-elif for each handler
    codet handler_chain = code_skipt{};

    // Process handlers in reverse to build the chain from inside out
    std::vector<const jsont *> handler_list;
    for(const auto &h : as_array(handlers))
      handler_list.push_back(&h);

    for(auto it = handler_list.rbegin(); it != handler_list.rend(); ++it)
    {
      const jsont &handler = **it;
      const jsont &handler_type = json_member(handler, "type");

      code_blockt except_block;
      except_block.add(
        code_frontend_assignt{exc_sym->symbol_expr(), false_exprt{}});

      // Handle "except Type as name:" — create the name variable
      const jsont &handler_name = json_member(handler, "name");
      if(
        !handler_name.is_null() && handler_name.is_string() &&
        !handler_name.value.empty())
      {
        std::string ename = handler_name.value;
        std::string eqname = qualify_name(ename);
        irep_idt eid{eqname};
        // Exception-bound name is typed as python_string so
        // 'except T as e: str(e)' can return the payload
        // registered by raise (or an empty string if no
        // payload was set).
        if(symbol_table.lookup(eid) == nullptr)
        {
          symbolt esym{eid, python_string_type(), "python"};
          esym.base_name = ename;
          esym.is_lvalue = true;
          esym.is_state_var = true;
          symbol_table.add(esym);
        }
        // If __exception_payload exists, copy it; otherwise
        // assign an empty string.
        irep_idt payload_id{"python::__exception_payload"};
        const symbolt *payload_sym = symbol_table.lookup(payload_id);
        if(payload_sym != nullptr)
        {
          except_block.add(code_frontend_assignt{
            symbol_table.lookup_ref(eid).symbol_expr(),
            payload_sym->symbol_expr()});
        }
        else
        {
          except_block.add(code_frontend_assignt{
            symbol_table.lookup_ref(eid).symbol_expr(),
            python_string_literal("")});
        }
      }

      const jsont &handler_body = json_member(handler, "body");
      if(handler_body.is_array())
      {
        for(const auto &s : as_array(handler_body))
          except_block.add(convert_statement(s));
      }

      exprt condition = exc_sym->symbol_expr();
      if(
        !handler_type.is_null() && is_node_type(handler_type, "Name") &&
        exc_type_sym != nullptr)
      {
        std::string htype = json_string(json_member(handler_type, "id"));
        if(htype != "Exception" && !htype.empty())
        {
          long type_hash = exception_type_hash(htype);
          condition = and_exprt{
            condition,
            equal_exprt{
              exc_type_sym->symbol_expr(),
              from_integer(type_hash, python_int_type())}};
        }
      }
      else if(
        !handler_type.is_null() && is_node_type(handler_type, "Tuple") &&
        exc_type_sym != nullptr)
      {
        // except (A, B, C): — match any of the named exception
        // types. Build the OR over their hashes; 'Exception' in
        // the tuple is a catch-all and falls back to the plain
        // active-flag condition.
        const jsont &elts = json_member(handler_type, "elts");
        exprt any_match = false_exprt{};
        bool has_catch_all = false;
        if(elts.is_array())
        {
          for(const auto &elt : as_array(elts))
          {
            if(!is_node_type(elt, "Name"))
              continue;
            std::string htype = json_string(json_member(elt, "id"));
            if(
              htype.empty() || htype == "Exception" || htype == "BaseException")
            {
              has_catch_all = true;
              break;
            }
            long type_hash = exception_type_hash(htype);
            any_match = or_exprt{
              any_match,
              equal_exprt{
                exc_type_sym->symbol_expr(),
                from_integer(type_hash, python_int_type())}};
          }
        }
        if(!has_catch_all)
          condition = and_exprt{condition, any_match};
      }

      handler_chain = code_ifthenelset{
        condition, std::move(except_block), std::move(handler_chain)};
    }

    block.add(std::move(handler_chain));

    // PLR §8.4: else runs iff no exception was raised in try
    // body. Use the snapshot captured before handler_chain.
    if(has_else)
    {
      code_blockt else_block;
      for(const auto &s : as_array(orelse))
        else_block.add(convert_statement(s));
      block.add(code_ifthenelset{not_exprt{exc_before}, std::move(else_block)});
    }
  }
  else
  {
    // No handlers — just execute the else block (guarded by
    // no-exception; in a try/else/finally without except, a
    // raise in try propagates past and else is skipped).
    if(has_else)
    {
      code_blockt else_block;
      for(const auto &s : as_array(orelse))
        else_block.add(convert_statement(s));
      if(exc_sym != nullptr)
        block.add(code_ifthenelset{
          not_exprt{exc_sym->symbol_expr()}, std::move(else_block)});
      else
        block.add(std::move(else_block));
    }
  }

  // Execute finally block (always runs)
  const jsont &finalbody = json_member(stmt, "finalbody");
  if(finalbody.is_array())
  {
    for(const auto &s : as_array(finalbody))
      block.add(convert_statement(s));
  }

  return std::move(block);
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
