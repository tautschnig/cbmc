/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "boolbv.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>

bvt boolbvt::convert_extractbits(const extractbits_exprt &expr)
{
  const std::size_t bv_width = boolbv_width(expr.type());

  auto const &src_bv = convert_bv(expr.src());

  auto const maybe_index_as_int = numeric_cast<mp_integer>(expr.index());

  // We only do constants for now.
  // Should implement a shift here.
  if(!maybe_index_as_int.has_value())
    return conversion_failed(expr);

  auto index_as_int = maybe_index_as_int.value();

  DATA_INVARIANT_WITH_DIAGNOSTICS(
    index_as_int >= 0 && index_as_int < src_bv.size(),
    "index of extractbits must be within the bitvector",
    expr.find_source_location(),
    irep_pretty_diagnosticst{expr});

  const std::size_t offset = numeric_cast_v<std::size_t>(index_as_int);

  // Handle the case where the extraction extends beyond the source bitvector.
  // This can occur when typecasting from a smaller bit field to a larger type.
  // In such cases, extract available bits and zero-extend the result.
  if(index_as_int + bv_width > src_bv.size())
  {
    bvt result_bv;
    result_bv.reserve(bv_width);

    // Extract available bits from the source
    const std::size_t available_bits = src_bv.size() - offset;
    result_bv.insert(result_bv.end(), src_bv.begin() + offset, src_bv.end());

    // Zero-extend the remaining bits
    for(std::size_t i = available_bits; i < bv_width; i++)
      result_bv.push_back(const_literal(false));

    return result_bv;
  }

  bvt result_bv(src_bv.begin() + offset, src_bv.begin() + offset + bv_width);

  return result_bv;
}
