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
#include <util/ieee_float.h>
#include <util/json.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include "python_types.h"
#include "python_value_type.h"

#include <cmath>
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

std::string python_convertert::qualify_name(const std::string &name) const
{
  // If inside a function and the name is declared global, use module scope
  if(!current_function.empty() && global_names.count(name))
    return "python::" + name;
  // Otherwise use function scope if inside a function
  if(!current_function.empty())
    return "python::" + current_function + "::" + name;
  return "python::" + name;
}

exprt python_convertert::unwrap_value(const exprt &e, const typet &target_type)
  const
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
      python_value_is(e, python_type_tagt::BOOL), python_value_bool(e)};
    exprt int_true = and_exprt{
      python_value_is(e, python_type_tagt::INT),
      notequal_exprt{python_value_int(e), from_integer(0, signedbv_typet{64})}};
    exprt float_true = and_exprt{
      python_value_is(e, python_type_tagt::FLOAT),
      notequal_exprt{python_value_float(e), safe_zero(double_type())}};
    exprt str_true = and_exprt{
      python_value_is(e, python_type_tagt::STR),
      notequal_exprt{
        member_exprt{python_value_str(e), "length", signedbv_typet{64}},
        from_integer(0, signedbv_typet{64})}};
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

  // For struct types (class instances, dicts, etc.) that don't fit
  // in the tagged union, return a nondet value. The struct can't be
  // stored in the int/float/bool/str/list fields.
  if(
    e.type().id() == ID_struct && !is_python_string_type(e.type()) &&
    !is_python_list_type(e.type()))
    return side_effect_expr_nondett{python_value_type(), source_locationt{}};

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
        python_value_is(e, python_type_tagt::BOOL), python_value_bool(e)},
      and_exprt{
        python_value_is(e, python_type_tagt::INT),
        notequal_exprt{
          python_value_int(e), from_integer(0, signedbv_typet{64})}}};
  }

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

exprt python_convertert::safe_zero(const typet &type) const
{
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
    return python_list_type(python_int_type()); // sets modeled as lists
  else if(type_name == "tuple" || type_name == "Tuple")
    return python_int_type(); // unparameterized tuple
  else if(
    type_name == "Any" || type_name == "Union" || type_name == "Literal" ||
    type_name == "Callable" || type_name == "BinaryIO" ||
    type_name == "TextIO" || type_name == "Iterator" ||
    type_name == "Iterable" || type_name == "Sequence" ||
    type_name == "Mapping" || type_name == "Type" || type_name == "ClassVar" ||
    type_name == "Final")
    return python_int_type(); // typing module types default to int
  else if(class_types.count(type_name))
    return class_types[type_name];
  else
  {
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
    result = convert_list(expr);
  else if(node_type == "ListComp")
    result = convert_list_comp(expr);
  else if(node_type == "JoinedStr")
  {
    // PLR §2.4.3: f-strings — concatenate literal parts with formatted values
    const jsont &values = json_member(expr, "values");
    if(!values.is_array() || as_array(values).empty())
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};

    // Build result by concatenating all parts
    struct_typet str_type = python_string_type();
    const auto &data_type = to_array_type(str_type.components()[1].type());

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
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};

    // All parts are constant — build the string literal
    exprt::operandst chars;
    for(char ch : all_bytes)
      chars.push_back(
        from_integer(static_cast<unsigned char>(ch), unsignedbv_typet{8}));
    while(chars.size() < PYTHON_MAX_STRING_LENGTH)
      chars.push_back(from_integer(0, unsignedbv_typet{8}));
    array_exprt data_expr{std::move(chars), data_type};
    exprt length_expr =
      from_integer(static_cast<long long>(all_bytes.size()), python_int_type());
    result = struct_exprt{{length_expr, data_expr}, str_type};
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
      std::string bytes_content = str_val.substr(2, str_val.size() - 3);
      // Model as list of integers
      typet lt = python_list_type(python_int_type());
      const auto &data_type =
        to_array_type(to_struct_type(lt).components()[1].type());
      exprt::operandst elems;
      for(unsigned char c : bytes_content)
        elems.push_back(from_integer(c, python_int_type()));
      while(elems.size() < PYTHON_MAX_LIST_LENGTH)
        elems.push_back(from_integer(0, python_int_type()));
      return struct_exprt{
        {from_integer(
           static_cast<long long>(bytes_content.size()), python_int_type()),
         array_exprt{std::move(elems), data_type}},
        lt};
    }

    // Detect complex number literals (e.g., "2j", "(1+2j)")
    if(!str_val.empty() && (str_val.back() == 'j' || str_val.back() == ')'))
    {
      // Model complex as nondet float (simplified)
      return side_effect_expr_nondett{double_type(), get_location(expr)};
    }

    // String literal → python_str struct { length, data[] }
    struct_typet str_type = python_string_type();
    const auto &components = str_type.components();
    const auto &data_type = to_array_type(components[1].type());

    // Build the data array
    exprt::operandst chars;
    for(char ch : str_val)
      chars.push_back(
        from_integer(static_cast<unsigned char>(ch), unsignedbv_typet{8}));
    // Pad with zeros to MAX_STRING_LENGTH
    while(chars.size() < PYTHON_MAX_STRING_LENGTH)
      chars.push_back(from_integer(0, unsignedbv_typet{8}));

    array_exprt data_expr{std::move(chars), data_type};
    exprt length_expr =
      from_integer(static_cast<long long>(str_val.size()), python_int_type());

    struct_exprt result{{length_expr, data_expr}, str_type};
    return std::move(result);
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

  // Look up in symbol table — check versioned names first, then
  // function-scoped, then global
  const symbolt *sym = nullptr;

  // Check if this variable has been versioned (type change renaming)
  std::string qname = qualify_name(id);
  auto ver_it = variable_versions.find(qname);
  if(ver_it != variable_versions.end())
    sym = symbol_table.lookup(ver_it->second);

  if(sym == nullptr && !current_function.empty())
  {
    irep_idt scoped_id{"python::" + current_function + "::" + id};
    sym = symbol_table.lookup(scoped_id);
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
      {"dict", 7}};
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
    struct_typet str_type = python_string_type();
    const auto &data_type = to_array_type(str_type.components()[1].type());

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

  // PLR §6.7: String repetition: "ab" * 3 → "ababab"
  if(
    op == "Mult" &&
    (is_python_string_type(left.type()) || is_python_string_type(right.type())))
  {
    exprt str_op = is_python_string_type(left.type()) ? left : right;
    exprt num_op = is_python_string_type(left.type()) ? right : left;
    num_op = safe_typecast(num_op, signedbv_typet{64});

    struct_typet str_type = python_string_type();
    const auto &data_type = to_array_type(str_type.components()[1].type());
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

  // Type promotion: if either operand is float, promote both
  if(left.type() != right.type())
  {
    if(left.type().id() == ID_floatbv)
      right = typecast_exprt{right, left.type()};
    else if(right.type().id() == ID_floatbv)
      left = typecast_exprt{left, right.type()};
  }

  if(op == "Add")
  {
    if(
      left.type().id() == ID_signedbv && !left.is_constant() &&
      !right.is_constant())
      add_check(
        not_exprt{plus_overflow_exprt{left, right}},
        "overflow",
        "integer overflow on addition",
        get_location(expr));
    return plus_exprt{left, right};
  }
  else if(op == "Sub")
  {
    if(
      left.type().id() == ID_signedbv && !left.is_constant() &&
      !right.is_constant())
      add_check(
        not_exprt{minus_overflow_exprt{left, right}},
        "overflow",
        "integer overflow on subtraction",
        get_location(expr));
    return minus_exprt{left, right};
  }
  else if(op == "Mult")
  {
    if(
      left.type().id() == ID_signedbv && !left.is_constant() &&
      !right.is_constant())
      add_check(
        not_exprt{mult_overflow_exprt{left, right}},
        "overflow",
        "integer overflow on multiplication",
        get_location(expr));
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
    add_check(
      notequal_exprt{right, safe_zero(right.type())},
      "division-by-zero",
      "division by zero",
      get_location(expr));
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
    return div_exprt{left, right};
  }
  else if(op == "Mod")
  {
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
    add_check(
      notequal_exprt{right, safe_zero(right.type())},
      "division-by-zero",
      "division by zero in modulo",
      get_location(expr));
    // Also set Python exception for try/except handling
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
    return mod_exprt{left, right};
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

    if(exp_known)
    {
      bool negative = exp_val < 0;
      if(negative)
        exp_val = -exp_val;

      if(exp_val == 0)
        return from_integer(1, left.type());

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
      exprt result = safe_zero(left.type()); // b==0 case: return 0 for safety
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
    return div_exprt{
      safe_typecast(left, float_type), safe_typecast(right, float_type)};
  }
  else if(op == "BitOr")
  {
    // Bitwise ops require bitvectors — cast if using unbounded ints
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    return bitor_exprt{left, right};
  }
  else if(op == "BitAnd")
  {
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    return bitand_exprt{left, right};
  }
  else if(op == "BitXor")
  {
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
    }
    return bitxor_exprt{left, right};
  }
  else if(op == "LShift")
  {
    if(left.type().id() == ID_integer)
    {
      left = typecast_exprt{left, signedbv_typet{64}};
      right = typecast_exprt{right, signedbv_typet{64}};
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
    return ashr_exprt{left, right};
  }
  else
  {
    log.error() << "Unsupported binary operator: " << op << messaget::eom;
    return nil_exprt{};
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
    return unary_minus_exprt{operand};
  else if(op == "UAdd")
    return operand;
  else if(op == "Not")
    return not_exprt{safe_typecast(operand, bool_typet{})};
  else
  {
    log.error() << "Unsupported unary operator: " << op << messaget::eom;
    return nil_exprt{};
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

    if(op == "And")
      result = and_exprt{
        safe_typecast(result, bool_typet{}), safe_typecast(next, bool_typet{})};
    else if(op == "Or")
      result = or_exprt{
        safe_typecast(result, bool_typet{}), safe_typecast(next, bool_typet{})};
    else
    {
      log.error() << "Unsupported bool operator: " << op << messaget::eom;
      return nil_exprt{};
    }
  }

  return result;
}

// PLR §6.10: Comparisons
// "Comparisons can be chained arbitrarily, e.g., x < y <= z is equivalent
// to x < y and y <= z."
exprt python_convertert::convert_compare(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  const jsont &ops = json_member(expr, "ops");
  const jsont &comparators = json_member(expr, "comparators");

  if(
    !ops.is_array() || !comparators.is_array() || as_array(ops).empty() ||
    as_array(comparators).empty())
  {
    log.error() << "Malformed Compare node" << messaget::eom;
    return nil_exprt{};
  }

  // Handle chained comparisons: a < b < c → (a < b) and (b < c)
  exprt result = nil_exprt{};
  exprt current_left = left;

  auto ops_it = as_array(ops).begin();
  auto comp_it = as_array(comparators).begin();
  for(; ops_it != as_array(ops).end(); ++ops_it, ++comp_it)
  {
    std::string op = json_string(json_member(*ops_it, "_type"));
    exprt right = convert_expression(*comp_it);

    if(current_left.is_nil() || right.is_nil())
      return nil_exprt{};

    // Unwrap tagged-union values
    if(is_python_value_type(current_left.type()))
      current_left = unwrap_value(current_left, right.type());
    if(is_python_value_type(right.type()))
      right = unwrap_value(right, current_left.type());

    // Type promotion for comparisons (skip for In/NotIn/Is/IsNot)
    if(
      current_left.type() != right.type() && op != "In" && op != "NotIn" &&
      op != "Is" && op != "IsNot")
    {
      if(current_left.type().id() == ID_floatbv)
      {
        // If right is an int constant, convert it exactly to float
        if(right.is_constant() && right.type().id() == ID_signedbv)
          right = safe_typecast(right, current_left.type());
        else
          right = safe_typecast(right, current_left.type());
      }
      else if(right.type().id() == ID_floatbv)
      {
        // If left is an int constant, convert it exactly to float
        if(
          current_left.is_constant() && current_left.type().id() == ID_signedbv)
          current_left = safe_typecast(current_left, right.type());
        // If left is numeric and right is a float constant representable
        // as int, cast to int for exact comparison
        else if(
          right.is_constant() && (current_left.type().id() == ID_signedbv ||
                                  current_left.type().id() == ID_integer))
        {
          ieee_floatt fv{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          fv.from_expr(to_constant_expr(right));
          mp_integer iv = fv.to_integer();
          ieee_floatt check{
            ieee_float_spect::double_precision(),
            ieee_floatt::rounding_modet::ROUND_TO_EVEN};
          check.from_integer(iv);
          if(fv == check)
            right = from_integer(iv, current_left.type());
          else
            current_left = safe_typecast(current_left, right.type());
        }
        else
          current_left = safe_typecast(current_left, right.type());
      }
      else if(
        is_python_value_type(current_left.type()) &&
        !is_python_value_type(right.type()))
        // Unwrap tagged union to match concrete type
        current_left = unwrap_value(current_left, right.type());
      else if(
        is_python_value_type(right.type()) &&
        !is_python_value_type(current_left.type()))
        right = unwrap_value(right, current_left.type());
      else
        // General case: cast right to left's type
        right = safe_typecast(right, current_left.type());
    }

    exprt cmp;

    // String ordering: compare first characters of data arrays
    if(
      is_python_string_type(current_left.type()) &&
      is_python_string_type(right.type()) &&
      (op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE"))
    {
      struct_typet str_type = python_string_type();
      const auto &data_type = to_array_type(str_type.components()[1].type());
      exprt left_char = index_exprt{
        member_exprt{current_left, "data", data_type},
        from_integer(0, python_int_type())};
      exprt right_char = index_exprt{
        member_exprt{right, "data", data_type},
        from_integer(0, python_int_type())};

      if(op == "Lt")
        cmp = binary_relation_exprt{left_char, ID_lt, right_char};
      else if(op == "LtE")
        cmp = binary_relation_exprt{left_char, ID_le, right_char};
      else if(op == "Gt")
        cmp = binary_relation_exprt{left_char, ID_gt, right_char};
      else
        cmp = binary_relation_exprt{left_char, ID_ge, right_char};
    }
    else if(op == "Eq")
    {
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = equal_exprt{current_left, right};
    }
    else if(op == "NotEq")
    {
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = notequal_exprt{current_left, right};
    }
    else if(op == "Lt" || op == "LtE" || op == "Gt" || op == "GtE")
    {
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      irep_idt rel_id = op == "Lt"    ? ID_lt
                        : op == "LtE" ? ID_le
                        : op == "Gt"  ? ID_gt
                                      : ID_ge;
      cmp = binary_relation_exprt{current_left, rel_id, right};
    }
    else if(op == "In" || op == "NotIn")
    {
      // x in lst → disjunction: lst.data[0]==x or lst.data[1]==x or ...
      if(is_python_list_type(right.type()))
      {
        const auto &list_st = to_struct_type(right.type());
        const auto &data_type = to_array_type(list_st.components()[1].type());
        member_exprt data{right, "data", data_type};
        member_exprt length{right, "length", signedbv_typet{64}};

        // Build disjunction for up to PYTHON_MAX_LIST_LENGTH elements
        // guarded by index < length
        exprt in_expr = false_exprt{};
        for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt elem = index_exprt{data, idx};
          // Ensure types match for equality comparison
          if(current_left.type() != elem.type())
            elem = safe_typecast(elem, current_left.type());
          exprt match = equal_exprt{current_left, elem};
          exprt in_range = binary_relation_exprt{idx, ID_lt, length};
          in_expr = or_exprt{in_expr, and_exprt{in_range, match}};
        }
        cmp = (op == "In") ? in_expr : not_exprt{in_expr};
      }
      else if(is_python_string_type(right.type()))
      {
        // PLR §6.10.2: "x in s" for strings — check character membership
        const auto &str_st = to_struct_type(right.type());
        const auto &data_type = to_array_type(str_st.components()[1].type());
        member_exprt data{right, "data", data_type};
        member_exprt length{right, "length", signedbv_typet{64}};

        exprt search_byte;
        if(is_python_string_type(current_left.type()))
          search_byte = index_exprt{
            member_exprt{current_left, "data", data_type},
            from_integer(0, signedbv_typet{64})};
        else
          search_byte = safe_typecast(current_left, unsignedbv_typet{8});

        exprt in_expr = false_exprt{};
        for(std::size_t i = 0; i < PYTHON_MAX_STRING_LENGTH; i++)
        {
          exprt idx = from_integer(i, signedbv_typet{64});
          exprt elem = index_exprt{data, idx};
          exprt match = equal_exprt{search_byte, elem};
          exprt in_range = binary_relation_exprt{idx, ID_lt, length};
          in_expr = or_exprt{in_expr, and_exprt{in_range, match}};
        }
        cmp = (op == "In") ? in_expr : not_exprt{in_expr};
      }
      else
      {
        log.warning() << "'in' operator only supported for lists"
                      << messaget::eom;
        cmp = (op == "In") ? exprt{false_exprt{}} : exprt{true_exprt{}};
      }
    }
    else if(op == "Is")
    {
      // PLR §6.10.3: Identity comparison
      // For tagged unions, "x is None" checks tag == NONE
      if(
        is_python_value_type(current_left.type()) && right.is_constant() &&
        right.type().id() == ID_signedbv)
      {
        mp_integer rv;
        if(
          !to_integer(to_constant_expr(right), rv) &&
          rv == mp_integer{-4611686018427387904LL})
        {
          cmp = python_value_is(current_left, python_type_tagt::NONE);
          goto done_cmp;
        }
      }
      if(
        is_python_value_type(right.type()) && current_left.is_constant() &&
        current_left.type().id() == ID_signedbv)
      {
        mp_integer lv;
        if(
          !to_integer(to_constant_expr(current_left), lv) &&
          lv == mp_integer{-4611686018427387904LL})
        {
          cmp = python_value_is(right, python_type_tagt::NONE);
          goto done_cmp;
        }
      }
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = equal_exprt{current_left, right};
    }
    else if(op == "IsNot")
    {
      // PLR §6.10.3: "x is not None" checks tag != NONE
      if(
        is_python_value_type(current_left.type()) && right.is_constant() &&
        right.type().id() == ID_signedbv)
      {
        mp_integer rv;
        if(
          !to_integer(to_constant_expr(right), rv) &&
          rv == mp_integer{-4611686018427387904LL})
        {
          cmp =
            not_exprt{python_value_is(current_left, python_type_tagt::NONE)};
          goto done_cmp;
        }
      }
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = notequal_exprt{current_left, right};
    }
    else
    {
      log.error() << "Unsupported comparison operator: " << op << messaget::eom;
      return nil_exprt{};
    }

  done_cmp:
    if(result.is_nil())
      result = cmp;
    else
      result = and_exprt{result, cmp};

    current_left = right;
  }

  return result;
}

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
      // Find the current class and its base class
      if(
        !current_class.empty() && class_bases.count(current_class) &&
        !class_bases[current_class].empty())
      {
        std::string base_class = class_bases[current_class][0];
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
                    // in the current scope (so self refers to Derived)
                    const jsont &init_body = json_member(item, "body");
                    if(init_body.is_array())
                    {
                      // Set current_class to base so nested super()
                      // resolves to the grandparent, not back to parent
                      std::string saved_class = current_class;
                      current_class = base_class;
                      for(const auto &s : as_array(init_body))
                        pending_checks.push_back(convert_statement(s));
                      current_class = saved_class;
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
      return side_effect_expr_nondett{python_int_type(), get_location(expr)};
    }

    // Check if this is a module function call: math.sqrt(x)
    if(is_node_type(obj_node, "Name"))
    {
      std::string obj_name = json_string(json_member(obj_node, "id"));
      if(imported_modules.count(obj_name))
      {
        // Resolve module.func to the function symbol
        // Try python::func_name first (registered by ImportFrom)
        irep_idt func_id{"python::" + method_name};
        const symbolt *sym = symbol_table.lookup(func_id);
        if(sym != nullptr && sym->type.id() == ID_code)
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
        // PLR stdlib: math module functions
        if(obj_name == "math")
        {
          exprt arg =
            args.is_array() && !as_array(args).empty()
              ? convert_expression(*as_array(args).begin())
              : side_effect_expr_nondett{double_type(), get_location(expr)};
          if(arg.type().id() != ID_floatbv)
            arg = safe_typecast(arg, double_type());

          if(method_name == "ceil")
          {
            // ceil(x) → smallest integer >= x
            // Model: typecast to int, then if result < x, add 1
            return plus_exprt{
              typecast_exprt{arg, python_int_type()},
              if_exprt{
                binary_relation_exprt{
                  typecast_exprt{
                    typecast_exprt{arg, python_int_type()}, double_type()},
                  ID_lt,
                  arg},
                from_integer(1, python_int_type()),
                from_integer(0, python_int_type())}};
          }
          if(method_name == "floor")
          {
            // floor(x) → largest integer <= x
            return minus_exprt{
              typecast_exprt{arg, python_int_type()},
              if_exprt{
                binary_relation_exprt{
                  typecast_exprt{
                    typecast_exprt{arg, python_int_type()}, double_type()},
                  ID_gt,
                  arg},
                from_integer(1, python_int_type()),
                from_integer(0, python_int_type())}};
          }
          if(method_name == "fabs")
            return if_exprt{
              binary_relation_exprt{arg, ID_lt, safe_zero(double_type())},
              unary_minus_exprt{arg},
              arg};
          if(method_name == "sqrt")
          {
            // For constant args, compute at conversion time
            if(arg.is_constant())
            {
              ieee_floatt fv{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              fv.from_expr(to_constant_expr(arg));
              double val = std::stod(fv.to_ansi_c_string());
              if(val >= 0)
              {
                ieee_floatt result{
                  ieee_float_spect::double_precision(),
                  ieee_floatt::rounding_modet::ROUND_TO_EVEN};
                result.from_double(std::sqrt(val));
                return result.to_expr();
              }
            }
            return side_effect_expr_nondett{double_type(), get_location(expr)};
          }
          // Trig/log/exp for constant args
          if(
            (method_name == "sin" || method_name == "cos" ||
             method_name == "tan" || method_name == "asin" ||
             method_name == "acos" || method_name == "atan" ||
             method_name == "log" || method_name == "exp" ||
             method_name == "log2" || method_name == "log10") &&
            arg.is_constant())
          {
            ieee_floatt fv{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            fv.from_expr(to_constant_expr(arg));
            double val = std::stod(fv.to_ansi_c_string());
            double res = 0;
            if(method_name == "sin")
              res = std::sin(val);
            else if(method_name == "cos")
              res = std::cos(val);
            else if(method_name == "tan")
              res = std::tan(val);
            else if(method_name == "asin")
              res = std::asin(val);
            else if(method_name == "acos")
              res = std::acos(val);
            else if(method_name == "atan")
              res = std::atan(val);
            else if(method_name == "log")
              res = std::log(val);
            else if(method_name == "exp")
              res = std::exp(val);
            else if(method_name == "log2")
              res = std::log2(val);
            else if(method_name == "log10")
              res = std::log10(val);
            ieee_floatt result{
              ieee_float_spect::double_precision(),
              ieee_floatt::rounding_modet::ROUND_TO_EVEN};
            result.from_double(res);
            return result.to_expr();
          }
          // Unknown math function — return nondet
          return side_effect_expr_nondett{double_type(), get_location(expr)};
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

    if(obj_base_type.id() == ID_struct)
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
          // PLib stdtypes: str.split(sep)
          // For constant string and delimiter, split at conversion time
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
                  struct_typet str_type = python_string_type();
                  const auto &data_type =
                    to_array_type(str_type.components()[1].type());
                  typet list_type = python_list_type(str_type);
                  const auto &list_data_type = to_array_type(
                    to_struct_type(list_type).components()[1].type());

                  exprt::operandst list_elems;
                  for(const auto &part : parts)
                  {
                    exprt::operandst chars;
                    for(char c : part)
                      chars.push_back(from_integer(
                        static_cast<unsigned char>(c), unsignedbv_typet{8}));
                    while(chars.size() < PYTHON_MAX_STRING_LENGTH)
                      chars.push_back(from_integer(0, unsignedbv_typet{8}));
                    list_elems.push_back(struct_exprt{
                      {from_integer(
                         static_cast<long long>(part.size()),
                         python_int_type()),
                       array_exprt{std::move(chars), data_type}},
                      str_type});
                  }
                  while(list_elems.size() < PYTHON_MAX_LIST_LENGTH)
                    list_elems.push_back(safe_zero(str_type));
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
          const auto &str_st = to_struct_type(obj_base_type);
          const auto &data_type = to_array_type(str_st.components()[1].type());
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
          method_name == "encode" || method_name == "decode" ||
          method_name == "removeprefix" || method_name == "removesuffix")
        {
          // Return nondet string with same length
          return side_effect_expr_nondett{
            python_string_type(), get_location(expr)};
        }
        if(method_name == "replace" || method_name == "format")
        {
          // PLib stdtypes: str.replace(old, new) for constant strings
          if(
            method_name == "replace" && obj.id() == ID_struct &&
            obj.operands().size() == 2 && obj.operands()[0].is_constant() &&
            args.is_array() && as_array(args).size() >= 2)
          {
            auto ait = as_array(args).begin();
            exprt old_expr = convert_expression(*ait);
            ++ait;
            exprt new_expr = convert_expression(*ait);
            // Extract all three as constant strings
            auto extract_str = [&](const exprt &e) -> std::string
            {
              if(
                e.id() != ID_struct || e.operands().size() != 2 ||
                !e.operands()[0].is_constant())
                return "";
              mp_integer len;
              if(to_integer(to_constant_expr(e.operands()[0]), len))
                return "";
              std::string s;
              for(mp_integer i = 0; i < len; ++i)
              {
                auto idx = i.to_ulong();
                if(
                  idx < e.operands()[1].operands().size() &&
                  e.operands()[1].operands()[idx].is_constant())
                {
                  mp_integer ch;
                  if(!to_integer(
                       to_constant_expr(e.operands()[1].operands()[idx]), ch))
                    s += static_cast<char>(ch.to_ulong());
                }
              }
              return s;
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
              struct_typet str_type = python_string_type();
              const auto &data_type =
                to_array_type(str_type.components()[1].type());
              exprt::operandst chars;
              for(char c : result)
                chars.push_back(from_integer(
                  static_cast<unsigned char>(c), unsignedbv_typet{8}));
              while(chars.size() < PYTHON_MAX_STRING_LENGTH)
                chars.push_back(from_integer(0, unsignedbv_typet{8}));
              return struct_exprt{
                {from_integer(
                   static_cast<long long>(result.size()), python_int_type()),
                 array_exprt{std::move(chars), data_type}},
                str_type};
            }
          }
          // PLib stdtypes: str.format() — substitute {} placeholders
          if(
            method_name == "format" && obj.id() == ID_struct &&
            obj.operands().size() == 2 && obj.operands()[0].is_constant())
          {
            // Extract format string
            mp_integer flen;
            if(!to_integer(to_constant_expr(obj.operands()[0]), flen))
            {
              std::string fmt;
              const auto &fdata = obj.operands()[1];
              for(mp_integer i = 0; i < flen; ++i)
              {
                std::size_t idx = i.to_ulong();
                if(
                  idx < fdata.operands().size() &&
                  fdata.operands()[idx].is_constant())
                {
                  mp_integer ch;
                  if(!to_integer(to_constant_expr(fdata.operands()[idx]), ch))
                    fmt += static_cast<char>(ch.to_ulong());
                }
              }
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
                if(i + 1 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == '}')
                {
                  if(
                    arg_idx < arg_exprs.size() &&
                    is_python_string_type(arg_exprs[arg_idx].type()) &&
                    arg_exprs[arg_idx].id() == ID_struct)
                  {
                    // Extract constant string arg
                    const auto &sa = arg_exprs[arg_idx];
                    mp_integer slen;
                    if(
                      sa.operands().size() == 2 &&
                      sa.operands()[0].is_constant() &&
                      !to_integer(to_constant_expr(sa.operands()[0]), slen))
                    {
                      for(mp_integer j = 0; j < slen; ++j)
                      {
                        std::size_t si = j.to_ulong();
                        if(
                          si < sa.operands()[1].operands().size() &&
                          sa.operands()[1].operands()[si].is_constant())
                        {
                          mp_integer sc;
                          if(!to_integer(
                               to_constant_expr(
                                 sa.operands()[1].operands()[si]),
                               sc))
                            result += static_cast<char>(sc.to_ulong());
                        }
                      }
                    }
                    else
                      all_const = false;
                  }
                  else
                    all_const = false;
                  arg_idx++;
                  i++; // skip }
                }
                else
                  result += fmt[i];
              }
              if(all_const)
              {
                struct_typet str_type = python_string_type();
                const auto &data_type =
                  to_array_type(str_type.components()[1].type());
                exprt::operandst chars;
                for(char c : result)
                  chars.push_back(from_integer(
                    static_cast<unsigned char>(c), unsignedbv_typet{8}));
                while(chars.size() < PYTHON_MAX_STRING_LENGTH)
                  chars.push_back(from_integer(0, unsignedbv_typet{8}));
                return struct_exprt{
                  {from_integer(
                     static_cast<long long>(result.size()), python_int_type()),
                   array_exprt{std::move(chars), data_type}},
                  str_type};
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
          if(is_python_string_type(prefix.type()))
          {
            const auto &str_st = to_struct_type(obj_base_type);
            const auto &data_type =
              to_array_type(str_st.components()[1].type());
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
          const auto &str_st = to_struct_type(obj_base_type);
          const auto &data_type = to_array_type(str_st.components()[1].type());
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
          return side_effect_expr_nondett{bool_typet{}, get_location(expr)};
        }
        if(
          method_name == "find" || method_name == "index" ||
          method_name == "rfind" || method_name == "rindex" ||
          method_name == "count")
        {
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
        }
        if(
          method_name == "join" || method_name == "partition" ||
          method_name == "rpartition")
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

            // Default value (second arg or 0)
            exprt default_val = safe_zero(vals_type.element_type());
            ++arg_it;
            if(arg_it != as_array(args).end())
              default_val = safe_typecast(
                convert_expression(*arg_it), vals_type.element_type());

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
        if(
          method_name == "keys" || method_name == "values" ||
          method_name == "items")
          return side_effect_expr_nondett{
            python_list_type(python_int_type()), get_location(expr)};
        if(
          method_name == "setdefault" || method_name == "pop" ||
          method_name == "popitem" || method_name == "update" ||
          method_name == "clear" || method_name == "copy")
          return side_effect_expr_nondett{
            python_int_type(), get_location(expr)};
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
    log.warning() << "Unknown method: " << method_name << messaget::eom;
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }

  // Handle nondet functions
  if(func_name == "nondet_int")
  {
    side_effect_expr_nondett nondet{python_int_type(), get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "nondet_float")
  {
    side_effect_expr_nondett nondet{double_type(), get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "nondet_bool")
  {
    side_effect_expr_nondett nondet{bool_typet{}, get_location(expr)};
    return std::move(nondet);
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
    member_exprt len{tmp, "length", signedbv_typet{64}};
    // If size argument provided, constrain length == size
    if(args.is_array() && !as_array(args).empty())
    {
      exprt size = convert_expression(*as_array(args).begin());
      pending_checks.push_back(code_assumet{
        equal_exprt{len, safe_typecast(size, signedbv_typet{64})}});
    }
    else
    {
      pending_checks.push_back(code_assumet{and_exprt{
        binary_relation_exprt{len, ID_ge, from_integer(0, signedbv_typet{64})},
        binary_relation_exprt{
          len,
          ID_le,
          from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}}});
    }
    return std::move(tmp);
  }
  else if(func_name == "nondet_list")
  {
    // Constrain length to valid bounds
    static unsigned nl_ctr = 0;
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
      binary_relation_exprt{
        len,
        ID_le,
        from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64})}}});
    return std::move(tmp);
  }
  else if(func_name == "nondet_dict")
  {
    typet dt = python_dict_type(python_string_type(), python_int_type());
    static unsigned nd_ctr = 0;
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
      binary_relation_exprt{
        len, ID_le, from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64})}}});
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
          // Use nondet for the result element (exact call is complex)
          // TODO: inline the function call for full precision
          pending_checks.push_back(code_ifthenelset{
            guard,
            code_frontend_assignt{
              index_exprt{dst_data, idx},
              side_effect_expr_nondett{ret_type, source_locationt{}}}});
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
      ++it;
      exprt list_arg = convert_expression(*it);
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
        if(
          is_python_string_type(arg.type()) ||
          is_python_list_type(arg.type()) || is_python_dict_type(arg.type()))
          return member_exprt{arg, "length", python_int_type()};

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
        return typecast_exprt{arg, bool_typet{}};
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
          // Build string literal
          struct_typet str_type = python_string_type();
          const auto &data_type =
            to_array_type(str_type.components()[1].type());
          exprt::operandst chars;
          for(char c : result)
            chars.push_back(
              from_integer(static_cast<unsigned char>(c), unsignedbv_typet{8}));
          while(chars.size() < PYTHON_MAX_STRING_LENGTH)
            chars.push_back(from_integer(0, unsignedbv_typet{8}));
          return struct_exprt{
            {from_integer(
               static_cast<long long>(result.size()), python_int_type()),
             array_exprt{std::move(chars), data_type}},
            str_type};
        }
      }
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
  }
  else if(func_name == "repr" || func_name == "ascii")
  {
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
      struct_typet str_type = python_string_type();
      const auto &data_type = to_array_type(str_type.components()[1].type());
      exprt::operandst chars;

      // For constant code points, encode as UTF-8
      mp_integer cp_val;
      if(
        code_point.is_constant() &&
        !to_integer(to_constant_expr(code_point), cp_val))
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

      std::size_t char_count = chars.size();
      while(chars.size() < PYTHON_MAX_STRING_LENGTH)
        chars.push_back(from_integer(0, unsignedbv_typet{8}));
      array_exprt data{std::move(chars), data_type};
      exprt length =
        from_integer(static_cast<long long>(char_count), signedbv_typet{64});
      return struct_exprt{{length, data}, str_type};
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
        struct_typet str_type = python_string_type();
        const auto &data_type = to_array_type(str_type.components()[1].type());
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
  // list() / reversed() / enumerate() — return nondet list
  else if(
    func_name == "list" || func_name == "reversed" || func_name == "enumerate")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(is_python_list_type(arg.type()))
        return arg; // list(lst) = copy
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
      if(is_python_list_type(arg.type()))
      {
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
                index_exprt{data, idx}, ID_gt, index_exprt{data, next}}};
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
      exprt arg = convert_expression(*as_array(args).begin());
      if(arg.type().id() == ID_floatbv)
      {
        // round(x) = floor(x + 0.5)
        ieee_floatt half{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        half.from_double(0.5);
        exprt sum = plus_exprt{arg, half.to_expr()};
        return typecast_exprt{sum, python_int_type()};
      }
      return arg; // int → int
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
      struct_typet::componentst comps;
      comps.push_back(struct_typet::componentt{"_0", python_int_type()});
      comps.push_back(struct_typet::componentt{"_1", python_int_type()});
      struct_typet tuple_type{comps};
      tuple_type.set_tag("python_tuple");
      return struct_exprt{{div_exprt{a, b}, mod_exprt{a, b}}, tuple_type};
    }
    return side_effect_expr_nondett{python_int_type(), get_location(expr)};
  }
  // str() — return nondet string
  else if(func_name == "str")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      // str(x) — convert to string (simplified: return nondet string)
      return side_effect_expr_nondett{python_string_type(), get_location(expr)};
    }
    return side_effect_expr_nondett{python_string_type(), get_location(expr)};
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
              for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
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
              if(match)
                return true_exprt{};
            }
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
              ieee_floatt result{
                ieee_float_spect::double_precision(),
                ieee_floatt::rounding_modet::ROUND_TO_EVEN};
              result.from_double(std::sqrt(rd * rd + id * id));
              return result.to_expr();
            }
          }
          // Variable complex: nondet with result >= 0
          {
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
      // Pad missing args with defaults
      const code_typet &init_type = to_code_type(init_sym->type);
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

  // Intercept imported math functions: route through our math model
  if(imported_math_funcs.count(func_name))
  {
    exprt arg = args.is_array() && !as_array(args).empty()
                  ? convert_expression(*as_array(args).begin())
                  : side_effect_expr_nondett{double_type(), get_location(expr)};
    if(arg.type().id() != ID_floatbv)
      arg = safe_typecast(arg, double_type());

    if(func_name == "ceil")
      return plus_exprt{
        typecast_exprt{arg, python_int_type()},
        if_exprt{
          binary_relation_exprt{
            typecast_exprt{
              typecast_exprt{arg, python_int_type()}, double_type()},
            ID_lt,
            arg},
          from_integer(1, python_int_type()),
          from_integer(0, python_int_type())}};
    if(func_name == "floor")
      return minus_exprt{
        typecast_exprt{arg, python_int_type()},
        if_exprt{
          binary_relation_exprt{
            typecast_exprt{
              typecast_exprt{arg, python_int_type()}, double_type()},
            ID_gt,
            arg},
          from_integer(1, python_int_type()),
          from_integer(0, python_int_type())}};
    if(func_name == "fabs")
      return if_exprt{
        binary_relation_exprt{arg, ID_lt, safe_zero(double_type())},
        unary_minus_exprt{arg},
        arg};
    if(func_name == "sqrt" && arg.is_constant())
    {
      ieee_floatt fv{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      fv.from_expr(to_constant_expr(arg));
      double val = std::stod(fv.to_ansi_c_string());
      if(val >= 0)
      {
        ieee_floatt result{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        result.from_double(std::sqrt(val));
        return result.to_expr();
      }
    }
    // Other math functions: fall through to registered symbol
  }

  // Check function aliases (lambda assignments: double = lambda x: x*2)
  if(sym == nullptr || sym->type.id() != ID_code)
  {
    auto alias_it = function_aliases.find(qualify_name(func_name));
    if(alias_it != function_aliases.end())
      sym = symbol_table.lookup(alias_it->second);
  }

  if(sym == nullptr || sym->type.id() != ID_code)
  {
    // Unknown function — return nondet value (sound overapproximation)
    // and add a failing property so the user knows the result is
    // overapproximated, matching CBMC's "no body for callee" pattern.
    log.warning() << "Unknown function '" << func_name
                  << "', returning nondet value" << messaget::eom;
    add_check(
      false_exprt{},
      "no-body",
      "no body for callee " + func_name,
      get_location(expr));
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
    // Extend arguments to full parameter count with placeholders
    arguments.resize(params.size(), nil_exprt{});

    for(const auto &kw : as_array(keywords))
    {
      std::string kw_name = json_string(json_member(kw, "arg"));
      exprt kw_val = convert_expression(json_member(kw, "value"));

      // Find the parameter index by name
      for(std::size_t i = 0; i < params.size(); i++)
      {
        if(id2string(params[i].get_base_name()) == kw_name)
        {
          arguments[i] = kw_val;
          break;
        }
      }
    }
  }

  // Fill in defaults for any remaining nil arguments.
  // Defaults are stored in the FunctionDef AST; look up the function's
  // definition to find them.
  if(arguments.size() < params.size())
    arguments.resize(params.size(), nil_exprt{});

  // Look up the function's AST to get defaults
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
              arguments[i] = convert_expression(*def_it);
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
      arguments[i] = safe_zero(params[i].type());
  }

  // Remove trailing nil arguments beyond param count
  while(!arguments.empty() && arguments.back().is_nil())
    arguments.pop_back();

  // Typecast arguments to match parameter types
  for(std::size_t i = 0; i < arguments.size() && i < params.size(); i++)
  {
    if(!arguments[i].is_nil() && arguments[i].type() != params[i].type())
      arguments[i] = safe_typecast(arguments[i], params[i].type());
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
      const auto &dict_st = to_struct_type(value.type());
      const auto &keys_type = to_array_type(dict_st.components()[1].type());
      const auto &vals_type = to_array_type(dict_st.components()[2].type());
      member_exprt length{value, "length", signedbv_typet{64}};
      member_exprt keys{value, "keys", keys_type};
      member_exprt vals{value, "values", vals_type};

      // Scan: result = values[i] where keys[i] == slice
      exprt result =
        safe_zero(vals_type.element_type()); // default if not found
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, signedbv_typet{64});
        exprt in_range = binary_relation_exprt{idx, ID_lt, length};
        exprt key_i = index_exprt{keys, idx};
        if(key_i.type() != slice.type())
          key_i = safe_typecast(key_i, slice.type());
        exprt match = equal_exprt{key_i, slice};
        result =
          if_exprt{and_exprt{in_range, match}, index_exprt{vals, idx}, result};
      }
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
    add_check(
      and_exprt{
        binary_relation_exprt{slice, ID_ge, safe_zero(slice.type())},
        binary_relation_exprt{slice, ID_lt, length}},
      "index-out-of-bounds",
      "string index out of range",
      get_location(expr));
    struct_typet str_type = python_string_type();
    const auto &data_type = to_array_type(str_type.components()[1].type());

    member_exprt data{value, "data", data_type};
    index_exprt char_val{data, slice};

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

  log.error() << "Subscript not yet supported for this type" << messaget::eom;
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
  struct_typet list_type = python_list_type(elem_type);
  array_typet data_type{
    elem_type, from_integer(PYTHON_MAX_LIST_LENGTH, python_int_type())};

  // Build data array: elements followed by zeros
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

// PLR §6.3.1: Attribute references
// "An attribute reference is a primary followed by a period and a name."
exprt python_convertert::convert_attribute(const jsont &expr)
{
  std::string attr = json_string(json_member(expr, "attr"));
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

  // For struct types (classes), access the member directly
  if(value.type().id() == ID_struct)
  {
    const auto &st = to_struct_type(value.type());
    if(st.has_component(attr))
      return member_exprt{value, attr, st.get_component(attr).type()};
  }

  log.warning() << "Cannot access attribute '" << attr << "', using nondet"
                << messaget::eom;
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
  };
  std::vector<gen_info> gens;

  for(const auto &gen : as_array(generators))
  {
    const jsont &gen_iter = json_member(gen, "iter");
    if(!is_node_type(gen_iter, "List"))
    {
      log.warning() << "List comprehension only supports literal list iterables"
                    << messaget::eom;
      return nil_exprt{};
    }
    gen_info gi;
    gi.var_name = json_string(json_member(json_member(gen, "target"), "id"));
    const jsont &elts = json_member(gen_iter, "elts");
    if(elts.is_array())
    {
      for(const auto &e : as_array(elts))
        gi.values.push_back(&e);
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
    for(const auto &combo : combos)
    {
      for(std::size_t i = 0; i < gi.values.size(); i++)
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
      exprt val = convert_expression(*gens[g].values[combo[g]]);
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

// PLR §6.14: Lambdas
// "Lambda expressions are used to create anonymous functions."
exprt python_convertert::convert_lambda(const jsont &expr)
{
  std::string lambda_name = "__lambda_" + std::to_string(lambda_counter++);
  source_locationt loc = get_location(expr);

  const jsont &args_node = json_member(expr, "args");
  const jsont &params = json_member(args_node, "args");
  const jsont &body_expr = json_member(expr, "body");

  code_typet::parameterst parameters;
  if(params.is_array())
  {
    for(const auto &param : as_array(params))
    {
      std::string param_name = json_string(json_member(param, "arg"));
      code_typet::parametert p{python_int_type()};
      p.set_identifier("python::" + lambda_name + "::" + param_name);
      p.set_base_name(param_name);
      parameters.push_back(p);
    }
  }

  std::string saved_func = current_function;
  current_function = lambda_name;

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

  exprt body_val = convert_expression(body_expr);
  current_function = saved_func;

  if(body_val.is_nil())
    return nil_exprt{};

  typet return_type = body_val.type();
  code_typet func_type{parameters, return_type};

  irep_idt func_id{"python::" + lambda_name};
  symbolt func_sym{func_id, func_type, "python"};
  func_sym.base_name = lambda_name;
  func_sym.location = loc;
  func_sym.is_lvalue = true;
  code_blockt body;
  body.add(code_frontend_returnt{body_val});
  func_sym.value = body;

  symbol_table.add(func_sym);
  return func_sym.symbol_expr();
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
            imported_math_funcs.insert(asname);
            typet ret = double_type();
            code_typet::parameterst params;
            if(
              name == "sqrt" || name == "floor" || name == "ceil" ||
              name == "fabs" || name == "log" || name == "exp" ||
              name == "sin" || name == "cos" || name == "tan")
            {
              code_typet::parametert p{double_type()};
              p.set_identifier("python::" + asname + "::__p0");
              p.set_base_name("__p0");
              params.push_back(p);
            }
            else if(name == "pow" || name == "fmod")
            {
              code_typet::parametert p0{double_type()};
              p0.set_identifier("python::" + asname + "::__p0");
              p0.set_base_name("__p0");
              params.push_back(p0);
              code_typet::parametert p1{double_type()};
              p1.set_identifier("python::" + asname + "::__p1");
              p1.set_base_name("__p1");
              params.push_back(p1);
            }
            else if(name == "frexp" || name == "modf")
            {
              // Returns tuple (float, int) — use tuple type
              code_typet::parametert p{double_type()};
              p.set_identifier("python::" + asname + "::__p0");
              p.set_base_name("__p0");
              params.push_back(p);
              ret = python_tuple_type({double_type(), python_int_type()});
            }
            else if(name == "radians" || name == "degrees")
            {
              code_typet::parametert p{double_type()};
              p.set_identifier("python::" + asname + "::__p0");
              p.set_base_name("__p0");
              params.push_back(p);
            }
            else if(name == "pi" || name == "e")
            {
              // Constants — register as global variables
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
            else
              continue; // Unknown math function

            code_typet func_type{params, ret};
            irep_idt func_id{"python::" + asname};
            if(symbol_table.lookup(func_id) == nullptr)
            {
              symbolt func_sym{func_id, func_type, "python"};
              func_sym.base_name = asname;
              func_sym.is_lvalue = true;
              // Body: return nondet value of return type
              code_blockt body;
              body.add(code_frontend_returnt{
                side_effect_expr_nondett{ret, source_locationt{}}});
              func_sym.value = body;
              symbol_table.add(func_sym);

              // Create parameter symbols
              for(std::size_t pi = 0; pi < params.size(); pi++)
              {
                std::string pname = "__p" + std::to_string(pi);
                irep_idt pid{"python::" + asname + "::" + pname};
                if(symbol_table.lookup(pid) == nullptr)
                {
                  symbolt psym{pid, params[pi].type(), "python"};
                  psym.base_name = pname;
                  psym.is_parameter = true;
                  psym.is_lvalue = true;
                  psym.is_state_var = true;
                  symbol_table.add(psym);
                }
                // Set parameter identifier in the type
              }
            }
          }
          // typing module — type aliases, no-op
          else if(module == "typing")
          {
            // Names like Any, Optional, List, Dict are type aliases
            // They don't need runtime symbols
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
  // PLR §8.6: The match statement — desugar to if-elif chain
  else if(node_type == "Match")
  {
    exprt subject = convert_expression(json_member(stmt, "subject"));
    const jsont &cases = json_member(stmt, "cases");
    if(!cases.is_array() || subject.is_nil())
      return code_skipt{};

    // Build if-elif chain from cases (reverse order)
    codet chain = code_skipt{};
    std::vector<const jsont *> case_list;
    for(const auto &c : as_array(cases))
      case_list.push_back(&c);

    for(auto it = case_list.rbegin(); it != case_list.rend(); ++it)
    {
      const jsont &match_case = **it;
      const jsont &pattern = json_member(match_case, "pattern");
      const jsont &body = json_member(match_case, "body");

      code_blockt case_body;
      if(body.is_array())
      {
        for(const auto &s : as_array(body))
          case_body.add(convert_statement(s));
      }

      // MatchValue: case <constant>
      if(is_node_type(pattern, "MatchValue"))
      {
        exprt val = convert_expression(json_member(pattern, "value"));
        if(!val.is_nil())
        {
          exprt cond = equal_exprt{subject, safe_typecast(val, subject.type())};
          chain =
            code_ifthenelset{cond, std::move(case_body), std::move(chain)};
          continue;
        }
      }
      // MatchAs with name=None: case _ (wildcard/default)
      if(is_node_type(pattern, "MatchAs"))
      {
        chain = std::move(case_body);
        continue;
      }
      // Unsupported pattern — use as default
      chain = std::move(case_body);
    }
    result = std::move(chain);
  }
  else if(node_type == "With")
    result = convert_with(stmt);
  else if(node_type == "Try" || node_type == "TryStar")
    result = convert_try(stmt);
  else if(node_type == "Global" || node_type == "Nonlocal")
  {
    // PLR §7.12/§7.13: global/nonlocal — track names for scope resolution
    // Nonlocal is treated like global (simplified: no closure support)
    const jsont &names = json_member(stmt, "names");
    if(names.is_array())
    {
      for(const auto &name : as_array(names))
      {
        if(name.is_string())
          global_names.insert(name.value);
      }
    }
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

  // If annotation gave a placeholder type (e.g., dict→int) but the RHS
  // has a concrete struct type, use the RHS type instead.
  const symbolt &sym = symbol_table.lookup_ref(symbol_id);
  if(
    sym.type != rhs.type() && rhs.type().id() == ID_struct &&
    (sym.type == python_int_type() || sym.type.id() != ID_struct))
  {
    symbol_table.get_writeable_ref(symbol_id).type = rhs.type();
  }

  const symbolt &sym2 = symbol_table.lookup_ref(symbol_id);

  // Type cast if needed
  if(rhs.type() != sym2.type)
    rhs = safe_typecast(rhs, sym2.type);

  code_frontend_assignt assign{sym2.symbol_expr(), rhs};
  assign.add_source_location() = loc;
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

        // Set __class_tag to the actual class being constructed
        if(class_tag_ids.count(call_name))
        {
          result.add(code_frontend_assignt{
            member_exprt{
              var_sym.symbol_expr(), "__class_tag", signedbv_typet{32}},
            from_integer(class_tag_ids[call_name], signedbv_typet{32})});
        }

        // Copy class-level default values from the class object
        irep_idt class_obj_id{"python::" + call_name};
        const symbolt *class_obj = symbol_table.lookup(class_obj_id);
        if(class_obj != nullptr && !class_obj->value.is_nil())
        {
          result.add(code_frontend_assignt{
            var_sym.symbol_expr(), class_obj->symbol_expr()});
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

          // Pad missing arguments with defaults (safe_zero for each param type)
          const code_typet &init_type = to_code_type(init_sym->type);
          while(arguments.size() < init_type.parameters().size())
          {
            std::size_t idx = arguments.size();
            arguments.push_back(safe_zero(init_type.parameters()[idx].type()));
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
          member_exprt field_expr{rhs, field, field_type};

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
      // struct → different struct: nondet
      else if(
        rhs.type().id() == ID_struct && existing->type.id() == ID_struct &&
        rhs.type() != existing->type)
      {
        rhs = safe_typecast(rhs, existing->type);
      }
      else if(if_else_depth > 0)
      {
        // Inside if/else: type change at a branch point.
        // Use python_value_type for the variable.
        // Create a tagged-union variable and wrap the value.
        struct_typet val_type = python_value_type();
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

  // Determine the LHS expression based on target type
  exprt lhs;
  if(is_node_type(target, "Name"))
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
    // Build the concat with content tracking (same as convert_bin_op)
    struct_typet str_type = python_string_type();
    const auto &data_type = to_array_type(str_type.components()[1].type());
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
        block.add(code_frontend_assignt{sym->symbol_expr(), tmp});
        return std::move(block);
      }
    }
  }

  // Type promotion
  if(lhs.type() != rhs.type())
  {
    if(lhs.type().id() == ID_floatbv)
      rhs = typecast_exprt{rhs, lhs.type()};
    else if(rhs.type().id() == ID_floatbv)
      rhs = typecast_exprt{rhs, lhs.type()};
  }

  exprt new_rhs;
  if(op == "Add")
    new_rhs = plus_exprt{lhs, rhs};
  else if(op == "Sub")
    new_rhs = minus_exprt{lhs, rhs};
  else if(op == "Mult")
    new_rhs = mult_exprt{lhs, rhs};
  else if(op == "FloorDiv")
    new_rhs = div_exprt{lhs, rhs};
  else if(op == "Mod")
    new_rhs = mod_exprt{lhs, rhs};
  else if(op == "BitOr")
    new_rhs = bitor_exprt{lhs, rhs};
  else if(op == "BitAnd")
    new_rhs = bitand_exprt{lhs, rhs};
  else if(op == "BitXor")
    new_rhs = bitxor_exprt{lhs, rhs};
  else if(op == "LShift")
    new_rhs = shl_exprt{lhs, rhs};
  else if(op == "RShift")
    new_rhs = ashr_exprt{lhs, rhs};
  else
  {
    log.error() << "Unsupported augmented assignment operator: " << op
                << messaget::eom;
    return code_skipt{};
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
    return std::move(if_stmt);
  }
  else
  {
    variable_versions = then_versions;
    if_else_depth--;

    code_ifthenelset if_stmt{test, std::move(then_block)};
    if_stmt.add_source_location() = get_location(stmt);
    return std::move(if_stmt);
  }
}

// PLR §8.2: The while statement
// "The while statement is used for repeated execution as long as an
// expression is true."
codet python_convertert::convert_while(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
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
    log.warning() << "for-in iteration requires a list, string, or dict"
                  << messaget::eom;
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
  const auto &st = to_struct_type(iterable.type());
  member_exprt data{iterable, "data", st.components()[1].type()};

  code_blockt result;

  // __idx = 0
  result.add(code_frontend_assignt{idx_var, from_integer(0, int_type)});

  // while(__idx < iterable.length)
  code_blockt body_block;

  // x = iterable.data[__idx] (typecast if needed)
  exprt elem_val = index_exprt{data, idx_var};
  // For string iteration, wrap the char byte in a single-char string struct
  if(is_string && is_python_string_type(loop_var.type()))
  {
    struct_typet str_type = python_string_type();
    const auto &str_data_type = to_array_type(str_type.components()[1].type());
    exprt::operandst chars;
    chars.push_back(elem_val);
    while(chars.size() < PYTHON_MAX_STRING_LENGTH)
      chars.push_back(from_integer(0, unsignedbv_typet{8}));
    array_exprt char_data{std::move(chars), str_data_type};
    exprt len_one = from_integer(1, signedbv_typet{64});
    elem_val = struct_exprt{{len_one, char_data}, str_type};
  }
  else if(elem_val.type() != loop_var.type())
    elem_val = safe_typecast(elem_val, loop_var.type());
  body_block.add(code_frontend_assignt{loop_var, elem_val});

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
      // If declared type is int (default) but actual return is float,
      // update function type to float (avoids truncation)
      if(ret_type == python_int_type() && ret_val.type().id() == ID_floatbv)
      {
        code_typet new_type = to_code_type(func_sym->type);
        new_type.return_type() = ret_val.type();
        symbol_table.get_writeable_ref(func_id).type = new_type;
      }
      else
        ret_val = safe_typecast(ret_val, ret_type);
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
  if(decorators.is_array())
  {
    for(const auto &dec : as_array(decorators))
    {
      if(
        is_node_type(dec, "Name") &&
        json_string(json_member(dec, "id")) == "overload")
        return code_skipt{};
    }
  }

  std::string func_name = json_string(json_member(stmt, "name"));
  source_locationt loc = get_location(stmt);

  // Build parameter list
  const jsont &args_node = json_member(stmt, "args");
  const jsont &params = json_member(args_node, "args");

  code_typet::parameterst parameters;
  if(params.is_array())
  {
    for(const auto &param : as_array(params))
    {
      std::string param_name = json_string(json_member(param, "arg"));
      const jsont &annotation = json_member(param, "annotation");
      if(annotation.is_null())
      {
        log.warning() << "parameter '" << param_name << "' of function '"
                      << func_name << "' has no type annotation"
                      << messaget::eom;
      }
      // Use tagged union for unannotated params
      typet param_type = annotation.is_null()
                           ? python_value_type()
                           : convert_type_annotation(annotation);

      code_typet::parametert p{param_type};
      p.set_identifier("python::" + func_name + "::" + param_name);
      p.set_base_name(param_name);
      parameters.push_back(p);
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
                  // Multiple different class return types — use int
                  // (will be typecast via safe_typecast on each path)
                  return_type = python_int_type();
              }
            }
          }
        }
        // Detect yield (generator function)
        if(is_node_type(s, "Expr"))
        {
          const jsont &val = json_member(s, "value");
          if(is_node_type(val, "Yield") || is_node_type(val, "YieldFrom"))
            has_yield = true;
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

    // Generator functions return a list (eager evaluation)
    if(has_yield)
      return_type = python_list_type(python_int_type());
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
  current_function = func_name;
  global_names.clear();

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
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  current_function = saved_function;
  global_names = saved_globals;

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
            }
          }
        }

        // Build method with class-qualified name
        const jsont &args_node = json_member(item, "args");
        const jsont &params = json_member(args_node, "args");

        code_typet::parameterst parameters;
        if(params.is_array())
        {
          for(const auto &param : as_array(params))
          {
            std::string param_name = json_string(json_member(param, "arg"));
            // Skip cls/self parameter for @classmethod/@staticmethod
            if(
              (is_classmethod || is_staticmethod) &&
              (param_name == "cls" || param_name == "self"))
              continue;
            typet param_type;
            if(param_name == "self")
              param_type =
                pointer_typet{class_type, config.ansi_c.pointer_width};
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
          }
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
        std::string saved_func = current_function;
        current_function = class_name + "::" + method_name;

        code_blockt method_body;
        const jsont &method_body_json = json_member(item, "body");
        if(method_body_json.is_array())
        {
          for(const auto &s : as_array(method_body_json))
            method_body.add(convert_statement(s));
        }

        current_function = saved_func;

        symbolt *sym_ptr = symbol_table.get_writeable(func_id);
        if(sym_ptr != nullptr)
          sym_ptr->value = method_body;
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

  // Add a failing assertion for uncaught exceptions only at top level
  if(current_function.empty())
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
  else
  {
    // Inside a function: the exception flag is set, and the caller's
    // try/except will check __exception_active. We need to return a
    // value of the correct type. Use safe_zero of the current return
    // type, which may be updated later by a return statement.
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

      handler_chain = code_ifthenelset{
        condition, std::move(except_block), std::move(handler_chain)};
    }

    block.add(std::move(handler_chain));

    // Execute else block (runs when no exception)
    const jsont &orelse = json_member(stmt, "orelse");
    if(orelse.is_array())
    {
      for(const auto &s : as_array(orelse))
        block.add(convert_statement(s));
    }
  }
  else
  {
    // No handlers — just execute the else block
    const jsont &orelse = json_member(stmt, "orelse");
    if(orelse.is_array())
    {
      for(const auto &s : as_array(orelse))
        block.add(convert_statement(s));
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
      continue;
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
  }

  return block;
}

// --- Main entry point ---

bool python_convertert::convert()
{
  const jsont &body = json_member(parse_tree.ast_json, "body");

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
                  // Only pre-register with int element type if elements
                  // are simple constants. Skip for complex elements
                  // (constructors, variables) — let pass 2 handle it.
                  const jsont &elts = json_member(val, "elts");
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
                    var_type = python_list_type(python_int_type());
                  else
                    continue; // defer to pass 2
                }
                else if(
                  is_node_type(val, "Tuple") || is_node_type(val, "Dict") ||
                  is_node_type(val, "Call") || is_node_type(val, "ListComp") ||
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
  if(body.is_array())
  {
    for(const auto &stmt : as_array(body))
    {
      if((is_node_type(stmt, "FunctionDef") ||
          is_node_type(stmt, "AsyncFunctionDef")))
        convert_function_def(stmt);
      else if(is_node_type(stmt, "ClassDef"))
        convert_class_def(stmt);
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
