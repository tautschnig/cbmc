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

    // Detect Python complex-literal strings (e.g. "2j", "(1+2j)",
    // "1e3+2e-1j") and fold them into a python_complex struct
    // expression. We only attempt the fold for strings that
    // syntactically look like complex literals — ending in 'j' / 'J',
    // or wrapped in parentheses — to preserve the previous behaviour
    // of leaving plain strings (e.g. "hello", "42") as strings.
    //
    // The parser is exception-free: malformed inputs are returned as
    // std::nullopt, which falls through to the plain string-literal
    // path. Unlike the earlier std::stod-based code, no input
    // (including "+j", "-j", "++1j") can escape as an uncaught C++
    // exception. See python_complex_parser.h for the grammar.
    const bool ends_j =
      !str_val.empty() && (str_val.back() == 'j' || str_val.back() == 'J');
    const bool parenthesised =
      str_val.size() >= 2 && str_val.front() == '(' && str_val.back() == ')';
    if(ends_j || parenthesised)
    {
      if(auto cv = parse_python_complex_string(str_val); cv.has_value())
      {
        ieee_floatt real_f{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        real_f.from_double(cv->first);
        ieee_floatt imag_f{
          ieee_float_spect::double_precision(),
          ieee_floatt::rounding_modet::ROUND_TO_EVEN};
        imag_f.from_double(cv->second);
        struct_typet::componentst comps;
        comps.push_back(struct_typet::componentt{"real", double_type()});
        comps.push_back(struct_typet::componentt{"imag", double_type()});
        struct_typet ct{comps};
        ct.set_tag("python_complex");
        return struct_exprt{{real_f.to_expr(), imag_f.to_expr()}, ct};
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
     is_python_dict_type(to_pointer_type(sym->type).base_type())))
  {
    return dereference_exprt{sym->symbol_expr()};
  }

  return sym->symbol_expr();
}
