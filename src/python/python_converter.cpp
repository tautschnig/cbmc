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
#include <util/simplify_expr.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/string_constant.h>
#include <util/string_expr.h>
#include <util/symbol.h>

#include "python_converter_helpers.h"
#include "python_types.h"
#include "python_value_type.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

/// PLR: Python type annotations are not enforced at runtime,
/// but downstream operations on a value whose runtime type
/// disagrees with its annotation will often raise TypeError
/// (e.g. 'x: int = "hello"; x + 5'). Our value-tracking
/// trusts annotations, so without this check we'd miss those
/// TypeErrors — emitting an explicit annotation-mismatch
/// property at the site of declaration restores soundness.

/// Walk a type-annotation AST and extract the component types
/// of a Union shape. Recognises:
///   - Subscript(Name("Union"), Tuple(T1, T2, ...))
///   - Subscript(Attribute(_, "Union"), Tuple(T1, T2, ...))
///   - BinOp(left, BitOr, right) — PEP 604 `T1 | T2`
/// Returns empty vector for non-union annotations.
std::vector<typet>
python_convertert::extract_union_components(const jsont &annotation)
{
  std::vector<typet> components;
  if(is_node_type(annotation, "Subscript"))
  {
    const jsont &val = json_member(annotation, "value");
    bool is_union = false;
    if(is_node_type(val, "Name"))
      is_union = json_string(json_member(val, "id")) == "Union";
    else if(is_node_type(val, "Attribute"))
      is_union = json_string(json_member(val, "attr")) == "Union";
    if(is_union)
    {
      const jsont &slice = json_member(annotation, "slice");
      if(is_node_type(slice, "Tuple"))
      {
        const jsont &elts = json_member(slice, "elts");
        if(elts.is_array())
        {
          for(const auto &e : as_array(elts))
            components.push_back(convert_type_annotation(e));
        }
      }
      else
      {
        // Single-element Union[T] — degenerate, but accept.
        components.push_back(convert_type_annotation(slice));
      }
    }
  }
  else if(is_node_type(annotation, "BinOp"))
  {
    // PEP 604: T1 | T2  → BinOp(left, BitOr, right). Walk both
    // sides recursively to flatten chains like T1 | T2 | T3.
    std::string op =
      json_string(json_member(json_member(annotation, "op"), "_type"));
    if(op == "BitOr")
    {
      auto left = extract_union_components(json_member(annotation, "left"));
      auto right = extract_union_components(json_member(annotation, "right"));
      if(!left.empty())
        components.insert(components.end(), left.begin(), left.end());
      else
        components.push_back(
          convert_type_annotation(json_member(annotation, "left")));
      if(!right.empty())
        components.insert(components.end(), right.begin(), right.end());
      else
        components.push_back(
          convert_type_annotation(json_member(annotation, "right")));
    }
  }
  return components;
}

bool python_convertert::annotation_includes_none(const jsont &annotation)
{
  if(annotation.is_null())
    return false;
  // Optional[T] — explicit Optional name on either Subscript
  // shape (typing.Optional / Optional).
  if(is_node_type(annotation, "Subscript"))
  {
    const jsont &val = json_member(annotation, "value");
    std::string base;
    if(is_node_type(val, "Attribute"))
      base = json_string(json_member(val, "attr"));
    else if(is_node_type(val, "Name"))
      base = json_string(json_member(val, "id"));
    if(base == "Optional")
      return true;
    if(base == "Union")
    {
      const jsont &slice = json_member(annotation, "slice");
      if(is_node_type(slice, "Tuple"))
      {
        const jsont &elts = json_member(slice, "elts");
        if(elts.is_array())
        {
          for(const auto &e : as_array(elts))
            if(annotation_includes_none(e))
              return true;
        }
      }
      else if(annotation_includes_none(slice))
        return true;
    }
  }
  // PEP 604: T1 | T2 | None — recurse on both sides.
  if(is_node_type(annotation, "BinOp"))
  {
    std::string op =
      json_string(json_member(json_member(annotation, "op"), "_type"));
    if(op == "BitOr")
    {
      if(annotation_includes_none(json_member(annotation, "left")))
        return true;
      if(annotation_includes_none(json_member(annotation, "right")))
        return true;
    }
  }
  // Bare 'None' constant or Name.
  if(is_node_type(annotation, "Constant"))
  {
    const jsont &v = json_member(annotation, "value");
    if(v.is_null())
      return true;
  }
  if(is_node_type(annotation, "Name"))
  {
    if(json_string(json_member(annotation, "id")) == "None")
      return true;
  }
  return false;
}

bool python_convertert::union_annotation_violated(
  const irep_idt &sym_id,
  const exprt &actual_value) const
{
  auto it = union_annotation_components.find(sym_id);
  if(it == union_annotation_components.end())
    return false;
  const std::vector<typet> &components = it->second;
  if(components.empty())
    return false;
  // Determine the "effective" actual type. If the argument is
  // a struct literal of python_value form (e.g. result of
  // make_python_value(FLOAT, 3.14) — a struct_exprt with the
  // tag in operand[0]), unwrap it to the underlying type so the
  // category check below can detect a mismatch. Otherwise use
  // the value's static type.
  typet effective = actual_value.type();
  if(
    is_python_value_type(actual_value.type()) &&
    actual_value.id() == ID_struct && actual_value.operands().size() >= 4 &&
    actual_value.operands()[0].is_constant())
  {
    mp_integer tag_val;
    if(!to_integer(to_constant_expr(actual_value.operands()[0]), tag_val))
    {
      auto tag = static_cast<int>(tag_val.to_long());
      if(tag == static_cast<int>(python_type_tagt::INT))
        effective = python_int_type();
      else if(tag == static_cast<int>(python_type_tagt::FLOAT))
        effective = double_type();
      else if(tag == static_cast<int>(python_type_tagt::BOOL))
        effective = bool_typet{};
      else if(tag == static_cast<int>(python_type_tagt::STR))
        effective = python_string_type();
    }
  }
  if(is_python_value_type(effective))
    return false;
  // Union compatibility uses STRICT category matching — float
  // does NOT coerce to int, bool does NOT coerce to int — so
  // we don't accept Union[int, str] against a float arg as
  // compatible (which annotation_types_incompatible would,
  // because of Python's general numeric-coercion semantics).
  // PLR §3.2 / typing.Union explicitly enumerates the allowed
  // types; passing a non-listed type is a TypeError-equivalent.
  auto strict_category = [](const typet &t) -> int
  {
    if(t.id() == ID_bool)
      return 1; // bool is its own category for Union purposes
    if(t.id() == ID_signedbv || t.id() == ID_unsignedbv || t.id() == ID_integer)
      return 2; // int
    if(t.id() == ID_floatbv)
      return 3; // float
    if(is_python_string_type(t))
      return 4;
    if(is_python_list_type(t))
      return 5;
    if(is_python_dict_type(t))
      return 6;
    if(is_python_set_type(t))
      return 7;
    if(t.id() == ID_struct || t.id() == ID_struct_tag)
      return 8; // class
    return 0;
  };
  int actual_cat = strict_category(effective);
  if(actual_cat == 0)
    return false; // unknown — don't flag
  // A python_value (Any) component accepts ANY value, so the union is trivially
  // satisfiable. This includes the container ABCs we model as Any
  // (Sequence/Iterable/Mapping/Collection/Container/Reversible -> python_value):
  // without this, `s: Sequence[str] | None` (an Any-like component + None)
  // spuriously rejected a list argument even though a list IS a Sequence
  // (sequence_2/3/4). `None` is NOT python_value, so `int | None` still strictly
  // rejects a list. PLR-safe: this only SUPPRESSES a would-be mismatch, it can
  // never introduce one (and so cannot turn a real bug into a false proof).
  for(const typet &c : components)
    if(is_python_value_type(c))
      return false;
  // Match against each component. If any component shares the
  // strict category, the union is satisfied.
  for(const typet &c : components)
  {
    int comp_cat = strict_category(c);
    if(comp_cat == actual_cat)
      return false;
    // Special case: bool is acceptable wherever int is
    // expected (bool subtypes int in Python). So
    // Union[int, str] accepts True / False.
    if(comp_cat == 2 && actual_cat == 1)
      return false;
    // Class types share the broad category but may have
    // different specific tags; defer to MRO walk.
    if(comp_cat == 8 && actual_cat == 8)
    {
      auto strip = [](const std::string &tag) -> std::string
      {
        const std::string p{"python_class_"};
        return tag.compare(0, p.size(), p) == 0 ? tag.substr(p.size()) : tag;
      };
      std::string c_tag, a_tag;
      if(c.id() == ID_struct)
        c_tag = strip(id2string(to_struct_type(c).get_tag()));
      if(effective.id() == ID_struct)
        a_tag = strip(id2string(to_struct_type(effective).get_tag()));
      if(c_tag == a_tag)
        return false;
      auto mro_it_c = class_mro.find(a_tag);
      if(mro_it_c != class_mro.end())
        for(const auto &anc : mro_it_c->second)
          if(anc == c_tag)
            return false;
    }
  }
  return true;
}

bool python_convertert::annotation_types_incompatible(
  const typet &declared,
  const typet &actual) const
{
  // Tagged-union (Any) on either side → compatible by
  // duck-typing. Skip.
  if(is_python_value_type(declared) || is_python_value_type(actual))
    return false;
  // Equal types: compatible.
  if(declared == actual)
    return false;
  // Numeric types (int/float/bool). The numeric tower is one-directional for
  // assignability (PLR / typing): int and bool ARE assignable where a float is
  // declared (int→float promotes, bool <: int <: float), but a float is NOT
  // assignable where an int/bool is declared (narrowing loses the fraction).
  // Flag only the narrowing float→int(/bool) case; keep the widening / same-
  // kind combos compatible. (Shared helper: this also governs the assign-RHS
  // and return-value annotation checks.)
  auto is_numeric = [](const typet &t)
  {
    return t.id() == ID_signedbv || t.id() == ID_unsignedbv ||
           t.id() == ID_floatbv || t.id() == ID_bool || t.id() == ID_integer;
  };
  auto is_int_kind = [](const typet &t)
  {
    return t.id() == ID_signedbv || t.id() == ID_unsignedbv ||
           t.id() == ID_integer || t.id() == ID_bool;
  };
  if(is_numeric(declared) && is_numeric(actual))
    return is_int_kind(declared) && actual.id() == ID_floatbv;
  // Categorize the remaining types. Different categories →
  // obvious incompatibility (e.g. str vs int).
  auto category = [&](const typet &t) -> int
  {
    if(is_python_string_type(t))
      return 1;
    if(is_python_list_type(t))
      return 2;
    if(is_python_dict_type(t))
      return 3;
    if(is_python_set_type(t))
      return 4;
    if(is_numeric(t))
      return 5;
    if(t.id() == ID_struct || t.id() == ID_struct_tag)
      return 6; // class
    if(t.id() == ID_code || t.id() == ID_pointer)
      return 7; // callable / pointer
    return 0;
  };
  int dc = category(declared);
  int ac = category(actual);
  // Pointer-to-struct and struct of same layout: treat as
  // compatible (common for class method 'self' passed by
  // pointer, and for class-by-value parameters taking a
  // class instance). Similarly class-hierarchy polymorphism
  // is out of scope for this simple category check.
  if(declared.id() == ID_pointer && actual.id() == ID_struct)
    return false;
  if(declared.id() == ID_struct && actual.id() == ID_pointer)
    return false;
  // Function calls whose declared return type couldn't be resolved
  // by the front-end (e.g., boto3.client's Union[ClassA, ClassB,
  // ...] overload-fallback) become signedbv at the IR level. We
  // can't reliably tell them apart from genuine integer values, so
  // when the declared annotation is a class/struct, accept a
  // signedbv RHS as compatible. This trades a small amount of
  // precision (e.g., 'x: SomeClass = 42' won't be flagged) for
  // suppressing the systematic FP on `cloudwatch: CloudWatch =
  // boto3.client('cloudwatch')`-shaped patterns.
  //
  // Restricted to the class category (dc == 6). String / list /
  // dict / set declared types stay strictly typed against
  // signedbv arguments — `x: str = 42` and `foo(s: str)` called
  // as `foo(42)` are real annotation mismatches that PLR §3.1
  // soundness requires us to flag.
  if(dc == 6 && actual.id() == ID_signedbv)
    return false;
  if(dc == 0 || ac == 0)
    return false; // unknown category — don't flag
  // Same category but different exact type — refine for the
  // class case using class hierarchy.
  if(dc == ac)
  {
    // For classes, check the inheritance relationship: an
    // assignment `pet: Animal = Dog()` where Dog extends Animal
    // is compatible (Liskov substitution); `pet: Animal =
    // Car()` is not. Walk class_mro of the actual class; if
    // the declared class appears in the chain, it's compatible.
    if(dc == 6 && declared.id() == ID_struct && actual.id() == ID_struct)
    {
      auto strip_prefix = [](const std::string &tag) -> std::string
      {
        const std::string prefix{"python_class_"};
        if(tag.compare(0, prefix.size(), prefix) == 0)
          return tag.substr(prefix.size());
        return tag;
      };
      std::string declared_tag =
        strip_prefix(id2string(to_struct_type(declared).get_tag()));
      std::string actual_tag =
        strip_prefix(id2string(to_struct_type(actual).get_tag()));
      if(declared_tag == actual_tag)
        return false;
      auto it = class_mro.find(actual_tag);
      if(it != class_mro.end())
      {
        for(const std::string &ancestor : it->second)
          if(ancestor == declared_tag)
            return false;
      }
      // Different classes, no inheritance link → incompatible.
      return true;
    }
    return false;
  }
  // Different categories → incompatible.
  return true;
}

// Helper: extract std::string from a constant string struct expression
// Also checks string_constants map for tracked symbol values
std::optional<double> python_convertert::try_eval_double(const exprt &e) const
{
  const exprt *ce = &e;
  if(ce->id() == ID_typecast && ce->operands().size() == 1)
    ce = &ce->operands()[0];
  // PLR §8.7: a function call to a leaf function whose body is
  // `return <constant>` returns the recorded constant. The call still
  // happens at runtime (for any side-effects in the call wrapping),
  // but for compile-time constant-folding purposes we can substitute
  // the return value.
  if(
    ce->id() == ID_side_effect && ce->get(ID_statement) == ID_function_call &&
    !ce->operands().empty() && ce->operands()[0].id() == ID_symbol)
  {
    irep_idt fn_id = to_symbol_expr(ce->operands()[0]).get_identifier();
    auto it = function_return_constants.find(fn_id);
    if(it != function_return_constants.end())
      return it->second;
  }
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
  // PLR §3.2.1: bool is a subtype of int; True == 1, False == 0.
  // Honor that here so callers like complex(True, False) get the
  // right values via constant-fold.
  if(ce->is_constant() && ce->type().id() == ID_bool)
  {
    return ce->is_true() ? 1.0 : 0.0;
  }
  if(ce->is_constant() && ce->type().id() == ID_floatbv)
  {
    ieee_floatt fv{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    fv.from_expr(to_constant_expr(*ce));
    // Convert to a host double directly via the IEEE-754
    // bit-pattern. ieee_float_valuet::to_double preserves the
    // full mantissa when the format spec matches double_precision
    // (which it does here). We previously round-tripped via
    // to_ansi_c_string + strtod, which used the formatter's
    // default precision and silently truncated values like 1/3
    // to ~6 significant digits — that was visible as 8 ** (1/3)
    // being folded to 1.999999 instead of 2.0 (PLR §6.5).
    // NaN and infinity round-trip cleanly via to_double too
    // (std::pow handles them per IEEE-754).
    const ieee_float_valuet &as_value = fv;
    return as_value.to_double();
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

std::string python_convertert::ast_value_category(const jsont &node) const
{
  if(is_node_type(node, "Constant"))
  {
    const jsont &v = json_member(node, "value");
    if(v.is_null())
      return "none";
    if(v.is_string())
      return "str";
    if(v.is_number())
    {
      // Distinguish int vs float by presence of '.' or 'e'.
      if(
        v.value.find('.') != std::string::npos ||
        v.value.find('e') != std::string::npos ||
        v.value.find('E') != std::string::npos)
        return "float";
      return "int";
    }
    if(v.is_true() || v.is_false())
      return "bool";
    return std::string{};
  }
  if(is_node_type(node, "List"))
    return "list";
  if(is_node_type(node, "Dict"))
    return "dict";
  if(is_node_type(node, "Set"))
    return "set";
  if(is_node_type(node, "Tuple"))
    return "tuple";
  if(is_node_type(node, "JoinedStr") || is_node_type(node, "FormattedValue"))
    return "str"; // f-strings are strings
  if(is_node_type(node, "Bytes"))
    return "bytes";
  if(is_node_type(node, "NameConstant"))
  {
    const jsont &v = json_member(node, "value");
    if(v.is_null())
      return "none";
    if(v.is_true() || v.is_false())
      return "bool";
  }
  if(is_node_type(node, "UnaryOp"))
  {
    // Unary minus on a numeric literal preserves int/float.
    const jsont &operand = json_member(node, "operand");
    return ast_value_category(operand);
  }
  return std::string{};
}
std::optional<std::string>
python_convertert::extract_string_value(const exprt &e) const
{
  // Native SMT-String constant: the value is carried directly as the
  // constant's id (e.g. constant_exprt{"inf", smt_string}).
  if(e.id() == ID_constant && e.type().id() == ID_string)
    return id2string(to_constant_expr(e).get_value());
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
      if(
        data_op.id() == ID_address_of && data_op.operands().size() == 1 &&
        data_op.operands()[0].id() == ID_index)
        arr = &data_op.operands()[0].operands()[0];
      // Legacy format: operands()[1] is array directly
      if(data_op.id() == ID_array)
        arr = &data_op;
      if(arr != nullptr && arr->id() == ID_array)
      {
        // PLR §3.6: the string struct's length is the
        // code-point count, but the data array stores UTF-8
        // bytes. Read up to the array's full size: collect
        // bytes until we've seen `slen` non-continuation
        // bytes, plus any trailing continuation bytes for
        // the last code point.
        std::string s;
        mp_integer codepoints_seen = 0;
        for(std::size_t i = 0; i < arr->operands().size(); ++i)
        {
          if(!arr->operands()[i].is_constant())
            return std::nullopt;
          mp_integer ch;
          if(to_integer(to_constant_expr(arr->operands()[i]), ch))
            return std::nullopt;
          unsigned char byte = static_cast<unsigned char>(ch.to_ulong());
          // Stop at the first leading byte (non-continuation)
          // AFTER having collected all `slen` code points.
          // Continuation bytes (10xxxxxx, [0x80, 0xC0)) are
          // tail bytes of the current code point and are
          // collected together with the leading byte.
          bool is_continuation = byte >= 0x80 && byte < 0xC0;
          if(!is_continuation && codepoints_seen >= slen)
            break;
          s += static_cast<char>(byte);
          if(!is_continuation)
            ++codepoints_seen;
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

// PLR control-flow correctness: per-branch snapshot + merge for
// conversion-time constant-tracking maps. See the declaration of
// tracking_snapshott in python_converter.h for the full rationale.

python_convertert::tracking_snapshott
python_convertert::snapshot_tracking() const
{
  tracking_snapshott snap;
  snap.string_constants = string_constants;
  snap.dict_literals = dict_literals;
  snap.list_literals = list_literals;
  snap.tuple_literals = tuple_literals;
  snap.float_constants = float_constants;
  snap.alias_targets = alias_targets;
  return snap;
}

void python_convertert::restore_tracking(const tracking_snapshott &snap)
{
  string_constants = snap.string_constants;
  dict_literals = snap.dict_literals;
  list_literals = snap.list_literals;
  tuple_literals = snap.tuple_literals;
  float_constants = snap.float_constants;
  alias_targets = snap.alias_targets;
}

namespace
{
/// Merge two maps: keep the entry under key K only when both maps
/// have it AND the values compare equal. Used by merge_tracking
/// to discard entries that disagree across branches. Keys present
/// in only one map are dropped (path-dependent).
template <typename K, typename V, typename Eq>
std::map<K, V>
merge_maps(const std::map<K, V> &a, const std::map<K, V> &b, Eq eq)
{
  std::map<K, V> out;
  for(const auto &kv : a)
  {
    auto it = b.find(kv.first);
    if(it != b.end() && eq(kv.second, it->second))
      out.insert(kv);
  }
  return out;
}
} // namespace

void python_convertert::merge_tracking(
  const tracking_snapshott &state0,
  const tracking_snapshott &state1)
{
  string_constants = merge_maps(
    state0.string_constants,
    state1.string_constants,
    [](const std::string &x, const std::string &y) { return x == y; });
  auto expr_eq = [](const exprt &x, const exprt &y) { return x == y; };
  dict_literals =
    merge_maps(state0.dict_literals, state1.dict_literals, expr_eq);
  list_literals =
    merge_maps(state0.list_literals, state1.list_literals, expr_eq);
  tuple_literals =
    merge_maps(state0.tuple_literals, state1.tuple_literals, expr_eq);
  float_constants = merge_maps(
    state0.float_constants,
    state1.float_constants,
    [](double x, double y) { return x == y; });
  alias_targets = merge_maps(
    state0.alias_targets,
    state1.alias_targets,
    [](const irep_idt &x, const irep_idt &y) { return x == y; });
}

/// Back-end-dispatching Python string literal. See
/// python-string-phase2-backend-abstraction.md.
exprt python_convertert::python_string_literal(const std::string &s)
{
  // Native SMT-String back-end (Plan A): an opaque SMT String constant whose
  // smt2_conv lowering produces the SMT-LIB literal "...".
  if(use_smt_string_native)
    return constant_exprt{irep_idt{s}, string_typet{}};
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

/// PLR §3.1: pre-scan a body to populate `escaped_mutables`.
///
/// A name "escapes" if it appears in any of these positions:
///   * As a Name element of a List literal:    `[..., name, ...]`
///   * As a Name value of a Dict literal:      `{..., key: name}`
///   * As an argument to a list-mutating method:
///     `lst.append(name)`, `lst.extend(name)`, `lst.insert(_, name)`
///
/// The scan walks Assign / AnnAssign / AugAssign / Expr / If / For /
/// While / Try / With statements and recurses into nested blocks.
/// FunctionDef and ClassDef bodies are NOT recursed into here — those
/// scopes have their own pre-scan invocations.
///
/// Implementation note: we conservatively qualify names against the
/// current_function context. The pre-scan must therefore be invoked
/// AFTER current_function has been set for the scope being scanned
/// (i.e. inside convert_function_def's body conversion, after the
/// `current_function = ...` assignment).
void python_convertert::note_mutable_extraction(
  const irep_idt &lhs_id,
  const exprt &rhs,
  const jsont &value)
{
  // This assignment is not (or no longer) a mutable extraction by default.
  extracted_container_alias.erase(lhs_id);

  // Provable-None binding tracking (shared plain-/ann-assign chokepoint):
  // record a `= None` binding so a later ordering comparison on this
  // symbol is a TypeError; any other RHS clears it (rebinds also clear via
  // invalidate_reassigned_symbol).
  if(is_python_none_constant(rhs))
    none_constants.insert(lhs_id);
  else
    none_constants.erase(lhs_id);

  // Conditional-alias shapes: `m = l or <default>` / `m = l and x` (BoolOp)
  // yield the SAME OBJECT as a mutable Name operand on the selected branch
  // (PLR §6.11: and/or return an operand, not a fresh bool). Binding by
  // VALUE was a false proof -- a later `m.append(x)` did not propagate to
  // `l` (ESBMC github_5955_alias_boolop_fail). Record the (single) mutable
  // Name operand as an extraction alias so an in-place mutation of `m`
  // HAVOCS it (sound over-approximation via the mutation-channel machinery;
  // precise pointer-aliasing would need per-branch truthiness). A ternary
  // with two distinct mutable operands is left as a documented residual
  // (the havoc map holds one source).
  if(is_node_type(value, "BoolOp"))
  {
    const jsont &vals = json_member(value, "values");
    std::vector<const jsont *> mut_names;
    if(vals.is_array())
      for(const auto &v : as_array(vals))
        if(is_node_type(v, "Name"))
        {
          exprt e = convert_expression(v);
          if(
            e.id() == ID_symbol &&
            (is_python_list_type(e.type()) || is_python_dict_type(e.type()) ||
             is_python_set_type(e.type())))
            mut_names.push_back(&v);
        }
    if(mut_names.size() == 1)
    {
      exprt src = convert_expression(*mut_names.front());
      extracted_container_alias[lhs_id] = src;
    }
    return;
  }

  if(!is_node_type(value, "Subscript"))
    return;

  // Only a result that may be a mutable object can be mutated in place.
  const typet &rt = rhs.type();
  if(
    !is_python_list_type(rt) && !is_python_dict_type(rt) &&
    !is_python_set_type(rt) && !is_python_value_type(rt))
    return;

  // SPIKE (--python-ref-mutables): under reference semantics a nested mutable
  // container element is stored by reference -- a python_value wrapping a
  // per-instance heap pointer (see convert_list). Extracting it (`r = c[i]`)
  // copies the POINTER, so a later mutation through `r` propagates to the
  // source slot precisely. The havoc-on-mutation guard below is the by-value
  // fallback; for a wrapped reference it is not only unnecessary but actively
  // destroys the precise result (it nondet-havocs the source). Skip recording
  // the alias so the guard never fires for the reference case. A non-reference
  // (by-value) subscript result keeps a concrete container type (list/dict/set)
  // and still records the alias below, so soundness for the unwrapped world is
  // unchanged.
  if(ref_mutables && is_python_value_type(rt))
    return;

  // The container must be a plain Name so we can re-resolve it without
  // re-emitting side effects, and havoc it later.
  const jsont &cont = json_member(value, "value");
  if(!is_node_type(cont, "Name"))
    return;
  exprt container = convert_expression(cont);
  if(container.id() != ID_symbol)
    return;
  const typet &ct = container.type();
  if(
    is_python_list_type(ct) || is_python_dict_type(ct) ||
    is_python_set_type(ct) || is_python_value_type(ct))
    extracted_container_alias[lhs_id] = container;

  // §0 write-through recording (spike doc §14b): the dict subscript's
  // lvalue slot with a MATERIALISED index temp is a re-emittable anchor.
  // Record it only for a non-escaped dict source (escape = unseen
  // structural mutation would make the write-through a FALSE WRITE).
  extracted_slot_alias.erase(lhs_id);
  const irep_idt src_id = to_symbol_expr(container).get_identifier();
  if(
    rhs.id() == ID_index && is_python_dict_type(ct) &&
    escaped_mutables.count(src_id) == 0)
  {
    const auto &ix = to_index_expr(rhs);
    if(
      ix.index().id() == ID_symbol && ix.array().id() == ID_member &&
      to_member_expr(ix.array()).get_component_name() == "values" &&
      to_member_expr(ix.array()).compound() == container)
    {
      extracted_slot_alias[lhs_id] =
        slot_aliast{rhs, src_id, json_string(json_member(cont, "id")), ""};
    }
  }
  // LIST-element slot form: `r = g[i]` returns the data[i] lvalue; a
  // CONSTANT index is re-emittable verbatim (no dependency that a later
  // statement could move without mentioning `g` -- reorderings
  // (insert/pop/sort/...) all mention the source and demote). A SYMBOLIC
  // index is NOT recorded: the index variable could be reassigned by a
  // statement that never mentions the source, and the re-emitted slot
  // would address a different element (a false write).
  else if(
    rhs.id() == ID_index && is_python_list_type(ct) &&
    escaped_mutables.count(src_id) == 0)
  {
    const auto &ix = to_index_expr(rhs);
    if(
      ix.index().is_constant() && ix.array().id() == ID_member &&
      to_member_expr(ix.array()).get_component_name() == "data" &&
      to_member_expr(ix.array()).compound() == container)
    {
      extracted_slot_alias[lhs_id] =
        slot_aliast{rhs, src_id, json_string(json_member(cont, "id")), ""};
    }
  }
  // KEY form: a STRING-keyed extraction whose subscript FOLDED to the
  // tracked literal's container value (read precision, dict18/19) has no
  // slot expression -- record the constant key instead; the write-through
  // emits a key-match store. Same demotion discipline as the slot form.
  else if(
    is_python_dict_type(ct) && escaped_mutables.count(src_id) == 0 &&
    (is_python_list_type(rhs.type()) || is_python_dict_type(rhs.type())))
  {
    const jsont &sl = json_member(value, "slice");
    if(is_node_type(sl, "Constant") && json_member(sl, "value").is_string())
    {
      extracted_slot_alias[lhs_id] = slot_aliast{
        nil_exprt{},
        src_id,
        json_string(json_member(cont, "id")),
        json_string(json_member(sl, "value"))};
    }
  }
}

/// Statement-level pre-scan for the NON-METHOD in-place mutation channels
/// through a tracked extraction alias (PLR §6.2.1/§6.5.3): an augmented
/// assign whose target is the alias, a subscript store into it, or a
/// `del` of one of its elements. Mutator METHOD calls are handled at the
/// call site (invalidate_extracted_source_on_mutation); these three
/// convert without a method name and previously bypassed invalidation
/// entirely -- a false-proof class (`v = d[1]; v += [5]; len(d[1])`
/// proved the STALE length). Runs BEFORE the statement converts (the
/// alias records are still intact); the write-back lands AFTER it
/// (pending_post_checks), so it stores the post-mutation value.
void python_convertert::handle_alias_mutation_channels(const jsont &stmt)
{
  if(extracted_container_alias.empty())
    return;
  const std::string nt = json_string(json_member(stmt, "_type"));
  std::vector<const jsont *> alias_names;
  auto add_if_name = [&](const jsont &n)
  {
    if(is_node_type(n, "Name"))
      alias_names.push_back(&n);
  };
  if(nt == "AugAssign")
  {
    // In-place for mutable containers; for scalars the aug-assign REBINDS
    // and note_mutable_extraction erases the alias on the rebind path --
    // but a pv-typed alias may be either, so treat a tracked target as a
    // potential mutation (write-through if the slot alias survived, else
    // the sound havoc).
    add_if_name(json_member(stmt, "target"));
  }
  else if(nt == "Assign")
  {
    const jsont &targets = json_member(stmt, "targets");
    if(targets.is_array())
    {
      for(const auto &t : as_array(targets))
      {
        if(is_node_type(t, "Subscript"))
          add_if_name(json_member(t, "value"));
      }
    }
  }
  else if(nt == "Delete")
  {
    const jsont &targets = json_member(stmt, "targets");
    if(targets.is_array())
    {
      for(const auto &t : as_array(targets))
      {
        if(is_node_type(t, "Subscript"))
          add_if_name(json_member(t, "value"));
      }
    }
  }
  for(const jsont *n : alias_names)
  {
    const irep_idt id{qualify_name(json_string(json_member(*n, "id")))};
    if(extracted_container_alias.count(id) == 0)
      continue;
    const symbolt *sym = symbol_table.lookup(id);
    if(sym != nullptr)
      invalidate_extracted_alias_inplace_mutation(sym->symbol_expr());
  }
}

void python_convertert::demote_slot_aliases_for_statement(const jsont &stmt)
{
  if(extracted_slot_alias.empty())
    return;
  const std::string nt = json_string(json_member(stmt, "_type"));
  // Control flow: conversion order stops matching runtime order (branch
  // joins, loop back-edges converting once) -- clear everything. Loops
  // stay precise WITHIN one body conversion because the extraction
  // statement re-assigns the index temp on each runtime execution.
  static const std::set<std::string> control{
    "If",
    "For",
    "AsyncFor",
    "While",
    "Try",
    "With",
    "AsyncWith",
    "FunctionDef",
    "AsyncFunctionDef",
    "ClassDef",
    "Match"};
  if(control.count(nt) > 0)
  {
    extracted_slot_alias.clear();
    return;
  }
  // Collect every Name mentioned in the statement.
  std::set<std::string> names;
  std::function<void(const jsont &)> scan = [&](const jsont &n)
  {
    if(!n.is_object())
      return;
    if(is_node_type(n, "Name"))
    {
      names.insert(json_string(json_member(n, "id")));
      return;
    }
    const auto &obj = static_cast<const json_objectt &>(n);
    for(const auto &kv : obj)
    {
      const jsont &child = kv.second;
      if(child.is_array())
      {
        for(const auto &c : as_array(child))
          scan(c);
      }
      else if(child.is_object())
        scan(child);
    }
  };
  scan(stmt);
  // Demote every alias whose SOURCE dict is mentioned: any touch of the
  // dict (store, del, pop, aliasing assign, call argument, even a read --
  // conservative) may invalidate the recorded slot index or the aliasing.
  for(auto it = extracted_slot_alias.begin(); it != extracted_slot_alias.end();)
  {
    if(names.count(it->second.source_name) > 0)
      it = extracted_slot_alias.erase(it);
    else
      ++it;
  }
}

bool python_convertert::invalidate_extracted_source_on_mutation(
  const exprt &obj,
  const std::string &method_name)
{
  // In-place mutators across list / dict / set.
  static const std::set<std::string> mutators = {
    "append",
    "extend",
    "insert",
    "remove",
    "pop",
    "sort",
    "reverse",
    "clear",
    "add",
    "discard",
    "update",
    "setdefault",
    "popitem"};
  if(mutators.count(method_name) == 0)
    return false;
  if(obj.id() != ID_symbol)
    return false;
  return invalidate_extracted_alias_inplace_mutation(to_symbol_expr(obj));
}

/// The channel-agnostic core of extraction-alias invalidation: \p obj (the
/// extracted alias) is being mutated IN PLACE by the statement currently
/// converting -- through a mutator METHOD, an augmented assign
/// (`v += [x]`, PLR §6.2.1 in-place for mutable sequences), a subscript
/// STORE (`v[i] = x`), or `del v[i]`. Keying the invalidation on mutator
/// method names only was a FALSE-PROOF class (the other three channels
/// left the source container stale). Writes the post-statement value back
/// through a still-valid §0 slot alias (precise), else havocs the source
/// and every sibling alias (sound floor).
bool python_convertert::invalidate_extracted_alias_inplace_mutation(
  const symbol_exprt &obj)
{
  const irep_idt obj_id = obj.get_identifier();
  auto it = extracted_container_alias.find(obj_id);
  if(it == extracted_container_alias.end())
    return false;
  // §0 write-through (spike doc §14b): a still-valid slot alias writes the
  // mutated value BACK THROUGH the materialised slot (pending_post_checks:
  // after the mutation statement) instead of havocing the source --
  // `v = d[1]; v.append(2); len(d[1])` is now PRECISE. Demotion (any
  // statement touching the dict, control flow, escape) erased the entry,
  // in which case the sound havoc below applies. Sibling aliases and the
  // constant-fold maps are handled identically on both paths (the fold
  // would otherwise serve the pre-mutation snapshot).
  auto sit = extracted_slot_alias.find(obj_id);
  if(sit != extracted_slot_alias.end() && sit->second.slot.is_nil())
  {
    // KEY-form write-through (string-keyed fold extraction): no slot
    // expression was recorded (the read folded for precision), so emit a
    // key-match store. The key chain is computed against the PRE-statement
    // keys (a value mutation never changes the key array; structural
    // changes demoted the alias), the store lands AFTER the statement.
    const symbolt *dsym = symbol_table.lookup(sit->second.source_id);
    if(dsym != nullptr && is_python_dict_type(dsym->type))
    {
      const exprt dict = dsym->symbol_expr();
      const auto &dst = to_struct_type(dsym->type);
      const auto &keys_t = to_array_type(dst.components()[1].type());
      const auto &vals_t = to_array_type(dst.components()[2].type());
      const signedbv_typet i64{64};
      member_exprt klen{dict, "length", i64};
      member_exprt keys{dict, "keys", keys_t};
      member_exprt vals{dict, "values", vals_t};
      exprt key_lit = python_string_literal(sit->second.key);
      exprt found_idx = from_integer(0, i64);
      for(int i = PYTHON_MAX_DICT_SIZE - 1; i >= 0; i--)
      {
        exprt idx = from_integer(i, i64);
        exprt key_i = python_dict_unbox_key(index_exprt{keys, idx});
        if(key_i.type() != key_lit.type())
          key_i = safe_typecast(key_i, key_lit.type());
        exprt match = string_equal(key_i, key_lit);
        exprt cond =
          and_exprt{binary_relation_exprt{idx, ID_lt, klen}, std::move(match)};
        found_idx = if_exprt{std::move(cond), idx, found_idx};
      }
      exprt wb = obj;
      const typet &et = vals_t.element_type();
      if(wb.type() != et)
      {
        // The coercion may MATERIALISE a canonical copy of `obj` (e.g.
        // wrap_value's __list_val_N snapshot) via pending_checks -- which
        // flush BEFORE the mutating statement, so the snapshot would miss
        // the mutation (a false proof: the write-back stored the stale
        // pre-mutation value). Splice any checks the coercion emits into
        // pending_post_checks AHEAD of the slot store, so the snapshot is
        // taken from the post-mutation value.
        const std::size_t pre = pending_checks.size();
        wb = is_python_value_type(et) ? wrap_value(wb) : coerce_element(wb, et);
        for(std::size_t k = pre; k < pending_checks.size(); k++)
          pending_post_checks.push_back(std::move(pending_checks[k]));
        pending_checks.erase(
          pending_checks.begin() + pre, pending_checks.end());
      }
      pending_post_checks.push_back(code_frontend_assignt{
        index_exprt{vals, std::move(found_idx), et}, std::move(wb)});
      const irep_idt sid = sit->second.source_id;
      dict_literals.erase(sid);
      list_literals.erase(sid);
      dict_runtime_value_overrides.erase(sid);
      dict_guaranteed_keys.erase(sid);
      havoc_sibling_extraction_aliases(it->second, obj_id);
      return true;
    }
  }
  if(sit != extracted_slot_alias.end() && !sit->second.slot.is_nil())
  {
    exprt wb = obj;
    const typet &et = sit->second.slot.type();
    if(wb.type() != et)
    {
      // See the KEY-form arm: splice coercion-materialised checks into
      // the post-statement queue so snapshots see the mutated value.
      const std::size_t pre = pending_checks.size();
      wb = is_python_value_type(et) ? wrap_value(wb) : coerce_element(wb, et);
      for(std::size_t k = pre; k < pending_checks.size(); k++)
        pending_post_checks.push_back(std::move(pending_checks[k]));
      pending_checks.erase(pending_checks.begin() + pre, pending_checks.end());
    }
    pending_post_checks.push_back(
      code_frontend_assignt{sit->second.slot, std::move(wb)});
    const irep_idt sid = sit->second.source_id;
    dict_literals.erase(sid);
    list_literals.erase(sid);
    dict_runtime_value_overrides.erase(sid);
    dict_guaranteed_keys.erase(sid);
    havoc_sibling_extraction_aliases(it->second, obj_id);
    return true;
  }
  // PLR reference semantics: `obj` is the same object as the source slot, so
  // mutating it may change the source container. We store nested elements by
  // value (no aliasing), so over-approximate soundly by havocing the source —
  // subsequent reads become nondet rather than a stale (false-proof) copy.
  // (The precise fix is reference semantics for mutable objects.)
  pending_checks.push_back(code_frontend_assignt{
    it->second,
    side_effect_expr_nondett{it->second.type(), source_locationt{}}});
  // Also drop the source from the constant-fold tracking maps, otherwise a
  // subsequent subscript read (e.g. d[k]) would still fold to the stale tracked
  // literal and bypass the havoc.
  if(it->second.id() == ID_symbol)
  {
    const irep_idt sid = to_symbol_expr(it->second).get_identifier();
    dict_literals.erase(sid);
    list_literals.erase(sid);
    dict_runtime_value_overrides.erase(sid);
    dict_guaranteed_keys.erase(sid);
  }
  // PLR object identity, SIBLING aliases: every OTHER extraction from the
  // same source container may be the SAME object (`v1 = d[1]; v2 = d[1];
  // v1.append(2)` -- CPython: len(v2) grows). Havocing only the source left
  // v2 a stale by-value copy: a FALSE PROOF (probed). Havoc all sibling
  // aliases of this source too (and the mutated alias itself stays as-is:
  // its own mutation is applied to it directly by the method model).
  havoc_sibling_extraction_aliases(
    it->second, to_symbol_expr(obj).get_identifier());
  return true;
}

void python_convertert::havoc_sibling_extraction_aliases(
  const exprt &source,
  const irep_idt &except_id)
{
  for(const auto &entry : extracted_container_alias)
  {
    if(entry.first == except_id)
      continue;
    if(entry.second != source)
      continue;
    const symbolt *als = symbol_table.lookup(entry.first);
    if(als == nullptr)
      continue;
    pending_checks.push_back(code_frontend_assignt{
      als->symbol_expr(),
      side_effect_expr_nondett{als->type, source_locationt{}}});
    invalidate_reassigned_symbol(entry.first);
  }
}

bool python_convertert::invalidate_dict_value_on_mutation(
  const jsont &subscript,
  const std::string &method_name)
{
  // In-place mutators across list / dict / set.
  static const std::set<std::string> mutators = {
    "append",
    "extend",
    "insert",
    "remove",
    "pop",
    "sort",
    "reverse",
    "clear",
    "add",
    "discard",
    "update",
    "setdefault",
    "popitem"};
  if(mutators.count(method_name) == 0)
    return false;
  if(!is_node_type(subscript, "Subscript"))
    return false;
  const jsont &base = json_member(subscript, "value");
  if(!is_node_type(base, "Name"))
    return false;
  exprt base_e = convert_expression(base);
  if(base_e.id() != ID_symbol || !is_python_dict_type(base_e.type()))
    return false;
  // ALL dict key kinds (int, string, heterogeneous value-typed) now use a
  // working lvalue value-slot (mutation propagates -- see the subscript
  // converter), so no compensating dict havoc is needed. But EXTRACTION
  // ALIASES of this dict (`v = d[1]; d[1].append(2)` -- CPython: len(v)
  // grows) are by-value copies that go STALE on the in-place slot
  // mutation: havoc them (a false proof otherwise -- probed).
  exprt base_e2 = convert_expression(base);
  if(base_e2.id() == ID_symbol)
    havoc_sibling_extraction_aliases(base_e2, irep_idt{});
  return false;
}

void python_convertert::collect_escaped_mutables(const jsont &body)
{
  if(!body.is_array())
    return;

  // Local lambdas that walk JSON nodes. We use std::function to allow
  // mutual recursion between scan_stmts, scan_expr, and the helpers.
  std::function<void(const jsont &)> scan_stmts;
  std::function<void(const jsont &)> scan_expr;

  auto add_escape = [&](const std::string &name)
  { escaped_mutables.insert(irep_idt{qualify_name(name)}); };

  scan_expr = [&](const jsont &expr)
  {
    if(!expr.is_object())
      return;
    if(is_node_type(expr, "List"))
    {
      const jsont &elts = json_member(expr, "elts");
      if(elts.is_array())
      {
        for(const auto &elt : as_array(elts))
        {
          if(is_node_type(elt, "Name"))
            add_escape(json_string(json_member(elt, "id")));
          else
            scan_expr(elt);
        }
      }
    }
    else if(is_node_type(expr, "Dict"))
    {
      const jsont &values = json_member(expr, "values");
      if(values.is_array())
      {
        for(const auto &v : as_array(values))
        {
          if(is_node_type(v, "Name"))
            add_escape(json_string(json_member(v, "id")));
          else
            scan_expr(v);
        }
      }
    }
    else if(is_node_type(expr, "Call"))
    {
      // .append(name) / .extend(name) / .insert(_, name)
      const jsont &func = json_member(expr, "func");
      const jsont &args = json_member(expr, "args");
      // .append(name) / .insert(_, name) — these store the
      // argument BY REFERENCE (Python aliasing semantics). NB:
      // .extend(name) is NOT an escape — it copies elements.
      if(is_node_type(func, "Attribute") && args.is_array())
      {
        std::string method = json_string(json_member(func, "attr"));
        if(method == "append")
        {
          auto args_arr = as_array(args);
          if(args_arr.begin() != args_arr.end())
          {
            const jsont &a0 = *args_arr.begin();
            if(is_node_type(a0, "Name"))
              add_escape(json_string(json_member(a0, "id")));
          }
        }
        else if(method == "insert")
        {
          // insert(idx, value) — value is the second arg
          auto args_arr = as_array(args);
          auto it = args_arr.begin();
          if(it != args_arr.end())
          {
            ++it;
            if(it != args_arr.end())
            {
              const jsont &a1 = *it;
              if(is_node_type(a1, "Name"))
                add_escape(json_string(json_member(a1, "id")));
            }
          }
        }
      }
      // Recurse into call args for nested patterns
      if(args.is_array())
        for(const auto &a : as_array(args))
          scan_expr(a);
    }
    else if(is_node_type(expr, "BinOp"))
    {
      scan_expr(json_member(expr, "left"));
      scan_expr(json_member(expr, "right"));
    }
    else if(is_node_type(expr, "BoolOp"))
    {
      const jsont &values = json_member(expr, "values");
      if(values.is_array())
        for(const auto &v : as_array(values))
          scan_expr(v);
    }
    else if(is_node_type(expr, "UnaryOp"))
    {
      scan_expr(json_member(expr, "operand"));
    }
    else if(is_node_type(expr, "IfExp"))
    {
      scan_expr(json_member(expr, "body"));
      scan_expr(json_member(expr, "orelse"));
    }
    else if(is_node_type(expr, "Subscript"))
    {
      scan_expr(json_member(expr, "value"));
    }
  };

  scan_stmts = [&](const jsont &stmts)
  {
    if(!stmts.is_array())
      return;
    for(const auto &s : as_array(stmts))
    {
      if(is_node_type(s, "Assign") || is_node_type(s, "AnnAssign"))
      {
        scan_expr(json_member(s, "value"));
      }
      else if(is_node_type(s, "AugAssign"))
      {
        scan_expr(json_member(s, "value"));
      }
      else if(is_node_type(s, "Expr"))
      {
        scan_expr(json_member(s, "value"));
      }
      else if(is_node_type(s, "Return"))
      {
        scan_expr(json_member(s, "value"));
      }
      else if(is_node_type(s, "If"))
      {
        scan_expr(json_member(s, "test"));
        scan_stmts(json_member(s, "body"));
        scan_stmts(json_member(s, "orelse"));
      }
      else if(is_node_type(s, "While"))
      {
        scan_expr(json_member(s, "test"));
        scan_stmts(json_member(s, "body"));
        scan_stmts(json_member(s, "orelse"));
      }
      else if(is_node_type(s, "For"))
      {
        scan_expr(json_member(s, "iter"));
        scan_stmts(json_member(s, "body"));
        scan_stmts(json_member(s, "orelse"));
      }
      else if(is_node_type(s, "With"))
      {
        scan_stmts(json_member(s, "body"));
      }
      else if(is_node_type(s, "Try"))
      {
        scan_stmts(json_member(s, "body"));
        scan_stmts(json_member(s, "orelse"));
        scan_stmts(json_member(s, "finalbody"));
        const jsont &handlers = json_member(s, "handlers");
        if(handlers.is_array())
          for(const auto &h : as_array(handlers))
            scan_stmts(json_member(h, "body"));
      }
      // FunctionDef / ClassDef: don't recurse — those have their own
      // pre-scan invocations driven by convert_function_def.
    }
  };

  scan_stmts(body);
}

void python_convertert::collect_empty_list_inferred_types(const jsont &body)
{
  if(!body.is_array())
    return;

  // Helper: extract a precise element type from an expression
  // node (the argument of .append / .extend).
  // We carry an enclosing-for-loops list so that 'name.append(x)'
  // where x is the iter-target of an enclosing for-loop can be
  // typed from the for-loop's iter source (dict key type / list
  // element type / single-char string).
  std::vector<const jsont *> for_stack;
  // Track inferred dict-key / list-element / string types from
  // dict / list / str literals seen in earlier Assigns. Used by
  // type_of_expr to resolve 'for k in name' where name was
  // assigned a literal earlier in the same body.
  std::map<irep_idt, typet> name_dict_key_t;
  std::map<irep_idt, typet> name_list_elem_t;
  std::set<irep_idt> name_is_string;
  // General name → value type, recorded for simple `name = <expr>`
  // assignments seen earlier in the same walk (e.g.
  // `candidate = actions[index]`), so a later `lst.append(candidate)`
  // can resolve the element type even when `name` is not a literal.
  std::map<irep_idt, typet> name_value_t;
  std::function<typet(const jsont &)> type_of_expr;
  type_of_expr = [&](const jsont &n) -> typet
  {
    if(is_node_type(n, "Constant"))
    {
      const jsont &v = json_member(n, "value");
      if(v.is_string())
        return python_string_type();
      if(v.is_number())
      {
        std::string vs = v.value;
        if(
          vs.find('.') != std::string::npos ||
          vs.find('e') != std::string::npos)
          return double_type();
        return python_int_type();
      }
      if(v.is_true() || v.is_false())
        return bool_typet{};
    }
    if(is_node_type(n, "Name"))
    {
      std::string nm = json_string(json_member(n, "id"));
      // Walk enclosing for-loops outermost → innermost; the
      // innermost match wins.
      const jsont *match_iter = nullptr;
      for(const jsont *fp : for_stack)
      {
        const jsont &target = json_member(*fp, "target");
        if(
          is_node_type(target, "Name") &&
          json_string(json_member(target, "id")) == nm)
        {
          match_iter = fp;
        }
      }
      if(match_iter != nullptr)
      {
        const jsont &iter = json_member(*match_iter, "iter");
        // PLR §6.3.4: extract the source Name for the iter.
        // Handles: for k in d, for k in d.keys(), for k in d.values(),
        // for k in d.items(), for k in (d.keys() / d.values()).
        const jsont *src_name_node = nullptr;
        bool iter_is_values = false;
        bool iter_is_items = false;
        if(is_node_type(iter, "Name"))
          src_name_node = &iter;
        else if(is_node_type(iter, "Call"))
        {
          const jsont &fn = json_member(iter, "func");
          if(is_node_type(fn, "Attribute"))
          {
            std::string m = json_string(json_member(fn, "attr"));
            const jsont &recv = json_member(fn, "value");
            if(is_node_type(recv, "Name"))
            {
              src_name_node = &recv;
              if(m == "values")
                iter_is_values = true;
              else if(m == "items")
                iter_is_items = true;
            }
          }
          // `for i in range(...)`: the loop target is an int.
          else if(
            is_node_type(fn, "Name") &&
            json_string(json_member(fn, "id")) == "range")
            return python_int_type();
        }
        if(src_name_node != nullptr)
        {
          std::string in = json_string(json_member(*src_name_node, "id"));
          irep_idt iid{qualify_name(in)};
          // Prefer the prescan-tracked types over the symbol
          // table (the symbol table may not yet hold the
          // pre-body literal at prescan time).
          auto dk = name_dict_key_t.find(iid);
          if(dk != name_dict_key_t.end())
          {
            if(iter_is_values)
            {
              // values yield value type — we don't track that
              // separately; fall through to symbol-table.
            }
            else if(iter_is_items)
            {
              // tuple type for (k,v) — skip.
            }
            else
            {
              // d (default), d.keys()
              return dk->second;
            }
          }
          auto le = name_list_elem_t.find(iid);
          if(le != name_list_elem_t.end())
            return le->second;
          if(name_is_string.count(iid) > 0)
            return python_string_type();
          const symbolt *isy = symbol_table.lookup(iid);
          if(isy != nullptr)
          {
            if(is_python_dict_type(isy->type))
            {
              const auto &dst = to_struct_type(isy->type);
              if(dst.components().size() >= 2 && !iter_is_values)
              {
                const auto &keys_arr_t =
                  to_array_type(dst.components()[1].type());
                return keys_arr_t.element_type();
              }
            }
            if(is_python_list_type(isy->type))
            {
              const auto &lst = to_struct_type(isy->type);
              if(lst.components().size() >= 2)
              {
                const auto &data_t = to_array_type(lst.components()[1].type());
                return data_t.element_type();
              }
            }
            if(is_python_string_type(isy->type))
              return python_string_type();
          }
        }
      }
      irep_idt sid{qualify_name(nm)};
      // Pre-scan-recorded shape: y = nondet_str() / y = "lit" /
      // y = chr(c) → python_string. Visible to the prescan
      // even though the symbol_table doesn't have y yet.
      if(name_is_string.count(sid) > 0)
        return python_string_type();
      auto le_it = name_list_elem_t.find(sid);
      if(le_it != name_list_elem_t.end())
        return python_list_type(le_it->second);
      auto nv_it = name_value_t.find(sid);
      if(nv_it != name_value_t.end())
        return nv_it->second;
      const symbolt *s = symbol_table.lookup(sid);
      if(s != nullptr && s->type.id() != ID_empty)
        return s->type;
    }
    // Direct call: nondet_str() / chr(...) / nondet_string()
    // returns python_string. Helps 'x.append(nondet_str())'.
    if(is_node_type(n, "Call") && is_node_type(json_member(n, "func"), "Name"))
    {
      std::string callee =
        json_string(json_member(json_member(n, "func"), "id"));
      if(
        callee == "nondet_str" || callee == "nondet_string" ||
        callee == "chr" || callee == "str")
        return python_string_type();
      if(callee == "nondet_int" || callee == "ord" || callee == "len")
        return python_int_type();
      if(callee == "nondet_float")
        return double_type();
      if(callee == "nondet_bool")
        return bool_typet{};
      // User-class constructor `ClassName(...)` → an object element.
      // Use the universal tagged `python_value` (the same element type a
      // list literal of objects uses), so `lst = []; lst.append(Obj())`
      // followed by `lst[i].method()` dispatches on the stored runtime
      // class via the tag — and this stays sound when the list ends up
      // holding mixed subclasses (a concrete struct element type would
      // lose the tag of any other subclass appended later).
      if(class_types.find(callee) != class_types.end())
        return python_value_type();
    }
    // Arithmetic / concatenation: int op int -> int, float involved
    // -> float, str + str -> str. Lets e.g. `d[i] = i * 2` infer an
    // int value type.
    if(is_node_type(n, "BinOp"))
    {
      typet lt = type_of_expr(json_member(n, "left"));
      typet rt = type_of_expr(json_member(n, "right"));
      auto is_int = [](const typet &t)
      { return t.id() == ID_signedbv || t.id() == ID_integer; };
      if(lt.id() == ID_floatbv || rt.id() == ID_floatbv)
        return double_type();
      if(is_int(lt) && is_int(rt))
        return python_int_type();
      if(is_python_string_type(lt) && is_python_string_type(rt))
        return python_string_type();
    }
    // Subscript `xs[i]` → element type of xs, so `r = [];
    // r.append(xs[i])` over an object list infers a `python_value`
    // (object) element and later `r[k].method()` dispatches correctly.
    if(is_node_type(n, "Subscript"))
    {
      const jsont &v = json_member(n, "value");
      if(is_node_type(v, "Name"))
      {
        std::string vn = json_string(json_member(v, "id"));
        irep_idt vid{qualify_name(vn)};
        auto le = name_list_elem_t.find(vid);
        if(le != name_list_elem_t.end())
          return le->second;
        const symbolt *vs = symbol_table.lookup(vid);
        if(vs != nullptr)
        {
          // A mutable-container parameter is passed by reference, so the
          // symbol is a pointer to the list struct — deref to reach it.
          typet vt = vs->type;
          if(vt.id() == ID_pointer)
            vt = to_pointer_type(vt).base_type();
          if(is_python_list_type(vt))
            return to_array_type(to_struct_type(vt).components()[1].type())
              .element_type();
        }
      }
    }
    return typet{}; // unknown
  };

  // Track which names are pending (i.e. we've seen 'name = []'
  // but not yet seen the inferring append).
  std::set<irep_idt> pending;
  // Names bound to an empty `{}` dict, not yet resolved by a d[k]=v.
  std::set<irep_idt> pending_dict;
  // Walk top-level statements in order. Reset pending when we
  // see another assignment to the same name (the second assign
  // shadows the empty-list start).
  std::function<void(const jsont &)> walk = [&](const jsont &node)
  {
    if(!node.is_array())
      return;
    for(const auto &stmt : as_array(node))
    {
      if(is_node_type(stmt, "Assign") || is_node_type(stmt, "AnnAssign"))
      {
        bool is_ann = is_node_type(stmt, "AnnAssign");
        const jsont &targets = json_member(stmt, "targets");
        const jsont &target_single = json_member(stmt, "target");
        const jsont &value = json_member(stmt, "value");
        bool single_target_name =
          (is_ann && is_node_type(target_single, "Name")) ||
          (!is_ann && targets.is_array() && as_array(targets).size() == 1 &&
           is_node_type(*as_array(targets).begin(), "Name"));
        if(single_target_name)
        {
          std::string nm =
            is_ann ? json_string(json_member(target_single, "id"))
                   : json_string(json_member(*as_array(targets).begin(), "id"));
          irep_idt sid{qualify_name(nm)};
          // Record the value's type for a simple `name = <expr>` (e.g.
          // `candidate = actions[index]`) so a later append of `name`
          // resolves its element type.
          {
            typet vt = type_of_expr(value);
            if(!vt.id().empty() && vt.id() != ID_empty)
              name_value_t[sid] = vt;
          }
          // For AnnAssign 'name: list[T] = []' we skip the
          // empty-list pending register because the declared
          // element type T is already precise. Bare 'list'
          // (Name) annotation is the polymorphic case.
          bool ann_is_parameterised =
            is_ann &&
            is_node_type(json_member(stmt, "annotation"), "Subscript");
          // 'name = []' — register as pending if not already
          // inferred and the value is an empty List, AND the
          // annotation (if any) doesn't already pin the type.
          // NOTE: parameterised `list[T] = []` is ALSO admitted here (was
          // excluded) so a later append/extend/insert of a value whose type is
          // uninferable or incompatible with T widens the element to
          // python_value (Any-dominance) instead of PUNNING it into the
          // concrete T -- a false proof (`xs: list[int] = []; xs.append(src())`
          // read back xs[0] as int). A correctly-typed store keeps T (the
          // inference's "first concrete wins"), so precision is retained; a
          // never-appended list keeps its annotated T. Bare/forward-ref/no-ann
          // lists were already admitted. EXCEPTION: under
          // --python-check-annotations the concrete T is kept (parameterised
          // lists NOT admitted) so the append/insert annotation-MISMATCH check
          // can fire on the incompatible store -- that opt-in mode reports the
          // laundering as a property instead of soundly widening it away.
          if(
            is_node_type(value, "List") &&
            json_member(value, "elts").is_array() &&
            as_array(json_member(value, "elts")).empty() &&
            empty_list_inferred_types.count(sid) == 0 &&
            (!ann_is_parameterised || !python_check_annotations))
          {
            pending.insert(sid);
          }
          else if(
            !ann_is_parameterised && is_node_type(value, "Dict") &&
            json_member(value, "keys").is_array() &&
            as_array(json_member(value, "keys")).empty() &&
            empty_dict_inferred_types.count(sid) == 0)
          {
            pending_dict.insert(sid);
          }
          else
          {
            pending.erase(sid);
            pending_dict.erase(sid);
            // Always record dict / list / string literal types
            // (regardless of annotation) so for-loop iter-
            // target lookups in type_of_expr resolve.
            if(is_node_type(value, "Dict"))
            {
              const jsont &keys = json_member(value, "keys");
              if(keys.is_array() && !as_array(keys).empty())
              {
                const jsont &fk = *as_array(keys).begin();
                if(is_node_type(fk, "Constant"))
                {
                  const jsont &kv = json_member(fk, "value");
                  if(kv.is_string())
                    name_dict_key_t[sid] = python_string_type();
                  else if(kv.is_number())
                  {
                    std::string vs = kv.value;
                    if(vs.find('.') != std::string::npos)
                      name_dict_key_t[sid] = double_type();
                    else
                      name_dict_key_t[sid] = python_int_type();
                  }
                }
              }
            }
            else if(is_node_type(value, "List"))
            {
              const jsont &elts = json_member(value, "elts");
              if(elts.is_array() && !as_array(elts).empty())
              {
                const jsont &fe = *as_array(elts).begin();
                typet et = type_of_expr(fe);
                if(!et.id().empty() && et.id() != ID_empty)
                  name_list_elem_t[sid] = et;
              }
            }
            else if(is_node_type(value, "Call"))
            {
              // 'l = d.keys()' / 'l = d.values()' — propagate the
              // dict's key/value type as l's element type.
              const jsont &fn = json_member(value, "func");
              if(is_node_type(fn, "Attribute"))
              {
                std::string m = json_string(json_member(fn, "attr"));
                const jsont &recv = json_member(fn, "value");
                if(is_node_type(recv, "Name") && m == "keys")
                {
                  std::string rn = json_string(json_member(recv, "id"));
                  irep_idt rid{qualify_name(rn)};
                  auto dk = name_dict_key_t.find(rid);
                  if(dk != name_dict_key_t.end())
                    name_list_elem_t[sid] = dk->second;
                }
              }
              // 'name = nondet_str()' / 'name = chr(...)' /
              // 'name = nondet_string()' produce a python_string
              // value. Record so subsequent 'lst.append(name)'
              // pre-scan resolves the element type.
              else if(is_node_type(fn, "Name"))
              {
                std::string callee = json_string(json_member(fn, "id"));
                if(
                  callee == "nondet_str" || callee == "nondet_string" ||
                  callee == "chr")
                  name_is_string.insert(sid);
              }
            }
            else if(
              is_node_type(value, "Constant") &&
              json_member(value, "value").is_string())
            {
              name_is_string.insert(sid);
            }
            // For an AnnAssign with parameterised list/dict
            // annotation, also record the inferred types from
            // the annotation. Lets the for-loop iter-target
            // lookup find the element type without needing the
            // RHS to be a literal.
            if(is_ann)
            {
              const jsont &ann = json_member(stmt, "annotation");
              if(
                is_node_type(ann, "Subscript") &&
                is_node_type(json_member(ann, "value"), "Name"))
              {
                std::string base =
                  json_string(json_member(json_member(ann, "value"), "id"));
                const jsont &slice = json_member(ann, "slice");
                if(base == "list" || base == "List")
                {
                  if(is_node_type(slice, "Name"))
                  {
                    std::string sn = json_string(json_member(slice, "id"));
                    if(sn == "str")
                      name_list_elem_t[sid] = python_string_type();
                    else if(sn == "int")
                      name_list_elem_t[sid] = python_int_type();
                    else if(sn == "float")
                      name_list_elem_t[sid] = double_type();
                  }
                }
                else if(base == "dict" || base == "Dict")
                {
                  // dict[K, V] — slice is Tuple(K, V) or single
                  // Name (the key).
                  const jsont *kn = nullptr;
                  if(is_node_type(slice, "Tuple"))
                  {
                    const jsont &telts = json_member(slice, "elts");
                    if(telts.is_array() && !as_array(telts).empty())
                      kn = &(*as_array(telts).begin());
                  }
                  else if(is_node_type(slice, "Name"))
                    kn = &slice;
                  if(kn != nullptr && is_node_type(*kn, "Name"))
                  {
                    std::string knm = json_string(json_member(*kn, "id"));
                    if(knm == "str")
                      name_dict_key_t[sid] = python_string_type();
                    else if(knm == "int")
                      name_dict_key_t[sid] = python_int_type();
                  }
                }
              }
            }
          }
        }
        // §dict: a `d[k] = v` store on a pending empty dict resolves
        // its key/value element types (first store wins). Works inside
        // loops because it sets the type at the `{}` creation site, not
        // via a per-iteration rebuild.
        if(!is_ann && targets.is_array() && as_array(targets).size() == 1)
        {
          const jsont &t0 = *as_array(targets).begin();
          if(
            is_node_type(t0, "Subscript") &&
            is_node_type(json_member(t0, "value"), "Name"))
          {
            irep_idt did{qualify_name(
              json_string(json_member(json_member(t0, "value"), "id")))};
            if(
              pending_dict.count(did) > 0 &&
              empty_dict_inferred_types.count(did) == 0)
            {
              typet kt = type_of_expr(json_member(t0, "slice"));
              typet vt = type_of_expr(value);
              // Slot-pun family, dict-value member (PLR §3.2 Any-dominance):
              // a CONTAINER first store (`d[k] = []` / `{}`) or an
              // uninferable value must infer the value type as python_value
              // (boxed, per-instance) -- a scalar default punned the stored
              // list to an int slot (`providers[k] = []; providers[k]
              // .append(m); ...items()` iterated an int: a false alarm on
              // the not-iterable obligation AND a latent false-proof shape,
              // the same root as the closed list-element / attr-field
              // members). Scalar first stores keep their precise type.
              // Mirrors the list rule below: NOT under
              // --python-check-annotations (that mode keeps the concrete
              // type so the store-mismatch is reported as a property).
              if(
                !python_check_annotations &&
                (is_node_type(value, "List") || is_node_type(value, "Dict") ||
                 is_node_type(value, "Set") ||
                 is_node_type(value, "ListComp") ||
                 is_node_type(value, "DictComp") || vt.id().empty() ||
                 vt.id() == ID_empty))
              {
                vt = python_value_type();
              }
              // An uninferable KEY expression (e.g. `k: Any = m.get(...)`;
              // method-call types are beyond type_of_expr) likewise infers
              // python_value: the value-domain key match (value_equal) is
              // sound and slot-capable, whereas bailing left the scalar
              // default -- the same pun (bedrock's providers[k] shape).
              if(kt.id().empty() || kt.id() == ID_empty)
                kt = python_value_type();
              empty_dict_inferred_types[did] = {kt, vt};
              pending_dict.erase(did);
            }
          }
        }
        // §list: a `xs[i] = v` store whose value type is uninferable or
        // incompatible with the list's known element type WIDENS the element to
        // python_value (Any-dominance) -- else the concrete slot PUNS the
        // mismatched value (a false proof). Targeted: only lists we already know
        // an element type for (a literal init recorded in name_list_elem_t, or
        // an in-progress inference) widen, and only on a genuine mismatch, so a
        // correctly-typed store leaves the list untouched (no precision cost).
        // Not done under --python-check-annotations (that mode keeps the
        // concrete type to report the mismatch as a property).
        if(
          !is_ann && !python_check_annotations && targets.is_array() &&
          as_array(targets).size() == 1)
        {
          const jsont &t0 = *as_array(targets).begin();
          if(
            is_node_type(t0, "Subscript") &&
            is_node_type(json_member(t0, "value"), "Name") &&
            !is_node_type(json_member(t0, "slice"), "Slice"))
          {
            irep_idt lid{qualify_name(
              json_string(json_member(json_member(t0, "value"), "id")))};
            typet cur_elem;
            auto eit = empty_list_inferred_types.find(lid);
            if(eit != empty_list_inferred_types.end())
              cur_elem = eit->second;
            else
            {
              auto nit = name_list_elem_t.find(lid);
              if(nit != name_list_elem_t.end())
                cur_elem = nit->second;
            }
            if(
              !cur_elem.id().empty() && cur_elem.id() != ID_empty &&
              !is_python_value_type(cur_elem))
            {
              typet vt = type_of_expr(value);
              const bool uninferable = vt.id().empty() || vt.id() == ID_empty;
              if(uninferable || vt != cur_elem)
                empty_list_inferred_types[lid] = python_value_type();
            }
          }
        }
        // §dict: `<target> = a.setdefault(k, default)` on a pending empty dict
        // resolves its key/value types from k / default -- the assignment
        // analogue of the chained `setdefault(...).method()` inference below.
        // Without it the dict's value type stayed the int default and a
        // mismatched default (e.g. "s") was coerced/lost, false-proving a later
        // use of the returned value (the setdefault analogue of ty-005). PLR
        // §6.4.6: setdefault inserts AND returns the default, so the dict value
        // type must accommodate type(default).
        if(!is_ann && is_node_type(value, "Call"))
        {
          const jsont &vfn = json_member(value, "func");
          if(
            is_node_type(vfn, "Attribute") &&
            json_string(json_member(vfn, "attr")) == "setdefault" &&
            is_node_type(json_member(vfn, "value"), "Name"))
          {
            irep_idt did{qualify_name(
              json_string(json_member(json_member(vfn, "value"), "id")))};
            const jsont &sdargs = json_member(value, "args");
            if(
              pending_dict.count(did) > 0 &&
              empty_dict_inferred_types.count(did) == 0 && sdargs.is_array() &&
              as_array(sdargs).size() >= 2)
            {
              auto ait = as_array(sdargs).begin();
              typet kt = type_of_expr(*ait);
              ++ait;
              typet vt = type_of_expr(*ait);
              if(
                !kt.id().empty() && kt.id() != ID_empty && !vt.id().empty() &&
                vt.id() != ID_empty)
              {
                empty_dict_inferred_types[did] = {kt, vt};
                pending_dict.erase(did);
              }
            }
          }
        }
      }
      else if(is_node_type(stmt, "Expr"))
      {
        const jsont &val = json_member(stmt, "value");
        if(is_node_type(val, "Call"))
        {
          const jsont &fn = json_member(val, "func");
          if(is_node_type(fn, "Attribute"))
          {
            std::string method = json_string(json_member(fn, "attr"));
            const jsont &obj_node = json_member(fn, "value");
            // PLR §6.4: `a.setdefault(k, default).<method>(...)` on a
            // pending empty dict infers the dict's key/value types from
            // k / default (the empty-dict inference otherwise only fires on
            // an `a[k] = v` subscript-assign, which setdefault code lacks).
            if(is_node_type(obj_node, "Call"))
            {
              const jsont &ofn = json_member(obj_node, "func");
              if(
                is_node_type(ofn, "Attribute") &&
                json_string(json_member(ofn, "attr")) == "setdefault" &&
                is_node_type(json_member(ofn, "value"), "Name"))
              {
                irep_idt did{qualify_name(
                  json_string(json_member(json_member(ofn, "value"), "id")))};
                const jsont &sdargs = json_member(obj_node, "args");
                if(
                  pending_dict.count(did) > 0 &&
                  empty_dict_inferred_types.count(did) == 0 &&
                  sdargs.is_array() && as_array(sdargs).size() >= 2)
                {
                  auto ait = as_array(sdargs).begin();
                  typet kt = type_of_expr(*ait);
                  ++ait;
                  const jsont &dft = *ait;
                  typet vt;
                  if(is_node_type(dft, "List"))
                  {
                    // List default: element type from its first element, or
                    // int (length-only asserts don't depend on it).
                    typet et = python_int_type();
                    const jsont &de = json_member(dft, "elts");
                    if(de.is_array() && !as_array(de).empty())
                    {
                      typet e0 = type_of_expr(*as_array(de).begin());
                      if(!e0.id().empty() && e0.id() != ID_empty)
                        et = e0;
                    }
                    vt = python_list_type(et);
                  }
                  else
                    vt = type_of_expr(dft);
                  if(
                    !kt.id().empty() && kt.id() != ID_empty &&
                    !vt.id().empty() && vt.id() != ID_empty)
                  {
                    empty_dict_inferred_types[did] = {kt, vt};
                    pending_dict.erase(did);
                  }
                }
              }
            }
            if(
              is_node_type(obj_node, "Name") &&
              (method == "append" || method == "extend" || method == "insert"))
            {
              std::string nm = json_string(json_member(obj_node, "id"));
              irep_idt sid{qualify_name(nm)};
              // Track a list that started as an empty literal (`pending`) or
              // that we have already begun inferring, so a LATER append can
              // widen the element type (Any dominates) -- not just the first.
              if(
                pending.count(sid) > 0 ||
                empty_list_inferred_types.count(sid) > 0)
              {
                const jsont &args = json_member(val, "args");
                // insert(index, value): the stored element is the 2nd arg.
                std::size_t val_arg_idx = (method == "insert") ? 1 : 0;
                if(args.is_array() && as_array(args).size() > val_arg_idx)
                {
                  auto arg_it = as_array(args).begin();
                  std::advance(arg_it, val_arg_idx);
                  const jsont &arg = *arg_it;
                  typet t;
                  if(method == "append" || method == "insert")
                    t = type_of_expr(arg);
                  else
                  {
                    // extend(X) — element type is X's element type.
                    if(is_node_type(arg, "List"))
                    {
                      const jsont &arg_elts = json_member(arg, "elts");
                      if(arg_elts.is_array() && !as_array(arg_elts).empty())
                        t = type_of_expr(*as_array(arg_elts).begin());
                    }
                    else if(is_node_type(arg, "Constant"))
                    {
                      // extend("aaa") — string yields chars
                      // (single-char strings).
                      const jsont &v = json_member(arg, "value");
                      if(v.is_string())
                        t = python_string_type();
                    }
                  }
                  const bool inferable = !t.id().empty() && t.id() != ID_empty;
                  auto cur = empty_list_inferred_types.find(sid);
                  const bool already_pv =
                    cur != empty_list_inferred_types.end() &&
                    is_python_value_type(cur->second);
                  if(!inferable)
                  {
                    // The appended element's type is uninferable (e.g. a call
                    // result the prescan can't resolve). An unknown element is
                    // Any, NOT int -- a concrete int default PUNS a non-int
                    // store (cast to int), a false proof. Model the element as
                    // python_value (Any) so the runtime tag is preserved and a
                    // later misuse faults. Any DOMINATES: this overrides a
                    // prior concrete inference (a mixed `[1, src()]` list must
                    // not pun `src()`).
                    empty_list_inferred_types[sid] = python_value_type();
                    pending.erase(sid);
                  }
                  else if(already_pv)
                  {
                    // Already Any — a later concrete append does not narrow it.
                  }
                  else if(cur == empty_list_inferred_types.end())
                  {
                    // First concrete inference wins (for the homogeneous case).
                    empty_list_inferred_types[sid] = t;
                    pending.erase(sid);
                  }
                  else if(cur->second != t)
                  {
                    // A second, DIFFERENT concrete element type: the list is
                    // heterogeneous -> python_value (Any) so neither concrete
                    // type puns the other.
                    empty_list_inferred_types[sid] = python_value_type();
                  }
                }
              }
            }
          }
        }
      }
      // Recurse into nested control-flow blocks so the inference
      // also fires inside if/while/for bodies. For 'For' nodes
      // also push the for-loop onto the stack so type_of_expr
      // can resolve the iter-target's type.
      if(stmt.is_object())
      {
        bool is_for_node =
          is_node_type(stmt, "For") || is_node_type(stmt, "AsyncFor");
        if(is_for_node)
          for_stack.push_back(&stmt);
        for(const char *child : {"body", "orelse", "finalbody", "handlers"})
        {
          const jsont &c = json_member(stmt, child);
          if(c.is_array())
            walk(c);
        }
        if(is_for_node)
          for_stack.pop_back();
      }
    }
  };
  walk(body);
}

void python_convertert::collect_function_global_mutations(
  const jsont &module_body)
{
  static const std::set<std::string> mutating_methods{
    "append",
    "extend",
    "insert",
    "remove",
    "pop",
    "clear",
    "sort",
    "reverse",
    "update",
    "setdefault",
    "popitem"};
  static const char *fields[] = {
    "body",           "orelse",  "handlers",    "finalbody", "test",  "value",
    "values",         "targets", "target",      "iter",      "args",  "elts",
    "keys",           "left",    "right",       "func",      "slice", "elt",
    "generators",     "ifs",     "comparators", "ops",       "exc",   "returns",
    "decorator_list", nullptr};

  // Within a function body: collect names mutated such that a later
  // module/global read could go stale across a call to this function.
  std::function<void(const jsont &)> scan_fn_body = [&](const jsont &node)
  {
    if(node.is_array())
    {
      for(const auto &e : to_json_array(node))
        scan_fn_body(e);
      return;
    }
    if(!node.is_object())
      return;
    const std::string nt = node["_type"].value;
    if(nt == "Global")
    {
      const jsont &names = node["names"];
      if(names.is_array())
        for(const auto &n : to_json_array(names))
          globals_mutated_in_functions.insert(n.value);
    }
    else if(nt == "Assign" || nt == "AnnAssign" || nt == "AugAssign")
    {
      auto note_target = [&](const jsont &t)
      {
        if(t.is_object() && t["_type"].value == "Subscript")
        {
          const jsont &b = t["value"];
          if(b.is_object() && b["_type"].value == "Name")
            globals_mutated_in_functions.insert(b["id"].value);
        }
      };
      if(nt == "Assign")
      {
        const jsont &targets = node["targets"];
        if(targets.is_array())
          for(const auto &t : to_json_array(targets))
            note_target(t);
      }
      else
        note_target(node["target"]);
    }
    else if(nt == "Expr")
    {
      const jsont &val = node["value"];
      if(val.is_object() && val["_type"].value == "Call")
      {
        const jsont &f = val["func"];
        if(
          f.is_object() && f["_type"].value == "Attribute" &&
          mutating_methods.count(f["attr"].value) && f["value"].is_object() &&
          f["value"]["_type"].value == "Name")
          globals_mutated_in_functions.insert(f["value"]["id"].value);
      }
    }
    for(const char **fp = fields; *fp; ++fp)
    {
      const jsont &child = node[*fp];
      if(!child.is_null())
        scan_fn_body(child);
    }
  };

  std::function<void(const jsont &)> scan = [&](const jsont &node)
  {
    if(node.is_array())
    {
      for(const auto &e : to_json_array(node))
        scan(e);
      return;
    }
    if(!node.is_object())
      return;
    const std::string nt = node["_type"].value;
    if(nt == "FunctionDef" || nt == "AsyncFunctionDef")
      scan_fn_body(node["body"]);
    for(const char **fp = fields; *fp; ++fp)
    {
      const jsont &child = node[*fp];
      if(!child.is_null())
        scan(child);
    }
  };
  scan(module_body);
}

void python_convertert::invalidate_global_value_tracking(
  bool include_dict_literals)
{
  // A module-level global key is "python::<name>" with no further "::"
  // (function-scoped locals are "python::<func>::<name>").
  auto is_global_key = [&](const irep_idt &k) -> bool
  {
    const std::string &s = id2string(k);
    if(s.compare(0, 8, "python::") != 0)
      return false;
    if(s.find("::", 8) != std::string::npos)
      return false; // function-scoped local, not a module global
    // Only invalidate globals that are actually MUTATED inside some
    // function — a call can only make a global stale if a function
    // mutates it. Never-mutated globals keep their conversion-time
    // folding (so e.g. a read-only global string used after an
    // unrelated call still folds precisely).
    return globals_mutated_in_functions.count(s.substr(8)) > 0;
  };
  auto prune = [&](auto &m)
  {
    for(auto it = m.begin(); it != m.end();)
      it = is_global_key(it->first) ? m.erase(it) : std::next(it);
  };
  // The SCALAR value maps are always invalidated. At the PRE-argument
  // site they are safe to erase (an argument referencing the global
  // falls back to the symbol value).
  prune(string_constants);
  prune(float_constants);
  // dict_literals is invalidated only at the POST-argument site
  // (include_dict_literals): a callee may have mutated a global dict in
  // place (`d[k]=v`), leaving a stale literal that folds a later
  // len/membership/subscript against the pre-call contents. It must NOT
  // be erased pre-argument (a `f(**d)` unpack reads it). list_literals
  // and tuple_literals are intentionally left intact (list reads are
  // already runtime, tuples are immutable, and `f(*c)` reads
  // list_literals structurally).
  if(include_dict_literals)
    prune(dict_literals);
}

void python_convertert::invalidate_loop_writes(const jsont &body)
{
  // Walk the body recursively, collect names assigned via Assign /
  // AnnAssign / AugAssign / For (target) / NamedExpr (walrus), then
  // invalidate the corresponding entries in the constant-tracking
  // maps so the body conversion doesn't fold against stale values.
  std::set<std::string> assigned;

  std::function<void(const jsont &)> scan = [&](const jsont &b)
  {
    if(!b.is_array())
      return;
    for(const auto &s : as_array(b))
    {
      if(is_node_type(s, "Assign"))
      {
        const jsont &targets = json_member(s, "targets");
        if(targets.is_array())
        {
          for(const auto &t : as_array(targets))
          {
            if(is_node_type(t, "Name"))
              assigned.insert(json_string(json_member(t, "id")));
            else if(is_node_type(t, "Tuple") || is_node_type(t, "List"))
            {
              const jsont &elts = json_member(t, "elts");
              if(elts.is_array())
              {
                for(const auto &e : as_array(elts))
                {
                  if(is_node_type(e, "Name"))
                    assigned.insert(json_string(json_member(e, "id")));
                  if(is_node_type(e, "Starred"))
                  {
                    const jsont &inner = json_member(e, "value");
                    if(is_node_type(inner, "Name"))
                      assigned.insert(json_string(json_member(inner, "id")));
                  }
                }
              }
            }
          }
        }
      }
      if(is_node_type(s, "AnnAssign") || is_node_type(s, "AugAssign"))
      {
        const jsont &t = json_member(s, "target");
        if(is_node_type(t, "Name"))
          assigned.insert(json_string(json_member(t, "id")));
      }
      if(is_node_type(s, "For"))
      {
        const jsont &t = json_member(s, "target");
        if(is_node_type(t, "Name"))
          assigned.insert(json_string(json_member(t, "id")));
        else if(is_node_type(t, "Tuple") || is_node_type(t, "List"))
        {
          const jsont &elts = json_member(t, "elts");
          if(elts.is_array())
          {
            for(const auto &e : as_array(elts))
              if(is_node_type(e, "Name"))
                assigned.insert(json_string(json_member(e, "id")));
          }
        }
      }
      // Recurse into nested control-flow.
      if(
        is_node_type(s, "If") || is_node_type(s, "While") ||
        is_node_type(s, "For") || is_node_type(s, "With") ||
        is_node_type(s, "Try"))
      {
        scan(json_member(s, "body"));
        scan(json_member(s, "orelse"));
        scan(json_member(s, "finalbody"));
        const jsont &handlers = json_member(s, "handlers");
        if(handlers.is_array())
        {
          for(const auto &h : as_array(handlers))
            scan(json_member(h, "body"));
        }
      }
      // Don't recurse into nested FunctionDef / ClassDef bodies —
      // those have their own scopes.
    }
  };
  scan(body);

  for(const std::string &n : assigned)
  {
    irep_idt id{qualify_name(n)};
    string_constants.erase(id);
    float_constants.erase(id);
    dict_literals.erase(id);
    list_literals.erase(id);
    tuple_literals.erase(id);
    complex_literals.erase(id);
  }
}

codet python_convertert::allocate_generator_cursor(
  const irep_idt &symbol_id,
  const jsont &value,
  const source_locationt &loc)
{
  // Recognise `gen()` where `gen` is a known generator function
  // (recorded in generator_functions when its def was processed).
  // The list-with-cursor model: each generator instance has a
  // hidden cursor symbol initialised to 0 here; next() consults
  // and advances it.
  if(!is_node_type(value, "Call"))
    return code_skipt{};
  const jsont &func = json_member(value, "func");
  std::string callee;
  if(is_node_type(func, "Name"))
    callee = json_string(json_member(func, "id"));
  else if(
    is_node_type(func, "Attribute") &&
    is_node_type(json_member(func, "value"), "Name"))
  {
    // Method generator `obj.gen()`: resolve the receiver's class so
    // we can test <Class>::<method> in generator_functions. Restricted
    // to a Name receiver so converting it has no side effects.
    std::string attr = json_string(json_member(func, "attr"));
    typet rt = convert_expression(json_member(func, "value")).type();
    if(rt.id() == ID_pointer)
      rt = to_pointer_type(rt).base_type();
    std::string tag;
    if(rt.id() == ID_struct_tag)
      tag = id2string(to_struct_tag_type(rt).get_identifier());
    else if(rt.id() == ID_struct)
      tag = id2string(to_struct_type(rt).get_tag());
    if(tag.rfind("python_class_", 0) == 0)
      tag = tag.substr(13);
    if(!tag.empty())
      callee = tag + "::" + attr;
  }
  if(callee.empty())
    return code_skipt{};
  // Try the qualified form (function defined in current scope)
  // and the unqualified form (top-level / nested function).
  std::string q_callee = qualify_name(callee);
  bool is_gen = generator_functions.count(callee) > 0 ||
                generator_functions.count(q_callee) > 0;
  if(!is_gen)
    return code_skipt{};

  std::string base = id2string(symbol_id);
  const std::string prefix{"python::"};
  if(base.compare(0, prefix.size(), prefix) == 0)
    base = base.substr(prefix.size());
  // Replace '::' with '_' so the resulting id is a flat,
  // human-readable identifier without nested scope syntax.
  std::string flat;
  flat.reserve(base.size());
  for(std::size_t i = 0; i < base.size(); ++i)
  {
    if(i + 1 < base.size() && base[i] == ':' && base[i + 1] == ':')
    {
      flat += '_';
      ++i;
    }
    else
      flat += base[i];
  }
  std::string cursor_name = "__cursor_" + flat;
  irep_idt cursor_id{prefix + cursor_name};

  if(symbol_table.lookup(cursor_id) == nullptr)
  {
    symbolt cs{cursor_id, signedbv_typet{64}, "python"};
    cs.base_name = cursor_name;
    cs.is_lvalue = true;
    cs.is_state_var = true;
    cs.is_static_lifetime = current_function.empty();
    symbol_table.add(cs);
  }
  generator_cursors[symbol_id] = cursor_id;

  code_frontend_assignt init{
    symbol_table.lookup_ref(cursor_id).symbol_expr(),
    from_integer(0, signedbv_typet{64})};
  init.add_source_location() = loc;
  return std::move(init);
}

exprt python_convertert::unwrap_value(const exprt &e, const typet &target_type)
{
  if(!is_python_value_type(e.type()))
    return e; // already concrete

  // PLR §3.2: when unwrapping a NONE-tagged python_value to a
  // typed natural-slot, produce the canonical None marker for
  // that slot rather than reading the wrong field (e.g.
  // __int_val = 0 for an Optional[int] None default) or
  // dereferencing NULL (e.g. *e.__str_ptr for a NONE-tagged
  // value reaching a typed-string target). The recognizer
  // matches both the literal struct form and a symbol-
  // expression whose stored value is python_value{NONE} (see
  // is_python_none); the latter handles imported-module-frozen
  // None defaults bound through __def_<func>_<idx> symbols.
  //
  // The same per-target marker conventions as
  // coerce_to_typed_slot — see its comment for the rationale.
  if(is_python_none(e, symbol_table))
  {
    if(
      target_type.id() == ID_signedbv || target_type.id() == ID_integer ||
      target_type == python_int_type())
      return from_integer(python_none_sentinel_int(), target_type);
    if(target_type.id() == ID_floatbv)
    {
      ieee_floatt v{
        ieee_float_spect{to_floatbv_type(target_type)},
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      v.from_integer(python_none_sentinel_int());
      return v.to_expr();
    }
    if(is_python_string_type(target_type))
      return safe_zero(target_type);
    if(
      is_python_list_type(target_type) || is_python_dict_type(target_type) ||
      is_python_set_type(target_type) || is_python_tuple_type(target_type))
      return safe_zero(target_type);
    // bool target: fall through to the truthiness builder
    // below — it already evaluates to False for NONE-tagged
    // input via the disjunction-of-tag-checks.
    // python_value target: fall through to the default
    // identity (caller wraps if needed).
  }

  // Extract the appropriate field based on target type
  if(
    target_type.id() == ID_signedbv || target_type.id() == ID_integer ||
    target_type == python_int_type())
    return python_value_int(e);
  else if(target_type.id() == ID_floatbv)
  {
    // PEP 484 numeric tower: int (and bool ⊂ int) promote to float. An
    // INT/BOOL-tagged python_value stores its payload in __int_val, not
    // __float_val, so reading __float_val unconditionally returned the unset
    // float slot (wrong value, e.g. an int-tagged union bound to a float
    // slot). Promote the integer payload when the tag is INT/BOOL; otherwise
    // read the float slot.
    exprt int_payload =
      typecast_exprt{python_value_int(e), to_floatbv_type(target_type)};
    return if_exprt{
      or_exprt{
        python_value_is(e, python_type_tagt::INT),
        python_value_is(e, python_type_tagt::BOOL)},
      int_payload,
      python_value_float(e)};
  }
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
      [&]
      {
        // Type the literal by the DENOTATION (integer under
        // --python-unbounded-ints, i64 otherwise): a mixed-type
        // comparison aborts the smt2 conversion.
        exprt iv = python_value_int(e);
        exprt z = from_integer(0, iv.type());
        return notequal_exprt{std::move(iv), std::move(z)};
      }()};
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
    // COMPLEX-tagged: dereference __class_ptr as a python_complex
    // struct; truthy iff real != 0 OR imag != 0 (PLR §6.10.1).
    struct_typet::componentst cplx_comps;
    cplx_comps.push_back(struct_typet::componentt{"real", double_type()});
    cplx_comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet cplx_struct_type{cplx_comps};
    cplx_struct_type.set_tag("python_complex");
    pointer_typet cplx_ptr_type{cplx_struct_type, 64};
    dereference_exprt cplx_deref{
      typecast_exprt{python_value_class_ptr(e), cplx_ptr_type},
      cplx_struct_type};
    exprt complex_true = and_exprt{
      python_value_is(e, python_type_tagt::COMPLEX),
      or_exprt{
        notequal_exprt{
          member_exprt{cplx_deref, "real", double_type()},
          safe_zero(double_type())},
        notequal_exprt{
          member_exprt{cplx_deref, "imag", double_type()},
          safe_zero(double_type())}}};
    // CLASS tag: per-instance identity-dispatched truthiness (shared
    // helper; a blanket always-truthy was a false proof -- see
    // pv_class_truthiness).
    exprt class_truthy = pv_class_truthiness(e);
    // TUPLE tag → truthy iff the tuple is non-empty. Its arity is not
    // recoverable from the opaque __class_ptr box, so use a SOUND nondet:
    // treating a boxed tuple as unconditionally truthy would be a FALSE PROOF
    // for a boxed empty tuple () (which is falsy and would then skip the
    // else-branch CPython takes). A fresh nondet explores both branches.
    exprt tuple_truthy;
    {
      static unsigned tt_ctr = 0;
      const std::string nm = "__tuple_truthy_nd_" + std::to_string(tt_ctr++);
      const irep_idt id{qualify_name(nm)};
      if(symbol_table.lookup(id) == nullptr)
      {
        symbolt s{id, bool_typet{}, "python"};
        s.base_name = nm;
        s.is_lvalue = true;
        s.is_state_var = true;
        symbol_table.add(s);
      }
      symbol_exprt nd = symbol_table.lookup_ref(id).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{
        nd, side_effect_expr_nondett{bool_typet{}, source_locationt{}}});
      tuple_truthy =
        and_exprt{python_value_is(e, python_type_tagt::TUPLE), std::move(nd)};
    }
    // DICT tag → truthy iff length > 0.
    {
      // Read the length from the dict struct via __class_ptr;
      // the dict layout has length at offset 0.
      pointer_typet len_ptr_type{signedbv_typet{64}, 64};
      exprt dict_len = dereference_exprt{
        typecast_exprt{python_value_class_ptr(e), len_ptr_type},
        signedbv_typet{64}};
      exprt dict_truthy_local = and_exprt{
        python_value_is(e, python_type_tagt::DICT),
        notequal_exprt{dict_len, from_integer(0, signedbv_typet{64})}};
      // NONE tag → false (not in any of the above)
      return or_exprt{
        or_exprt{bool_true, int_true},
        or_exprt{
          or_exprt{
            float_true, or_exprt{or_exprt{str_true, list_true}, complex_true}},
          or_exprt{or_exprt{class_truthy, tuple_truthy}, dict_truthy_local}}};
    }
  }
  else if(is_python_string_type(target_type))
    return python_value_str(e);
  else if(is_python_list_type(target_type))
  {
    // Per-instance provenance at the pv->list BINDING boundary: dispatch a
    // single-owner __iter__ for a CLASS-tagged value instead of derefing
    // the (garbage) list slot -- `return response.get(k, [])` under a
    // `-> List[...]` annotation previously collapsed the stub response
    // object to a nondet list, and every downstream iteration
    // false-alarmed (bedrock line 91). No obligation here: binding does
    // not raise in CPython (lower_pv_iterable adds it for iteration).
    code_blockt hdr;
    exprt view = pv_class_iter_view(e, hdr, source_locationt{});
    if(hdr.statements().empty())
      return python_value_list(e); // no dispatch engaged: previous path
    for(auto &st : hdr.statements())
      pending_checks.push_back(std::move(st));
    if(view.type() == target_type)
      return view;
    // Target element type differs from the view's (python_value):
    // materialise a TARGET-typed temp with the dispatched LENGTH and
    // per-slot element unwrap -- the stub-common length-0 case makes all
    // downstream loops iterate zero times; a non-empty view unwraps each
    // in-bounds element into the target element type (out-of-bounds slots
    // stay nondet, never read).
    static unsigned uw_view_ctr = 0;
    const std::string tn = "__unwrap_view_" + std::to_string(uw_view_ctr++);
    const irep_idt tid{qualify_name(tn)};
    if(symbol_table.lookup(tid) == nullptr)
    {
      symbolt ts{tid, target_type, "python"};
      ts.base_name = tn;
      ts.is_lvalue = true;
      ts.is_state_var = true;
      ts.is_static_lifetime = current_function.empty();
      symbol_table.add(ts);
    }
    symbol_exprt tsym2 = symbol_table.lookup_ref(tid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tsym2, side_effect_expr_nondett{target_type, source_locationt{}}});
    const signedbv_typet i64{64};
    member_exprt vlen{view, "length", i64};
    pending_checks.push_back(
      code_frontend_assignt{member_exprt{tsym2, "length", i64}, vlen});
    const auto &tgt_st = to_struct_type(target_type);
    const auto &tgt_arr = to_array_type(tgt_st.components()[1].type());
    const auto &view_st = to_struct_type(view.type());
    const auto &view_arr = to_array_type(view_st.components()[1].type());
    member_exprt tdata{tsym2, "data", tgt_arr};
    member_exprt vdata{view, "data", view_arr};
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; ++i)
    {
      exprt idx = from_integer(i, i64);
      exprt elem =
        unwrap_value(index_exprt{vdata, idx}, tgt_arr.element_type());
      code_frontend_assignt asg{index_exprt{tdata, idx}, std::move(elem)};
      pending_checks.push_back(code_ifthenelset{
        binary_relation_exprt{idx, ID_lt, vlen}, std::move(asg)});
    }
    return tsym2;
  }
  else if(is_python_dict_type(target_type))
  {
    // PLR §3.3.1: unwrap a dict stored via wrap_value.
    // wrap_value for dicts stores the dict in a static
    // __class_val_N symbol and tags the tagged-union with
    // CLASS + address of that symbol. Unwrap dereferences
    // __class_ptr and casts to the target dict type.
    pointer_typet cls_ptr_type{target_type, 64};
    return dereference_exprt{
      typecast_exprt{python_value_class_ptr(e), cls_ptr_type}, target_type};
  }
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
        typecast_exprt{python_value_class_ptr(e), cls_ptr_type}, target_type};
    }
    // python_complex unwrap: dereference __class_ptr as a
    // python_complex struct. Used when an annotated assignment
    // 'z: complex = ...' or `complex(...)` argument propagation
    // pulls a tagged-union back to its underlying struct.
    if(ttag == "python_complex")
    {
      pointer_typet cplx_ptr_type{target_type, 64};
      return dereference_exprt{
        typecast_exprt{python_value_class_ptr(e), cplx_ptr_type}, target_type};
    }
    return side_effect_expr_nondett{target_type, source_locationt{}};
  }
  else if(
    target_type.id() == ID_struct_tag && !is_python_value_type(target_type))
    return side_effect_expr_nondett{target_type, source_locationt{}};

  // Default: extract int
  return python_value_int(e);
}

/// PLR §3.1: rebuild a list-struct expression so its element type
/// is `python_value`. Each existing data element is `wrap_value`'d
/// individually. Used when promoting an escaped mutable's storage.
exprt python_convertert::rebuild_list_as_pv(const exprt &list_expr)
{
  if(!is_python_list_type(list_expr.type()))
    return list_expr;
  // If element type is already python_value, no rebuild needed.
  const auto &src_st = to_struct_type(list_expr.type());
  const auto &src_data_type = to_array_type(src_st.components()[1].type());
  if(is_python_value_type(src_data_type.element_type()))
    return list_expr;

  typet pv_list_type = python_list_type(python_value_type());
  const auto &pv_data_type =
    to_array_type(to_struct_type(pv_list_type).components()[1].type());

  // For struct_exprt RHS (a literal), iterate its operands directly so
  // we keep the original element values (constants, etc.). For an
  // already-stored value, fall back to indexing into the data array.
  exprt::operandst wrapped_elems;
  exprt src_len;
  // Literal DATA decodes through list_literal_element so the flag's
  // store-chain shape folds exactly like the bounded array literal.
  std::optional<std::size_t> lit_data_n;
  if(list_expr.id() == ID_struct && list_expr.operands().size() == 2)
    lit_data_n = list_literal_data_size(list_expr.operands()[1]);
  if(lit_data_n.has_value())
  {
    src_len = list_expr.operands()[0];
    const exprt &data_arr = list_expr.operands()[1];
    for(std::size_t i = 0; i < *lit_data_n; i++)
    {
      auto el = list_literal_element(data_arr, i);
      if(!el.has_value())
        break;
      wrapped_elems.push_back(wrap_value(std::move(*el)));
    }
  }
  else
  {
    src_len = member_exprt{list_expr, "length", signedbv_typet{64}};
    member_exprt src_data{list_expr, "data", src_data_type};
    // Bounded COPY of an unbounded list: fail closed past the scan.
    emit_scan_bound_guard(src_len, source_locationt{});
    for(std::size_t i = 0; i < PYTHON_MAX_LIST_LENGTH; i++)
    {
      exprt elem = index_exprt{src_data, from_integer(i, signedbv_typet{64})};
      wrapped_elems.push_back(wrap_value(elem));
    }
  }
  // Pad with safe_zero(python_value) so the array size matches.
  // Trim if the source had a non-default array size larger than max.
  if(wrapped_elems.size() > PYTHON_MAX_LIST_LENGTH)
    wrapped_elems.resize(PYTHON_MAX_LIST_LENGTH);

  return struct_exprt{
    {src_len, build_list_data(std::move(wrapped_elems), pv_data_type)},
    pv_list_type};
}

/// PLR §4.1 (Truth Value Testing): return a bool-typed expression
/// that is true iff `e` is truthy in Python. See header for the
/// list of false values.
exprt python_convertert::native_or_member_string_length(const exprt &s)
{
  if(s.type().id() == ID_string)
  {
    // Direction B: declare the GENERIC length intrinsic with its native Int
    // result and typecast to i64; the shared encoder emits (str.len s) [Int]
    // and convert_typecast supplies the int2bv (net SMT identical to the
    // retired Python-specific length branch, no encoder-side Python logic).
    const irep_idt fn{ID_cprover_string_length_func};
    if(symbol_table.lookup(fn) == nullptr)
    {
      symbolt fs{
        fn, mathematical_function_typet({s.type()}, integer_typet{}), "python"};
      fs.base_name = id2string(fn);
      symbol_table.add(fs);
    }
    function_application_exprt app{
      symbol_table.lookup_ref(fn).symbol_expr(), {s}};
    app.type() = integer_typet{};
    // Soundness (front-end): len() reads the Int str.len(s) as int2bv into i64
    // (via the typecast below). Bound it below 2^63 so that read is faithful
    // (no modular wrap to a spurious astronomically-long string in a
    // counterexample model). str.len(s) is deterministic, so one assume
    // constrains it globally. This belongs in the front-end -- which chose the
    // int2bv-over-bit-vector representation of len -- not the back-end (which
    // previously emitted it per String symbol, gated on the sort).
    pending_checks.push_back(code_assumet{binary_relation_exprt{
      app, ID_lt, from_integer(mp_integer{1} << 63, integer_typet{})}});
    return typecast_exprt{std::move(app), signedbv_typet{64}};
  }
  return member_exprt{s, "length", signedbv_typet{64}};
}

exprt python_convertert::bounded_nondet_string(const source_locationt &loc)
{
  // Refined (default) back-end: a string is a {length, data} struct, not an
  // smt_string. Building an smt_string here and then taking its length routes
  // a cprover_string_length_func over an smt_string into the refinement string
  // solver, which has no axioms for it and aborts in add_axioms_for_length.
  // So produce a nondet python_string struct with its length bound, mirroring
  // nondet_str's refined path -- this keeps EVERY nondet-string fallback site
  // (re.sub, the str-builtin / call-method / call-user fallbacks) sound on the
  // refined back-end rather than crashing.
  if(!use_smt_string_native)
  {
    static unsigned rctr = 0;
    const std::string rnm = "__bnd_str_" + std::to_string(rctr++);
    const irep_idt rid{qualify_name(rnm)};
    if(symbol_table.lookup(rid) == nullptr)
    {
      symbolt s{rid, python_string_type(), "python"};
      s.base_name = rnm;
      s.is_lvalue = true;
      s.is_state_var = true;
      symbol_table.add(s);
    }
    const symbol_exprt tmp = symbol_table.lookup_ref(rid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      tmp, side_effect_expr_nondett{python_string_type(), loc}});
    // Bind the struct's length field to the solver-visible length so a
    // downstream len() (which routes through cprover_string_length_func) and a
    // direct .length read agree, then bound it to [0, PYTHON_MAX_STRING_LENGTH].
    exprt len_intr = emit_string_int_function(
      ID_cprover_string_length_func, tmp, symbol_table, pending_checks);
    pending_checks.push_back(code_assumet{
      equal_exprt{member_exprt{tmp, "length", signedbv_typet{64}}, len_intr}});
    pending_checks.push_back(code_assumet{and_exprt{
      binary_relation_exprt{
        len_intr, ID_ge, from_integer(0, signedbv_typet{64})},
      binary_relation_exprt{
        len_intr,
        ID_le,
        from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}}});
    return std::move(tmp);
  }

  static unsigned ctr = 0;
  const std::string nm = "__bnd_smtstr_" + std::to_string(ctr++);
  const irep_idt id{qualify_name(nm)};
  if(symbol_table.lookup(id) == nullptr)
  {
    symbolt s{id, string_typet{}, "python"};
    s.base_name = nm;
    s.is_lvalue = true;
    s.is_state_var = true;
    symbol_table.add(s);
  }
  const symbol_exprt sym = symbol_table.lookup_ref(id).symbol_expr();
  pending_checks.push_back(
    code_frontend_assignt{sym, side_effect_expr_nondett{string_typet{}, loc}});
  // Constrain length to [0, PYTHON_MAX_STRING_LENGTH]. The lower bound rules
  // out the spurious negative (int2bv-wrapped) length models; the upper bound
  // matches the refined backend.
  const exprt len = native_or_member_string_length(sym);
  const typet lt = len.type();
  pending_checks.push_back(code_assumet{and_exprt{
    binary_relation_exprt{len, ID_ge, from_integer(0, lt)},
    binary_relation_exprt{
      len, ID_le, from_integer(PYTHON_MAX_STRING_LENGTH, lt)}}});
  return sym;
}

exprt python_convertert::native_string_app(
  const irep_idt &fn,
  std::vector<typet> arg_types,
  const exprt::operandst &args,
  const typet &ret)
{
  if(symbol_table.lookup(fn) == nullptr)
  {
    symbolt fs{
      fn, mathematical_function_typet(std::move(arg_types), ret), "python"};
    fs.base_name = id2string(fn);
    symbol_table.add(fs);
  }
  function_application_exprt app{
    symbol_table.lookup_ref(fn).symbol_expr(), args};
  app.type() = ret;
  return std::move(app);
}

exprt python_convertert::string_struct_view(const exprt &s)
{
  if(s.id() == ID_struct && s.operands().size() == 2)
    return s;
  return struct_exprt{
    {member_exprt{s, "length", signedbv_typet{64}},
     member_exprt{s, "data", pointer_typet{unsignedbv_typet{8}, 64}}},
    s.type()};
}

exprt python_convertert::string_concat(const exprt &a, const exprt &b)
{
  if(a.type().id() == ID_string && b.type().id() == ID_string)
    // Native SMT strings concatenate via the GENERIC value-returning concat
    // id, lowered by the shared upstream SMT-LIB encoder to (str.++ a b).
    // (Formerly a Python-specific cprover_string_smt_strcat_func lowered by a
    // parallel smt2_conv branch; retired in favour of the shared facility --
    // the lowering is identical for native String operands.)
    return native_string_app(
      ID_cprover_string_concat_func,
      {a.type(), b.type()},
      {a, b},
      string_typet{});
  return emit_string_function(
    ID_cprover_string_concat_func,
    {string_struct_view(a), string_struct_view(b)},
    symbol_table,
    pending_checks,
    loop_depth > 0,
    current_function);
}

exprt python_convertert::bind_string_length_hint(
  const exprt &produced,
  const exprt &length_hint,
  const source_locationt &loc)
{
  // Native SMT-String back-end length-hint wrapper. A produced native string
  // (e.g. the result of `s + t`) carries its length only as
  // `int2bv(str.len(<term>))`. cvc5 cannot relate that across the int2bv
  // boundary for an exact length relation: `int2bv(a+b)` and
  // `bvadd(int2bv a, int2bv b)` need a no-overflow argument the solver does not
  // discharge cheaply, so `len(s + t) == len(s) + len(t)`-style queries time
  // out (verified: the int2bv-wrapped equality is a cvc5 timeout while the
  // pure-Int form is instant). We alias the produced string to a fresh symbol
  // r (`r == produced`) and assume `len(r) == length_hint`, so len() queries
  // over r discharge by congruence against the hint. Sound: under the
  // per-string `str.len < 2^63` bound (emitted in smt2_conv) the hint is
  // implied by `r == produced`, so it adds no models.
  PRECONDITION(produced.type().id() == ID_string);
  static unsigned strlen_hint_ctr = 0;
  const std::string nm = "__strlen_hint_" + std::to_string(strlen_hint_ctr++);
  const irep_idt id{qualify_name(nm)};
  if(symbol_table.lookup(id) == nullptr)
  {
    symbolt sy{id, string_typet{}, "python"};
    sy.base_name = nm;
    sy.is_lvalue = true;
    sy.is_state_var = true;
    symbol_table.add(sy);
  }
  const symbol_exprt r = symbol_table.lookup_ref(id).symbol_expr();
  pending_checks.push_back(
    code_frontend_assignt{r, side_effect_expr_nondett{string_typet{}, loc}});
  pending_checks.push_back(code_assumet{equal_exprt{r, produced}});
  pending_checks.push_back(
    code_assumet{equal_exprt{native_or_member_string_length(r), length_hint}});
  return std::move(r);
}

/// Native SMT String str(n): compose sign handling over the GENERIC
/// from_int id (SMT-LIB str.from_int is defined only for naturals; negatives
/// yield ""): n < 0 ? "-" ++ from_int(-n) : from_int(n). Pure composition of
/// generic intrinsics -- no Python-specific back-end lowering.
exprt python_convertert::native_string_of_int(const exprt &n_in)
{
  const exprt n = n_in.type().id() == ID_integer
                    ? n_in
                    : exprt{typecast_exprt{n_in, integer_typet{}}};
  auto from_int_app = [&](const exprt &v) -> exprt
  {
    return native_string_app(
      ID_cprover_string_from_int_func, {integer_typet{}}, {v}, string_typet{});
  };
  const exprt neg_str = native_string_app(
    ID_cprover_string_concat_func,
    {string_typet{}, string_typet{}},
    {constant_exprt{"-", string_typet{}}, from_int_app(unary_minus_exprt{n})},
    string_typet{});
  return if_exprt{
    binary_relation_exprt{n, ID_lt, from_integer(0, integer_typet{})},
    neg_str,
    from_int_app(n)};
}

exprt python_convertert::string_substr(
  const exprt &s,
  const exprt &start,
  const exprt &len)
{
  const signedbv_typet i64{64};
  const exprt start64 = start.type() == i64 ? start : safe_typecast(start, i64);
  const exprt len64 = len.type() == i64 ? len : safe_typecast(len, i64);
  if(s.type().id() == ID_string)
  {
    // Native SMT String: emit the GENERIC substring id; the shared encoder
    // lowers it flat to (str.substr s offset len) -- SMT-LIB's exact contract,
    // which is also Python's (offset, len) convention. Positions are native
    // Int (Direction B). The callers clamp positions non-negative, so hop
    // through unsigned bv64 first: convert_typecast then emits the plain
    // (bv2nat x) -- the signed form's negative-case let/ite chains nested in
    // str.substr positions are a cvc5 performance cliff (observed hang).
    auto to_int = [](const exprt &e) -> exprt
    {
      return typecast_exprt{
        typecast_exprt{e, unsignedbv_typet{64}}, integer_typet{}};
    };
    return native_string_app(
      ID_cprover_string_substring_func,
      {s.type(), integer_typet{}, integer_typet{}},
      {s, to_int(start64), to_int(len64)},
      string_typet{});
  }
  return emit_string_function(
    ID_cprover_string_substring_func,
    {string_struct_view(s), start64, plus_exprt{start64, len64}},
    symbol_table,
    pending_checks,
    loop_depth > 0,
    current_function);
}

exprt python_convertert::string_equal(const exprt &a_in, const exprt &b_in)
{
  // Native: a stored key may be a string-id HANDLE; take its denotation.
  exprt a = python_dict_unbox_key(a_in);
  exprt b = python_dict_unbox_key(b_in);
  if(a.type().id() == ID_string && b.type().id() == ID_string)
    return equal_exprt{a, b};
  exprt r = emit_string_bool_function(
    ID_cprover_string_equal_func,
    string_struct_view(a),
    string_struct_view(b),
    symbol_table,
    pending_checks);
  if(r.type() != bool_typet{})
    r = typecast_exprt{std::move(r), bool_typet{}};
  return r;
}

exprt python_convertert::python_truthiness(const exprt &e)
{
  const typet &t = e.type();

  // Already bool — identity.
  if(t.id() == ID_bool)
    return e;

  // None sentinel for int representation: -2^62 (legacy encoding).
  const mp_integer none_sentinel = python_none_sentinel_int();

  // Concrete numeric types.
  if(t.id() == ID_signedbv || t.id() == ID_integer)
  {
    // 0 and the None-sentinel both represent falsy values.
    return and_exprt{
      notequal_exprt{e, from_integer(0, t)},
      notequal_exprt{e, from_integer(none_sentinel, t)}};
  }
  if(t.id() == ID_unsignedbv || t.id() == ID_c_bool)
    return notequal_exprt{e, from_integer(0, t)};
  if(t.id() == ID_floatbv)
  {
    // PLR §4.4: 0.0 (and -0.0) are falsy; NaN is truthy.
    // IEEE: 0.0 == -0.0, so a single != 0.0 covers both.
    // Also exclude the None sentinel (used to encode None
    // when bound to a typed-float slot via
    // coerce_to_typed_slot) so `if x:` for `x = None` bound
    // to a typed-float param correctly returns False.
    ieee_floatt none_f{
      ieee_float_spect{to_floatbv_type(t)},
      ieee_floatt::rounding_modet::ROUND_TO_EVEN};
    none_f.from_integer(none_sentinel);
    return and_exprt{
      not_exprt{ieee_float_equal_exprt{e, safe_zero(t)}},
      not_exprt{ieee_float_equal_exprt{e, none_f.to_expr()}}};
  }

  // Python tagged union: dispatch on the __tag field.
  if(is_python_value_type(t))
  {
    // For each non-NONE tag, build (tag-match AND value-truthy).
    // The disjunction is true exactly when one of the cases matches
    // and the underlying value is truthy. NONE / unrecognised tags
    // fall through to false.
    auto bool_truthy = and_exprt{
      python_value_is(e, python_type_tagt::BOOL),
      notequal_exprt{
        python_value_bool(e), from_integer(0, signedbv_typet{32})}};
    auto int_truthy = and_exprt{
      python_value_is(e, python_type_tagt::INT),
      [&]
      {
        // Literals typed by the DENOTATION (integer under
        // --python-unbounded-ints, i64 otherwise).
        exprt iv = python_value_int(e);
        const typet it_ = iv.type();
        return and_exprt{
          notequal_exprt{iv, from_integer(0, it_)},
          notequal_exprt{
            python_value_int(e), from_integer(none_sentinel, it_)}};
      }()};
    auto float_truthy = and_exprt{
      python_value_is(e, python_type_tagt::FLOAT),
      notequal_exprt{python_value_float(e), safe_zero(double_type())}};
    auto str_truthy = and_exprt{
      python_value_is(e, python_type_tagt::STR),
      notequal_exprt{
        native_or_member_string_length(python_value_str(e)),
        from_integer(0, signedbv_typet{64})}};
    auto list_truthy = and_exprt{
      python_value_is(e, python_type_tagt::LIST),
      notequal_exprt{
        member_exprt{python_value_list(e), "length", signedbv_typet{64}},
        from_integer(0, signedbv_typet{64})}};
    // CLASS-tagged python_value: dispatch on the per-instance class
    // identity (__class_tag) against the ClassDef-time truthiness
    // classification (PLR §4.4: __bool__ over __len__; no dunder ->
    // truthy). A blanket always-truthy was a FALSE PROOF (`_AnyDict`
    // with __len__ -> 0 is FALSY in CPython, so `if response:` guards
    // never run -- we executed them) and a path-explosion driver on the
    // stub-heavy corpus. FALSY classes are excluded from the truthy
    // disjunct; UNKNOWN classes contribute a nondet conjunct (sound in
    // both directions); TRUTHY (or unclassified/foreign) instances keep
    // the presence-is-truthy default via the residual arm.
    exprt class_truthy = pv_class_truthiness(e);
    // COMPLEX-tagged python_value: dereference __class_ptr as a
    // python_complex struct and apply PLR §6.10.1: 0+0j is
    // falsy, anything else is truthy.
    struct_typet::componentst complex_comps;
    complex_comps.push_back(struct_typet::componentt{"real", double_type()});
    complex_comps.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet complex_struct_type{complex_comps};
    complex_struct_type.set_tag("python_complex");
    pointer_typet complex_ptr_type{complex_struct_type, 64};
    dereference_exprt complex_deref{
      typecast_exprt{python_value_class_ptr(e), complex_ptr_type},
      complex_struct_type};
    auto complex_truthy = and_exprt{
      python_value_is(e, python_type_tagt::COMPLEX),
      or_exprt{
        not_exprt{ieee_float_equal_exprt{
          member_exprt{complex_deref, "real", double_type()},
          safe_zero(double_type())}},
        not_exprt{ieee_float_equal_exprt{
          member_exprt{complex_deref, "imag", double_type()},
          safe_zero(double_type())}}}};
    // DICT-tagged python_value: __class_ptr points at a dict; deref
    // and read length. Use canonical dict[str, python_value] type.
    typet dict_type = canonical_str_dict_type();
    pointer_typet dict_ptr_type{dict_type, 64};
    dereference_exprt dict_deref{
      typecast_exprt{python_value_class_ptr(e), dict_ptr_type}, dict_type};
    auto dict_truthy = and_exprt{
      python_value_is(e, python_type_tagt::DICT),
      notequal_exprt{
        member_exprt{dict_deref, "length", signedbv_typet{64}},
        from_integer(0, signedbv_typet{64})}};
    return or_exprt{
      or_exprt{or_exprt{bool_truthy, int_truthy}, float_truthy},
      or_exprt{
        or_exprt{or_exprt{str_truthy, list_truthy}, complex_truthy},
        or_exprt{dict_truthy, class_truthy}}};
  }

  // String / list / dict: truthy iff length > 0.
  if(
    is_python_string_type(t) || is_python_list_type(t) ||
    is_python_dict_type(t))
  {
    return notequal_exprt{
      native_or_member_string_length(e),
      from_integer(0, signedbv_typet{64})};
  }

  // Tuple: truthy iff it has any field. Tuples are tagged structs;
  // size is encoded in the type (no length field).
  if(is_python_tuple_type(t))
  {
    if(t.id() == ID_struct)
      return to_struct_type(t).components().empty() ? exprt{false_exprt{}}
                                                    : exprt{true_exprt{}};
  }

  // Pointer-to-list/dict: deref and recurse.
  if(t.id() == ID_pointer)
  {
    const auto &base = to_pointer_type(t).base_type();
    if(is_python_list_type(base) || is_python_dict_type(base))
      return python_truthiness(dereference_exprt{e});
  }

  // Class instance struct: try __bool__ then __len__; default True.
  if(t.id() == ID_struct || t.id() == ID_struct_tag)
  {
    std::string tag;
    if(t.id() == ID_struct)
      tag = id2string(to_struct_type(t).get_tag());
    else
      tag = id2string(to_struct_tag_type(t).get_identifier());
    // Complex: 0+0j is falsy. Use IEEE float equality so that -0.0
    // compares equal to 0.0 (a bit-level != would wrongly treat
    // complex(-0.0, 0.0) as truthy); NaN stays truthy (NaN != 0.0).
    if(tag == "python_complex" && t.id() == ID_struct)
    {
      return or_exprt{
        not_exprt{ieee_float_equal_exprt{
          member_exprt{e, "real", double_type()}, safe_zero(double_type())}},
        not_exprt{ieee_float_equal_exprt{
          member_exprt{e, "imag", double_type()}, safe_zero(double_type())}}};
    }
    if(tag.substr(0, 13) == "python_class_")
    {
      std::string cls = tag.substr(13);
      irep_idt bid{"python::" + cls + "::__bool__"};
      const symbolt *bsym = symbol_table.lookup(bid);
      if(bsym != nullptr && bsym->type.id() == ID_code)
      {
        return side_effect_expr_function_callt{
          bsym->symbol_expr(),
          {address_of_exprt{e}},
          bool_typet{},
          source_locationt{}};
      }
      irep_idt lid{"python::" + cls + "::__len__"};
      const symbolt *lsym = symbol_table.lookup(lid);
      if(lsym != nullptr && lsym->type.id() == ID_code)
      {
        side_effect_expr_function_callt call{
          lsym->symbol_expr(),
          {address_of_exprt{e}},
          to_code_type(lsym->type).return_type(),
          source_locationt{}};
        return notequal_exprt{
          std::move(call), safe_zero(to_code_type(lsym->type).return_type())};
      }
    }
    // No dunder available — class instances default to truthy.
    return true_exprt{};
  }

  // Unknown type: conservative nondet truthiness.
  return side_effect_expr_nondett{bool_typet{}, source_locationt{}};
}

bool python_convertert::any_user_class_defines_method(
  const std::string &method_name)
{
  for(const auto &[cls_name, cls_type] : class_types)
  {
    (void)cls_type;
    const symbolt *msym =
      symbol_table.lookup(irep_idt{"python::" + cls_name + "::" + method_name});
    if(msym != nullptr && msym->type.id() == ID_code)
      return true;
  }
  return false;
}

exprt python_convertert::unwrap_any_container_receiver(
  const exprt &obj,
  const std::string &method_name)
{
  if(!is_python_value_type(obj.type()))
    return obj;

  // Methods that unambiguously belong to a single built-in container type.
  // Ambiguous names shared across containers (pop / remove / clear / copy /
  // update / count) are intentionally excluded: disambiguating them on an
  // Any receiver needs the runtime tag (see
  // dispatch_any_container_method_by_tag).
  static const std::set<std::string> list_only = {
    "append", "extend", "insert", "sort", "reverse"};
  static const std::set<std::string> dict_only = {
    "setdefault", "popitem", "keys", "values", "items"};
  static const std::set<std::string> set_only = {"add", "discard"};

  const bool is_list = list_only.count(method_name) > 0;
  const bool is_dict = dict_only.count(method_name) > 0;
  const bool is_set = set_only.count(method_name) > 0;
  if(!is_list && !is_dict && !is_set)
    return obj;

  // If a user class defines this method, it is not a built-in container
  // method on this receiver -- leave it for the virtual-dispatch path.
  if(any_user_class_defines_method(method_name))
    return obj;

  // Unwrap to the concrete, by-reference container lvalue. The container is
  // shared via __list_ptr / __class_ptr (see make_python_value), so methods
  // that mutate the returned lvalue propagate to the caller's object.
  if(is_list)
  {
    exprt lst = python_value_list(obj);
    // Model-bound assumption (intrinsic, inventory D): a list reachable behind
    // an Any slot (e.g. `p[k].append(x)` through a python_value dict value) has
    // an UNCONSTRAINED length, so the mutator's capacity ASSERT fired
    // spuriously ("container capacity exceeded" -- the dict-if-not-in-idiom
    // regression under the Dict[str, object] lowering). Assume the pre-state
    // fits the bounded-container model, exactly like the nondet-list creation
    // sites do (python_converter_call_nondet.cpp) -- the same design
    // assumption, applied at the unbox chokepoint.
    if(is_python_list_type(lst.type()))
    {
      const signedbv_typet i64{64};
      member_exprt len{lst, "length", i64};
      pending_checks.push_back(code_assumet{and_exprt{
        binary_relation_exprt{len, ID_ge, from_integer(0, i64)},
        binary_relation_exprt{
          len, ID_lt, from_integer(PYTHON_MAX_LIST_LENGTH, i64)}}});
    }
    return lst;
  }

  if(is_set)
  {
    const typet set_type = python_set_type();
    return dereference_exprt{
      typecast_exprt{python_value_class_ptr(obj), pointer_typet{set_type, 64}},
      set_type};
  }

  const typet dict_type = canonical_str_dict_type();
  return dereference_exprt{
    typecast_exprt{python_value_class_ptr(obj), pointer_typet{dict_type, 64}},
    dict_type};
}

std::optional<exprt> python_convertert::dispatch_any_container_method_by_tag(
  const jsont &expr,
  const exprt &obj,
  const std::string &method_name,
  const jsont &args)
{
  if(!is_python_value_type(obj.type()))
    return std::nullopt;

  // Names shared across more than one built-in container. Disambiguated here
  // at runtime via python_value.__tag.
  static const std::set<std::string> ambiguous = {
    "pop", "remove", "clear", "copy", "update"};
  if(!ambiguous.count(method_name))
    return std::nullopt;
  if(any_user_class_defines_method(method_name))
    return std::nullopt;

  // A shared temp holds the method result on whichever branch is live; it is
  // left nondet when the runtime tag is neither LIST nor DICT (e.g. the value
  // is something on which this method would raise at runtime).
  static unsigned ctr = 0;
  const irep_idt tid{qualify_name("__any_meth_" + std::to_string(ctr++))};
  if(symbol_table.lookup(tid) == nullptr)
  {
    symbolt ts{tid, python_value_type(), "python"};
    ts.base_name = id2string(tid);
    ts.is_lvalue = true;
    ts.is_state_var = true;
    ts.is_static_lifetime = current_function.empty();
    symbol_table.add(ts);
  }
  const symbol_exprt result_sym = symbol_table.lookup_ref(tid).symbol_expr();

  bool any_branch = false;

  // Run a container handler on its by-reference view, then guard everything it
  // emitted (effects + result assignment) by __tag == tag so it only fires
  // when that container is the live one.
  auto run_branch =
    [&](const exprt &view, const typet &view_type, python_type_tagt tag)
  {
    const std::size_t before = pending_checks.size();
    std::optional<exprt> r;
    if(is_python_list_type(view_type))
      r = try_list_method(expr, view, view_type, method_name, args);
    else if(is_python_dict_type(view_type))
      r = try_dict_method(expr, view, view_type, method_name, args);
    else if(is_python_set_type(view_type))
      r = try_set_method(expr, view, view_type, method_name, args);
    if(!r.has_value())
    {
      // Handler declined: drop anything it may have emitted.
      pending_checks.erase(
        pending_checks.begin() + before, pending_checks.end());
      return;
    }
    exprt res = *r;
    if(!is_python_value_type(res.type()))
      res = wrap_value(res);
    pending_checks.push_back(code_frontend_assignt{result_sym, res});
    guard_pending_checks(before, python_value_is(obj, tag));
    any_branch = true;
  };

  const typet list_type = python_list_type(python_value_type());
  run_branch(python_value_list(obj), list_type, python_type_tagt::LIST);

  const typet dict_type = canonical_str_dict_type();
  const exprt dict_view = dereference_exprt{
    typecast_exprt{python_value_class_ptr(obj), pointer_typet{dict_type, 64}},
    dict_type};
  run_branch(dict_view, dict_type, python_type_tagt::DICT);

  const typet set_type = python_set_type();
  const exprt set_view = dereference_exprt{
    typecast_exprt{python_value_class_ptr(obj), pointer_typet{set_type, 64}},
    set_type};
  run_branch(set_view, set_type, python_type_tagt::SET);

  if(!any_branch)
    return std::nullopt;
  return result_sym;
}

exprt python_convertert::wrap_value(const exprt &e)
{
  if(is_python_value_type(e.type()))
    return e; // already wrapped

  // PLR §3.2: an int constant equal to the legacy None sentinel
  // represents None — wrap as the canonical NONE-tagged
  // python_value rather than {INT, sentinel}. Without this, the
  // tagged-union truthiness check would have to special-case
  // the sentinel value in the INT-tagged slot. Long-term this
  // should disappear once typed numeric slots stop using the
  // sentinel encoding altogether (see the typed-numeric is-None
  // residual in doc/python-frontend-plans.md).
  if(is_python_none_constant(e))
    return python_none_value();

  python_type_tagt tag = python_type_tagt::INT;
  if(e.type().id() == ID_floatbv)
    tag = python_type_tagt::FLOAT;
  else if(e.type().id() == ID_bool)
    tag = python_type_tagt::BOOL;
  else if(is_python_string_type(e.type()))
  {
    // Native: the __str payload is a string-id HANDLE (constants intern;
    // computed strings get a fresh handle + strtab axiom), so a string
    // wrapped into python_value by a construction site reached more than
    // once (function return / loop) does not alias. Refined: __str is
    // inline (value semantics, no aliasing).
    if(python_smt_string_native_flag())
      return make_python_value(python_type_tagt::STR, string_to_handle(e));
    return make_python_value(python_type_tagt::STR, e);
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
    // PLR §3.2: canonicalize a string-keyed dict to
    // dict[python_string, python_value] BEFORE boxing. Every consumer
    // of a DICT-tagged python_value (subscript read, In/NotIn key
    // membership, len) casts __class_ptr to that canonical layout; a
    // concrete dict[str, str] boxed as-is was silently misread through
    // the pun (a dict crossing a call boundary lost all its values).
    exprt to_box = e;
    if(is_python_dict_type(e.type()))
    {
      const auto &src_st = to_struct_type(e.type());
      const typet &src_keys_t = src_st.components()[1].type();
      const typet &src_vals_t = src_st.components()[2].type();
      const typet &key_elem_t = to_array_type(src_keys_t).element_type();
      const typet canon = canonical_str_dict_type();
      if(e.type() != canon && is_python_string_type(key_elem_t))
      {
        const auto &canon_st = to_struct_type(canon);
        member_exprt src_len{e, "length", signedbv_typet{64}};
        member_exprt src_keys{e, "keys", src_keys_t};
        member_exprt src_vals{e, "values", src_vals_t};
        exprt::operandst keys, vals;
        for(std::size_t i = 0; i < PYTHON_MAX_DICT_SIZE; i++)
        {
          const exprt idx = from_integer(i, signedbv_typet{64});
          keys.push_back(index_exprt{src_keys, idx});
          vals.push_back(wrap_value(index_exprt{src_vals, idx}));
        }
        to_box = struct_exprt{
          {src_len,
           array_exprt{
             std::move(keys), to_array_type(canon_st.components()[1].type())},
           array_exprt{
             std::move(vals), to_array_type(canon_st.components()[2].type())}},
          canon};
      }
    }
    const exprt &boxee = to_box;
    // We need a persistent pointer target. Materialise the struct
    // into a static-lifetime symbol so address_of yields a valid
    // pointer across statement boundaries.
    static unsigned class_wrap_counter = 0;
    std::string tmp_name =
      "__class_val_" + std::to_string(class_wrap_counter++);
    std::string tmp_qname = qualify_name(tmp_name);
    irep_idt tmp_id{tmp_qname};
    if(symbol_table.lookup(tmp_id) == nullptr)
    {
      symbolt tmp_sym{tmp_id, boxee.type(), "python"};
      tmp_sym.base_name = tmp_name;
      tmp_sym.is_lvalue = true;
      tmp_sym.is_state_var = true;
      symbol_table.add(tmp_sym);
    }
    const symbolt &tmp_sym = symbol_table.lookup_ref(tmp_id);
    pending_checks.push_back(
      code_frontend_assignt{tmp_sym.symbol_expr(), boxee});
    // Ensure the materialised copy carries a valid __class_tag.
    // The 'return ClassName(args)' path doesn't explicitly set
    // __class_tag on the return-tmp before the wrap, so we
    // re-emit it here based on the struct's declared class tag.
    // The struct's type-tag 'python_class_<Name>' gives us the
    // class name.
    const auto &st = to_struct_type(e.type());
    std::string stag = id2string(st.get_tag());
    const std::string prefix = "python_class_";
    // Dict struct: use DICT tag (not CLASS). Distinguishes
    // dict-wrapped-in-tagged-union from a user-class instance
    // so len() / unwrap operations can safely dereference the
    // pointer as a dict struct.
    if(is_python_dict_type(e.type()))
    {
      return make_python_value(
        python_type_tagt::DICT, address_of_exprt{tmp_sym.symbol_expr()});
    }
    // python_set struct: use SET tag (not CLASS) so len() / truthiness /
    // unwrap_value and the Any-receiver method dispatch dereference it as a
    // set struct.
    if(is_python_set_type(e.type()))
    {
      return make_python_value(
        python_type_tagt::SET, address_of_exprt{tmp_sym.symbol_expr()});
    }
    // python_tuple struct: use TUPLE tag (not CLASS) so isinstance(x, tuple),
    // truthiness (empty tuple is falsy) and comparison treat it as a tuple.
    if(is_python_tuple_type(e.type()))
    {
      return make_python_value(
        python_type_tagt::TUPLE, address_of_exprt{tmp_sym.symbol_expr()});
    }
    // python_complex struct: use COMPLEX tag (not CLASS).
    // Lets python_truthiness / unwrap_value dereference and
    // apply PLR §6.10.1 (0+0j is falsy).
    if(stag == "python_complex")
    {
      return make_python_value(
        python_type_tagt::COMPLEX, address_of_exprt{tmp_sym.symbol_expr()});
    }
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

  // Unbounded ("int boxing"): an int value is materialised behind an
  // integer* so python_value stays fixed-width and keeps full precision.
  if(tag == python_type_tagt::INT && unbounded_ints)
    return make_python_value(tag, box_int_for_storage(e));
  return make_python_value(tag, e);
}

/// Tag-aware equality for two python_value operands. Compares the
/// active variant: STR by string content (a struct equal_exprt would
/// compare the data pointers, which differ between equal-content
/// strings), the rest by their scalar payload. The STR comparison reads
/// the inline __str field on both operands; for a non-STR operand __str
/// is the empty {0, NULL} default, and the surrounding tag guard
/// discards the result anyway.

exprt python_convertert::materialize_call_operand(const exprt &e)
{
  if(
    e.id() != ID_side_effect ||
    to_side_effect_expr(e).get_statement() != ID_function_call ||
    e.type().id() == ID_empty || e.type().id() == ID_code)
    return e;
  static unsigned call_operand_ctr = 0;
  const std::string nm = "__call_operand_" + std::to_string(call_operand_ctr++);
  const irep_idt id{qualify_name(nm)};
  if(symbol_table.lookup(id) == nullptr)
  {
    symbolt s{id, e.type(), "python"};
    s.base_name = nm;
    s.is_lvalue = true;
    s.is_state_var = true;
    symbol_table.add(s);
  }
  symbol_exprt se = symbol_table.lookup_ref(id).symbol_expr();
  pending_checks.push_back(code_frontend_assignt{se, e});
  return std::move(se);
}

void python_convertert::invalidate_list_literals_referencing(
  const irep_idt &sym)
{
  if(list_literals.empty() && tuple_literals.empty() && dict_literals.empty())
    return;
  std::function<bool(const exprt &)> refs = [&](const exprt &e) -> bool
  {
    if(e.id() == ID_symbol && to_symbol_expr(e).get_identifier() == sym)
      return true;
    for(const auto &op : e.operands())
      if(refs(op))
        return true;
    return false;
  };
  // A cached literal that references `sym` (e.g. `t = (b, a)` caches a struct
  // holding symbol b) goes STALE when `sym` is reassigned: a later fold/read
  // reusing the snapshot reads the NEW value -> a false proof (found by the
  // mutation-oracle for both list_literals and, via the tuple-repeat fold,
  // tuple_literals). Drop such entries from every constant-tracking map that
  // stores element expressions. The entry keyed by `sym` itself is kept (it is
  // being (re)assigned, so its snapshot is refreshed by the store).
  const auto sweep = [&](std::map<irep_idt, exprt> &m)
  {
    for(auto it = m.begin(); it != m.end();)
    {
      if(it->first != sym && refs(it->second))
        it = m.erase(it);
      else
        ++it;
    }
  };
  sweep(list_literals);
  sweep(tuple_literals);
  sweep(dict_literals);
}

void python_convertert::invalidate_reassigned_symbol(const irep_idt &sym)
{
  invalidate_list_literals_referencing(sym);
  float_constants.erase(sym);
  string_constants.erase(sym);
  none_constants.erase(sym);
}

exprt python_convertert::value_equal(const exprt &a, const exprt &b)
{
  PRECONDITION(
    is_python_value_type(a.type()) && is_python_value_type(b.type()));
  const signedbv_typet i32{32};
  member_exprt at{a, "__tag", i32};
  exprt tags_eq = equal_exprt{at, member_exprt{b, "__tag", i32}};
  const auto tag_is = [&](python_type_tagt t) {
    return equal_exprt{at, from_integer(static_cast<int>(t), i32)};
  };
  exprt str_eq = emit_string_bool_function(
    ID_cprover_string_equal_func,
    python_value_str(a),
    python_value_str(b),
    symbol_table,
    pending_checks);
  if(str_eq.type() != bool_typet{})
    str_eq = typecast_exprt{std::move(str_eq), bool_typet{}};
  exprt float_eq = equal_exprt{
    member_exprt{a, "__float_val", double_type()},
    member_exprt{b, "__float_val", double_type()}};
  exprt bool_eq = equal_exprt{
    member_exprt{a, "__bool_val", i32}, member_exprt{b, "__bool_val", i32}};
  // INT default also covers NONE (both payloads are 0).
  exprt int_eq = equal_exprt{python_value_int(a), python_value_int(b)};
  return and_exprt{
    tags_eq,
    if_exprt{
      tag_is(python_type_tagt::STR),
      str_eq,
      if_exprt{
        tag_is(python_type_tagt::FLOAT),
        float_eq,
        if_exprt{tag_is(python_type_tagt::BOOL), bool_eq, int_eq}}}};
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

    // PLR §4.1: scalar → bool is Python truth value testing.
    if(target.id() == ID_bool)
      return python_truthiness(e);
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
    // address_of requires an lvalue. A non-lvalue struct (a literal or
    // a call result) is materialised into a temp first, so the callee
    // receives a valid address. PLR §3.1: lvalue args (symbol / member
    // / index / deref) are addressed directly so mutations propagate to
    // the caller; an rvalue has no caller storage to propagate to, so a
    // temp is semantically correct. This makes every struct→pointer
    // boundary (all method-dispatch paths included) pointer-uniform.
    const typet &p_base = to_pointer_type(target).base_type();
    bool e_lvalue = e.id() == ID_symbol || e.id() == ID_member ||
                    e.id() == ID_index || e.id() == ID_dereference;
    // PLR §3.1: a container argument (list / dict) bound to a
    // by-reference parameter whose element types differ cannot be passed
    // by raw pointer reinterpret — the element layouts differ. When the
    // difference is a value-widening (a concrete element type → the
    // python_value tagged union) the argument is *promotable*: each
    // widened array component is rebuilt element-wise via wrap_value into
    // a temp of the parameter type, its address is passed, and — for an
    // lvalue arg — the (possibly mutated) elements are copied back so
    // mutations propagate. This is the shared list/dict/set boundary.
    //
    // The soundness guard: only the value-widened components are written
    // back; a component that differs in a non-widening way (e.g. str vs
    // int dict KEYS) is *not* promotable, so we fall back to a by-value
    // temp (element-wise convert, no write-back) — identical to the
    // pre-by-reference behaviour, never an unsound reinterpret.
    bool same_container =
      (is_python_list_type(p_base) && is_python_list_type(e.type())) ||
      (is_python_dict_type(p_base) && is_python_dict_type(e.type()));
    if(same_container && p_base != e.type())
    {
      const auto &dst_st = to_struct_type(p_base);
      const auto &src_st = to_struct_type(e.type());
      auto widened = [&](std::size_t c)
      {
        const typet &de =
          to_array_type(dst_st.components()[c].type()).element_type();
        const typet &se =
          to_array_type(src_st.components()[c].type()).element_type();
        return de != se && is_python_value_type(de);
      };
      // Promotable iff every differing array component widens to value.
      bool promotable =
        dst_st.components().size() == src_st.components().size();
      for(std::size_t c = 1; promotable && c < dst_st.components().size(); c++)
      {
        const typet &de =
          to_array_type(dst_st.components()[c].type()).element_type();
        const typet &se =
          to_array_type(src_st.components()[c].type()).element_type();
        if(de != se && !is_python_value_type(de))
          promotable = false;
      }
      // Build a temp of the parameter type. For promotable components we
      // wrap_value element-wise (and remember which to write back); for
      // matching components we copy as-is; for a non-promotable component
      // we element-wise safe_typecast (by-value, no write-back).
      exprt::operandst comps;
      comps.push_back(member_exprt{
        e, dst_st.components()[0].get_name(), dst_st.components()[0].type()});
      for(std::size_t c = 1; c < dst_st.components().size(); c++)
      {
        const auto &dst_at = to_array_type(dst_st.components()[c].type());
        const auto &src_at = to_array_type(src_st.components()[c].type());
        member_exprt sa{e, src_st.components()[c].get_name(), src_at};
        if(dst_at.element_type() == src_at.element_type())
        {
          comps.push_back(sa);
          continue;
        }
        // Bounded COPY: under --python-smt-containers the array size
        // is infinite; rebuild the scan prefix and fail closed on
        // longer lists (the length member is component 0).
        std::size_t rebuild_cap = PYTHON_MAX_LIST_LENGTH;
        mp_integer asz;
        if(
          dst_at.size().is_constant() &&
          !to_integer(to_constant_expr(dst_at.size()), asz))
          rebuild_cap = numeric_cast_v<std::size_t>(asz);
        else
          emit_scan_bound_guard(
            member_exprt{e, "length", signedbv_typet{64}}, source_locationt{});
        exprt::operandst elems;
        for(std::size_t k = 0; k < rebuild_cap; k++)
        {
          exprt el = index_exprt{sa, from_integer(k, signedbv_typet{64})};
          elems.push_back(
            widened(c) ? wrap_value(el)
                       : safe_typecast(el, dst_at.element_type()));
        }
        comps.push_back(build_list_data(std::move(elems), dst_at));
      }
      exprt promoted = struct_exprt{std::move(comps), p_base};
      static unsigned cp_ctr = 0;
      irep_idt tid{qualify_name("__byref_cont_" + std::to_string(cp_ctr++))};
      if(symbol_table.lookup(tid) == nullptr)
      {
        symbolt ts{tid, p_base, "python"};
        ts.base_name = id2string(tid);
        ts.is_lvalue = true;
        ts.is_state_var = true;
        ts.is_static_lifetime = current_function.empty();
        symbol_table.add(ts);
      }
      symbol_exprt bsym = symbol_table.lookup_ref(tid).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{bsym, promoted});
      // Write mutations back only when the promotion is reversible (every
      // differing component widened to value) and the arg is an lvalue.
      if(promotable && e_lvalue)
      {
        for(std::size_t c = 1; c < dst_st.components().size(); c++)
        {
          const auto &dst_at = to_array_type(dst_st.components()[c].type());
          const auto &src_at = to_array_type(src_st.components()[c].type());
          member_exprt ba{bsym, dst_st.components()[c].get_name(), dst_at};
          member_exprt ta{e, src_st.components()[c].get_name(), src_at};
          const typet et = src_at.element_type();
          std::size_t wb_cap = PYTHON_MAX_LIST_LENGTH;
          mp_integer asz;
          if(
            dst_at.size().is_constant() &&
            !to_integer(to_constant_expr(dst_at.size()), asz))
            wb_cap = numeric_cast_v<std::size_t>(asz);
          // (the promote loop above already emitted the fail-closed
          // scan guard for the non-constant/infinite case)
          for(std::size_t k = 0; k < wb_cap; k++)
          {
            exprt idx = from_integer(k, signedbv_typet{64});
            exprt src_el = index_exprt{ba, idx};
            exprt back = widened(c) ? unwrap_value(src_el, et) : exprt{src_el};
            if(back.type() != et)
              back = safe_typecast(back, et);
            pending_post_checks.push_back(
              code_frontend_assignt{index_exprt{ta, idx}, back});
          }
        }
        pending_post_checks.push_back(code_frontend_assignt{
          member_exprt{e, "length", signedbv_typet{64}},
          member_exprt{bsym, "length", signedbv_typet{64}}});
      }
      return address_of_exprt{bsym};
    }
    exprt obj = e;
    if(!e_lvalue)
    {
      static unsigned byref_tmp_ctr = 0;
      std::string tn = "__byref_tmp_" + std::to_string(byref_tmp_ctr++);
      irep_idt tid{qualify_name(tn)};
      if(symbol_table.lookup(tid) == nullptr)
      {
        symbolt ts{tid, e.type(), "python"};
        ts.base_name = tn;
        ts.is_lvalue = true;
        ts.is_state_var = true;
        ts.is_static_lifetime = current_function.empty();
        symbol_table.add(ts);
      }
      obj = symbol_table.lookup_ref(tid).symbol_expr();
      pending_checks.push_back(code_frontend_assignt{obj, e});
    }
    return typecast_exprt{address_of_exprt{obj}, target};
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

  // PLR §4.1: any-source-type → bool dispatches through truthiness.
  if(target.id() == ID_bool)
    return python_truthiness(e);

  // List/dict type coercion: list[float] → list[int] etc.
  // A raw typecast is only well-formed when the flattened widths agree,
  // i.e. when the ELEMENT types match. With differing element types
  // (e.g. list[int64] vs list[python_value]) the bit-level layouts
  // differ, and the reinterpreting cast handed the SAT back-end
  // mismatched-width bitvectors (garbage literals — an invariant
  // failure in lcnf, found by the AWS code-action corpus sweep via
  // `[] if c else <pv-list>` ternaries).
  if(
    (is_python_list_type(e.type()) && is_python_list_type(target)) ||
    (is_python_dict_type(e.type()) && is_python_dict_type(target)))
  {
    auto element_types_match = [](const typet &a, const typet &b)
    {
      const auto &ast = to_struct_type(a);
      const auto &bst = to_struct_type(b);
      if(ast.components().size() != bst.components().size())
        return false;
      for(std::size_t i = 0; i < ast.components().size(); i++)
        if(ast.components()[i].type() != bst.components()[i].type())
          return false;
      return true;
    };
    if(element_types_match(e.type(), target))
      return typecast_exprt{e, target};
    // Value-widening: rebuild element-wise into the tagged union.
    if(is_python_list_type(target))
    {
      const auto &tgt_data =
        to_array_type(to_struct_type(target).components()[1].type());
      if(is_python_value_type(tgt_data.element_type()))
      {
        exprt rebuilt = rebuild_list_as_pv(e);
        if(rebuilt.type() == target)
          return rebuilt;
      }
      // General element-wise rebuild preserving the LENGTH: coerce each
      // slot into the target element type through coerce_element (the
      // shared write choke point). Bit-preservation mattered in practice:
      // `return r.get(k, [])` under a List[...] annotation flows an empty
      // list[int64] into a list[dict]-typed slot, and a nondet fallback
      // here loses length==0 (regression caught by
      // class-to-list-annotation-coercion).
      const typet &tgt_elem = tgt_data.element_type();
      mp_integer cap_i;
      if(
        tgt_data.size().is_constant() &&
        !to_integer(to_constant_expr(tgt_data.size()), cap_i))
      {
        const std::size_t cap = static_cast<std::size_t>(cap_i.to_long());
        const auto &src_data =
          to_array_type(to_struct_type(e.type()).components()[1].type());
        exprt::operandst elems;
        exprt src_len;
        if(
          e.id() == ID_struct && e.operands().size() == 2 &&
          e.operands()[1].id() == ID_array)
        {
          src_len = e.operands()[0];
          for(const auto &op : e.operands()[1].operands())
          {
            if(elems.size() >= cap)
              break;
            elems.push_back(coerce_element(op, tgt_elem));
          }
        }
        else
        {
          src_len = member_exprt{e, "length", signedbv_typet{64}};
          member_exprt sdata{e, "data", src_data};
          mp_integer scap_i{0};
          if(
            src_data.size().is_constant() &&
            !to_integer(to_constant_expr(src_data.size()), scap_i))
          {
            const std::size_t n =
              std::min(cap, static_cast<std::size_t>(scap_i.to_long()));
            for(std::size_t i = 0; i < n; i++)
              elems.push_back(coerce_element(
                index_exprt{sdata, from_integer(i, signedbv_typet{64})},
                tgt_elem));
          }
        }
        if(!src_len.is_nil())
        {
          bool well_typed = true;
          for(const auto &el : elems)
            if(el.type() != tgt_elem)
              well_typed = false;
          if(well_typed)
          {
            while(elems.size() < cap)
              elems.push_back(safe_zero(tgt_elem));
            return struct_exprt{
              {src_len,
               array_exprt{
                 std::move(elems), array_typet{tgt_elem, tgt_data.size()}}},
              target};
          }
        }
      }
    }
    // Dict analogue of the list rebuild: coerce keys and values
    // slot-wise into the target key/value element types, preserving
    // the LENGTH. Bit-preservation matters for the ubiquitous
    // stub shape `return {} / return {'K': v}` under a `-> dict`
    // annotation: the two return literals have different concrete
    // layouts, and the nondet fallback here destroyed the non-empty
    // return's values (every poll-loop postcondition became
    // unprovable — the wait-until-ACTIVE demo shape).
    if(is_python_dict_type(target))
    {
      const auto &tgt_st = to_struct_type(target);
      const auto &tgt_keys_arr = to_array_type(tgt_st.components()[1].type());
      const auto &tgt_vals_arr = to_array_type(tgt_st.components()[2].type());
      const typet &tgt_key = tgt_keys_arr.element_type();
      const typet &tgt_val = tgt_vals_arr.element_type();
      mp_integer cap_i;
      if(
        tgt_keys_arr.size().is_constant() &&
        !to_integer(to_constant_expr(tgt_keys_arr.size()), cap_i))
      {
        const std::size_t cap = static_cast<std::size_t>(cap_i.to_long());
        exprt::operandst keys, vals;
        exprt src_len = nil_exprt{};
        if(
          e.id() == ID_struct && e.operands().size() == 3 &&
          e.operands()[1].id() == ID_array && e.operands()[2].id() == ID_array)
        {
          src_len = e.operands()[0];
          const auto &src_keys = e.operands()[1].operands();
          const auto &src_vals = e.operands()[2].operands();
          for(std::size_t i = 0;
              i < std::min(src_keys.size(), src_vals.size()) && i < cap;
              i++)
          {
            keys.push_back(coerce_element(src_keys[i], tgt_key));
            vals.push_back(coerce_element(src_vals[i], tgt_val));
          }
        }
        else
        {
          const auto &src_st = to_struct_type(e.type());
          const auto &src_keys_arr =
            to_array_type(src_st.components()[1].type());
          const auto &src_vals_arr =
            to_array_type(src_st.components()[2].type());
          src_len = member_exprt{e, "length", signedbv_typet{64}};
          member_exprt skeys{e, "keys", src_keys_arr};
          member_exprt svals{e, "values", src_vals_arr};
          mp_integer scap_i{0};
          if(
            src_keys_arr.size().is_constant() &&
            !to_integer(to_constant_expr(src_keys_arr.size()), scap_i))
          {
            const std::size_t n =
              std::min(cap, static_cast<std::size_t>(scap_i.to_long()));
            for(std::size_t i = 0; i < n; i++)
            {
              const exprt idx = from_integer(i, signedbv_typet{64});
              keys.push_back(coerce_element(index_exprt{skeys, idx}, tgt_key));
              vals.push_back(coerce_element(index_exprt{svals, idx}, tgt_val));
            }
          }
        }
        if(!src_len.is_nil())
        {
          bool well_typed = true;
          for(const auto &el : keys)
            if(el.type() != tgt_key)
              well_typed = false;
          for(const auto &el : vals)
            if(el.type() != tgt_val)
              well_typed = false;
          if(well_typed)
          {
            while(keys.size() < cap)
              keys.push_back(safe_zero(tgt_key));
            while(vals.size() < cap)
              vals.push_back(safe_zero(tgt_val));
            return struct_exprt{
              {src_len,
               array_exprt{
                 std::move(keys), array_typet{tgt_key, tgt_keys_arr.size()}},
               array_exprt{
                 std::move(vals), array_typet{tgt_val, tgt_vals_arr.size()}}},
              target};
          }
        }
      }
    }
    // No sound reinterpretation exists: over-approximate with a nondet
    // of the target type rather than emit an ill-typed cast.
    log_overapprox(
      "container coercion with incompatible element layout — "
      "returning nondet");
    return side_effect_expr_nondett{target, e.source_location()};
  }

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

  // PLR §3.3 / §4: pointer-to-struct passed where the same
  // struct is expected → dereference. Common for class
  // methods passing `self` (Foo*) to a constructor that
  // expects a Foo by value.
  if(
    e.type().id() == ID_pointer &&
    to_pointer_type(e.type()).base_type() == target &&
    (target.id() == ID_struct || target.id() == ID_struct_tag))
  {
    return dereference_exprt{e};
  }

  // Struct-to-scalar or other incompatible: return a nondet value
  // of the target type (overapproximation, avoids crash)
  return side_effect_expr_nondett{target, source_locationt{}};
}

exprt python_convertert::coerce_to_typed_slot(
  const exprt &expr,
  const typet &target_type)
{
  // PLR §3.2 None-marker binding for typed slots.
  //
  // A python_value{NONE} value binds to a typed parameter,
  // assignment target, or return slot as the canonical None
  // marker for that target type rather than going through
  // unwrap_value's NULL-deref or zero-field-extraction path.
  // Each natural-type slot has its own marker convention:
  //   - python_string:  {length=0, data=NULL} (cluster v9, the
  //                     length-0 Optional[str] fast-path).
  //                     Distinguishable from `""` (which has
  //                     length=0 but data ≠ NULL) via a
  //                     data-pointer check at compare sites.
  //   - python_int:     python_none_sentinel_int() cast to the
  //                     target integer type (matches the
  //                     existing 'is None' typed-int compare).
  //   - python_float:   python_none_sentinel_int() cast to
  //                     floatbv (signed sentinel; -2^62 is far
  //                     from any plausible computation result
  //                     and is exactly representable as IEEE
  //                     double).
  //   - python_list:    {length=0, data=zero-filled-array}
  //                     (via safe_zero). Conflates with `[]`
  //                     (empty list literal) at compare sites
  //                     — `arg is None` for typed-list slots
  //                     uses `length == 0` which is also true
  //                     for `[]`. The architecturally clean
  //                     answer for typed-list None is to lower
  //                     `Optional[list]` annotations to
  //                     python_value (tagged-union) at the
  //                     typing layer rather than via the typed
  //                     marker — out of scope here.
  //   - python_dict:    {length=0, keys=[], values=[]} (via
  //                     safe_zero). Same conflation caveat.
  //
  // The recognizer accepts BOTH the literal struct form and a
  // symbol-expression whose stored value is python_value{NONE}
  // — see `is_python_none`. Without the symbol-form path the
  // defaults loop would bind `__def_foo_N.__int_val` (= 0,
  // not the None sentinel) for `Optional[int] = None`.
  if(is_python_none(expr, symbol_table))
  {
    if(is_python_string_type(target_type))
      return safe_zero(target_type);
    if(
      target_type.id() == ID_signedbv || target_type.id() == ID_integer ||
      target_type == python_int_type())
    {
      return from_integer(python_none_sentinel_int(), target_type);
    }
    if(target_type.id() == ID_floatbv)
    {
      ieee_floatt v{
        ieee_float_spect{to_floatbv_type(target_type)},
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      v.from_integer(python_none_sentinel_int());
      return v.to_expr();
    }
    if(is_python_list_type(target_type) || is_python_dict_type(target_type))
      return safe_zero(target_type);
    if(is_python_set_type(target_type))
    {
      // PLR: empty-set marker for typed-set slots —
      // {bitmap=0, offset=0} (no elements). Same conflation
      // caveat as list/dict (`set() is None` returns False at
      // compare side; the None marker is indistinguishable
      // from a real empty set there) but boundary code is at
      // least deterministic rather than NULL-deref.
      return safe_zero(target_type);
    }
    if(is_python_tuple_type(target_type))
    {
      // PLR: typed-tuple slots binding None get a zeroed
      // tuple of the declared shape. Tuples are static-arity,
      // so the marker conflates with literal `(0, 0, ..., 0)`
      // tuples; rarely meaningful in practice since
      // Optional[tuple] defaults of None are uncommon.
      return safe_zero(target_type);
    }
  }

  // PLR §3.1: object identity is preserved across boundaries.
  // When binding a class-instance Name (symbol of a
  // python_class_<C> struct type) to a python_value tagged-
  // union slot, pass the address of the caller's storage
  // rather than wrapping a fresh copy. Otherwise mutations
  // the callee performs through the parameter (`p.attr = X`)
  // would hit a copy and be invisible to the caller.
  //
  // Restricted to symbol-typed argument expressions (i.e.
  // plain Names) so that expression results from method
  // returns continue to materialise their own backing storage.
  // The class_tag is set on the caller's storage as a side
  // effect (via pending_checks) so isinstance dispatches
  // correctly inside the callee.
  //
  // Previously open-coded only in python_converter_call_user.cpp
  // (call-argument boundary). Centralised here so the same
  // identity-preservation rule applies to assign-RHS and
  // return-value boundaries too.
  // An LVALUE instance bound to an Any/python_value slot must preserve object
  // identity: box address_of(expr) so callee mutations propagate back to the
  // caller's object (PLR §3.1 reference semantics). A plain symbol is the
  // common case; a DEREFERENCE (`*r`, an aliased instance pointer -- `r = obj`)
  // / MEMBER (`obj.field`) / INDEX (`lst[i]`) are equally persistent lvalues,
  // and address_of of each yields the same storage (e.g. address_of(*r) == r),
  // so they share identity too. Without this, `f(r)` where `r = obj` boxed a
  // throwaway __class_val copy -- callee mutations were lost (a false proof:
  // a2_narrowing_alias). RVALUE structs (a fresh `V()`) are NOT lvalues here,
  // so they keep the copy path (a new object, identity irrelevant).
  if(
    (expr.id() == ID_symbol || expr.id() == ID_dereference ||
     expr.id() == ID_member || expr.id() == ID_index) &&
    is_python_value_type(target_type) &&
    (expr.type().id() == ID_struct || expr.type().id() == ID_struct_tag))
  {
    std::string atag;
    if(expr.type().id() == ID_struct)
      atag = id2string(to_struct_type(expr.type()).get_tag());
    else
      atag = id2string(to_struct_tag_type(expr.type()).get_identifier());
    if(atag.compare(0, 13, "python_class_") == 0)
    {
      std::string cname = atag.substr(13);
      auto ti = class_tag_ids.find(cname);
      if(ti != class_tag_ids.end())
      {
        pending_checks.push_back(code_frontend_assignt{
          member_exprt{expr, "__class_tag", signedbv_typet{32}},
          from_integer(ti->second, signedbv_typet{32})});
      }
      return make_python_value(python_type_tagt::CLASS, address_of_exprt{expr});
    }
  }

  if(expr.type() == target_type)
    return expr;

  // Class instance into a LIST-typed slot (PLR §3.3.1): the value converts
  // through ITS OWN __iter__ (a `return response.get(k, [])` under a
  // `-> List[...]` annotation flows the stub response object here;
  // safe_typecast could only relabel/nondet it, severing provenance -- the
  // bedrock family). Statically-known receiver class: dispatch is direct
  // (no identity guard needed). VALID/UNKNOWN protocol only; an INVALID
  // __iter__ falls through (the iteration sites raise the TypeError).
  if(is_python_list_type(target_type) && expr.type().id() == ID_struct)
  {
    const std::string ctag = id2string(to_struct_type(expr.type()).get_tag());
    if(ctag.substr(0, 13) == "python_class_")
    {
      const std::string bare = ctag.substr(13);
      const symbolt *isym =
        symbol_table.lookup(irep_idt{"python::" + bare + "::__iter__"});
      if(
        isym != nullptr && isym->type.id() == ID_code &&
        iter_protocol_of(bare) != iter_protocol_kindt::INVALID)
      {
        const code_typet &it_t = to_code_type(isym->type);
        if(is_python_list_type(it_t.return_type()))
        {
          // Materialise the receiver (an rvalue call result cannot be
          // address-taken) and the dispatched call.
          static unsigned cls2list_ctr = 0;
          const std::string rn =
            "__cls2list_recv_" + std::to_string(cls2list_ctr);
          const std::string tn = "__cls2list_" + std::to_string(cls2list_ctr);
          ++cls2list_ctr;
          const irep_idt rid{qualify_name(rn)}, tid{qualify_name(tn)};
          if(symbol_table.lookup(rid) == nullptr)
          {
            symbolt rs{rid, expr.type(), "python"};
            rs.base_name = rn;
            rs.is_lvalue = true;
            rs.is_state_var = true;
            rs.is_static_lifetime = current_function.empty();
            symbol_table.add(rs);
          }
          if(symbol_table.lookup(tid) == nullptr)
          {
            symbolt ts{tid, it_t.return_type(), "python"};
            ts.base_name = tn;
            ts.is_lvalue = true;
            ts.is_state_var = true;
            ts.is_static_lifetime = current_function.empty();
            symbol_table.add(ts);
          }
          symbol_exprt rsym = symbol_table.lookup_ref(rid).symbol_expr();
          symbol_exprt tsym = symbol_table.lookup_ref(tid).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{rsym, expr});
          side_effect_expr_function_callt icall{
            isym->symbol_expr(),
            {typecast_exprt{
              address_of_exprt{rsym},
              to_code_type(isym->type).parameters()[0].type()}},
            it_t.return_type(),
            source_locationt{}};
          pending_checks.push_back(code_frontend_assignt{tsym, icall});
          if(it_t.return_type() == target_type)
            return tsym;
          return safe_typecast(tsym, target_type);
        }
      }
    }
  }

  return safe_typecast(expr, target_type);
}

exprt python_convertert::coerce_call_argument(
  const exprt &arg,
  const typet &param_type,
  const irep_idt &param_id)
{
  // PLR §3.2 / type-safety: Python annotations are NOT runtime coercions.
  // Binding a tagged-union/Any value (python_value) to a concretely-typed
  // SCALAR parameter coerces it via unwrap_value, which reads the requested
  // field (e.g. __int_val) with no tag check -- so a str-tagged value bound to
  // an `int` parameter is silently used as an int instead of raising TypeError.
  // Emit a runtime tag obligation, but ONLY when the parameter is EXPLICITLY
  // ANNOTATED (annotation provenance): an inferred/default scalar param type
  // (lambda / unannotated param) really accepts Any, so asserting its tag would
  // false-alarm. bool ⊂ int and int/bool promote to float (PEP 484 numeric
  // tower), so those tags are accepted. The obligation is over the arg's actual
  // runtime tag, so a genuinely-matching value never false-alarms.
  // PLR §3.2 / type-safety: binding a tagged-union/Any value to a concretely-
  // typed SCALAR parameter. A MISMATCHED tag means any concrete use inside the
  // callee raises TypeError; model that as a CONDITIONAL, CATCHABLE exception
  // at the binding (the boundary approximation of the callee-internal raise),
  // and bind the value GUARDEDLY: unwrap when the tag matches (precise),
  // nondet otherwise (never a silent pun). This keeps the 004-family sound in
  // default mode (a mismatched arg's use fails via the exception path) while
  // an UNKNOWABLE tag no longer hard-fails the call site (the previous
  // add_check ASSERT false-alarmed on 4 real-world boto3 benchmarks; under
  // --python-no-exception-checks -- the benchmark configuration -- the
  // exception path is not a failure). Only for EXPLICITLY ANNOTATED params
  // (provenance); bool ⊂ int, int/bool promote to float (PEP 484).
  if(
    !param_id.empty() && explicitly_annotated_params.count(param_id) &&
    is_python_value_type(arg.type()) && !is_python_none(arg, symbol_table))
  {
    exprt ok = nil_exprt{};
    if(
      param_type.id() == ID_signedbv || param_type.id() == ID_integer ||
      param_type == python_int_type())
      ok = or_exprt{
        python_value_is(arg, python_type_tagt::INT),
        python_value_is(arg, python_type_tagt::BOOL)};
    else if(param_type.id() == ID_floatbv)
      ok = or_exprt{
        or_exprt{
          python_value_is(arg, python_type_tagt::FLOAT),
          python_value_is(arg, python_type_tagt::INT)},
        python_value_is(arg, python_type_tagt::BOOL)};
    else if(is_python_string_type(param_type))
      ok = python_value_is(arg, python_type_tagt::STR);
    if(!ok.is_nil())
    {
      emit_conditional_exception(not_exprt{ok}, "TypeError");
      return if_exprt{
        std::move(ok),
        coerce_to_typed_slot(arg, param_type),
        side_effect_expr_nondett{param_type, arg.source_location()}};
    }
  }
  return coerce_to_typed_slot(arg, param_type);
}

exprt python_convertert::coerce_assign_rhs(
  const exprt &rhs,
  const typet &lhs_type)
{
  // Native backend string-id handles: a str value assigned into a HANDLE
  // slot (a boxed-in-aggregate string -- see python_string_handle_type)
  // allocates a handle whose strtab image is the value.
  if(
    python_smt_string_native_flag() && is_python_string_handle_type(lhs_type) &&
    is_python_string_type(rhs.type()))
    return string_to_handle(rhs);

  // Int-id handles: an int value assigned into a HANDLE-typed int slot
  // (class fields under --python-unbounded-ints) allocates a handle
  // whose inttab image is the value (full precision through the Int
  // table).
  if(
    python_unbounded_ints_flag() && is_python_int_handle_type(lhs_type) &&
    (rhs.type().id() == ID_integer || rhs.type().id() == ID_signedbv ||
     rhs.type().id() == ID_bool))
  {
    exprt v = rhs;
    if(v.type().id() != ID_integer)
      v = typecast_exprt{v, integer_typet{}};
    return int_to_handle(v);
  }

  return coerce_to_typed_slot(rhs, lhs_type);
}

exprt python_convertert::coerce_return_value(
  const exprt &ret_val,
  const typet &return_type)
{
  return coerce_to_typed_slot(ret_val, return_type);
}

exprt python_convertert::coerce_element(
  const exprt &elem,
  const typet &element_type)
{
  // A python_value flowing into a STRING-shaped slot (handle,
  // or plain string) unwraps through its STRING denotation. Without this,
  // coerce_to_typed_slot pattern-matches the slot's machine type and
  // extracts __int_val for bv64 slots -- comparing an int payload against
  // string keys (the csv `_dialects.pop(name, None)` crash: `name` was a
  // pv). Same choke-point discipline as the str->bv64 handle rule below;
  // covers get/pop/setdefault/subscript key coercions in one place.
  if(is_python_value_type(elem.type()))
  {
    const bool string_slot = is_python_string_handle_type(element_type) ||
                             is_python_string_type(element_type);
    if(string_slot)
      return coerce_element(python_value_str(elem), element_type);
  }
  // Native string-id handles: a str value stored into a HANDLE element/
  // value slot (dict values, list elements -- the aggregate constructors
  // enforce the representation invariant) allocates a handle. The
  // CONTEXT rule (str -> any bv64 slot) backs up the comment-flag
  // recognition: the ID_C_ flag is dropped by some type-rebuilding paths
  // (observed: the annotated-empty-list element type reaching append),
  // and under the native backend the ONLY meaning of storing a string
  // into a 64-bit integer slot is a handle.
  if(
    python_smt_string_native_flag() && is_python_string_type(elem.type()) &&
    (is_python_string_handle_type(element_type) ||
     (element_type.id() == ID_signedbv &&
      to_signedbv_type(element_type).get_width() == 64)))
    return string_to_handle(elem);
  return coerce_to_typed_slot(elem, element_type);
}

void python_convertert::emit_capacity_guard(
  code_blockt &block,
  const exprt &length,
  long cap,
  const source_locationt &loc,
  bool unbounded_write)
{
  // P1/P2 (--python-smt-containers): a length-indexed store into an
  // infinite LIST data array has no capacity to guard. Dict appends
  // keep the guard (see the declaration) — keyed on the caller's
  // explicit parameter, NOT on the cap value: the list and dict caps
  // are independently configurable and may coincide.
  if(python_smt_containers_flag() && unbounded_write)
    return;

  binary_relation_exprt in_bounds{
    length, ID_lt, from_integer(cap, length.type())};
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment("container capacity exceeded (verifier model bound)");
  code_assertt cap_assert{in_bounds};
  cap_assert.add_source_location() = aloc;
  block.add(std::move(cap_assert));
  code_assumet cap_assume{in_bounds};
  cap_assume.add_source_location() = loc;
  block.add(std::move(cap_assume));
}

void python_convertert::emit_count_capacity_guard(
  std::vector<codet> &checks,
  const exprt &count,
  long cap,
  const source_locationt &loc)
{
  // NOTE (--python-smt-containers): this guard is NOT suppressed for
  // infinite-array lists. Its callers are producers with bounded COPY
  // LOOPS (extend, concatenation, repetition, slice assignment): the
  // loop writes at most `cap` slots regardless of the data array's
  // size, so a longer result would be silently TRUNCATED — the guard
  // keeps that fail-closed (python-model-bound at the producing
  // operation) until each producer is converted to length-driven
  // copying (P1 backlog). The append/subscript guards, whose stores
  // are length-indexed and exact on infinite arrays, ARE suppressed
  // (see emit_capacity_guard / emit_index_capacity_guard).

  // count <= cap: a resulting length of exactly `cap` fills indices
  // 0..cap-1 (valid); cap+1 would overflow the modelled data array.
  binary_relation_exprt in_bounds{
    count, ID_le, from_integer(cap, count.type())};
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment("container capacity exceeded (verifier model bound)");
  code_assertt cap_assert{in_bounds};
  cap_assert.add_source_location() = aloc;
  checks.push_back(std::move(cap_assert));
  code_assumet cap_assume{in_bounds};
  cap_assume.add_source_location() = loc;
  checks.push_back(std::move(cap_assume));
}

void python_convertert::emit_int_overflow_guard(
  const exprt &no_overflow,
  const source_locationt &loc)
{
  // Guard only a DEFINITE (statically-provable) overflow -- the constant /
  // folded cases (10**19, 1<<70, big-literal arithmetic). A symbolic operation
  // whose overflow cannot be decided at conversion time is left to the default
  // 64-bit wrap (a documented bound; --python-unbounded-ints is the sound
  // mode). Asserting on every POSSIBLY-overflowing symbolic arithmetic would be
  // far too noisy -- it fires on `def f(a, b): return a * b` and `x + 1` on a
  // nondet `x` -- unlike the container-capacity guards, whose size is usually
  // provably in range. So: report+cut only when `no_overflow` simplifies to a
  // definite false.
  const namespacet ns{symbol_table};
  const exprt simp = simplify_expr(no_overflow, ns);
  if(!simp.is_false())
    return;
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment("integer exceeds 64-bit verifier model bound");
  code_assertt ovf_assert{false_exprt{}};
  ovf_assert.add_source_location() = aloc;
  pending_checks.push_back(std::move(ovf_assert));
  code_assumet ovf_assume{false_exprt{}};
  ovf_assume.add_source_location() = loc;
  pending_checks.push_back(std::move(ovf_assume));
}

bool python_convertert::is_aliased_list_element(const jsont &node)
{
  // `<base>[idx]` where base is a Name in aliased_mutable_lists (mutating an
  // element of a list with aliased by-value mutable elements).
  if(is_node_type(node, "Subscript"))
  {
    const jsont &base = json_member(node, "value");
    if(!is_node_type(base, "Name"))
      return false;
    return aliased_mutable_lists.count(
             irep_idt{qualify_name(json_string(json_member(base, "id")))}) > 0;
  }
  // A Name bound to a shared inner element (`row = g[i]`): mutating it in place
  // mutates the shared object.
  if(is_node_type(node, "Name"))
    return shared_inner_mutables.count(
             irep_idt{qualify_name(json_string(json_member(node, "id")))}) > 0;
  return false;
}

void python_convertert::emit_aliased_mutation_guard(const source_locationt &loc)
{
  // PLR object identity: mutating an element of a list whose by-value mutable
  // elements are aliased (shared via a replicating/sharing op) is not modelled
  // -- the value representation cannot propagate the mutation to the aliases.
  // Report it as a model bound (assert false) and cut the path (assume false)
  // so no downstream observation can produce a false proof.
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment(
    "in-place mutation through an unmodelled mutable-element alias "
    "(verifier model bound)");
  code_assertt a{false_exprt{}};
  a.add_source_location() = aloc;
  pending_checks.push_back(std::move(a));
  code_assumet as{false_exprt{}};
  as.add_source_location() = loc;
  pending_checks.push_back(std::move(as));
}

void python_convertert::emit_set_range_guard(
  std::vector<codet> &checks,
  const exprt &elem,
  const source_locationt &loc,
  const exprt &included)
{
  // 0 <= elem < 64: the element must lie in the 64-bit bitmap's representable
  // range (offset 0). Outside it, `1 << elem` overflows and silently drops the
  // element -- an unsound omission. Report + cut instead. When `included` is
  // given, only require the range on the path where the element is actually
  // added (`included ==> in-range`).
  const exprt e64 = elem.type().id() == ID_signedbv
                      ? elem
                      : typecast_exprt{elem, signedbv_typet{64}};
  exprt in_range{and_exprt{
    binary_relation_exprt{e64, ID_ge, from_integer(0, e64.type())},
    binary_relation_exprt{e64, ID_lt, from_integer(64, e64.type())}}};
  exprt in_bounds = included.is_nil()
                      ? in_range
                      : exprt{or_exprt{not_exprt{included}, in_range}};
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment("set element outside modelled bitmap range (model bound)");
  code_assertt cap_assert{in_bounds};
  cap_assert.add_source_location() = aloc;
  checks.push_back(std::move(cap_assert));
  code_assumet cap_assume{in_bounds};
  cap_assume.add_source_location() = loc;
  checks.push_back(std::move(cap_assume));
}

void python_convertert::emit_index_capacity_guard(
  std::vector<codet> &checks,
  const exprt &idx,
  const exprt &length,
  long cap,
  const source_locationt &loc)
{
  // P1 (--python-smt-containers): infinite-array lists have no
  // capacity to guard (see emit_capacity_guard). The only caller is
  // the LIST subscript read/write path, so suppression under the flag
  // is unconditional — NOT keyed on the cap value (the list and dict
  // caps are independently configurable and may coincide).
  if(python_smt_containers_flag())
    return;
  // Fire ONLY for a valid Python index that exceeds the modelled array:
  //   (idx < length) ==> (idx < cap)
  // An idx >= length is a normal IndexError (handled separately, the access is
  // never taken), so it must NOT be reported as a model-bound violation. An
  // idx < length but >= cap means the list's length outran the modelled data
  // (a silently over-capacity producer) -- that is the case we report + cut.
  exprt idx_lt_len{binary_relation_exprt{idx, ID_lt, length}};
  exprt idx_lt_cap{
    binary_relation_exprt{idx, ID_lt, from_integer(cap, idx.type())}};
  exprt in_bounds{or_exprt{not_exprt{idx_lt_len}, idx_lt_cap}};
  source_locationt aloc = loc;
  aloc.set_property_class("python-model-bound");
  aloc.set_comment("container capacity exceeded (verifier model bound)");
  code_assertt cap_assert{in_bounds};
  cap_assert.add_source_location() = aloc;
  checks.push_back(std::move(cap_assert));
  code_assumet cap_assume{in_bounds};
  cap_assume.add_source_location() = loc;
  checks.push_back(std::move(cap_assume));
}

void python_convertert::coerce_call_arguments(
  exprt::operandst &args,
  const code_typet::parameterst &params)
{
  const std::size_t n = std::min(args.size(), params.size());
  for(std::size_t i = 0; i < n; i++)
    args[i] = coerce_call_argument(
      args[i], params[i].type(), params[i].get_identifier());
}

std::optional<symbol_exprt> python_convertert::mro_owner_class_object(
  const std::string &class_name,
  const std::string &attr) const
{
  // Walk the C3 MRO of class_name; return the first class
  // object that explicitly declares `attr` in its body.
  // PLR §3.3.2: class attribute lookup walks MRO at access
  // time, so a subclass that inherits an attr resolves to
  // the owning ancestor's class object, and updates to that
  // ancestor's storage are visible through subclass reads.
  auto try_class = [&](const std::string &cn) -> std::optional<symbol_exprt>
  {
    auto it = class_owned_attrs.find(cn);
    if(it == class_owned_attrs.end() || it->second.count(attr) == 0)
      return std::nullopt;
    irep_idt class_obj_id{"python::" + cn};
    const symbolt *class_obj = symbol_table.lookup(class_obj_id);
    if(class_obj == nullptr)
      return std::nullopt;
    return class_obj->symbol_expr();
  };
  // Try the class itself first.
  if(auto r = try_class(class_name))
    return r;
  // Walk the MRO (skipping index 0 which is class_name itself).
  auto mro_it = class_mro.find(class_name);
  if(mro_it != class_mro.end())
  {
    for(std::size_t i = 1; i < mro_it->second.size(); ++i)
    {
      if(auto r = try_class(mro_it->second[i]))
        return r;
    }
  }
  // Fall back to scanning class_bases (for classes with no
  // computed MRO, e.g. ones whose ClassDef ran before the C3
  // step).
  auto bases_it = class_bases.find(class_name);
  if(bases_it != class_bases.end())
  {
    for(const auto &base : bases_it->second)
    {
      if(auto r = mro_owner_class_object(base, attr))
        return r;
    }
  }
  return std::nullopt;
}

std::pair<irep_idt, const symbolt *>
python_convertert::lookup_init_via_mro(const std::string &class_name) const
{
  irep_idt init_id{"python::" + class_name + "::__init__"};
  const symbolt *init_sym = symbol_table.lookup(init_id);
  if(init_sym != nullptr)
    return {init_id, init_sym};
  // Inheritance fallback: walk the C3 MRO and return the first
  // ancestor that defines `__init__`.
  auto mro_it = class_mro.find(class_name);
  if(mro_it == class_mro.end())
    return {irep_idt{}, nullptr};

  for(std::size_t i = 1; i < mro_it->second.size(); ++i)
  {
    irep_idt ancestor_init_id{"python::" + mro_it->second[i] + "::__init__"};
    const symbolt *ancestor_sym = symbol_table.lookup(ancestor_init_id);
    if(ancestor_sym != nullptr)
      return {ancestor_init_id, ancestor_sym};
  }
  return {irep_idt{}, nullptr};
}

/// PLR §7.5/§6.10: if `obj.<attr>` has a `__present_<attr>` flag (i.e. the attr
/// is `del`-eted somewhere in the program), return an assignment setting it true
/// -- any store to the attribute makes it present. Returns nullopt when there is
/// no such flag, so ordinary attributes are unaffected. Mirrors
/// maybe_shadow_assign; call it at the same store sites.
std::optional<code_frontend_assignt> python_convertert::maybe_present_assign(
  const exprt &obj_lvalue,
  const std::string &attr)
{
  if(deleted_attr_targets.count(attr) == 0)
    return std::nullopt;
  const std::string present_name = "__present_" + attr;
  const struct_typet *st = nullptr;
  if(obj_lvalue.type().id() == ID_struct)
    st = &to_struct_type(obj_lvalue.type());
  else if(obj_lvalue.type().id() == ID_struct_tag)
  {
    std::string tag =
      id2string(to_struct_tag_type(obj_lvalue.type()).get_identifier());
    std::string cls_name =
      tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
    auto cls_it = class_types.find(cls_name);
    if(cls_it != class_types.end())
      st = &cls_it->second;
  }
  if(st == nullptr || !st->has_component(present_name))
    return std::nullopt;
  return code_frontend_assignt{
    member_exprt{obj_lvalue, present_name, c_bool_typet{8}},
    from_integer(1, c_bool_typet{8})};
}

std::optional<code_frontend_assignt> python_convertert::maybe_shadow_assign(
  const exprt &obj_lvalue,
  const std::string &attr)
{
  // Determine the class name from the struct tag. Both
  // ID_struct (literal-typed struct exprt) and ID_struct_tag
  // (struct_tag_typet referencing a named struct) shapes can
  // appear here.
  std::string tag;
  if(obj_lvalue.type().id() == ID_struct)
    tag = id2string(to_struct_type(obj_lvalue.type()).get_tag());
  else if(obj_lvalue.type().id() == ID_struct_tag)
    tag = id2string(to_struct_tag_type(obj_lvalue.type()).get_identifier());
  else
    return std::nullopt;
  std::string cls_name =
    tag.substr(0, 13) == "python_class_" ? tag.substr(13) : tag;
  auto cla_it = class_level_attrs.find(cls_name);
  if(cla_it == class_level_attrs.end() || cla_it->second.count(attr) == 0)
    return std::nullopt;
  std::string shadow_name = "__shadow_" + attr;
  // Resolve the struct components — we need to know the shadow
  // field exists.
  const struct_typet *st = nullptr;
  if(obj_lvalue.type().id() == ID_struct)
    st = &to_struct_type(obj_lvalue.type());
  else
  {
    auto cls_it = class_types.find(cls_name);
    if(cls_it != class_types.end())
      st = &cls_it->second;
  }
  if(st == nullptr || !st->has_component(shadow_name))
    return std::nullopt;
  // Skip if obj is the class object itself (writing
  // `Class.attr` should NOT set a shadow flag — Class.attr is
  // the canonical class-level storage).
  if(obj_lvalue.id() == ID_symbol)
  {
    std::string sid = id2string(to_symbol_expr(obj_lvalue).get_identifier());
    if(sid == "python::" + cls_name)
      return std::nullopt;
  }
  return code_frontend_assignt{
    member_exprt{obj_lvalue, shadow_name, c_bool_typet{8}},
    from_integer(1, c_bool_typet{8})};
}

std::optional<code_blockt> python_convertert::build_dataclass_init_block(
  const std::string &class_name,
  const exprt &self_lvalue,
  const jsont &call_node,
  const source_locationt &loc)
{
  auto fit = dataclass_init_fields.find(class_name);
  if(fit == dataclass_init_fields.end())
    return std::nullopt;
  // Only synthesize when there is NO explicit/inherited __init__ (otherwise
  // that real __init__ runs and binds the fields itself).
  if(lookup_init_via_mro(class_name).second != nullptr)
    return std::nullopt;
  auto cit = class_types.find(class_name);
  if(cit == class_types.end())
    return std::nullopt;
  const struct_typet &st = cit->second;
  const std::vector<std::string> &fields = fit->second;

  // Gather positional and keyword constructor arguments.
  std::vector<exprt> pos;
  const jsont &args = json_member(call_node, "args");
  if(args.is_array())
    for(const auto &a : as_array(args))
      pos.push_back(convert_expression(a));
  std::map<std::string, exprt> kw;
  const jsont &kws = json_member(call_node, "keywords");
  if(kws.is_array())
    for(const auto &k : as_array(kws))
    {
      const jsont &arg = json_member(k, "arg");
      if(!arg.is_null())
        kw[json_string(arg)] = convert_expression(json_member(k, "value"));
    }

  code_blockt block;
  for(std::size_t i = 0; i < fields.size(); ++i)
  {
    const std::string &fn = fields[i];
    if(!st.has_component(fn))
      continue;
    const typet &ft = st.get_component(fn).type();
    exprt val;
    if(i < pos.size())
      val = pos[i]; // positional arg
    else
    {
      auto kit = kw.find(fn);
      if(kit != kw.end())
        val = kit->second; // keyword arg
      else
        continue; // omitted -> leave the class-level default in place
    }
    if(val.is_nil())
      continue;
    if(val.type() != ft)
      val = safe_typecast(val, ft);
    code_frontend_assignt a{member_exprt{self_lvalue, fn, ft}, val};
    a.add_source_location() = loc;
    block.add(std::move(a));
    // If the field also exists as a class-level attribute, reads route through
    // `__shadow_<f> ? self.f : Class.f`; mark the shadow so the read picks the
    // instance value we just bound (mirrors a normal `self.f = ...` store).
    if(auto shadow = maybe_shadow_assign(self_lvalue, fn))
    {
      shadow->add_source_location() = loc;
      block.add(std::move(*shadow));
    }
    if(auto present = maybe_present_assign(self_lvalue, fn))
    {
      present->add_source_location() = loc;
      block.add(std::move(*present));
    }
  }
  if(block.statements().empty())
    return std::nullopt;
  return block;
}

std::vector<codet> python_convertert::build_class_construction(
  const std::string &class_name,
  const exprt &self_lvalue,
  const jsont &call_node,
  const source_locationt &loc)
{
  std::vector<codet> out;
  // Per-instance class-identity provenance (plan §1): EVERY instance
  // construction stamps __class_tag first, so identity-refined dunder
  // dispatch and tag obligations (isinstance, pv-CLASS __getitem__/__iter__,
  // subscript/iterability) can read it back through the boxed __class_ptr.
  // Previously only some wrap sites set it, so instances constructed inside
  // methods carried a NONDET tag -- identity checks were unprovable.
  {
    auto ti = class_tag_ids.find(class_name);
    // Guard on a struct-typed receiver: a scalar-to-class REBIND reaches
    // here with the lvalue still carrying the old scalar type (the
    // rebind-scalar-to-class shape) -- member_exprt on a non-struct is a
    // frontend invariant violation.
    const typet &recv_t = self_lvalue.type();
    const bool recv_is_struct =
      recv_t.id() == ID_struct || recv_t.id() == ID_struct_tag;
    if(ti != class_tag_ids.end() && recv_is_struct)
    {
      code_frontend_assignt stamp{
        member_exprt{self_lvalue, "__class_tag", signedbv_typet{32}},
        from_integer(ti->second, signedbv_typet{32})};
      stamp.add_source_location() = loc;
      out.push_back(std::move(stamp));
    }
  }
  if(
    auto init_call =
      build_class_init_call(class_name, self_lvalue, call_node, loc))
  {
    code_expressiont c{*init_call};
    c.add_source_location() = loc;
    out.push_back(std::move(c));
    return out;
  }
  // No explicit/inherited __init__: a @dataclass binds its fields here.
  if(
    auto dc =
      build_dataclass_init_block(class_name, self_lvalue, call_node, loc))
    for(auto &s : dc->statements())
      out.push_back(std::move(s));
  return out;
}

std::optional<side_effect_expr_function_callt>
python_convertert::build_class_init_call(
  const std::string &class_name,
  const exprt &self_lvalue,
  const jsont &call_node,
  const source_locationt &loc)
{
  auto [init_id, init_sym] = lookup_init_via_mro(class_name);
  if(init_sym == nullptr)
    return std::nullopt;

  // PLR §8.7: constructor arity / unexpected-kwarg validation. C(...)
  // calls __init__(self, ...) with the new instance as the implicit
  // self. Single chokepoint for every constructor caller (assignment,
  // expression, with-statement, ...).
  validate_call_signature(
    init_id, call_node, json_member(call_node, "args"), 1);

  const code_typet &init_type = to_code_type(init_sym->type);
  const auto &params = init_type.parameters();

  exprt::operandst init_args;
  init_args.push_back(address_of_exprt{self_lvalue});

  // Positional arguments from the Call AST.
  const jsont &call_args = json_member(call_node, "args");
  if(call_args.is_array())
  {
    for(const auto &a : as_array(call_args))
      init_args.push_back(convert_expression(a));
  }

  // PLR §8.7: pack trailing positionals into the *args list parameter. The
  // loop above pushed self + each positional separately; if __init__ has a
  // vararg param (`def __init__(self, *parts)`), collapse the positionals
  // at/after its slot into one list — mirroring the user-call path —
  // otherwise `*parts` is left unbound (e.g. `len(parts)` is wrong).
  {
    auto va_it = function_vararg_index.find(init_sym->name);
    if(va_it != function_vararg_index.end() && va_it->second < params.size())
    {
      const std::size_t va_idx = va_it->second;
      const typet &va_param_type = params[va_idx].type();
      if(is_python_list_type(va_param_type) && va_idx <= init_args.size())
      {
        const auto &list_st = to_struct_type(va_param_type);
        const auto &data_type = to_array_type(list_st.components()[1].type());
        exprt::operandst elems;
        for(std::size_t i = va_idx; i < init_args.size(); i++)
        {
          exprt a = init_args[i];
          if(a.type() != data_type.element_type())
            a = coerce_element(a, data_type.element_type());
          elems.push_back(std::move(a));
        }
        const std::size_t n_packed = elems.size();
        exprt packed = struct_exprt{
          {from_integer(static_cast<long long>(n_packed), signedbv_typet{64}),
           build_list_data(std::move(elems), data_type)},
          va_param_type};
        init_args.resize(va_idx);
        init_args.push_back(std::move(packed));
      }
    }
  }

  // Keyword arguments — match by parameter base-name; fill any
  // index gaps with `nil_exprt` so the default-padding pass below
  // can replace them with `safe_zero` of the param type.
  const jsont &keywords = json_member(call_node, "keywords");
  if(keywords.is_array())
  {
    for(const auto &kw : as_array(keywords))
    {
      std::string kw_name = json_string(json_member(kw, "arg"));
      exprt kw_val = convert_expression(json_member(kw, "value"));
      for(std::size_t pi = 0; pi < params.size(); pi++)
      {
        if(id2string(params[pi].get_base_name()) == kw_name)
        {
          while(init_args.size() <= pi)
            init_args.push_back(nil_exprt{});
          init_args[pi] = std::move(kw_val);
          break;
        }
      }
    }
  }

  // Pad any unprovided positional with safe_zero(param_type).
  while(init_args.size() < params.size())
    init_args.push_back(safe_zero(params[init_args.size()].type()));

  // Replace nil entries (left as gaps by keyword matching) with
  // the same safe_zero default.
  for(std::size_t i = 0; i < init_args.size() && i < params.size(); i++)
  {
    if(init_args[i].is_nil())
      init_args[i] = safe_zero(params[i].type());
  }

  // Apply PLR §3.2 call-boundary adaptations (None markers, etc.).
  coerce_call_arguments(init_args, params);

  return side_effect_expr_function_callt{
    init_sym->symbol_expr(), std::move(init_args), empty_typet{}, loc};
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
  // Native SMT String sort (--python-smt-strings): the zero value is
  // the empty string. Falling through to the nondet fallback would
  // put a nondet inside array_of bases (e.g. the items() tuple-list
  // default), which cvc5 rejects: ((as const ...) val) requires a
  // constant VALUE argument.
  if(type.id() == ID_string)
    return constant_exprt{irep_idt{""}, type};
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
        // Zero-fill the array. An INFINITE data array
        // (--python-smt-containers) has no literal form: array_of.
        const auto &arr_type = to_array_type(comp.type());
        mp_integer size;
        if(
          arr_type.size().is_constant() &&
          !to_integer(to_constant_expr(arr_type.size()), size))
        {
          exprt::operandst elems;
          for(mp_integer i = 0; i < size; ++i)
            elems.push_back(safe_zero(arr_type.element_type()));
          fields.push_back(array_exprt{std::move(elems), arr_type});
        }
        else
        {
          fields.push_back(
            array_of_exprt{safe_zero(arr_type.element_type()), arr_type});
        }
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
    if(!arr_type.size().is_constant())
      return array_of_exprt{safe_zero(arr_type.element_type()), arr_type};
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
    // The base may be a `Name` (`Dict`, `list`, ...) or an `Attribute`
    // such as `typing.Dict` / `typing.List`. For Attribute nodes we
    // use the attr (e.g., "Dict"); for Name nodes we use the id.
    const jsont &val_node = json_member(annotation, "value");
    std::string base;
    if(is_node_type(val_node, "Attribute"))
      base = json_string(json_member(val_node, "attr"));
    else
      base = json_string(json_member(val_node, "id"));
    if(base == "list" || base == "List")
    {
      // Extract element type from the slice
      typet elem_type =
        convert_type_annotation(json_member(annotation, "slice"));
      // SPIKE (--python-ref-mutables): a nested LIST element (a list whose
      // items are themselves lists) is stored as a python_value REFERENCE by
      // the literal wrapping in convert_list. Lower the annotation to match
      // (list[python_value]) so parameter / return / annotated-local
      // boundaries don't mismatch list[python_value] against a concrete nested
      // list type (which aborts in value_set::assign). Restricted to list
      // elements only: convert_list wraps ONLY list elements (dict/set
      // elements stay by-value), so lowering list[dict]/list[set] would create
      // the opposite mismatch.
      if(ref_mutables && is_python_list_type(elem_type))
        return python_list_type(python_value_type());
      return python_list_type(elem_type);
    }
    else if(base == "Optional")
    {
      // PLR §3.2: Optional[T] is shorthand for Union[T, None].
      // For container types (list, dict, set, tuple) we lower to
      // python_value (tagged union) at the typing layer rather
      // than to T's natural type, because:
      //   - Typed-list/dict/set None markers (length=0) conflate
      //     with empty literals at compare sites — the
      //     architecturally clean encoding is the tagged-union
      //     where the NONE tag is distinguishable from any
      //     non-NONE tag.
      //   - Consumer code (subscript, len, iter, comparison,
      //     truthiness) already handles python_value via tag
      //     dispatch, so the behavioural change is mostly
      //     unwrapping at the right places.
      //
      // For natural-type T (int, float, str), keep the inner
      // type and rely on the per-target None marker (sentinel /
      // {0,NULL}) — those don't have the empty-container
      // conflation issue and the marker fast-paths in
      // python_truthiness / compare are already in place.
      typet inner = convert_type_annotation(json_member(annotation, "slice"));
      if(
        is_python_list_type(inner) || is_python_dict_type(inner) ||
        is_python_set_type(inner) || is_python_tuple_type(inner))
      {
        return python_value_type();
      }
      // PLR §3.2: for a natural-type T (int/float/bool/str),
      // Optional[T] also lowers to the tagged union so the NONE
      // tag is distinguishable from a real T value. This lets the
      // operation-site tag obligation (numeric binop on a
      // NONE-tagged value -> TypeError) fire for `None + 1`, which
      // the int-sentinel encoding silently computed. The tag check
      // is conditional on the runtime tag, so a real T value still
      // unwraps and operates normally. (str inner already had a
      // {0,NULL} None marker, but routing it through the union
      // unifies the None handling across all scalar T.)
      if(
        inner.id() == ID_signedbv || inner.id() == ID_floatbv ||
        inner.id() == ID_bool || is_python_string_type(inner))
        return python_value_type();
      // PLR §3.2: Optional[ClassName] is Union[ClassName, None]. Lower
      // a class-typed inner to the tagged union too, so the NONE tag is
      // distinguishable from a real instance and — crucially for
      // self-referential fields (`tail: Optional["List"]`) — the field
      // is a fixed-size union (instance reached via __class_ptr) rather
      // than the class struct, which for a recursive class would be
      // infinitely sized and is otherwise truncated to a bare
      // {__class_tag} placeholder that loses all instance state.
      if(
        (inner.id() == ID_struct &&
         id2string(to_struct_type(inner).get_tag()).rfind("python_class_", 0) ==
           0) ||
        (inner.id() == ID_struct_tag &&
         id2string(to_struct_tag_type(inner).get_identifier())
             .rfind("python_class_", 0) == 0))
        return python_value_type();
      return inner;
    }
    else if(base == "Union")
    {
      // PLR §3.2: Union[T1, T2, ...] is the tagged-union of its
      // members. If any member is None and any other is a
      // container type (list/dict/set/tuple), lower to
      // python_value so the NONE tag is distinguishable. This
      // mirrors the Optional case above. For Union of
      // non-container types only, the BinOp path (or a generic
      // python_value fallback at convert_type_annotation's end)
      // already returns python_value.
      return python_value_type();
    }
    else if(
      base == "Set" || base == "FrozenSet" || base == "set" ||
      base == "frozenset")
    {
      // PLR §3.2: bitmap-backed `python_set_type()` only models
      // sets of small non-negative integers. For string / class /
      // tuple / float element types, use `python_list_type(elem)`
      // instead — the `set()` builtin and Set-display literal
      // converters already produce list-backed values for those
      // element types, and equality / membership operations on
      // list-typed sets are handled via the multiset-eq path
      // (PLR §3.2 set order-insensitive equality) and the list
      // iteration path (`x in lst → ∃i. lst[i] == x`).
      const jsont &slice = json_member(annotation, "slice");
      if(is_node_type(slice, "Name"))
      {
        std::string elem = json_string(json_member(slice, "id"));
        // Numeric element types: bitmap is the precise model.
        if(
          elem == "int" || elem == "bool" || elem == "float" ||
          elem == "complex")
          return python_set_type();
        // String element type: list-backed.
        if(elem == "str")
          return python_list_type(python_string_type());
        // Class / unrecognised: list-backed with python_value.
        if(class_types.count(elem))
          return python_list_type(python_value_type());
        return python_list_type(python_value_type());
      }
      // Bare or non-Name slice: keep python_set_type (the
      // existing default that prevents fall-through to int).
      return python_set_type();
    }
    // dict[K, V] / Dict[K, V] — extract key and value types.
    // Restricted to primitive value types (int/float/bool/str);
    // complex value types (set, nested dict, class) in the
    // value position trigger solver-side symbol-table gaps
    // for refined strings, so fall back to int.
    if(base == "dict" || base == "Dict")
    {
      const jsont &slice = json_member(annotation, "slice");
      typet key_t = python_string_type();
      typet val_t = python_int_type();
      bool safe = false;
      if(is_node_type(slice, "Tuple"))
      {
        const jsont &elts = json_member(slice, "elts");
        if(elts.is_array() && as_array(elts).size() >= 2)
        {
          auto it = as_array(elts).begin();
          key_t = convert_type_annotation(*it);
          ++it;
          val_t = convert_type_annotation(*it);
          auto is_safe = [](const typet &t)
          {
            return t.id() == ID_signedbv || t.id() == ID_floatbv ||
                   t.id() == ID_bool || is_python_string_type(t) ||
                   is_python_list_type(t) || is_python_tuple_type(t) ||
                   is_python_dict_type(t) || is_python_set_type(t);
          };
          safe = is_safe(key_t) && is_safe(val_t);
        }
      }
      if(safe)
        return python_dict_type(key_t, val_t);
      // Non-"safe" element types (e.g. `Dict[str, object]` / `dict[str, Any]`):
      // the old python_int_type() fallback modelled the DICT as a concrete int
      // -- a latent unsoundness AND the dominant real-world FP cluster (a
      // `-> Dict[str, object]` callee's result was INT-tagged, so `r["k"]`
      // raised a spurious "not subscriptable" TypeError; 14 of the 51 boto3
      // benchmarks). When the KEY type is safe, keep the DICT shape with
      // python_value (Any) VALUES so the tag and subscript stay precise;
      // otherwise lower to python_value (Any/top), the sound fallback.
      {
        auto is_safe_key = [](const typet &t)
        {
          return t.id() == ID_signedbv || t.id() == ID_floatbv ||
                 t.id() == ID_bool || is_python_string_type(t);
        };
        if(is_safe_key(key_t))
          return python_dict_type(key_t, python_value_type());
      }
      return python_value_type();
    }
    if(base == "tuple" || base == "Tuple")
    {
      // tuple[int, int] / Tuple[int, int] → struct with _0, _1, ... components
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

  // Handle Constant None annotation (-> None / x: None).
  // Python treats `None` as the type whose only value is None.
  // Encode it as python_int_type since our None sentinel
  // (-2^62) is a python_int value; using empty_typet here
  // breaks the call site (parameter is void → arg is
  // nondet) and the comparison `x is None` falls back to
  // an irrelevant equality.
  if(is_node_type(annotation, "Constant"))
  {
    const jsont &val = json_member(annotation, "value");
    if(val.is_null())
      return python_int_type();
  }

  // PLR §4.7.2: Forward references — string annotations like -> "Foo"
  if(is_node_type(annotation, "Constant"))
  {
    const jsont &val = json_member(annotation, "value");
    if(val.is_string())
    {
      std::string ref_name = val.value;
      // PLR §3.2: a string (forward-ref) annotation may itself be
      // a union, e.g. `"int | str"` or `"Optional[int]"`. Parse
      // those as the tagged union so the runtime tag is tracked,
      // matching the BinOp / Union[...] / Optional[...] handling
      // above. Without this the string form fell through to the
      // unknown-forward-ref int fallback, erasing the tag.
      if(
        ref_name.find('|') != std::string::npos ||
        ref_name.rfind("Union[", 0) == 0 ||
        ref_name.rfind("Optional[", 0) == 0 ||
        ref_name.rfind("typing.Union[", 0) == 0 ||
        ref_name.rfind("typing.Optional[", 0) == 0)
        return python_value_type();
      // PLR §8.13: an enum-class annotation types the binding as the
      // enum's member value type (members resolve to their values), so an
      // argument like `EnumClass.MEMBER` matches the parameter.
      if(enum_members.count(ref_name))
      {
        auto vit = enum_value_type.find(ref_name);
        return vit != enum_value_type.end() ? vit->second : python_int_type();
      }
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
      // Unknown forward reference: the frontend does not know this type, so
      // the sound over-approximation is python_value (Any / top), NOT int.
      // Using int here both modelled an unknown value with concrete int
      // semantics (latent unsoundness) and made --python-check-annotations
      // read it as a precise `int` declaration (spurious mismatch).
      return python_value_type(); // unknown forward ref -> Any
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

  // Handle Attribute annotations (e.g., typing.List, boto3.Kinesis.Kinesis).
  // We use the rightmost `attr` as the simple type name and dispatch as
  // if the user had written that name directly.
  std::string type_name;
  if(is_node_type(annotation, "Attribute"))
    type_name = json_string(json_member(annotation, "attr"));
  else
    type_name = json_string(json_member(annotation, "id"));

  if(type_name == "int")
    return python_int_type();
  else if(type_name == "float")
    return double_type();
  else if(type_name == "bool")
    return bool_typet{};
  else if(type_name == "complex")
  {
    // PLR §6.10.1: complex is the python_complex struct with
    // {real, imag : double} fields. Tag it so callers like the
    // math/cmath dispatch can detect a complex argument and
    // emit TypeError where appropriate.
    struct_typet::componentst cc;
    cc.push_back(struct_typet::componentt{"real", double_type()});
    cc.push_back(struct_typet::componentt{"imag", double_type()});
    struct_typet ct{cc};
    ct.set_tag("python_complex");
    return ct;
  }
  else if(type_name == "str")
    return python_string_type();
  else if(type_name == "bytes" || type_name == "bytearray")
    // PLR §4.6: bytes is a sequence of integers in [0, 256). We
    // model it as a list of unsigned 8-bit integers, sharing the
    // same struct shape as Python lists so existing list-method
    // and indexing handlers work transparently.
    return python_list_type(unsignedbv_typet{8});
  else if(type_name == "None" || type_name == "NoneType")
    return empty_typet{};
  else if(type_name == "list" || type_name == "List")
    // PLR §3.2: bare 'list' means 'list[Any]' — heterogeneous
    // element type. python_value is the closest match.
    return python_list_type(python_value_type());
  else if(type_name == "dict" || type_name == "Dict")
    return python_dict_type(python_value_type(), python_value_type());
  else if(type_name == "set" || type_name == "Set")
    return python_set_type();
  else if(type_name == "tuple" || type_name == "Tuple")
    // PLR §3.2: bare `tuple` is `tuple[Any, ...]` (any length / element type);
    // there is no fixed-size struct for that, so python_value (Any) is the
    // correct top model. (The old int fallback flagged `t: tuple = (1, 2)`.)
    return python_value_type(); // unparameterized tuple -> Any
  else if(
    type_name == "Any" || type_name == "Union" || type_name == "Literal" ||
    type_name == "Callable" || type_name == "BinaryIO" ||
    type_name == "TextIO" || type_name == "Iterator" ||
    type_name == "Iterable" || type_name == "Sequence" ||
    type_name == "Mapping" || type_name == "Type" || type_name == "ClassVar" ||
    type_name == "Final" || type_name == "object")
    return python_value_type();
  // PLR §8.13: an enum-class annotation types the binding as the enum's
  // member value type — members resolve to their values, so an argument
  // like `EnumClass.MEMBER` matches the parameter (rather than the class
  // struct, which a member value can't be coerced to).
  else if(enum_members.count(type_name))
  {
    auto vit = enum_value_type.find(type_name);
    return vit != enum_value_type.end() ? vit->second : python_int_type();
  }
  else if(class_types.count(type_name))
  {
    // A "pure" subclass of int/bool (no own instance attributes) IS that
    // built-in, so resolve the annotation to int — keeps `U(v)` (also
    // modelled as int) and `x: U` consistent.
    auto bit = class_bases.find(type_name);
    auto oit = class_owned_attrs.find(type_name);
    if(
      bit != class_bases.end() &&
      (oit == class_owned_attrs.end() || oit->second.empty()))
    {
      for(const auto &b : bit->second)
        if(b == "int" || b == "bool")
          return python_int_type();
    }
    return class_types[type_name];
  }
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
                    << ", defaulting to Any (python_value)" << messaget::eom;
    // Unknown/unmodeled annotation (e.g. `range`, an unmodeled builtin, or an
    // unresolved name): the frontend does not know the type, so the sound
    // over-approximation is python_value (Any / top) rather than int. The old
    // int fallback (a) modelled an unknown value with concrete int semantics --
    // a latent unsoundness -- and (b) made --python-check-annotations read it as
    // a precise `int` declaration and flag the real value as a mismatch (e.g.
    // `r: range = range(4)`). python_value is compatible with every value type
    // in the checker and is the correct top element.
    return python_value_type();
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
  {
    result = convert_call(expr);
    // PLR §7.12 + object identity: a call of ANY form (free function,
    // method, transitively) may mutate module globals -- a global
    // scalar (`global cfg; cfg=...`) or a global dict in place
    // (`d[k]=v`). After the call's arguments have been converted (incl.
    // `f(**d)` which reads dict_literals), invalidate the global-keyed
    // scalar + dict tracking so a later read does not fold against the
    // stale pre-call value. This is the single chokepoint every Call
    // node passes through, so it uniformly covers methods (`c.m()`) and
    // transitive call chains that the per-callee sites missed. symex
    // recovers the real post-call value/contents from the symbol;
    // list_literals are left intact (list reads are runtime, `f(*c)`
    // reads them structurally).
    invalidate_global_value_tracking(/*include_dict_literals=*/true);
  }
  else if(node_type == "IfExp")
    result = convert_if_exp(expr);
  else if(node_type == "Subscript")
  {
    result = convert_subscript(expr);
    // Native string-id handles: an ELEMENT READ of a handle slot denotes
    // strtab(h). Unwrapped HERE (the rvalue dispatch) and NOT inside
    // convert_subscript: assignment paths call convert_subscript directly
    // for their store TARGET, which must stay a raw lvalue slot.
    if(
      python_smt_string_native_flag() &&
      is_python_string_handle_type(result.type()))
      result = string_handle_to_string(result);
  }
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
      bool unhashable_elt = false;
      std::vector<mp_integer> values;
      for(const auto &elt : as_array(elts))
      {
        exprt val = convert_expression(elt);
        // PLR §3.2: list/dict/set are unhashable, so using one as a
        // set element raises TypeError (tuple is fine).
        if(is_unhashable_type(val.type()))
          unhashable_elt = true;
        // PLR §3: an int / bool / INTEGRAL-float constant participates in
        // numeric element equality (1 == 1.0 == True, all hash equal), so it
        // maps to the SAME bitmap bit and dedups. A non-integral float / string
        // / non-constant is not an int-bitmap element (falls to the list model).
        std::optional<mp_integer> nk = python_numeric_key(val);
        if(nk.has_value())
          values.push_back(*nk);
        else
          all_int_constant = false;
      }
      if(unhashable_elt)
        emit_conditional_exception(true_exprt{}, "TypeError");
      if(all_int_constant && !values.empty())
      {
        mp_integer bitmap{0};
        for(const auto &v : values)
        {
          if(v >= 0 && v < 64)
          {
            // Use bitwise OR so duplicate values don't double-add.
            // {1, 2, 1} should produce bitmap (1<<1 | 1<<2) = 6,
            // not (2 + 4 + 2) = 8. mp_integer has no | operator
            // but at this stage the values are small (<64 bits),
            // so cast to long long for the OR and back.
            mp_integer bit_v = power(2, v);
            unsigned long long b_l = bit_v.to_ulong();
            unsigned long long bm_l = bitmap.to_ulong();
            bitmap = mp_integer{(long long)(bm_l | b_l)};
          }
          else
          {
            // Element outside the modelled bitmap range [0, 64): it would be
            // silently dropped (an unsound omission, e.g. `100 not in {0,100}`
            // would hold). Report python-model-bound + cut.
            emit_set_range_guard(
              pending_checks,
              from_integer(v, signedbv_typet{64}),
              get_location(expr));
          }
        }
        result = struct_exprt{
          {from_integer(bitmap, unsignedbv_typet{64}),
           from_integer(0, signedbv_typet{64})},
          python_set_type()};
      }
      else
      {
        // Non-integer set: fall back to list model. Count
        // distinct string-constant elements; if duplicates are
        // present, build the resulting struct directly with
        // the unique elements rather than going through
        // convert_list (which keeps duplicates).
        // Non-integer set: list-backed model. Dedup CONSTANT elements by the
        // unified canonical_key (str / None / tuple / non-integral float /
        // cross-type numeric); a symbolic element is kept (sound: not provably
        // equal to anything). Only when a provable duplicate is removed do we
        // build the deduped list directly (otherwise the existing convert_list
        // path is used unchanged) -- so the blast radius is duplicate-bearing
        // non-int set literals only.
        std::vector<exprt> uniq;
        std::vector<std::optional<std::string>> ukeys;
        bool all_string = true;
        std::size_t total = 0;
        if(elts.is_array())
        {
          for(const auto &elt : as_array(elts))
          {
            ++total;
            exprt ce = convert_expression(elt);
            if(!is_python_string_type(ce.type()))
              all_string = false;
            std::optional<std::string> ck = canonical_key(ce);
            bool merged = false;
            if(ck.has_value())
              for(std::size_t i = 0; i < uniq.size(); ++i)
                if(ukeys[i].has_value() && *ukeys[i] == *ck)
                {
                  merged = true;
                  break;
                }
            if(!merged)
            {
              uniq.push_back(ce);
              ukeys.push_back(ck);
            }
          }
        }
        if(uniq.size() != total)
        {
          // A duplicate was removed. Build the deduped set-semantic list: keep
          // a str element type when every element is a string (unchanged), else
          // use the universal python_value element type (wrap each element).
          const bool as_str = all_string;
          typet elem_t = as_str ? python_string_type() : python_value_type();
          typet list_t = python_list_type(elem_t);
          const auto &list_st = to_struct_type(list_t);
          const auto &data_t = to_array_type(list_st.components()[1].type());
          exprt::operandst ops;
          for(auto &e : uniq)
            ops.push_back(as_str ? e : wrap_value(e));
          struct_exprt se{
            {from_integer((long)uniq.size(), signedbv_typet{64}),
             build_list_data(std::move(ops), data_t)},
            list_t};
          // PLR §3.2: tag as set-semantic (order-insensitive multiset).
          se.set("#python_set_semantic", "1");
          result = std::move(se);
        }
        else
        {
          result = convert_list(expr);
          // Tag the synthesised list as set-semantic. PLR §3.2:
          // the literal `{a, b, c}` is a set, so equality
          // comparison must be order-insensitive even when the
          // backing storage is a list.
          if(result.id() == ID_struct)
            result.set("#python_set_semantic", "1");
        }
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
      // PLR §6.12: the walrus rebinds `name` -- invalidate its stale tracking
      // like any reassignment, then re-establish the scalar constant when the
      // value folds to one (keeps `(b := 2); t[b]` precise).
      invalidate_reassigned_symbol(sym_id);
      {
        auto ev = try_eval_double(rhs);
        if(ev.has_value())
          float_constants[sym_id] = ev.value();
      }
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
      // PLR §6.12: `(m := l)` binds m to the SAME OBJECT as the mutable
      // Name l; a value copy was a false proof (a later `m.append(x)` did
      // not reach l -- ESBMC github_5955_alias_walrus_fail). Record the
      // extraction alias so an in-place mutation of m havocs l (sound;
      // mirrors the BoolOp shape in note_mutable_extraction).
      if(
        is_node_type(value, "Name") && rhs.id() == ID_symbol &&
        (is_python_list_type(rhs.type()) || is_python_dict_type(rhs.type()) ||
         is_python_set_type(rhs.type())))
        extracted_container_alias[sym_id] = rhs;
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
      return python_string_literal("");

    // Build result by concatenating all parts
    typet str_type = python_string_type();
    const auto &data_type = array_typet(
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64}));

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
      // Parallel to `parts`: each part's statically-known code-point length
      // (nullopt if variable). When ALL parts are known under the native
      // backend, the whole f-string is emitted as one nondet string of the
      // summed length, avoiding a slow CVC5 concat of several length-
      // constrained nondet strings (see the all-known shortcut below).
      std::vector<std::optional<mp_integer>> part_len;
      auto cp_count = [](const std::string &s) -> mp_integer
      {
        mp_integer n = 0;
        for(char c : s)
        {
          const unsigned char uc = static_cast<unsigned char>(c);
          if(uc < 0x80 || uc >= 0xC0)
            ++n;
        }
        return n;
      };
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
          part_len.push_back(cp_count(cval.value));
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
        // PLR §2.4.3: validate the format presentation code against the
        // value's concrete type (a numeric code on a str -> ValueError, etc.).
        // The type char is the trailing letter of the spec; gated on a constant
        // spec + a concrete value type (symbolic/Any never flagged).
        if(spec_constant && !spec_str.empty())
        {
          const char last = spec_str.back();
          const char code = std::isalpha(static_cast<unsigned char>(last))
                              ? static_cast<char>(std::tolower(
                                  static_cast<unsigned char>(last)))
                              : char{0};
          if(code != 0)
          {
            exprt fv = convert_expression(json_member(v, "value"));
            if(!fv.is_nil())
            {
              const char *exc = format_code_violation(
                code, value_format_category(fv.type()), false);
              if(exc != nullptr)
                emit_conditional_exception(true_exprt{}, exc);
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
              ID_cprover_string_length_func, tv, symbol_table, pending_checks);
            pending_checks.push_back(code_assumet{
              equal_exprt{len_intr, from_integer(width, signedbv_typet{64})}});
            parts.push_back(std::move(tv));
            part_len.push_back(mp_integer{width});
            handled_by_pad_spec = true;
          }
        }
        if(handled_by_pad_spec)
          continue;
        // Constant-value formatting with explicit spec:
        // f"{42:d}" → "42", f"{2.5:.1f}" → "2.5".
        if(spec_constant && !no_spec)
        {
          exprt inner_v = convert_expression(json_member(v, "value"));
          auto cv = try_eval_double(inner_v);
          if(cv.has_value())
          {
            double d = cv.value();
            std::string formatted;
            bool ok = true;
            if(spec_str == "d" || spec_str == "n")
            {
              if(d == std::floor(d) && std::abs(d) < 1e15)
                formatted = std::to_string(static_cast<long long>(d));
              else
                ok = false;
            }
            else if(!spec_str.empty() && spec_str[0] == '.')
            {
              // .Nf or .N
              std::size_t i = 1;
              while(i < spec_str.size() &&
                    std::isdigit((unsigned char)spec_str[i]))
                ++i;
              if(i > 1)
              {
                int prec = std::stoi(spec_str.substr(1, i - 1));
                std::ostringstream oss;
                if(
                  i < spec_str.size() &&
                  (spec_str[i] == 'f' || spec_str[i] == 'F'))
                  oss << std::fixed << std::setprecision(prec) << d;
                else
                  ok = false;
                if(ok)
                  formatted = oss.str();
              }
              else
                ok = false;
            }
            else
              ok = false;
            if(ok)
            {
              parts.push_back(python_string_literal(formatted));
              part_len.push_back(cp_count(formatted));
              continue;
            }
          }
        }
        if(!(no_spec && no_conv))
        {
          parts_ok = false;
          break;
        }
        exprt inner = convert_expression(json_member(v, "value"));
        // Bool: emit literal "True" / "False" for constants;
        // for symbolic bools, emit a length-1-or-larger nondet
        // string (the test only checks len > 0).
        if(inner.type().id() == ID_bool || inner.type().id() == ID_c_bool)
        {
          if(inner.is_true())
            parts.push_back(python_string_literal("True"));
          else if(inner.is_false())
            parts.push_back(python_string_literal("False"));
          else
          {
            // Symbolic bool: choose at runtime via if-then-else.
            parts.push_back(if_exprt{
              inner,
              python_string_literal("True"),
              python_string_literal("False")});
          }
          // "True"=4, "False"=5; a symbolic bool is one or the other so its
          // length is not statically fixed.
          part_len.push_back(
            inner.is_true()    ? std::optional<mp_integer>{mp_integer{4}}
            : inner.is_false() ? std::optional<mp_integer>{mp_integer{5}}
                               : std::nullopt);
          continue;
        }
        if(inner.type().id() == ID_signedbv || inner.type().id() == ID_integer)
        {
          exprt i64 = inner.type() == signedbv_typet{64}
                        ? inner
                        : safe_typecast(inner, signedbv_typet{64});
          if(use_smt_string_native)
          {
            // Native: str(n) composes sign handling over the GENERIC from_int
            // id (see native_string_of_int).
            parts.push_back(native_string_of_int(i64));
          }
          else
          {
            parts.push_back(emit_string_function(
              ID_cprover_string_of_int_func,
              {i64},
              symbol_table,
              pending_checks,
              loop_depth > 0,
              current_function));
            ensure_fn(ID_cprover_associate_array_to_pointer_func);
            ensure_fn(ID_cprover_associate_length_to_array_func);
          }
        }
        else if(inner.type().id() == ID_floatbv)
        {
          // CONSTANT float: fold to CPython repr (py_float_repr) -- precise
          // AND correct (a wrong fold would be a false proof; the shared
          // helper matches CPython bit-for-bit). Backend-independent.
          if(auto fev = try_eval_double(inner);
             fev.has_value() && inner.is_constant())
          {
            parts.push_back(python_string_literal(py_float_repr(*fev)));
            part_len.push_back(std::nullopt);
            continue;
          }
          if(use_smt_string_native)
          {
            // No SMT primitive for str(float); sound nondet SMT String.
            parts.push_back(bounded_nondet_string(get_location(v)));
          }
          else
          {
            parts.push_back(emit_string_function(
              ID_cprover_string_of_double_func,
              {inner},
              symbol_table,
              pending_checks,
              loop_depth > 0,
              current_function));
            ensure_fn(ID_cprover_associate_array_to_pointer_func);
            ensure_fn(ID_cprover_associate_length_to_array_func);
          }
        }
        else if(is_python_string_type(inner.type()))
          parts.push_back(inner);
        else
        {
          parts_ok = false;
          break;
        }
        // int / float / str interpolations have a runtime-variable length.
        part_len.push_back(std::nullopt);
      }
      if(parts_ok && !parts.empty())
      {
        // Native fast path: if every part has a statically-known length, the
        // whole f-string has a known total length, so emit ONE nondet SMT
        // String of that length rather than concatenating several length-
        // constrained nondet strings (which CVC5's string solver handles
        // poorly — e.g. f"{h:02}:{m:02}:{s:02}" timed out as a 5-way concat).
        // Sound over-approximation: content is nondet, length is exact.
        if(
          use_smt_string_native && parts.size() == part_len.size() &&
          std::all_of(
            part_len.begin(),
            part_len.end(),
            [](const auto &o) { return o.has_value(); }))
        {
          mp_integer total = 0;
          for(const auto &o : part_len)
            total += *o;
          static unsigned fctr = 0;
          const std::string nm = "__fstr_len_" + std::to_string(fctr++);
          const irep_idt id{qualify_name(nm)};
          if(symbol_table.lookup(id) == nullptr)
          {
            symbolt s{id, string_typet{}, "python"};
            s.base_name = nm;
            s.is_lvalue = true;
            s.is_state_var = true;
            symbol_table.add(s);
          }
          const symbol_exprt sym = symbol_table.lookup_ref(id).symbol_expr();
          pending_checks.push_back(code_frontend_assignt{
            sym, side_effect_expr_nondett{string_typet{}, get_location(expr)}});
          pending_checks.push_back(code_assumet{equal_exprt{
            native_or_member_string_length(sym),
            from_integer(total, signedbv_typet{64})}});
          return sym;
        }
        // Chain-concatenate via the representation-neutral primitive.
        exprt acc = parts[0];
        for(std::size_t i = 1; i < parts.size(); i++)
          acc = string_concat(acc, parts[i]);
        // Native back-end: attach an exact length hint (sum of the part
        // lengths) so a len() relation over the f-string result is decidable
        // without int2bv-over-sum reasoning (which times out).
        if(use_smt_string_native && acc.type().id() == ID_string)
        {
          exprt total_len = native_or_member_string_length(parts[0]);
          for(std::size_t i = 1; i < parts.size(); i++)
            total_len =
              plus_exprt{total_len, native_or_member_string_length(parts[i])};
          return bind_string_length_hint(acc, total_len, get_location(expr));
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
    // PLR §3.4.4: `await obj` requires __await__. A concrete class whose MRO
    // defines none is not awaitable -> TypeError. (Coroutine objects from
    // `async def` are not python_class_ instances, so they are not flagged.)
    if(
      !result.is_nil() &&
      concrete_class_lacks_dunder(result.type(), "__await__"))
      emit_conditional_exception(true_exprt{}, "TypeError");
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
    // PLR §6.2.9: an expression-context yield (`x = yield X`, `f(yield X)`,
    // `[yield X]`) must still count toward the eager `__gen_result` list. The
    // bare-statement path (convert_expr_stmt) appends for `yield X;` statements
    // and returns early, so control only reaches here for expression-context
    // yields -- meaning every yield is counted exactly once. (yield-from is
    // appended element-wise at the statement level only.) The value of the
    // yield expression itself (the value sent in via .send()) is not tracked
    // by the eager model -- plan §1 Phase 2.
    if(node_type == "Yield")
    {
      code_blockt app =
        build_gen_result_append(v.is_null() ? python_none_value() : result);
      if(!app.statements().empty())
        pending_checks.push_back(std::move(app));
    }
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

exprt python_convertert::python_value_class_tag(const exprt &value) const
{
  const pointer_typet i32_ptr{signedbv_typet{32}, 64};
  return dereference_exprt{
    typecast_exprt{python_value_class_ptr(value), i32_ptr}, signedbv_typet{32}};
}

exprt python_convertert::python_value_is_class_of(
  const exprt &value,
  const std::vector<std::string> &owners) const
{
  exprt id_match = false_exprt{};
  for(const auto &o : owners)
  {
    auto ti = class_tag_ids.find(o);
    if(ti == class_tag_ids.end())
      continue;
    equal_exprt eq{
      python_value_class_tag(value),
      from_integer(ti->second, signedbv_typet{32})};
    id_match = id_match.id() == ID_false
                 ? exprt{std::move(eq)}
                 : exprt{or_exprt{std::move(id_match), std::move(eq)}};
  }
  return and_exprt{
    python_value_is(value, python_type_tagt::CLASS), std::move(id_match)};
}

void python_convertert::invalidate_mutated_dict_literals(const jsont &stmt)
{
  static const std::set<std::string> mutators = {
    "append",
    "extend",
    "insert",
    "remove",
    "pop",
    "sort",
    "reverse",
    "clear",
    "add",
    "discard",
    "update",
    "setdefault",
    "popitem"};
  std::function<void(const jsont &)> scan = [&](const jsont &n)
  {
    if(!n.is_object())
      return;
    if(is_node_type(n, "Call"))
    {
      const jsont &fn = json_member(n, "func");
      if(
        is_node_type(fn, "Attribute") &&
        mutators.count(json_string(json_member(fn, "attr"))) > 0)
      {
        const jsont &recv = json_member(fn, "value");
        if(is_node_type(recv, "Subscript"))
        {
          const jsont &base = json_member(recv, "value");
          if(is_node_type(base, "Name"))
          {
            const irep_idt did{
              qualify_name(json_string(json_member(base, "id")))};
            dict_literals.erase(did);
            dict_runtime_value_overrides.erase(did);
          }
        }
      }
    }
    const auto &obj = static_cast<const json_objectt &>(n);
    for(const auto &kv : obj)
    {
      const jsont &child = kv.second;
      if(child.is_array())
      {
        for(const auto &ch : as_array(child))
          scan(ch);
      }
      else if(child.is_object())
        scan(child);
    }
  };
  scan(stmt);
}

exprt python_convertert::pv_class_truthiness(const exprt &e) const
{
  // PLR §4.4: __bool__ over __len__; an instance with neither is truthy.
  // FALSY-classified classes are excluded from the truthy disjunct (the
  // `_AnyDict.__len__ -> 0` stub shape made every `if response:` guard
  // falsy in CPython while the old blanket-truthy model executed the
  // branch -- a false proof AND a path-explosion driver); UNKNOWN
  // classes contribute a nondet arm (sound in both directions); TRUTHY
  // and unclassified instances keep presence-is-truthy.
  std::vector<std::string> falsy_or_unknown;
  exprt unknown_nondet_arm = false_exprt{};
  for(const auto &entry : class_truthiness)
  {
    const std::string &cn = entry.first;
    const truthiness_kindt k = truthiness_of(cn);
    if(k == truthiness_kindt::TRUTHY)
      continue;
    falsy_or_unknown.push_back(cn);
    if(k == truthiness_kindt::UNKNOWN)
      unknown_nondet_arm = or_exprt{
        std::move(unknown_nondet_arm),
        and_exprt{
          python_value_is_class_of(e, {cn}),
          side_effect_expr_nondett{bool_typet{}, source_locationt{}}}};
  }
  if(falsy_or_unknown.empty())
    return python_value_is(e, python_type_tagt::CLASS);
  return or_exprt{
    and_exprt{
      python_value_is(e, python_type_tagt::CLASS),
      not_exprt{python_value_is_class_of(e, falsy_or_unknown)}},
    std::move(unknown_nondet_arm)};
}

symbol_exprt python_convertert::strtab_symbol()
{
  const irep_idt id{"python::__cbmc_strtab"};
  if(symbol_table.lookup(id) == nullptr)
  {
    mathematical_function_typet ft{{signedbv_typet{64}}, string_typet{}};
    symbolt s{id, ft, "python"};
    s.base_name = "__cbmc_strtab";
    s.is_lvalue = false;
    s.is_state_var = false;
    s.is_static_lifetime = true;
    symbol_table.add(s);
  }
  return symbol_table.lookup_ref(id).symbol_expr();
}

exprt python_convertert::string_handle_to_string(const exprt &handle)
{
  strtab_symbol(); // ensure the symbol-table entry exists
  // Folds interned-constant handles to the string constant (see the
  // denotation helper); UF application otherwise.
  return python_string_handle_denotation(handle);
}

exprt python_convertert::string_to_handle(const exprt &str)
{
  // Representation guard: the strtab axiom's rhs must be a PLAIN
  // smt_string-sorted term. Two leak shapes are excluded:
  // - bytes values / legacy refined-string structs (a bytes-returning
  //   stub method stored into a str slot -- io.read via nondet_bytes());
  // - the refined->String BRIDGE struct `{length, address_of(data[0])}`
  //   typed smt_string: legal only as an INTRINSIC argument (the smt2
  //   lowering bridges it); inside a goto assignment the value-set
  //   walker visits the ID_struct constructor (it contains an address)
  //   and aborts on its non-struct type (s3_to_dynamodb).
  // Allocate a handle with an UNCONSTRAINED image instead: strtab(h) is
  // then an arbitrary string -- a sound over-approximation.
  if(str.type().id() != ID_string || str.id() == ID_struct)
  {
    static unsigned strh_u_ctr = 0;
    const std::string hn = "__strh_u_" + std::to_string(strh_u_ctr++);
    const irep_idt hid{qualify_name(hn)};
    if(symbol_table.lookup(hid) == nullptr)
    {
      symbolt hs{hid, python_string_handle_type(), "python"};
      hs.base_name = hn;
      hs.is_lvalue = true;
      hs.is_state_var = true;
      hs.is_static_lifetime = current_function.empty();
      symbol_table.add(hs);
    }
    symbol_exprt h = symbol_table.lookup_ref(hid).symbol_expr();
    pending_checks.push_back(code_frontend_assignt{
      h, side_effect_expr_nondett{h.type(), source_locationt{}}});
    return std::move(h);
  }
  // CONSTANT strings intern to a DETERMINISTIC handle id with a
  // namespace-visible definition symbol (python::__strconst_<id>, value =
  // the constant). This is what makes the backend's strtab-aware constant
  // RECOVERY work (smt2_convt::try_extract_string_literal follows
  // strtab(<const id>) through the intern symbol): the regex/string
  // intrinsic lowerings keep their compile-time precision for constant
  // operands that flow through handles (e.g. the pv __str payload). The
  // global ASSUME strtab(id) == "<const>" (emitted once per intern) keeps
  // general solver-level String reasoning consistent. Equal constants
  // share one id, so `is`-style handle equality on equal literals also
  // holds -- consistent with CPython's small-literal interning latitude.
  if(str.id() == ID_constant && str.type().id() == ID_string)
  {
    const std::string text = id2string(to_constant_expr(str).get_value());
    auto it = string_intern_ids.find(text);
    if(it != string_intern_ids.end())
      return from_integer(it->second, python_string_handle_type());
    const long long id = static_cast<long long>(string_intern_ids.size()) + 1;
    string_intern_ids[text] = id;
    python_string_intern_reverse()[id] = text;
    const irep_idt cs_id{"python::__strconst_" + std::to_string(id)};
    if(symbol_table.lookup(cs_id) == nullptr)
    {
      symbolt cs{cs_id, string_typet{}, "python"};
      cs.base_name = id2string(cs_id);
      cs.is_static_lifetime = true;
      cs.value = str;
      symbol_table.add(cs);
    }
    exprt h = from_integer(id, python_string_handle_type());
    // The definitional ASSUME must use the RAW UF application: the folding
    // denotation would reduce it to "s" == "s" (vacuous, dropped), leaving
    // SYMBOLIC reads of the same handle without the axiom (the split-test
    // regression: `xs[0] == "a"` compared an unconstrained strtab(1)).
    emit_strtab_axiom(h, str);
    return h;
  }
  static unsigned strh_ctr = 0;
  const std::string hn = "__strh_" + std::to_string(strh_ctr++);
  const irep_idt hid{qualify_name(hn)};
  if(symbol_table.lookup(hid) == nullptr)
  {
    symbolt hs{hid, python_string_handle_type(), "python"};
    hs.base_name = hn;
    hs.is_lvalue = true;
    hs.is_state_var = true;
    hs.is_static_lifetime = current_function.empty();
    symbol_table.add(hs);
  }
  symbol_exprt h = symbol_table.lookup_ref(hid).symbol_expr();
  // The handle VALUE is irrelevant (only its strtab image matters); leave
  // it nondet and constrain the image. Two allocations of equal strings
  // may get different handles -- string EQUALITY goes through strtab, and
  // Python `is` identity on strings is not modelled (PLR: unspecified for
  // computed strings).
  pending_checks.push_back(code_frontend_assignt{
    h, side_effect_expr_nondett{h.type(), source_locationt{}}});
  emit_strtab_axiom(h, str);
  return std::move(h);
}

/// Emit the handle-allocation axiom `strtab(h) == str` as a ghost-Bool
/// ASSIGNMENT followed by an assume of the ghost. The assignment form
/// matters for the backend's strtab-aware constant recovery: the SSA
/// conversion order is assignments FIRST, assumptions later
/// (symex_target_equation::convert_without_assertions), so an axiom
/// emitted only as an assume is recorded AFTER the string intrinsics
/// that need it have already converted. As an assignment, set_to records
/// the definitional equality in program order. Uses the RAW UF
/// application (the folding denotation would reduce interned-constant
/// axioms to a vacuous "s" == "s").
void python_convertert::emit_strtab_axiom(const exprt &h, const exprt &str)
{
  static unsigned strdef_ctr = 0;
  const std::string gn = "__strdef_" + std::to_string(strdef_ctr++);
  const irep_idt gid{qualify_name(gn)};
  if(symbol_table.lookup(gid) == nullptr)
  {
    symbolt gs{gid, bool_typet{}, "python"};
    gs.base_name = gn;
    gs.is_lvalue = true;
    gs.is_state_var = true;
    gs.is_static_lifetime = current_function.empty();
    symbol_table.add(gs);
  }
  symbol_exprt g = symbol_table.lookup_ref(gid).symbol_expr();
  equal_exprt ax{function_application_exprt{strtab_symbol(), {h}}, str};
  pending_checks.push_back(code_frontend_assignt{g, std::move(ax)});
  pending_checks.push_back(code_assumet{g});
}

symbol_exprt python_convertert::inttab_symbol()
{
  const irep_idt id{"python::__cbmc_inttab"};
  if(symbol_table.lookup(id) == nullptr)
  {
    mathematical_function_typet ft{{signedbv_typet{64}}, integer_typet{}};
    symbolt s{id, ft, "python"};
    s.base_name = "__cbmc_inttab";
    s.is_lvalue = false;
    s.is_state_var = false;
    s.is_static_lifetime = true;
    symbol_table.add(s);
  }
  return symbol_table.lookup_ref(id).symbol_expr();
}

exprt python_convertert::int_to_handle(const exprt &val)
{
  static unsigned inth_ctr = 0;
  const std::string hn = "__inth_" + std::to_string(inth_ctr++);
  const irep_idt hid{qualify_name(hn)};
  if(symbol_table.lookup(hid) == nullptr)
  {
    symbolt hs{hid, python_int_handle_type(), "python"};
    hs.base_name = hn;
    hs.is_lvalue = true;
    hs.is_state_var = true;
    hs.is_static_lifetime = current_function.empty();
    symbol_table.add(hs);
  }
  symbol_exprt h = symbol_table.lookup_ref(hid).symbol_expr();
  pending_checks.push_back(code_frontend_assignt{
    h, side_effect_expr_nondett{h.type(), source_locationt{}}});
  pending_checks.push_back(code_assumet{
    equal_exprt{function_application_exprt{inttab_symbol(), {h}}, val}});
  return std::move(h);
}

bool python_convertert::fold_covers_call_shape(
  const jsont &call,
  std::size_t max_positional,
  const std::set<std::string> &allowed_keywords)
{
  const jsont &args = json_member(call, "args");
  if(args.is_array() && as_array(args).size() > max_positional)
    return false;
  const jsont &kws = json_member(call, "keywords");
  if(kws.is_array())
  {
    for(const auto &kw : as_array(kws))
    {
      const jsont &arg = json_member(kw, "arg");
      // A **spread (arg == null) is never covered by a fold.
      if(!arg.is_string())
        return false;
      if(allowed_keywords.count(json_string(arg)) == 0)
        return false;
    }
  }
  return true;
}
