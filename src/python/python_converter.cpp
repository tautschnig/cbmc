/// \file
/// Python to GOTO converter — converts Python JSON AST to CBMC symbol table

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
    condition = typecast_exprt{condition, bool_typet{}};

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
  else if(type_name == "list")
    return python_list_type(python_int_type()); // unparameterized list
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

exprt python_convertert::convert_expression(const jsont &expr)
{
  std::string node_type = json_string(json_member(expr, "_type"));

  if(node_type == "Constant")
    return convert_constant(expr);
  else if(node_type == "Name")
    return convert_name(expr);
  else if(node_type == "BinOp")
    return convert_bin_op(expr);
  else if(node_type == "UnaryOp")
    return convert_unary_op(expr);
  else if(node_type == "BoolOp")
    return convert_bool_op(expr);
  else if(node_type == "Compare")
    return convert_compare(expr);
  else if(node_type == "Call")
    return convert_call(expr);
  else if(node_type == "IfExp")
    return convert_if_exp(expr);
  else if(node_type == "Subscript")
    return convert_subscript(expr);
  else if(node_type == "Tuple")
    return convert_tuple(expr);
  else if(node_type == "List")
    return convert_list(expr);
  else if(node_type == "Attribute")
    return convert_attribute(expr);
  else if(node_type == "Dict")
    return convert_dict(expr);
  else if(node_type == "Set")
    return convert_list(expr); // Model sets as lists
  else if(node_type == "ListComp")
    return convert_list_comp(expr);
  else if(node_type == "Lambda")
    return convert_lambda(expr);
  else
  {
    log.error() << "Unsupported Python expression type: " << node_type
                << messaget::eom;
    return nil_exprt{};
  }
}

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
    // Python None — for now treat as 0
    return from_integer(0, python_int_type());
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
    // String literal → python_str struct { length, data[] }
    std::string str_val = value.value;
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

exprt python_convertert::convert_name(const jsont &expr)
{
  std::string id = json_string(json_member(expr, "id"));

  if(id == "True")
    return true_exprt{};
  else if(id == "False")
    return false_exprt{};
  else if(id == "None")
    return from_integer(0, python_int_type());

  // Look up in symbol table — try function-scoped first, then global
  const symbolt *sym = nullptr;
  if(!current_function.empty())
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

exprt python_convertert::convert_bin_op(const jsont &expr)
{
  exprt left = convert_expression(json_member(expr, "left"));
  exprt right = convert_expression(json_member(expr, "right"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(left.is_nil() || right.is_nil())
    return nil_exprt{};

  // String concatenation
  if(
    is_python_string_type(left.type()) && is_python_string_type(right.type()) &&
    op == "Add")
  {
    struct_typet str_type = python_string_type();
    const auto &data_type = to_array_type(str_type.components()[1].type());

    // length = left.length + right.length
    exprt new_length = plus_exprt{
      member_exprt{left, "length", python_int_type()},
      member_exprt{right, "length", python_int_type()}};

    // data is nondet (content tracking is future work)
    side_effect_expr_nondett nondet_data{data_type, source_locationt{}};

    struct_exprt result{{new_length, nondet_data}, str_type};
    return std::move(result);
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
      notequal_exprt{right, from_integer(0, right.type())},
      "division-by-zero",
      "division by zero",
      get_location(expr));
    return div_exprt{left, right};
  }
  else if(op == "Mod")
  {
    add_check(
      notequal_exprt{right, from_integer(0, right.type())},
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

exprt python_convertert::convert_unary_op(const jsont &expr)
{
  exprt operand = convert_expression(json_member(expr, "operand"));
  std::string op = json_string(json_member(json_member(expr, "op"), "_type"));

  if(operand.is_nil())
    return nil_exprt{};

  if(op == "USub")
    return unary_minus_exprt{operand};
  else if(op == "UAdd")
    return operand;
  else if(op == "Not")
    return not_exprt{operand};
  else
  {
    log.error() << "Unsupported unary operator: " << op << messaget::eom;
    return nil_exprt{};
  }
}

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
      result = and_exprt{result, next};
    else if(op == "Or")
      result = or_exprt{result, next};
    else
    {
      log.error() << "Unsupported bool operator: " << op << messaget::eom;
      return nil_exprt{};
    }
  }

  return result;
}

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

    // Type promotion for comparisons
    if(current_left.type() != right.type())
    {
      if(current_left.type().id() == ID_floatbv)
        right = typecast_exprt{right, current_left.type()};
      else if(right.type().id() == ID_floatbv)
        current_left = typecast_exprt{current_left, right.type()};
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

exprt python_convertert::convert_call(const jsont &expr)
{
  const jsont &func = json_member(expr, "func");
  const jsont &args = json_member(expr, "args");

  std::string func_name;
  if(is_node_type(func, "Name"))
    func_name = json_string(json_member(func, "id"));
  else if(is_node_type(func, "Attribute"))
  {
    // Method call: obj.method(args)
    std::string method_name = json_string(json_member(func, "attr"));
    exprt obj = convert_expression(json_member(func, "value"));
    if(obj.is_nil())
      return nil_exprt{};

    // Find the class name from the object's type
    if(obj.type().id() == ID_struct)
    {
      const auto &st = to_struct_type(obj.type());
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
            arguments.push_back(obj); // already a pointer
          else
            arguments.push_back(address_of_exprt{obj});
          if(args.is_array())
          {
            for(const auto &arg : as_array(args))
              arguments.push_back(convert_expression(arg));
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
    return from_integer(0, python_int_type());
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
    return from_integer(0, python_int_type());
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
          }
        }
      }
      // Non-generator argument: all([True, False]) etc.
      exprt arg_expr = convert_expression(arg);
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
          binary_relation_exprt{arg, ID_ge, from_integer(0, arg.type())},
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
        irep_idt op = (func_name == "min") ? ID_lt : ID_gt;
        return if_exprt{binary_relation_exprt{a, op, b}, a, b};
      }
    }
    return nil_exprt{};
  }

  // Regular function call — check if it's a class constructor
  if(class_types.count(func_name))
  {
    // Constructor call as expression: create a nondet struct and call __init__
    // This handles cases like return Foo(x) or f(Foo(x))
    const struct_typet &cls_type = class_types[func_name];
    side_effect_expr_nondett self_nondet{cls_type, get_location(expr)};

    irep_idt init_id{"python::" + func_name + "::__init__"};
    const symbolt *init_sym = symbol_table.lookup(init_id);
    if(init_sym != nullptr)
    {
      exprt::operandst init_args;
      init_args.push_back(self_nondet);
      if(args.is_array())
      {
        for(const auto &arg : as_array(args))
          init_args.push_back(convert_expression(arg));
      }
      // We can't easily call __init__ and return the struct as a pure
      // expression. Return nondet of the class type — the caller
      // (convert_assign) handles the proper init for assignments.
      // For return statements and nested expressions, this is an
      // overapproximation.
    }
    return std::move(self_nondet);
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

  side_effect_expr_function_callt call{
    sym->symbol_expr(),
    std::move(arguments),
    func_type.return_type(),
    get_location(expr)};

  return std::move(call);
}

exprt python_convertert::convert_if_exp(const jsont &expr)
{
  exprt test = convert_expression(json_member(expr, "test"));
  exprt body = convert_expression(json_member(expr, "body"));
  exprt orelse = convert_expression(json_member(expr, "orelse"));

  if(test.is_nil() || body.is_nil() || orelse.is_nil())
    return nil_exprt{};

  return if_exprt{test, body, orelse};
}

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

  // List/string slicing: lst[1:4] → new list with elements [1..4)
  const jsont &slice_json = json_member(expr, "slice");
  if(is_node_type(slice_json, "Slice") && is_python_list_type(value.type()))
  {
    const jsont &lower_json = json_member(slice_json, "lower");
    const jsont &upper_json = json_member(slice_json, "upper");

    exprt lower = lower_json.is_null() ? from_integer(0, signedbv_typet{64})
                                       : convert_expression(lower_json);
    exprt upper = upper_json.is_null()
                    ? member_exprt{value, "length", signedbv_typet{64}}
                    : convert_expression(upper_json);

    // For constant bounds, build the result list directly
    const auto &list_st = to_struct_type(value.type());
    const auto &data_type = to_array_type(list_st.components()[1].type());
    typet elem_type = data_type.element_type();
    member_exprt src_data{value, "data", data_type};

    struct_typet result_type = python_list_type(elem_type);
    array_typet result_data_type{
      elem_type, from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64})};

    // new_length = upper - lower
    exprt new_length = minus_exprt{upper, lower};

    // Build data: result[i] = src[lower + i]
    exprt::operandst result_elems;
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
    {
      exprt idx = from_integer(i, signedbv_typet{64});
      result_elems.push_back(index_exprt{src_data, plus_exprt{lower, idx}});
    }

    array_exprt result_data{std::move(result_elems), result_data_type};
    return struct_exprt{{new_length, result_data}, result_type};
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
        binary_relation_exprt{slice, ID_ge, from_integer(0, slice.type())},
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
    add_check(
      and_exprt{
        binary_relation_exprt{slice, ID_ge, from_integer(0, slice.type())},
        binary_relation_exprt{slice, ID_lt, length}},
      "index-out-of-bounds",
      "list index out of range",
      get_location(expr));

    const auto &st = to_struct_type(value.type());
    const auto &data_type = to_array_type(st.components()[1].type());
    member_exprt data{value, "data", data_type};
    return index_exprt{data, slice};
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
    data_elems.push_back(from_integer(0, elem_type));

  array_exprt data{std::move(data_elems), data_type};
  exprt length =
    from_integer(static_cast<long long>(elements.size()), python_int_type());

  return struct_exprt{{length, data}, list_type};
}

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

  log.error() << "Cannot access attribute '" << attr << "'" << messaget::eom;
  return nil_exprt{};
}

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

exprt python_convertert::convert_list_comp(const jsont &expr)
{
  // [elt for target in iter] — simple single-generator case
  const jsont &elt = json_member(expr, "elt");
  const jsont &generators = json_member(expr, "generators");

  if(!generators.is_array() || as_array(generators).empty())
    return nil_exprt{};

  const jsont &gen = *as_array(generators).begin();
  const jsont &gen_target = json_member(gen, "target");
  const jsont &gen_iter = json_member(gen, "iter");

  // Only handle literal list iterables for now
  if(!is_node_type(gen_iter, "List"))
  {
    log.warning() << "List comprehension only supports literal list iterables"
                  << messaget::eom;
    return nil_exprt{};
  }

  std::string iter_var = json_string(json_member(gen_target, "id"));
  const jsont &iter_elts = json_member(gen_iter, "elts");
  if(!iter_elts.is_array())
    return nil_exprt{};

  // For literal list iterables, unroll: evaluate elt for each value.
  // We create the iteration variable, assign each value, and evaluate elt.
  std::string qname = qualify_name(iter_var);
  irep_idt iter_sym_id{qname};

  // First, determine element type from the first iterable element
  if(as_array(iter_elts).empty())
    return nil_exprt{};

  exprt first_val = convert_expression(*as_array(iter_elts).begin());
  if(first_val.is_nil())
    return nil_exprt{};

  if(symbol_table.lookup(iter_sym_id) == nullptr)
  {
    symbolt sym{iter_sym_id, first_val.type(), "python"};
    sym.base_name = iter_var;
    sym.is_lvalue = true;
    sym.is_state_var = true;
    symbol_table.add(sym);
  }

  // For each iterable element, evaluate elt.
  // Since elt references the iteration variable symbolically, all results
  // will be the same symbolic expression. For literal iterables, we need
  // to substitute. Use a simple approach: for each concrete value, build
  // the elt expression and replace the symbol reference with the value.
  exprt::operandst elements;
  for(const auto &iter_val_json : as_array(iter_elts))
  {
    exprt iter_val = convert_expression(iter_val_json);
    // Evaluate elt — it will reference the symbol.
    // We then substitute the symbol with the concrete value.
    exprt elt_expr = convert_expression(elt);
    // Simple substitution: replace symbol_exprt for iter_var with iter_val
    std::function<void(exprt &)> substitute = [&](exprt &e)
    {
      if(
        e.id() == ID_symbol &&
        to_symbol_expr(e).get_identifier() == iter_sym_id)
      {
        e = iter_val;
      }
      else
      {
        for(auto &op : e.operands())
          substitute(op);
      }
    };
    substitute(elt_expr);
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
    data_elems.push_back(from_integer(0, elem_type));

  array_exprt data{std::move(data_elems), data_type};
  exprt length =
    from_integer(static_cast<long long>(elements.size()), python_int_type());

  return struct_exprt{{length, data}, list_type};
}

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
    rhs = typecast_exprt{rhs, sym.type};

  code_frontend_assignt assign{sym.symbol_expr(), rhs};
  assign.add_source_location() = loc;
  return std::move(assign);
}

codet python_convertert::convert_assign(const jsont &stmt)
{
  // x = expr  OR  a = b = expr (multiple targets)
  const jsont &targets = json_member(stmt, "targets");
  const jsont &value = json_member(stmt, "value");

  if(!targets.is_array() || as_array(targets).empty())
    return code_skipt{};

  source_locationt loc = get_location(stmt);

  // Check if RHS is a constructor call: x = ClassName(args)
  if(
    is_node_type(value, "Call") &&
    is_node_type(json_member(value, "func"), "Name"))
  {
    std::string call_name =
      json_string(json_member(json_member(value, "func"), "id"));
    if(class_types.count(call_name))
    {
      // Constructor: declare var as struct, call __init__(&var, args)
      const struct_typet &cls_type = class_types[call_name];
      const jsont &first_target = *as_array(targets).begin();
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
    if(is_node_type(target, "Tuple"))
    {
      const jsont &elts = json_member(target, "elts");
      if(elts.is_array() && is_python_tuple_type(rhs.type()))
      {
        const auto &tuple_st = to_struct_type(rhs.type());
        std::size_t idx = 0;
        for(const auto &elt : as_array(elts))
        {
          std::string elt_name = json_string(json_member(elt, "id"));
          std::string field = "_" + std::to_string(idx);
          if(tuple_st.has_component(field))
          {
            typet field_type = tuple_st.get_component(field).type();
            member_exprt field_expr{rhs, field, field_type};

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
            const symbolt &sym = symbol_table.lookup_ref(sym_id);
            code_frontend_assignt assign{sym.symbol_expr(), field_expr};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
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
                typed_rhs = typecast_exprt{typed_rhs, lhs.type()};
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
              typed_rhs = typecast_exprt{typed_rhs, lhs.type()};
            code_frontend_assignt assign{lhs, typed_rhs};
            assign.add_source_location() = loc;
            block.add(std::move(assign));
            continue;
          }
        }
      }
    }

    std::string var_name = json_string(json_member(target, "id"));
    std::string qualified_name = qualify_name(var_name);
    irep_idt symbol_id{qualified_name};

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
      typed_rhs = typecast_exprt{typed_rhs, sym.type};

    code_frontend_assignt assign{sym.symbol_expr(), typed_rhs};
    assign.add_source_location() = loc;
    block.add(std::move(assign));
  }

  if(block.statements().size() == 1)
    return block.statements().front();
  return std::move(block);
}

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

codet python_convertert::convert_assert(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  // Ensure the test is boolean
  if(test.type() != bool_typet{})
    test = typecast_exprt{test, bool_typet{}};

  source_locationt loc = get_location(stmt);
  loc.set_property_class("assertion");
  loc.set_comment("Python assertion");

  code_assertt assertion{test};
  assertion.add_source_location() = loc;
  return std::move(assertion);
}

codet python_convertert::convert_if(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = typecast_exprt{test, bool_typet{}};

  // Convert body
  code_blockt then_block;
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      then_block.add(convert_statement(s));
  }

  // Convert orelse (may be empty, elif chain, or else block)
  const jsont &orelse = json_member(stmt, "orelse");
  if(orelse.is_array() && !as_array(orelse).empty())
  {
    code_blockt else_block;
    for(const auto &s : as_array(orelse))
      else_block.add(convert_statement(s));

    code_ifthenelset if_stmt{
      test, std::move(then_block), std::move(else_block)};
    if_stmt.add_source_location() = get_location(stmt);
    return std::move(if_stmt);
  }
  else
  {
    code_ifthenelset if_stmt{test, std::move(then_block)};
    if_stmt.add_source_location() = get_location(stmt);
    return std::move(if_stmt);
  }
}

codet python_convertert::convert_while(const jsont &stmt)
{
  exprt test = convert_expression(json_member(stmt, "test"));
  if(test.is_nil())
    return code_skipt{};

  if(test.type() != bool_typet{})
    test = typecast_exprt{test, bool_typet{}};

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
    elem_type = int_type; // string iteration yields char codes for now

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

  // x = iterable.data[__idx]
  body_block.add(code_frontend_assignt{loop_var, index_exprt{data, idx_var}});

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

codet python_convertert::convert_return(const jsont &stmt)
{
  const jsont &value = json_member(stmt, "value");

  if(value.is_null())
    return code_frontend_returnt{};

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
    return code_frontend_returnt{};

  code_frontend_returnt ret{ret_val};
  ret.add_source_location() = get_location(stmt);
  return std::move(ret);
}

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
                      << func_name << "' has no type annotation; assuming int"
                      << messaget::eom;
      }
      typet param_type = convert_type_annotation(annotation);

      code_typet::parametert p{param_type};
      p.set_identifier("python::" + func_name + "::" + param_name);
      p.set_base_name(param_name);
      parameters.push_back(p);
    }
  }

  // Return type
  const jsont &returns = json_member(stmt, "returns");
  typet return_type =
    returns.is_null() ? empty_typet{} : convert_type_annotation(returns);

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

  // Scan __init__ body for self.attr = ... assignments to determine fields
  if(init_method != nullptr)
  {
    const jsont &init_body = json_member(*init_method, "body");
    if(init_body.is_array())
    {
      for(const auto &s : as_array(init_body))
      {
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

        typet attr_type = python_int_type(); // default
        if(!rhs_name.empty())
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
                break;
              }
            }
          }
        }

        components.push_back(struct_typet::componentt{attr_name, attr_type});
      }
    }
  }

  struct_typet class_type{components};
  class_type.set_tag("python_class_" + class_name);
  class_types[class_name] = class_type;

  // Record base classes for isinstance checks
  const jsont &bases = json_member(stmt, "bases");
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
              param_type = convert_type_annotation(annotation);
            }

            code_typet::parametert p{param_type};
            p.set_identifier(
              "python::" + class_name + "::" + method_name + "::" + param_name);
            p.set_base_name(param_name);
            parameters.push_back(p);
          }
        }

        const jsont &returns = json_member(item, "returns");
        typet return_type =
          returns.is_null() ? empty_typet{} : convert_type_annotation(returns);

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
          cond = typecast_exprt{cond, bool_typet{}};
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

codet python_convertert::convert_break()
{
  return code_breakt{};
}

codet python_convertert::convert_continue()
{
  return code_continuet{};
}

codet python_convertert::convert_pass()
{
  return code_skipt{};
}

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

  // Set the exception flag
  irep_idt exc_id{"python::__exception_active"};
  const symbolt *exc_sym = symbol_table.lookup(exc_id);
  if(exc_sym != nullptr)
  {
    code_frontend_assignt set_flag{exc_sym->symbol_expr(), true_exprt{}};
    set_flag.add_source_location() = loc;
    block.add(std::move(set_flag));
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
      block.add(code_frontend_returnt{from_integer(0, python_int_type())});
    }
  }

  return std::move(block);
}

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

codet python_convertert::convert_try(const jsont &stmt)
{
  code_blockt block;
  source_locationt loc = get_location(stmt);

  irep_idt exc_id{"python::__exception_active"};
  const symbolt *exc_sym = symbol_table.lookup(exc_id);

  // Execute the try body
  const jsont &body = json_member(stmt, "body");
  if(body.is_array())
  {
    for(const auto &s : as_array(body))
      block.add(convert_statement(s));
  }

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

    // Execute the first handler's body (simplified: ignore exception type)
    const jsont &handler = *as_array(handlers).begin();
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

    // if(__exception_active) { clear; except_body } else { else_body }
    code_ifthenelset if_exc{
      exc_sym->symbol_expr(), std::move(except_block), std::move(else_block)};
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
    if(is_node_type(stmt, "FunctionDef") || is_node_type(stmt, "ClassDef"))
      continue;

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
