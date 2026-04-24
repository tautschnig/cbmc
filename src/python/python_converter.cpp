/// \file
/// Python to GOTO converter — converts Python JSON AST to CBMC symbol table
///
/// This converter implements the semantics defined in the Python Language
/// Reference (https://docs.python.org/3/reference/). Comments throughout
/// this file cite specific sections using the format:
///
///   PLR §X.Y: section title
///
/// where PLR = Python Language Reference, and the section numbers correspond
/// to the online documentation structure. The reference source is also
/// available at ~/cpython.git/Doc/reference/.
///
/// Key reference files:
///   expressions.rst    — PLR §6: Expressions
///   simple_stmts.rst   — PLR §7: Simple statements
///   compound_stmts.rst — PLR §8: Compound statements
///   datamodel.rst      — PLR §3: Data model
///   executionmodel.rst — PLR §4: Execution model

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
    return python_value_bool(e);
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
      // Layout-compatible: extract base fields from derived struct
      const auto &tgt_st = to_struct_type(target);
      exprt::operandst fields;
      for(const auto &comp : tgt_st.components())
        fields.push_back(member_exprt{e, comp.get_name(), comp.type()});
      return struct_exprt{std::move(fields), target};
    }
  }

  // Struct-to-scalar or other incompatible: return a nondet value
  // of the target type (overapproximation, avoids crash)
  return side_effect_expr_nondett{target, source_locationt{}};
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
  else if(node_type == "Lambda")
    result = convert_lambda(expr);
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

  // PLR §4.7: "str" type, §6.7: binary arithmetic
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

  // Unwrap tagged-union values to concrete types for operations
  if(is_python_value_type(left.type()))
    left = unwrap_value(
      left, right.type().id() != ID_struct ? right.type() : python_int_type());
  if(is_python_value_type(right.type()))
    right = unwrap_value(right, left.type());

  // List repetition with content tracking: lst * n
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
    add_check(
      notequal_exprt{right, safe_zero(right.type())},
      "division-by-zero",
      "division by zero",
      get_location(expr));
    return div_exprt{left, right};
  }
  else if(op == "Mod")
  {
    add_check(
      notequal_exprt{right, safe_zero(right.type())},
      "division-by-zero",
      "division by zero in modulo",
      get_location(expr));
    return mod_exprt{left, right};
  }
  else if(op == "Pow")
  {
    // Power not directly supported — would need a library model
    log.error() << "Power operator (**) not yet supported" << messaget::eom;
    return nil_exprt{};
  }
  else if(op == "Div")
  {
    // True division — promote to float
    typet float_type = double_type();
    return div_exprt{
      typecast_exprt{left, float_type}, typecast_exprt{right, float_type}};
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
        right = safe_typecast(right, current_left.type());
      else if(right.type().id() == ID_floatbv)
        current_left = safe_typecast(current_left, right.type());
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
      cmp = equal_exprt{current_left, right};
    else if(op == "NotEq")
      cmp = notequal_exprt{current_left, right};
    else if(op == "Lt")
      cmp = binary_relation_exprt{current_left, ID_lt, right};
    else if(op == "LtE")
      cmp = binary_relation_exprt{current_left, ID_le, right};
    else if(op == "Gt")
      cmp = binary_relation_exprt{current_left, ID_gt, right};
    else if(op == "GtE")
      cmp = binary_relation_exprt{current_left, ID_ge, right};
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
      else
      {
        log.warning() << "'in' operator only supported for lists"
                      << messaget::eom;
        cmp = (op == "In") ? exprt{false_exprt{}} : exprt{true_exprt{}};
      }
    }
    else if(op == "Is")
    {
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = equal_exprt{current_left, right};
    }
    else if(op == "IsNot")
    {
      if(current_left.type() != right.type())
        right = safe_typecast(right, current_left.type());
      cmp = notequal_exprt{current_left, right};
    }
    else
    {
      log.error() << "Unsupported comparison operator: " << op << messaget::eom;
      return nil_exprt{};
    }

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

    // Check if this is a module function call: math.sqrt(x)
    const jsont &obj_node = json_member(func, "value");
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
        // Not registered — return nondet float for math functions
        if(obj_name == "math")
          return side_effect_expr_nondett{double_type(), get_location(expr)};
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
          if(obj.type().id() == ID_pointer)
            arguments.push_back(obj);
          else if(obj.id() == ID_side_effect)
          {
            // Can't take address of nondet/side_effect — use nondet pointer
            arguments.push_back(side_effect_expr_nondett{
              pointer_typet{obj.type(), config.ansi_c.pointer_width},
              get_location(expr)});
          }
          else
            arguments.push_back(address_of_exprt{obj});
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
    log.error() << "Unknown method: " << method_name << messaget::eom;
    return nil_exprt{};
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
    side_effect_expr_nondett nondet{python_string_type(), get_location(expr)};
    return std::move(nondet);
  }
  else if(func_name == "len")
  {
    // len(s) → s.length for strings and lists
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(
        !arg.is_nil() &&
        (is_python_string_type(arg.type()) || is_python_list_type(arg.type())))
      {
        return member_exprt{arg, "length", python_int_type()};
      }
    }
    log.error() << "len() requires a string or list argument" << messaget::eom;
    return nil_exprt{};
  }
  // Built-in type constructors: int(), float(), bool()
  else if(func_name == "int")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
        return typecast_exprt{arg, python_int_type()};
    }
    mp_integer none_val = mp_integer(1) << 62;
    none_val = -none_val;
    return from_integer(none_val, python_int_type());
  }
  else if(func_name == "float")
  {
    if(args.is_array() && !as_array(args).empty())
    {
      exprt arg = convert_expression(*as_array(args).begin());
      if(!arg.is_nil())
        return typecast_exprt{arg, double_type()};
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
  // list() / sorted() / reversed() — return nondet list
  else if(
    func_name == "list" || func_name == "sorted" || func_name == "reversed" ||
    func_name == "enumerate")
  {
    return side_effect_expr_nondett{
      python_list_type(python_int_type()), get_location(expr)};
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
  // isinstance(obj, Class) — static type check
  else if(func_name == "isinstance")
  {
    if(args.is_array() && as_array(args).size() >= 2)
    {
      auto it = as_array(args).begin();
      exprt obj = convert_expression(*it);
      ++it;
      // Second arg is the class name
      std::string cls_name;
      if(is_node_type(*it, "Name"))
        cls_name = json_string(json_member(*it, "id"));

      if(!obj.is_nil() && !cls_name.empty() && obj.type().id() == ID_struct)
      {
        const auto &st = to_struct_type(obj.type());
        std::string tag = id2string(st.get_tag());
        // Extract the actual class name from the tag
        std::string obj_class;
        if(tag.substr(0, 13) == "python_class_")
          obj_class = tag.substr(13);

        // Check if obj_class is cls_name or inherits from it
        if(!obj_class.empty())
        {
          std::string check = obj_class;
          while(!check.empty())
          {
            if(check == cls_name)
              return true_exprt{};
            // Walk up the inheritance chain
            auto it = class_bases.find(check);
            if(it != class_bases.end() && !it->second.empty())
              check = it->second[0]; // single inheritance
            else
              break;
          }
          return false_exprt{};
        }
      }
      // If we can't determine statically, return nondet bool
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
        // abs(x) = x >= 0 ? x : -x
        return if_exprt{
          binary_relation_exprt{arg, ID_ge, safe_zero(arg.type())},
          arg,
          unary_minus_exprt{arg}};
      }
    }
    return nil_exprt{};
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
        is_node_type(stmt, "FunctionDef") &&
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

  // Remove any remaining nil arguments (shouldn't happen with correct code)
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

  // Dict subscript with string key: d["key"] → d.key (member access)
  // Check this BEFORE converting the slice, since we need the raw string.
  if(value.type().id() == ID_struct)
  {
    const auto &st = to_struct_type(value.type());
    if(id2string(st.get_tag()) == "python_dict")
    {
      const jsont &slice_node = json_member(expr, "slice");
      if(is_node_type(slice_node, "Constant"))
      {
        const jsont &sv = json_member(slice_node, "value");
        if(sv.is_string())
        {
          std::string key = sv.value;
          for(char &c : key)
          {
            if(!std::isalnum(c) && c != '_')
              c = '_';
          }
          if(st.has_component(key))
            return member_exprt{value, key, st.get_component(key).type()};
        }
      }
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
    member_exprt length{value, "length", python_int_type()};

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

  // Build a struct type with one field per key (string keys only)
  struct_typet::componentst components;
  exprt::operandst field_values;

  auto key_it = as_array(keys).begin();
  auto val_it = as_array(values).begin();
  for(; key_it != as_array(keys).end(); ++key_it, ++val_it)
  {
    // Only support string literal keys
    if(!is_node_type(*key_it, "Constant"))
      continue;
    const jsont &key_val = json_member(*key_it, "value");
    if(!key_val.is_string())
      continue;

    std::string key_name = key_val.value;
    exprt val = convert_expression(*val_it);
    if(val.is_nil())
      return nil_exprt{};

    // Sanitize key name for use as a struct field (replace spaces, etc.)
    for(char &c : key_name)
    {
      if(!std::isalnum(c) && c != '_')
        c = '_';
    }

    components.push_back(struct_typet::componentt{key_name, val.type()});
    field_values.push_back(val);
  }

  struct_typet dict_type{components};
  dict_type.set_tag("python_dict");
  return struct_exprt{std::move(field_values), dict_type};
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
  else if(node_type == "FunctionDef")
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
        }
      }
      result = std::move(del_block);
    }
    else
      result = code_skipt{};
  }
  else if(node_type == "With")
    result = convert_with(stmt);
  else if(node_type == "Try" || node_type == "TryStar")
    result = convert_try(stmt);
  else if(node_type == "Global")
  {
    // Track global names for the current function scope
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
  // x: int = 5
  const jsont &target = json_member(stmt, "target");
  const jsont &annotation = json_member(stmt, "annotation");
  const jsont &value = json_member(stmt, "value");

  std::string var_name = json_string(json_member(target, "id"));
  typet var_type = convert_type_annotation(annotation);
  source_locationt loc = get_location(stmt);

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

  const symbolt &sym = symbol_table.lookup_ref(symbol_id);

  // Type cast if needed
  if(rhs.type() != sym.type)
    rhs = safe_typecast(rhs, sym.type);

  code_frontend_assignt assign{sym.symbol_expr(), rhs};
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
              block.add(code_frontend_assignt{
                symbol_table.lookup_ref(sym_id).symbol_expr(), field_expr});
            }
          }
          idx++;
        }
        continue;
      }
    }

    // Handle subscript assignment: lst[i] = value
    if(is_node_type(target, "Subscript"))
    {
      exprt obj = convert_expression(json_member(target, "value"));
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
      if(src_numeric && tgt_numeric)
      {
        rhs = safe_typecast(rhs, existing->type);
        // Fall through to normal assignment below
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
      try_depth > 0 &&
      (typed_rhs.id() == ID_side_effect || rhs_has_side_effect))
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
        code_frontend_assignt eval{tmp_sym.symbol_expr(), typed_rhs};
        eval.add_source_location() = loc;
        block.add(std::move(eval));

        // Guard the actual assignment
        code_frontend_assignt assign{sym.symbol_expr(), tmp_sym.symbol_expr()};
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

    exprt start, stop;
    if(as_array(range_args).size() == 1)
    {
      start = from_integer(0, int_type);
      stop = convert_expression(*as_array(range_args).begin());
    }
    else
    {
      start = convert_expression(*as_array(range_args).begin());
      stop = convert_expression(*std::next(as_array(range_args).begin(), 1));
    }

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
    body_block.add(code_frontend_assignt{
      loop_sym, plus_exprt{loop_sym, from_integer(1, int_type)}});

    code_whilet while_stmt{
      binary_relation_exprt{loop_sym, ID_lt, stop}, std::move(body_block)};
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
  if(!is_list && !is_string)
  {
    log.error() << "for-in iteration requires a list or string"
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

      block.add(code_frontend_returnt{tmp_sym.symbol_expr()});
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
      ret_val =
        safe_typecast(ret_val, to_code_type(func_sym->type).return_type());
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
  const jsont &returns = json_member(stmt, "returns");
  typet return_type;
  if(!returns.is_null())
  {
    return_type = convert_type_annotation(returns);
  }
  else
  {
    // No return annotation — scan body for return statements
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
            has_value_return = true;
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

    if(has_value_return)
      return_type = python_int_type();
    else
      return_type = empty_typet{};
  }

  code_typet func_type{parameters, return_type};

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

  code_blockt body_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      body_block.add(convert_statement(s));
  }

  current_function = saved_function;
  global_names = saved_globals;

  // Update the symbol with the body
  symbolt *sym_ptr = symbol_table.get_writeable(symbol_id);
  if(sym_ptr != nullptr)
    sym_ptr->value = body_block;

  // Function definitions don't produce executable code at the call site
  return code_skipt{};
}

// PLR §8.9: Class definitions
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
        is_node_type(item, "FunctionDef") &&
        json_string(json_member(item, "name")) == "__init__")
      {
        init_method = &item;
        break;
      }
    }
  }

  // Inherit fields from base classes (layout-compatible for dispatch)
  const jsont &bases = json_member(stmt, "bases");
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
            // Skip __class_tag (added separately)
            if(id2string(comp.get_name()) == "__class_tag")
              continue;
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
      if(is_node_type(item, "FunctionDef"))
      {
        std::string method_name = json_string(json_member(item, "name"));

        // Build method with class-qualified name
        const jsont &args_node = json_member(item, "args");
        const jsont &params = json_member(args_node, "args");

        code_typet::parameterst parameters;
        if(params.is_array())
        {
          for(const auto &param : as_array(params))
          {
            std::string param_name = json_string(json_member(param, "arg"));
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
    long type_hash = 0;
    for(char c : exc_type)
      type_hash += static_cast<unsigned char>(c);
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
    // Inside a function: return a default value (exception propagates)
    // The caller's try/except will check __exception_active.
    // Check if the function returns void
    irep_idt func_id{"python::" + current_function};
    const symbolt *func_sym = symbol_table.lookup(func_id);
    if(
      func_sym != nullptr &&
      to_code_type(func_sym->type).return_type().id() == ID_empty)
    {
      block.add(code_frontend_returnt{});
    }
    else
    {
      typet ret_type = to_code_type(func_sym->type).return_type();
      block.add(code_frontend_returnt{safe_zero(ret_type)});
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
// "The try statement specifies exception handlers and/or cleanup code
// for a group of statements."
codet python_convertert::convert_try(const jsont &stmt)
{
  code_blockt block;
  source_locationt loc = get_location(stmt);

  irep_idt exc_id{"python::__exception_active"};
  const symbolt *exc_sym = symbol_table.lookup(exc_id);

  // Execute the try body
  const jsont &body = json_member(stmt, "body");
  try_depth++;
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      block.add(convert_statement(s));
  }
  try_depth--;

  // Check for except handlers
  const jsont &handlers = json_member(stmt, "handlers");
  if(handlers.is_array() && !as_array(handlers).empty() && exc_sym != nullptr)
  {
    // Build the except body
    code_blockt except_block;

    // Clear the exception flag
    code_frontend_assignt clear_flag{exc_sym->symbol_expr(), false_exprt{}};
    clear_flag.add_source_location() = loc;
    except_block.add(std::move(clear_flag));

    // Execute the first handler's body with type checking
    const jsont &handler = *as_array(handlers).begin();
    const jsont &handler_type = json_member(handler, "type");

    // Check if handler specifies a type (except TypeError: ...)
    const symbolt *exc_type_sym =
      symbol_table.lookup("python::__exception_type");
    bool has_type_check = false;
    exprt type_match = true_exprt{};

    if(
      !handler_type.is_null() && is_node_type(handler_type, "Name") &&
      exc_type_sym != nullptr)
    {
      std::string handler_type_name =
        json_string(json_member(handler_type, "id"));
      if(handler_type_name != "Exception" && !handler_type_name.empty())
      {
        // Compute hash of handler type name
        long type_hash = 0;
        for(char c : handler_type_name)
          type_hash += static_cast<unsigned char>(c);
        type_match = equal_exprt{
          exc_type_sym->symbol_expr(),
          from_integer(type_hash, python_int_type())};
        has_type_check = true;
      }
    }

    const jsont &handler_body = json_member(handler, "body");
    if(handler_body.is_array())
    {
      for(const auto &s : as_array(handler_body))
        except_block.add(convert_statement(s));
    }

    // Build the else body (runs when no exception)
    code_blockt else_block;
    const jsont &orelse = json_member(stmt, "orelse");
    if(orelse.is_array())
    {
      for(const auto &s : as_array(orelse))
        else_block.add(convert_statement(s));
    }

    // if(__exception_active && type_matches) { clear; except_body }
    // else { else_body }
    exprt condition = exc_sym->symbol_expr();
    if(has_type_check)
      condition = and_exprt{condition, type_match};

    code_ifthenelset if_exc{
      condition, std::move(except_block), std::move(else_block)};
    if_exc.add_source_location() = loc;
    block.add(std::move(if_exc));
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
    if(is_node_type(stmt, "FunctionDef"))
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
                    var_type = python_string_type();
                }
                else if(is_node_type(val, "List"))
                  var_type = python_list_type(python_int_type());
                else if(
                  is_node_type(val, "Tuple") || is_node_type(val, "Dict") ||
                  is_node_type(val, "Call") || is_node_type(val, "ListComp") ||
                  is_node_type(val, "Lambda") || is_node_type(val, "Set") ||
                  is_node_type(val, "Subscript"))
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
      if(is_node_type(stmt, "FunctionDef"))
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
      if(
        sym.type.id() == ID_struct &&
        to_struct_type(sym.type).get_tag() == tag && sym.type != cls_type)
      {
        symbol_table.get_writeable_ref(sym.name).type = cls_type;
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

  return false;
}
