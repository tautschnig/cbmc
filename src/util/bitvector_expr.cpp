/*******************************************************************\

Module: API to expression classes for bitvectors

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "bitvector_expr.h"

#include "arith_tools.h"
#include "bitvector_types.h"
#include "mathematical_types.h"

shift_exprt::shift_exprt(
  exprt _src,
  const irep_idt &_id,
  const std::size_t _distance)
  : binary_exprt(std::move(_src), _id, from_integer(_distance, integer_typet()))
{
}

extractbit_exprt::extractbit_exprt(exprt _src, const std::size_t _index)
  : binary_predicate_exprt(
      std::move(_src),
      ID_extractbit,
      from_integer(_index, integer_typet()))
{
}

extractbits_exprt::extractbits_exprt(
  exprt _src,
  const std::size_t _upper,
  const std::size_t _lower,
  typet _type)
  : expr_protectedt(ID_extractbits, std::move(_type))
{
  PRECONDITION(_upper >= _lower);
  add_to_operands(
    std::move(_src),
    from_integer(_upper, integer_typet()),
    from_integer(_lower, integer_typet()));
}

exprt popcount_exprt::lower() const
{
  // Hacker's Delight, variant pop0:
  // x = (x & 0x55555555) + ((x >> 1) & 0x55555555);
  // x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  // x = (x & 0x0F0F0F0F) + ((x >> 4) & 0x0F0F0F0F);
  // x = (x & 0x00FF00FF) + ((x >> 8) & 0x00FF00FF);
  // etc.
  // return x;
  // http://www.hackersdelight.org/permissions.htm

  // make sure the operand width is a power of two
  exprt x = op();
  const auto x_width = to_bitvector_type(x.type()).get_width();
  CHECK_RETURN(x_width >= 1);
  const std::size_t bits = address_bits(x_width);
  const std::size_t new_width = numeric_cast_v<std::size_t>(power(2, bits));

  const bool need_typecast =
    new_width > x_width || x.type().id() != ID_unsignedbv;

  if(need_typecast)
    x = typecast_exprt(x, unsignedbv_typet(new_width));

  // repeatedly compute x = (x & bitmask) + ((x >> shift) & bitmask)
  for(std::size_t shift = 1; shift < new_width; shift <<= 1)
  {
    // x >> shift
    lshr_exprt shifted_x(
      x, from_integer(shift, unsignedbv_typet(address_bits(shift) + 1)));
    // bitmask is a string of alternating shift-many bits starting from lsb set
    // to 1
    std::string bitstring;
    bitstring.reserve(new_width);
    for(std::size_t i = 0; i < new_width / (2 * shift); ++i)
      bitstring += std::string(shift, '0') + std::string(shift, '1');
    const mp_integer value = binary2integer(bitstring, false);
    const constant_exprt bitmask(integer2bvrep(value, new_width), x.type());
    // build the expression
    x = plus_exprt(bitand_exprt(x, bitmask), bitand_exprt(shifted_x, bitmask));
  }

  // the result is restricted to the result type
  return typecast_exprt::conditional_cast(x, type());
}

exprt count_leading_zeros_exprt::lower() const
{
  // x = x | (x >> 1);
  // x = x | (x >> 2);
  // x = x | (x >> 4);
  // x = x | (x >> 8);
  // etc.
  // return popcount(~x);

  // make sure the operand width is a power of two
  exprt x = op();
  const auto x_width = to_bitvector_type(x.type()).get_width();
  CHECK_RETURN(x_width >= 1);
  const std::size_t bits = address_bits(x_width);
  const std::size_t new_width = numeric_cast_v<std::size_t>(power(2, bits));

  const bool need_typecast =
    new_width > x_width || x.type().id() != ID_unsignedbv;

  if(need_typecast)
    x = typecast_exprt(x, unsignedbv_typet(new_width));

  // repeatedly compute x = x | (x >> shift)
  for(std::size_t shift = 1; shift < new_width; shift <<= 1)
  {
    // x >> shift
    lshr_exprt shifted_x(
      x, from_integer(shift, unsignedbv_typet(address_bits(shift) + 1)));
    // build the expression
    x = bitor_exprt{x, shifted_x};
  }

  // the result is restricted to the result type
  return popcount_exprt{
    bitnot_exprt{typecast_exprt::conditional_cast(x, op().type())}, type()}
    .lower();
}

exprt count_trailing_zeros_exprt::lower() const
{
  exprt x = op();
  const auto int_width = to_bitvector_type(x.type()).get_width();
  CHECK_RETURN(int_width >= 1);

  // popcount(x ^ ((unsigned)x - 1)) - 1
  const unsignedbv_typet ut{int_width};
  minus_exprt minus_one{typecast_exprt::conditional_cast(x, ut),
                        from_integer(1, ut)};
  popcount_exprt popcount{
    bitxor_exprt{x, typecast_exprt::conditional_cast(minus_one, x.type())}};
  minus_exprt result{popcount.lower(), from_integer(1, x.type())};

  return typecast_exprt::conditional_cast(result, type());
}

exprt find_first_set_exprt::lower() const
{
  exprt x = op();
  const auto int_width = to_bitvector_type(x.type()).get_width();
  CHECK_RETURN(int_width >= 1);

  // bitwidth(x) - clz(x & ~((unsigned)x - 1));
  const unsignedbv_typet ut{int_width};
  minus_exprt minus_one{typecast_exprt::conditional_cast(x, ut),
                        from_integer(1, ut)};
  count_leading_zeros_exprt clz{bitand_exprt{
    x, bitnot_exprt{typecast_exprt::conditional_cast(minus_one, x.type())}}};
  minus_exprt result{from_integer(int_width, x.type()), clz.lower()};

  return typecast_exprt::conditional_cast(result, type());
}
