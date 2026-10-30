/*******************************************************************\

Module: Go front-end: floating-point and complex numbers

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Conversion of floating-point and complex values (Go1.27 §Numeric_types:
/// "float32 the set of all IEEE 754 32-bit floating-point numbers"; float64
/// likewise with 64 bits; complex64 and complex128 have float32 and float64
/// parts).
///
/// Floating-point operations round to nearest, ties to even, the IEEE 754
/// default that Go uses. The specification allows more ("An implementation
/// may combine multiple floating-point operations into a single fused
/// operation, possibly across statements"); the operations are modelled as
/// gc performs them on amd64, without fusion (see the plan document's
/// limitations). Complex multiplication follows gc: the parts are computed
/// as (ac - bd) and (ad + bc) in float64 and, for complex64, rounded back.
/// Complex division is rt.ComplexDiv (helper/runtime/rt.go), which gives
/// gc's results.

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/mp_arith.h>
#include <util/std_code.h>

#include "go_convert.h"
#include "go_parse_tree.h"

/// Round to nearest, ties to even.
static exprt rounding()
{
  return ieee_floatt::rounding_mode_expr(ieee_floatt::ROUND_TO_EVEN);
}

/// The IEEE 754 operation \p id on \p x and \p y.
static exprt float_op(const exprt &x, const irep_idt &id, const exprt &y)
{
  return ieee_float_op_exprt{x, id, y, rounding()};
}

/// \p value rounded to the floating-point type \p type.
static exprt to_float(const exprt &value, const typet &type)
{
  if(value.type() == type)
    return value;
  return floatbv_typecast_exprt{value, rounding(), type};
}

static const typet &part_type(const typet &complex)
{
  return to_struct_type(complex).components()[0].type();
}

static exprt real_part(const exprt &z)
{
  if(z.id() == ID_struct)
    return to_struct_expr(z).operands()[0];
  return member_exprt{z, to_struct_type(z.type()).components()[0]};
}

static exprt imag_part(const exprt &z)
{
  if(z.id() == ID_struct)
    return to_struct_expr(z).operands()[1];
  return member_exprt{z, to_struct_type(z.type()).components()[1]};
}

/// The complex number re + im i of type \p type.
static exprt make_complex(exprt re, exprt im, const typet &type)
{
  return struct_exprt{{std::move(re), std::move(im)}, type};
}

exprt go_convertert::float_constant(const irept &expr, const typet &type)
{
  // The helper records the IEEE 754 bits of the value rounded to the type
  // (Go1.27 §Constants: numeric constants are exact until they are given a
  // type).
  const auto bits = [&](const char *field, const typet &float_type)
  {
    const std::size_t width = to_floatbv_type(float_type).get_width();
    return constant_exprt{
      integer2bvrep(string2integer(expr.get_string(field)), width), float_type};
  };
  if(type.id() == ID_floatbv)
    return bits("_fb", type);
  PRECONDITION(is_go_complex(type));
  return make_complex(
    bits("_fbr", part_type(type)), bits("_fbi", part_type(type)), type);
}

bool go_convertert::is_float_like(const typet &type)
{
  return type.id() == ID_floatbv || is_go_complex(type);
}

exprt go_convertert::float_arithmetic(
  const irep_idt &op,
  exprt x,
  exprt y,
  const typet &type,
  const source_locationt &location)
{
  // Go1.27 §Arithmetic_operators: +, -, *, / apply to floating-point and
  // complex numbers. §Floating_point_operators: "The result of a
  // floating-point or complex division by zero is not specified beyond the
  // IEEE 754 standard; whether a run-time panic occurs is
  // implementation-specific." gc does not panic, so neither does this.
  const irep_idt id = op == "+"   ? ID_floatbv_plus
                      : op == "-" ? ID_floatbv_minus
                      : op == "*" ? ID_floatbv_mult
                      : op == "/" ? ID_floatbv_div
                                  : irep_idt{};
  if(id.empty())
  {
    error(irept{}, "operator " + id2string(op) + " on floating-point values");
    return nil_exprt{};
  }
  x = go_cast(x, type);
  y = go_cast(y, type);
  exprt result;
  if(type.id() == ID_floatbv)
    result = float_op(x, id, y);
  else if(op == "+" || op == "-")
  {
    // Componentwise.
    code_blockt block;
    x = evaluate_once(x, location, block);
    y = evaluate_once(y, location, block);
    result = with_prefix(
      make_complex(
        float_op(real_part(x), id, real_part(y)),
        float_op(imag_part(x), id, imag_part(y)),
        type),
      block,
      location);
  }
  else if(op == "*")
  {
    // As gc: (a+bi)(c+di) = (ac - bd) + (ad + bc)i, computed in float64.
    code_blockt block;
    x = evaluate_once(x, location, block);
    y = evaluate_once(y, location, block);
    const typet wide = ieee_float_spect::double_precision().to_type();
    const exprt a = to_float(real_part(x), wide);
    const exprt b = to_float(imag_part(x), wide);
    const exprt c = to_float(real_part(y), wide);
    const exprt d = to_float(imag_part(y), wide);
    const exprt re = float_op(
      float_op(a, ID_floatbv_mult, c),
      ID_floatbv_minus,
      float_op(b, ID_floatbv_mult, d));
    const exprt im = float_op(
      float_op(a, ID_floatbv_mult, d),
      ID_floatbv_plus,
      float_op(b, ID_floatbv_mult, c));
    result = with_prefix(
      make_complex(
        to_float(re, part_type(type)), to_float(im, part_type(type)), type),
      block,
      location);
  }
  else
  {
    // gc divides complex64 values as complex128 values.
    const struct_typet wide = go_type_convertert::complex_type(
      ieee_float_spect::double_precision().to_type());
    const auto widen = [&](const exprt &z)
    { return type == wide ? z : convert_complex(z, wide); };
    exprt quotient = runtime_call("ComplexDiv", {widen(x), widen(y)}, location);
    result = type == wide ? quotient : convert_complex(quotient, type);
  }
  result.add_source_location() = location;
  return result;
}

exprt go_convertert::convert_complex(const exprt &z, const typet &type)
{
  // Go1.27 §Conversions: "a complex number to another complex type, the
  // result value is rounded to the precision specified by the destination
  // type."
  if(z.type() == type)
    return z;
  code_blockt block;
  const source_locationt location = z.source_location();
  const exprt v = evaluate_once(z, location, block);
  return with_prefix(
    make_complex(
      to_float(real_part(v), part_type(type)),
      to_float(imag_part(v), part_type(type)),
      type),
    block,
    location);
}

exprt go_convertert::float_equality(const exprt &x, const exprt &y)
{
  // Go1.27 §Comparison_operators: "Two floating-point values are compared
  // as defined by the IEEE 754 standard." "Two complex values u and v are
  // equal if both real(u) == real(v) and imag(u) == imag(v)."
  if(x.type().id() == ID_floatbv)
    return ieee_float_equal_exprt{x, y};
  return and_exprt{
    ieee_float_equal_exprt{real_part(x), real_part(y)},
    ieee_float_equal_exprt{imag_part(x), imag_part(y)}};
}

exprt go_convertert::numeric_conversion(
  const exprt &value,
  const typet &type,
  const source_locationt &location)
{
  // Go1.27 §Conversions, "Conversions between numeric types": "When
  // converting a floating-point number to an integer, the fraction is
  // discarded (truncation towards zero). When converting an integer or
  // floating-point number to a floating-point type, or a complex number to
  // another complex type, the result value is rounded to the precision
  // specified by the destination type." "In all non-constant conversions
  // involving floating-point or complex values, if the result type cannot
  // represent the value the conversion succeeds but the result value is
  // implementation-dependent": an arbitrary value of the result type.
  const typet &from = value.type();
  if(type.id() == ID_floatbv && !is_go_complex(from))
    return to_float(value, type);
  if(is_go_complex(type) && is_go_complex(from))
    return convert_complex(value, type);
  if(
    from.id() == ID_floatbv &&
    (type.id() == ID_signedbv || type.id() == ID_unsignedbv))
  {
    code_blockt block;
    const exprt x = evaluate_once(value, location, block);
    // In range: trunc(x) is in [min, max]. The bounds are compared in x's
    // type: max + 1 is a power of two, and min - 1 is used if representable
    // (otherwise no float lies between min - 1 and min).
    const std::size_t width = to_bitvector_type(type).get_width();
    const bool is_signed = type.id() == ID_signedbv;
    const mp_integer min = is_signed ? -power(2, width - 1) : mp_integer{0};
    const mp_integer max_plus_one =
      is_signed ? power(2, width - 1) : power(2, width);
    const auto as_float = [&](const mp_integer &v)
    {
      ieee_floatt f{to_floatbv_type(from), ieee_floatt::ROUND_TO_EVEN};
      f.from_integer(v);
      return f;
    };
    const ieee_floatt below = as_float(min - 1);
    const exprt lower =
      below.to_integer() == min - 1
        ? exprt{binary_relation_exprt{x, ID_gt, below.to_expr()}}
        : exprt{binary_relation_exprt{x, ID_ge, as_float(min).to_expr()}};
    const exprt upper =
      binary_relation_exprt{x, ID_lt, as_float(max_plus_one).to_expr()};
    const exprt result = if_exprt{
      and_exprt{lower, upper},
      typecast_exprt{x, type}, // truncates towards zero
      side_effect_expr_nondett{type, location}};
    return with_prefix(result, block, location);
  }
  return nil_exprt{};
}

exprt go_convertert::float_min_max(const exprt &x, const exprt &y, bool is_min)
{
  // Go1.27 §Min_and_max: "For floating-point arguments negative zero, NaN,
  // and infinity the following rules apply": "negative zero is smaller
  // than (non-negative) zero", and "if any argument is a NaN, the result is
  // a NaN". x and y have no side effects.
  const exprt x_nan = isnan_exprt{x};
  const exprt y_nan = isnan_exprt{y};
  const exprt less = binary_relation_exprt{x, ID_lt, y};
  const exprt greater = binary_relation_exprt{x, ID_gt, y};
  // Equal values (only +0 and -0 can be equal yet differ): the negative
  // one for min, the other for max.
  const exprt x_negative = sign_exprt{x};
  const exprt equal_choice =
    is_min ? if_exprt{x_negative, x, y} : if_exprt{x_negative, y, x};
  return if_exprt{
    x_nan,
    x,
    if_exprt{
      y_nan,
      y,
      if_exprt{
        is_min ? less : greater,
        x,
        if_exprt{is_min ? greater : less, y, equal_choice}}}};
}

exprt go_convertert::convert_complex_builtin(
  const irept &call,
  const irep_idt &callee)
{
  // Go1.27 §Complex_numbers: "The built-in function complex constructs a
  // complex value from a floating-point real and imaginary part, while real
  // and imag extract the real and imaginary parts of a complex value."
  const source_locationt location = location_of(call);
  const auto &args = go_list(call, "Args");
  const typet type = expr_type(call);
  if(type.is_nil())
    return nil_exprt{};
  if(callee == "complex")
  {
    exprt re = convert_expr(args[0]);
    exprt im = convert_expr(args[1]);
    if(re.is_nil() || im.is_nil())
      return nil_exprt{};
    return make_complex(
      to_float(re, part_type(type)), to_float(im, part_type(type)), type);
  }
  exprt z = convert_expr(args[0]);
  if(z.is_nil())
    return nil_exprt{};
  exprt part = callee == "real" ? real_part(z) : imag_part(z);
  part.add_source_location() = location;
  return part;
}
