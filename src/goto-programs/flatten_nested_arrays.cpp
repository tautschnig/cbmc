/// \file
/// Flatten nested arrays: array(array(T, M), N) -> array(T, N*M)

#include "flatten_nested_arrays.h"

#include <util/arith_tools.h>
#include <util/std_expr.h>

#include "goto_model.h"

/// Create a normalized linearized index: outer*size + inner.
/// Multiplication operands are sorted for canonical form.
static exprt
make_linear_index(const exprt &outer, const exprt &inner, const exprt &size)
{
  const typet &t = inner.type();
  const exprt o = typecast_exprt::conditional_cast(outer, t);
  const exprt s = typecast_exprt::conditional_cast(size, t);
  return plus_exprt{mult_exprt{o < s ? o : s, o < s ? s : o}, inner};
}

/// Check if a type contains a 2D array (but not 3D+).
static bool have_to_flatten(const typet &type)
{
  if(
    type.id() == ID_array &&
    to_array_type(type).element_type().id() == ID_array)
  {
    const auto &inner = to_array_type(to_array_type(type).element_type());
    if(inner.element_type().id() != ID_array)
      return true;
  }
  if(type.id() == ID_struct || type.id() == ID_union)
  {
    for(const auto &c : to_struct_union_type(type).components())
      if(have_to_flatten(c.type()))
        return true;
  }
  if(type.id() == ID_code)
  {
    const auto &ct = to_code_type(type);
    if(have_to_flatten(ct.return_type()))
      return true;
    for(const auto &p : ct.parameters())
      if(have_to_flatten(p.type()))
        return true;
  }
  if(type.id() == ID_pointer || type.id() == ID_complex)
    return have_to_flatten(to_type_with_subtype(type).subtype());
  return false;
}

/// Check if an expression contains a flattenable nested array.
/// Returns false for expressions containing address_of (pointer
/// arithmetic depends on the inner array dimension) or 3D+ types.
static bool have_to_flatten_expr(const exprt &expr)
{
  // Don't flatten inside address_of when the operand is a sub-array
  // (pointer arithmetic depends on the inner array dimension)
  if(expr.id() == ID_address_of)
  {
    const auto &obj = to_unary_expr(expr).op();
    if(obj.type().id() == ID_array && have_to_flatten(obj.type()))
      return false;
  }
  // Skip 3D+ expression types
  if(
    expr.type().id() == ID_array &&
    to_array_type(expr.type()).element_type().id() == ID_array &&
    to_array_type(to_array_type(expr.type()).element_type())
        .element_type()
        .id() == ID_array)
  {
    return false;
  }
  if(have_to_flatten(expr.type()))
    return true;
  for(const auto &op : expr.operands())
    if(have_to_flatten_expr(op))
      return true;
  return false;
}

static void flatten_type(typet &type)
{
  if(!have_to_flatten(type))
    return;
  if(type.id() == ID_struct || type.id() == ID_union)
  {
    for(auto &c : to_struct_union_type(type).components())
      flatten_type(c.type());
  }
  else if(type.id() == ID_code)
  {
    auto &ct = to_code_type(type);
    flatten_type(ct.return_type());
    for(auto &p : ct.parameters())
      flatten_type(p.type());
  }
  else if(type.id() == ID_pointer || type.id() == ID_complex)
  {
    flatten_type(to_type_with_subtype(type).subtype());
  }
  else if(type.id() == ID_array)
  {
    auto &arr = to_array_type(type);
    flatten_type(arr.element_type());
    if(arr.element_type().id() == ID_array)
    {
      const auto &inner = to_array_type(arr.element_type());
      exprt flat_size = mult_exprt{
        arr.size(),
        typecast_exprt::conditional_cast(inner.size(), arr.size().type())};
      type = array_typet{inner.element_type(), std::move(flat_size)};
    }
  }
}

/// Flatten index(index(a, i), j) -> index(a, i*M+j)
static bool try_flatten_double_index(exprt &expr)
{
  if(expr.id() != ID_index)
    return false;
  auto &outer = to_index_expr(expr);
  if(outer.array().id() != ID_index)
    return false;
  auto &inner = to_index_expr(outer.array());
  if(
    inner.array().type().id() != ID_array ||
    to_array_type(inner.array().type()).element_type().id() != ID_array)
  {
    return false;
  }
  const auto &ia =
    to_array_type(to_array_type(inner.array().type()).element_type());
  // Only 2D
  if(ia.element_type().id() == ID_array)
    return false;
  // Skip if base is index into higher-dimensional array
  if(
    inner.array().id() == ID_index &&
    to_index_expr(inner.array()).array().type().id() == ID_array &&
    to_array_type(to_index_expr(inner.array()).array().type())
        .element_type()
        .id() == ID_array)
  {
    return false;
  }

  exprt fi = make_linear_index(inner.index(), outer.index(), ia.size());
  exprt base = inner.array();
  expr = index_exprt{std::move(base), std::move(fi), ia.element_type()};
  flatten_type(expr.type());
  return true;
}

/// Flatten with(a, i, with(a[i], j, v)) -> with(a, i*M+j, v)
static bool try_flatten_nested_with(exprt &expr)
{
  if(expr.id() != ID_with)
    return false;
  if(
    expr.type().id() != ID_array ||
    to_array_type(expr.type()).element_type().id() != ID_array)
  {
    return false;
  }
  const auto &inner_elem =
    to_array_type(to_array_type(expr.type()).element_type());
  if(inner_elem.element_type().id() == ID_array)
    return false;

  auto &w = to_with_expr(expr);
  if(w.new_value().id() != ID_with)
    return false;
  const auto &iw = to_with_expr(w.new_value());
  if(
    iw.old().id() != ID_index || to_index_expr(iw.old()).array() != w.old() ||
    to_index_expr(iw.old()).index() != w.where())
  {
    return false;
  }

  const auto &ia = to_array_type(to_array_type(w.type()).element_type());
  exprt fi = make_linear_index(w.where(), iw.where(), ia.size());
  exprt old_a = w.old();
  exprt val = iw.new_value();
  typet ft = expr.type();
  flatten_type(ft);
  expr = with_exprt{std::move(old_a), std::move(fi), std::move(val)};
  expr.type() = std::move(ft);
  return true;
}

static void flatten_expr(exprt &expr);

/// Flatten {{a,b},{c,d}} -> {a,b,c,d}
static bool try_flatten_array_constant(exprt &expr)
{
  if(
    expr.id() != ID_array || expr.type().id() != ID_array ||
    to_array_type(expr.type()).element_type().id() != ID_array)
  {
    return false;
  }
  const auto &inner_type =
    to_array_type(to_array_type(expr.type()).element_type());
  if(inner_type.element_type().id() == ID_array)
    return false;

  const auto inner_size = numeric_cast<mp_integer>(inner_type.size());
  exprt::operandst flat;
  for(auto &op : expr.operands())
  {
    flatten_expr(op);
    if(op.id() == ID_array)
    {
      for(auto &iop : op.operands())
        flat.push_back(std::move(iop));
    }
    else if(inner_size.has_value())
    {
      for(mp_integer j = 0; j < *inner_size; ++j)
      {
        flat.push_back(index_exprt{
          op,
          from_integer(j, inner_type.size().type()),
          inner_type.element_type()});
      }
    }
    else
    {
      flat.push_back(std::move(op));
    }
  }
  typet ft = expr.type();
  flatten_type(ft);
  expr = array_exprt{std::move(flat), to_array_type(ft)};
  return true;
}

static void flatten_expr(exprt &expr)
{
  if(!have_to_flatten_expr(expr))
    return;

  bool changed = false;
  while(try_flatten_double_index(expr) || try_flatten_nested_with(expr) ||
        try_flatten_array_constant(expr))
  {
    changed = true;
  }

  for(auto &op : expr.operands())
    flatten_expr(op);

  if(changed || expr.id() == ID_symbol)
    flatten_type(expr.type());
}

void flatten_nested_arrays(goto_modelt &goto_model)
{
  bool found = false;
  for(const auto &e : goto_model.symbol_table)
  {
    if(
      have_to_flatten(e.second.type) ||
      (!e.second.value.is_nil() && have_to_flatten_expr(e.second.value)))
    {
      found = true;
      break;
    }
  }
  if(!found)
    return;

  for(auto &gf : goto_model.goto_functions.function_map)
  {
    for(auto &inst : gf.second.body.instructions)
    {
      inst.transform(
        [](exprt e)
        {
          flatten_expr(e);
          return e;
        });
      if(inst.is_decl())
        flatten_type(inst.decl_symbol().type());
      else if(inst.is_dead())
        flatten_type(inst.dead_symbol().type());
    }
  }

  for(auto it = goto_model.symbol_table.begin();
      it != goto_model.symbol_table.end();
      ++it)
  {
    auto &s = it.get_writeable_symbol();
    if(
      have_to_flatten(s.type) ||
      (!s.value.is_nil() && have_to_flatten_expr(s.value)))
    {
      flatten_expr(s.value);
      flatten_type(s.type);
    }
  }
}
