#include <solvers/sat/satcheck_cadical.h>

#include "array_propagator.h"

#include <functional>
/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <util/arith_tools.h>
#include <util/byte_operators.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/simplify_expr.h>
#include <util/std_expr.h>

#include "boolbv.h"

#include <algorithm>

bvt boolbvt::convert_index(const index_exprt &expr)
{
  const exprt &array=expr.array();
  const exprt &index=expr.index();

  const typet &array_op_type = array.type();

  bvt bv;

  if(array_op_type.id()==ID_array)
  {
    const array_typet &array_type=
      to_array_type(array_op_type);

    // see if the array size is constant

    if(is_unbounded_array(array_type))
    {
      // use array decision procedure

      // Typecast between array types with different element sizes
      // (e.g., SIMD reinterpretation int32[4] <-> int64[2]) cannot be
      // handled by the array theory's element-wise constraints.
      // Lower to byte_extract which the bitvector solver handles.
      if(
        array.id() == ID_typecast &&
        to_typecast_expr(array).op().type().id() == ID_array &&
        to_array_type(array.type()).element_type() !=
          to_array_type(to_typecast_expr(array).op().type()).element_type())
      {
        const auto &src = to_typecast_expr(array).op();
        const auto elem_size = boolbv_width(array_type.element_type()) / 8;
        return convert_bv(lower_byte_operators(
          byte_extract_exprt(
            ID_byte_extract_little_endian,
            src,
            mult_exprt(
              typecast_exprt::conditional_cast(
                index, signedbv_typet(config.ansi_c.pointer_width)),
              from_integer(
                elem_size, signedbv_typet(config.ansi_c.pointer_width))),
            config.ansi_c.char_width,
            array_type.element_type()),
          ns));
      }

      if(has_byte_operator(expr))
      {
        const index_exprt final_expr =
          to_index_expr(lower_byte_operators(expr, ns));
        CHECK_RETURN(final_expr != expr);
        bv = convert_bv(final_expr);

        // record type if array is a symbol
        const exprt &final_array = final_expr.array();
        if(
          final_array.id() == ID_symbol || final_array.id() == ID_nondet_symbol)
        {
          const auto &array_width_opt = bv_width.get_width_opt(array_type);
          (void)map.get_literals(
            final_array.get(ID_identifier),
            array_type,
            array_width_opt.value_or(0));
        }

        // make sure we have the index in the cache
        convert_bv(final_expr.index());
      }
      else
      {
        // Phase B lazy select: return free BVs for all selects
        if(
          cdclt_propagator && lazy_arrays &&
          bv_width.get_width_opt(expr.type()).has_value() &&
          expr.type().id() != ID_array)
        {
          const auto width = bv_width.get_width_opt(expr.type()).value();
          bv = prop.new_variables(width);
          for(const auto &lit : bv)
            prop.set_frozen(lit);
          bvt idx_bv = convert_bv(index);
          for(const auto &lit : idx_bv)
            prop.set_frozen(lit);
          lazy_selects.push_back(lazy_selectt{bv, expr, idx_bv});
          record_array_index(expr);
          // Also register indices for the store chain walk.
          // The ITE encoding would create selects on intermediate
          // arrays; register those indices for the array theory.
          {
            exprt arr = array;
            while(arr.id() == ID_with)
            {
              record_array_index(
                index_exprt{to_with_expr(arr).old(), index, expr.type()});
              arr = to_with_expr(arr).old();
            }
          }
          return bv;
        }
        // CDCL(T) lazy select (disabled by default — opt-in via
        // --refine-arrays with CaDiCaL).
        if(
          false && cdclt_propagator &&
          bv_width.get_width_opt(expr.type()).has_value() &&
          expr.type().id() != ID_array && array.id() == ID_with)
        {
          const auto width = bv_width.get_width_opt(expr.type()).value();
          bv = prop.new_variables(width);
          for(const auto &lit : bv)
            prop.set_frozen(lit);

          // Walk the store chain and register axioms
          // For store(old, j, v)[i]:
          //   (j == i) → bv == convert_bv(v)
          //   (j != i) → bv == convert_bv(old[i])
          // The old[i] recursively creates another lazy select.
          const exprt lazy_sym = symbol_exprt{
            "array_theory::lazy#" + std::to_string(lazy_selects.size()),
            expr.type()};
          // Map the lazy BV so get_value can read it
          map.set_literals(
            to_symbol_expr(lazy_sym).get_identifier(), expr.type(), bv);

          exprt arr = array;
          bvt current_bv = bv;
          while(arr.id() == ID_with)
          {
            const with_exprt &w = to_with_expr(arr);
            const literalt idx_eq = convert(equal_exprt{
              index,
              typecast_exprt::conditional_cast(w.where(), index.type())});

            if(!idx_eq.is_constant())
            {
              // (j == i) → current_bv == v
              const literalt val_eq =
                convert(equal_exprt{lazy_sym, w.new_value()});
              if(!val_eq.is_constant())
              {
                cdclt_propagator->add_implication(
                  idx_eq.dimacs(), val_eq.dimacs());
                cdclt_propagator->add_ackermann_clause(
                  idx_eq.dimacs(), val_eq.dimacs());
                auto *cad = dynamic_cast<satcheck_cadical_baset *>(&prop);
                if(cad)
                {
                  cad->observe_var(idx_eq.var_no());
                  cad->observe_var(val_eq.var_no());
                }
              }
            }
            else if(idx_eq == const_literal(true))
            {
              // Index definitely matches — assert directly
              const bvt val_bv = convert_bv(w.new_value());
              for(std::size_t k = 0; k < current_bv.size() && k < val_bv.size();
                  k++)
              {
                prop.lcnf(!current_bv[k], val_bv[k]);
                prop.lcnf(current_bv[k], !val_bv[k]);
              }
              break; // definite match, no need to continue
            }

            arr = w.old();
          }

          lazy_selects.push_back({bv, expr});
          record_array_index(expr);
          return bv;
        }

        // 2D inlining: for a[i][j] where a is a symbol with a known
        // with-expression definition, substitute the definition so the
        // ITE encoding can walk the store chain directly.
        if(
          array.id() == ID_index &&
          bv_width.get_width_opt(expr.type()).has_value())
        {
          const index_exprt &outer = to_index_expr(array);
          auto def_it = array_2d_definitions.find(outer.array());
          if(def_it != array_2d_definitions.end())
          {
            // Replace symbol with its with-expression definition
            const index_exprt inlined_outer{
              def_it->second, outer.index(), outer.type()};
            const index_exprt inlined{inlined_outer, index, expr.type()};
            bv = convert_bv(inlined);
            record_array_index(expr);
            return bv;
          }
        }

        // 2D ITE encoding: for a[i][j] where a is a with-expression
        // on an array-of-arrays, walk the store chain with compound
        // conditions (outer_idx==k && inner_idx==l → v).
        if(
          array.id() == ID_index &&
          to_index_expr(array).array().id() == ID_with &&
          bv_width.get_width_opt(expr.type()).has_value())
        {
          const index_exprt &outer = to_index_expr(array);
          const exprt &outer_idx = outer.index();
          const exprt &inner_idx = index;

          std::function<bvt(const exprt &)> flatten_2d =
            [&](const exprt &arr) -> bvt
          {
            if(arr.id() == ID_with)
            {
              const with_exprt &w = to_with_expr(arr);
              if(
                w.new_value().id() == ID_with &&
                to_with_expr(w.new_value()).old().id() == ID_index &&
                to_index_expr(to_with_expr(w.new_value()).old()).array() ==
                  w.old() &&
                to_index_expr(to_with_expr(w.new_value()).old()).index() ==
                  w.where())
              {
                const with_exprt &iw = to_with_expr(w.new_value());
                const literalt both = prop.land(
                  convert(equal_exprt{
                    outer_idx,
                    typecast_exprt::conditional_cast(
                      w.where(), outer_idx.type())}),
                  convert(equal_exprt{
                    inner_idx,
                    typecast_exprt::conditional_cast(
                      iw.where(), inner_idx.type())}));
                return bv_utils.select(
                  both, convert_bv(iw.new_value()), flatten_2d(w.old()));
              }
              const literalt oeq = convert(equal_exprt{
                outer_idx,
                typecast_exprt::conditional_cast(w.where(), outer_idx.type())});
              return bv_utils.select(
                oeq,
                convert_bv(index_exprt{w.new_value(), inner_idx, expr.type()}),
                flatten_2d(w.old()));
            }
            if(arr.id() == ID_if)
            {
              return bv_utils.select(
                convert(to_if_expr(arr).cond()),
                flatten_2d(to_if_expr(arr).true_case()),
                flatten_2d(to_if_expr(arr).false_case()));
            }
            // Base: use definition inlining or free variables
            auto dit = array_2d_definitions.find(arr);
            if(dit != array_2d_definitions.end())
              return flatten_2d(dit->second);
            // Free variables for base symbol
            bvt base_bv =
              prop.new_variables(bv_width.get_width_opt(expr.type()).value());
            record_array_index(expr);
            return base_bv;
          };

          bv = flatten_2d(outer.array());
          return bv;
        }

        // ITE encoding for stores: encode read-over-write as bitvector
        // mux. Only for element-typed results (not array-of-arrays).
        if(
          array.id() == ID_with &&
          bv_width.get_width_opt(expr.type()).has_value())
        {
          const with_exprt &with_expr = to_with_expr(array);
          const literalt idx_eq = convert(equal_exprt{
            index,
            typecast_exprt::conditional_cast(with_expr.where(), index.type())});
          const bvt bv_val = convert_bv(with_expr.new_value());
          const bvt bv_old =
            convert_bv(index_exprt{with_expr.old(), index, expr.type()});
          bv = bv_utils.select(idx_eq, bv_val, bv_old);
          record_array_index(expr);
          return bv;
        }

        if(
          array.id() == ID_if &&
          bv_width.get_width_opt(expr.type()).has_value())
        {
          const if_exprt &if_expr = to_if_expr(array);
          const literalt cond = convert(if_expr.cond());
          const bvt bv_true =
            convert_bv(index_exprt{if_expr.true_case(), index, expr.type()});
          const bvt bv_false =
            convert_bv(index_exprt{if_expr.false_case(), index, expr.type()});
          bv = bv_utils.select(cond, bv_true, bv_false);
          record_array_index(expr);
          return bv;
        }

        // free variables
        bv = prop.new_variables(boolbv_width(expr.type()));

        record_array_index(expr);

        // record type if array is a symbol
        if(array.id() == ID_symbol || array.id() == ID_nondet_symbol)
        {
          const auto &array_width_opt = bv_width.get_width_opt(array_type);
          (void)map.get_literals(
            array.get(ID_identifier), array_type, array_width_opt.value_or(0));
        }

        // make sure we have the index in the cache
        convert_bv(index);
      }

      return bv;
    }

    // Must have a finite size
    mp_integer array_size =
      numeric_cast_v<mp_integer>(to_constant_expr(array_type.size()));

    {
      // see if the index address is constant
      // many of these are compacted by simplify_expr
      // but variable location writes will block this
      auto maybe_index_value = numeric_cast<mp_integer>(index);
      if(maybe_index_value.has_value())
      {
        return convert_index(array, maybe_index_value.value());
      }
    }

    // Special case : arrays of one thing (useful for constants)
    // TODO : merge with ACTUAL_ARRAY_HACK so that ranges of the same
    // value, bit-patterns with the same value, etc. are treated like
    // this rather than as a series of individual options.
    #define UNIFORM_ARRAY_HACK
    #ifdef UNIFORM_ARRAY_HACK
    bool is_uniform = array.id() == ID_array_of;

    if(array.is_constant() || array.id() == ID_array)
    {
      is_uniform = array.operands().size() <= 1 ||
                   std::all_of(
                     ++array.operands().begin(),
                     array.operands().end(),
                     [&array](const exprt &expr) {
                       return expr == to_multi_ary_expr(array).op0();
                     });
    }

    if(is_uniform && prop.has_set_to())
    {
      static int uniform_array_counter;  // Temporary hack

      const std::string identifier = CPROVER_PREFIX "internal_uniform_array_" +
                                     std::to_string(uniform_array_counter++);

      symbol_exprt result(identifier, expr.type());
      bv = convert_bv(result);

      // return an unconstrained value in case of an empty array (the access is
      // necessarily out-of-bounds)
      if(!array.has_operands())
        return bv;

      equal_exprt value_equality(result, to_multi_ary_expr(array).op0());

      binary_relation_exprt lower_bound(
        from_integer(0, index.type()), ID_le, index);
      binary_relation_exprt upper_bound(
        index, ID_lt, from_integer(array_size, index.type()));

      and_exprt range_condition(std::move(lower_bound), std::move(upper_bound));
      implies_exprt implication(
        std::move(range_condition), std::move(value_equality));

      // Simplify may remove the lower bound if the type
      // is correct.
      prop.l_set_to_true(convert(simplify_expr(implication, ns)));

      return bv;
    }
    #endif

    #define ACTUAL_ARRAY_HACK
    #ifdef ACTUAL_ARRAY_HACK
    // More useful when updates to concrete locations in
    // actual arrays are compacted by simplify_expr
    if((array.is_constant() || array.id() == ID_array) && prop.has_set_to())
    {
      #ifdef CONSTANT_ARRAY_HACK
      // TODO : Compile the whole array into a single relation
      #endif

      // Symbol for output
      static int actual_array_counter;  // Temporary hack

      const std::string identifier = CPROVER_PREFIX "internal_actual_array_" +
                                     std::to_string(actual_array_counter++);

      symbol_exprt result(identifier, expr.type());
      bv = convert_bv(result);

      // add implications

#ifdef COMPACT_EQUAL_CONST
      bv_utils.equal_const_register(convert_bv(index));  // Definitely
      bv_utils.equal_const_register(convert_bv(result)); // Maybe
#endif

      exprt::operandst::const_iterator it = array.operands().begin();

      for(mp_integer i=0; i<array_size; i=i+1)
      {
        INVARIANT(
          it != array.operands().end(),
          "this loop iterates over the array, so `it` shouldn't be increased "
          "past the array's end");

        // Cache comparisons and equalities
        prop.l_set_to_true(convert(implies_exprt(
          equal_exprt(index, from_integer(i, index.type())),
          equal_exprt(result, *it++))));
      }

      return bv;
    }

#endif


    // TODO : As with constant index, there is a trade-off
    // of when it is best to flatten the whole array and
    // when it is best to use the array theory and then use
    // one or more of the above encoding strategies.

    // get literals for the whole array
    const std::size_t width = boolbv_width(expr.type());

    const bvt &array_bv =
      convert_bv(array, numeric_cast_v<std::size_t>(array_size * width));

    // TODO: maybe a shifter-like construction would be better
    // Would be a lot more compact but propagate worse

    if(prop.has_set_to())
    {
      // free variables
      bv = prop.new_variables(width);

      // add implications

#ifdef COMPACT_EQUAL_CONST
      bv_utils.equal_const_register(convert_bv(index));  // Definitely
#endif

      bvt equal_bv;
      equal_bv.resize(width);

      for(mp_integer i=0; i<array_size; i=i+1)
      {
        mp_integer offset=i*width;

        for(std::size_t j=0; j<width; j++)
          equal_bv[j] = prop.lequal(
            bv[j], array_bv[numeric_cast_v<std::size_t>(offset + j)]);

        prop.l_set_to_true(prop.limplies(
          convert(equal_exprt(index, from_integer(i, index.type()))),
          prop.land(equal_bv)));
      }
    }
    else
    {
      bv.resize(width);

#ifdef COMPACT_EQUAL_CONST
      bv_utils.equal_const_register(convert_bv(index));  // Definitely
#endif

      typet constant_type=index.type(); // type of index operand

      DATA_INVARIANT(
        array_size > 0,
        "non-positive array sizes are forbidden in goto programs");

      for(mp_integer i=0; i<array_size; i=i+1)
      {
        literalt e =
          convert(equal_exprt(index, from_integer(i, constant_type)));

        mp_integer offset=i*width;

        for(std::size_t j=0; j<width; j++)
        {
          literalt l = array_bv[numeric_cast_v<std::size_t>(offset + j)];

          if(i==0) // this initializes bv
            bv[j]=l;
          else
            bv[j]=prop.lselect(e, l, bv[j]);
        }
      }
    }
  }
  else
    return conversion_failed(expr);

  return bv;
}

/// index operator with constant index
bvt boolbvt::convert_index(
  const exprt &array,
  const mp_integer &index)
{
  const array_typet &array_type = to_array_type(array.type());

  std::size_t width = boolbv_width(array_type.element_type());

  // TODO: If the underlying array can use one of the
  // improvements given above then it may be better to use
  // the array theory for short chains of updates and then
  // the improved array handling rather than full flattening.
  // Note that the calculation is non-trivial as the cost of
  // full flattening is amortised against all uses of
  // the array (constant and variable indexes) and updated
  // versions of it.

  const bvt &tmp=convert_bv(array); // recursive call

  mp_integer offset=index*width;

  if(offset>=0 &&
     offset+width<=mp_integer(tmp.size()))
  {
    // in bounds

    // The assertion below is disabled as we want to be able
    // to run CBMC without simplifier.
    // Expression simplification should remove these cases
    // assert(array.id()!=ID_array_of &&
    //       array.id()!=ID_array);
    // If not there are large improvements possible as above

    std::size_t offset_int = numeric_cast_v<std::size_t>(offset);
    return bvt(tmp.begin() + offset_int, tmp.begin() + offset_int + width);
  }
  else if(array.id() == ID_member || array.id() == ID_index)
  {
    // out of bounds for the component, but not necessarily outside the bounds
    // of the underlying object
    object_descriptor_exprt o;
    o.build(array, ns);
    CHECK_RETURN(o.offset().id() != ID_unknown);

    const auto subtype_bytes_opt =
      pointer_offset_size(array_type.element_type(), ns);
    CHECK_RETURN(subtype_bytes_opt.has_value());

    exprt new_offset = simplify_expr(
      plus_exprt(
        o.offset(), from_integer(index * (*subtype_bytes_opt), o.offset().type())),
      ns);

    byte_extract_exprt be =
      make_byte_extract(o.root_object(), new_offset, array_type.element_type());

    return convert_bv(be);
  }
  else if(
    array.id() == ID_byte_extract_big_endian ||
    array.id() == ID_byte_extract_little_endian)
  {
    const byte_extract_exprt &byte_extract_expr = to_byte_extract_expr(array);

    const auto subtype_bytes_opt =
      pointer_offset_size(array_type.element_type(), ns);
    CHECK_RETURN(subtype_bytes_opt.has_value());

    // add offset to index
    exprt new_offset = simplify_expr(
      plus_exprt{
        byte_extract_expr.offset(),
        from_integer(
          index * (*subtype_bytes_opt), byte_extract_expr.offset().type())},
      ns);

    byte_extract_exprt be = byte_extract_expr;
    be.offset() = new_offset;
    be.type() = array_type.element_type();

    return convert_bv(be);
  }
  else
  {
    // out of bounds
    return prop.new_variables(width);
  }
}
