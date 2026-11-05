/*******************************************************************\

Module: Go front-end: data races (spike)

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Data races (spike, see doc/architectural/go-data-races.md): before each
/// instruction of a Go function that accesses memory that other goroutines
/// may access (package variables, and anything through a pointer), the
/// access is announced in a flag per location and kind (read, write),
/// which is reset right away; then the flags of the conflicting kinds are
/// checked. Another goroutine scheduled between the set and the reset
/// fails the check of a conflicting access: that is, two conflicting
/// accesses can be adjacent in some sequentially consistent interleaving,
/// which is a data race (GoMem §Memory_model, DRF-SC).
///
/// The flags are fields of shadow memory (or, with GO_RACE_FLAGS=arrays,
/// two shared unbounded arrays indexed by the accessed address). Accesses
/// in atomic sections (the models of
/// channels, sync and sync/atomic) synchronise: they are not announced.
/// An access of an entry or the header of a map is one of the map's header,
/// as in gc. Accesses of values of size zero are none; `_ = x` reads x.
/// Accesses in operands of `&&`, `||` and `?:` are made only
/// when the operand is evaluated.
/// Spike limitations: an access is keyed by its first byte only; accesses
/// of the runtime and sync models are not checked at all.

#include "go_race_check.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/fresh_symbol.h>
#include <util/message.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/std_expr.h>
#include <util/string_constant.h>
#include <util/symbol_table_base.h>

#include <goto-programs/goto_model.h>

#include <linking/static_lifetime_init.h>

#include "go_types.h"

#include <cstdlib>
#include <vector>

static const char *const write_flags_id = "go::$race_w";
static const char *const read_flags_id = "go::$race_r";

/// Whether \p function_id is a Go function whose accesses are checked.
static bool is_checked_function(const irep_idt &function_id)
{
  const std::string id = id2string(function_id);
  if(id.rfind("go::", 0) != 0)
    return false;
  // The runtime and the models that synchronise; the front-end's generated
  // functions (dispatchers, equality, panics).
  for(const char *prefix :
      {"go::cprover/rt.", "go::sync.", "go::sync/atomic.", "go::$"})
  {
    if(id.rfind(prefix, 0) == 0)
      return false;
  }
  return true;
}

/// Whether \p symbol may be accessed by more than one goroutine.
static bool is_shared_symbol(const namespacet &ns, const symbol_exprt &symbol)
{
  const irep_idt &id = symbol.get_identifier();
  const std::string name = id2string(id);
  // The front-end's own state (panics, deadlock detection, ...) and the
  // flags themselves.
  if(name.rfind("go::$", 0) == 0 || name.rfind("__CPROVER", 0) == 0)
    return false;
  const symbolt *s;
  if(ns.lookup(id, s))
    return false;
  return s->is_static_lifetime && !s->is_thread_local && !s->is_type &&
         s->type.id() != ID_code;
}

/// Whether \p expr is an lvalue in memory that other goroutines may
/// access.
static bool is_shared_lvalue(const namespacet &ns, const exprt &expr)
{
  if(expr.id() == ID_dereference)
    return true;
  if(expr.id() == ID_symbol)
    return is_shared_symbol(ns, to_symbol_expr(expr));
  if(expr.id() == ID_member)
    return is_shared_lvalue(ns, to_member_expr(expr).compound());
  if(expr.id() == ID_index)
    return is_shared_lvalue(ns, to_index_expr(expr).array());
  return false;
}

/// An access of shared memory, made only if \p guard holds.
struct accesst
{
  exprt lvalue;
  bool write;
  exprt guard;
};

static exprt skip_typecasts(exprt expr)
{
  while(expr.id() == ID_typecast)
    expr = to_typecast_expr(expr).op();
  return expr;
}

/// If \p expr is a part of a map's representation (go_maps.cpp: a header
/// field, or an entry of the keys or vals arrays), the map, else nil. As in
/// gc, whose map operations announce an access of the map as a whole
/// (runtime.mapaccess1 calls racereadpc on the map), such an access is one
/// of the header: the entry arrays are replaced as the map grows, and
/// flagging their entries would key accesses on locations that change.
static exprt map_of_part(const exprt &expr)
{
  if(expr.id() == ID_member)
  {
    const exprt compound = to_member_expr(expr).compound();
    if(compound.id() == ID_dereference)
    {
      const exprt &map = to_dereference_expr(compound).pointer();
      if(is_go_map(map.type()))
        return map;
    }
    return nil_exprt{};
  }
  if(expr.id() != ID_dereference)
    return nil_exprt{};
  exprt pointer = skip_typecasts(to_dereference_expr(expr).pointer());
  if(pointer.id() == ID_plus && pointer.operands().size() == 2)
    pointer = skip_typecasts(to_plus_expr(pointer).op0());
  if(pointer.id() != ID_member)
    return nil_exprt{};
  const irep_idt &name = to_member_expr(pointer).get_component_name();
  if(name != "keys" && name != "vals")
    return nil_exprt{};
  return map_of_part(pointer);
}

/// The accesses of \p expr, which is read (or written if \p write) if
/// \p guard holds: maximal shared lvalues, and the reads in their
/// addresses. The operands of `&&`, `||` and `?:` after the first are
/// evaluated only conditionally (Go1.27 §Logical_operators).
static void collect(
  const namespacet &ns,
  const exprt &expr,
  bool write,
  const exprt &guard,
  std::vector<accesst> &accesses)
{
  if(expr.id() == ID_address_of)
  {
    // Only the address: the operands of index expressions and pointers.
    const exprt &object = to_address_of_expr(expr).object();
    if(object.id() == ID_dereference)
      collect(
        ns, to_dereference_expr(object).pointer(), false, guard, accesses);
    else if(object.id() == ID_index)
    {
      collect(
        ns,
        address_of_exprt{to_index_expr(object).array()},
        false,
        guard,
        accesses);
      collect(ns, to_index_expr(object).index(), false, guard, accesses);
    }
    else if(object.id() == ID_member)
      collect(
        ns,
        address_of_exprt{to_member_expr(object).compound()},
        false,
        guard,
        accesses);
    return;
  }
  if(
    expr.id() == ID_side_effect || expr.id() == ID_pointer_object ||
    expr.id() == ID_pointer_offset || expr.id() == ID_object_size)
  {
    return;
  }
  if(is_shared_lvalue(ns, expr))
  {
    const exprt map = map_of_part(expr);
    if(map.is_not_nil())
    {
      accesses.push_back({dereference_exprt{map}, write, guard});
      collect(ns, map, false, guard, accesses);
      return;
    }
    // Values of size zero (empty structs, zero-length arrays) have no
    // memory to race on.
    const auto size = pointer_offset_size(expr.type(), ns);
    if(size.has_value() && *size == 0)
      return;
    accesses.push_back({expr, write, guard});
    // The reads that compute its address.
    collect(ns, address_of_exprt{expr}, false, guard, accesses);
    return;
  }
  if(expr.id() == ID_and || expr.id() == ID_or)
  {
    // a && b && c: b only if a, c only if a && b (|| with negations).
    exprt g = guard;
    for(const auto &op : expr.operands())
    {
      collect(ns, op, false, g, accesses);
      g = and_exprt{g, expr.id() == ID_and ? op : not_exprt{op}};
    }
    return;
  }
  if(expr.id() == ID_if)
  {
    const if_exprt &if_expr = to_if_expr(expr);
    collect(ns, if_expr.cond(), false, guard, accesses);
    collect(
      ns,
      if_expr.true_case(),
      false,
      and_exprt{guard, if_expr.cond()},
      accesses);
    collect(
      ns,
      if_expr.false_case(),
      false,
      and_exprt{guard, not_exprt{if_expr.cond()}},
      accesses);
    return;
  }
  for(const auto &op : expr.operands())
    collect(ns, op, false, guard, accesses);
}

/// The index of the flags of the location that \p lvalue denotes.
static exprt flag_index(const exprt &lvalue)
{
  const unsignedbv_typet index_type{64};
  const address_of_exprt address{lvalue};
  const exprt object = typecast_exprt{
    pointer_object_exprt{address, unsignedbv_typet{64}}, index_type};
  const exprt offset = typecast_exprt{
    pointer_offset_exprt{address, signedbv_typet{64}}, index_type};
  return bitor_exprt{
    shl_exprt{object, from_integer(32, index_type)},
    bitand_exprt{offset, from_integer(0xffffffff, index_type)}};
}

static symbol_exprt
flags_symbol(symbol_table_baset &symbol_table, const char *id)
{
  const array_typet type{bool_typet{}, infinity_exprt{unsignedbv_typet{64}}};
  if(!symbol_table.has_symbol(id))
  {
    symbolt symbol{id, type, "go"};
    symbol.base_name = id;
    symbol.is_static_lifetime = true;
    symbol.is_lvalue = true;
    symbol.value = array_of_exprt{false_exprt{}, type};
    symbol_table.add(symbol);
  }
  return symbol_exprt{id, type};
}

/// A function symbol of the shadow-memory API (goto-symex implements it).
static symbol_exprt shadow_function(
  symbol_table_baset &symbol_table,
  const irep_idt &id,
  const typet &return_type)
{
  code_typet type{{}, return_type};
  type.make_ellipsis();
  if(!symbol_table.has_symbol(id))
  {
    symbolt symbol{id, type, "C"};
    symbol.base_name = id;
    symbol_table.add(symbol);
  }
  return symbol_exprt{id, type};
}

/// The address of the first byte of \p lvalue: shadow memory replicates a
/// value over the bytes of the pointee's type, which does not work for
/// types with pointers (SPIKE: only the first byte is flagged).
static exprt byte_address(const exprt &lvalue)
{
  return typecast_exprt{
    address_of_exprt{lvalue}, pointer_type(unsigned_char_type())};
}

/// The name of shadow-memory field \p name, as the API takes it.
static exprt field_name(const std::string &name)
{
  const string_constantt text{name};
  return typecast_exprt{
    address_of_exprt{index_exprt{text, from_integer(0, c_index_type())}},
    pointer_type(empty_typet{})};
}

static bool may_start_goroutine(const goto_modelt &goto_model)
{
  for(const auto &f : goto_model.goto_functions.function_map)
    for(const auto &instruction : f.second.body.instructions)
      if(instruction.is_start_thread())
        return true;
  return false;
}

void go_race_check(goto_modelt &goto_model, message_handlert &message_handler)
{
  messaget log{message_handler};
  if(!may_start_goroutine(goto_model))
    return;
  const namespacet ns{goto_model.symbol_table};
  // SPIKE: the flags in shadow memory (option A) or in two global arrays
  // (option B, with GO_RACE_FLAGS=arrays).
  const char *mode = std::getenv("GO_RACE_FLAGS");
  const bool use_shadow = mode == nullptr || std::string{mode} != "arrays";
  const symbol_exprt set_field = shadow_function(
    goto_model.symbol_table, CPROVER_PREFIX "set_field", empty_typet{});
  const symbol_exprt get_field = shadow_function(
    goto_model.symbol_table, CPROVER_PREFIX "get_field", signedbv_typet{32});
  const symbol_exprt field_decl = shadow_function(
    goto_model.symbol_table, CPROVER_PREFIX "field_decl_global", empty_typet{});
  // Bodiless, as the C library's declarations: goto-symex implements them.
  for(const auto &function : {set_field, get_field, field_decl})
    goto_model.goto_functions.function_map[function.get_identifier()];
  const symbol_exprt write_flags =
    flags_symbol(goto_model.symbol_table, write_flags_id);
  const symbol_exprt read_flags =
    flags_symbol(goto_model.symbol_table, read_flags_id);

  // The flags start clear (__CPROVER_initialize was generated before they
  // were added).
  {
    auto init =
      goto_model.goto_functions.function_map.find(INITIALIZE_FUNCTION);
    if(init != goto_model.goto_functions.function_map.end())
    {
      goto_programt &body = init->second.body;
      for(const auto &flags : {write_flags, read_flags})
      {
        body.insert_before(
          body.instructions.begin(),
          goto_programt::make_assignment(
            flags, array_of_exprt{false_exprt{}, to_array_type(flags.type())}));
      }
    }
  }

  // The fields of shadow memory, which goto-symex collects from calls of
  // __CPROVER_field_decl_global anywhere before it starts: in a function
  // that is never called (executing the call fails a "no body" property).
  if(use_shadow)
  {
    const irep_idt fields_id = "go::$race_fields";
    goto_programt body;
    for(const char *name : {"W", "R"})
    {
      body.add(goto_programt::make_function_call(code_function_callt{
        field_decl,
        {field_name(name),
         // As the C front-end writes `(_Bool)0`, which shadow memory takes
         // as zero-initialisation.
         typecast_exprt{
           from_integer(0, signed_int_type()), c_bool_typet{8}}}}));
    }
    body.add(goto_programt::make_end_function());
    const code_typet type{{}, empty_typet{}};
    symbolt symbol{fields_id, type, "go"};
    symbol.base_name = "$race_fields";
    goto_model.symbol_table.insert(symbol);
    goto_functiont &function =
      goto_model.goto_functions.function_map[fields_id];
    function.body.swap(body);
  }

  std::size_t instrumented = 0;
  for(auto &f : goto_model.goto_functions.function_map)
  {
    if(!is_checked_function(f.first))
      continue;
    goto_programt &body = f.second.body;
    std::size_t atomic_depth = 0;
    // Generated instructions (e.g., of map operations) may have no location:
    // their checks get the last one before them.
    source_locationt last_location;
    for(auto it = body.instructions.begin(); it != body.instructions.end();
        ++it)
    {
      if(it->is_atomic_begin())
      {
        ++atomic_depth;
        continue;
      }
      if(it->is_atomic_end())
      {
        if(atomic_depth > 0)
          --atomic_depth;
        continue;
      }
      if(atomic_depth > 0)
        continue;
      if(!it->source_location().get_file().empty())
        last_location = it->source_location();

      std::vector<accesst> accesses;
      const exprt always = true_exprt{};
      if(it->is_assign())
      {
        collect(ns, it->assign_lhs(), true, always, accesses);
        collect(ns, it->assign_rhs(), false, always, accesses);
      }
      else if(it->is_function_call())
      {
        if(it->call_lhs().is_not_nil())
          collect(ns, it->call_lhs(), true, always, accesses);
        for(const auto &argument : it->call_arguments())
          collect(ns, argument, false, always, accesses);
        collect(ns, it->call_function(), false, always, accesses);
      }
      else if(it->is_set_return_value())
        collect(ns, it->return_value(), false, always, accesses);
      else if(it->is_goto() || it->is_assume() || it->is_assert())
        collect(ns, it->condition(), false, always, accesses);
      else if(it->is_other())
      {
        // Including expression statements (`_ = x`), which gc's race
        // detector reports as reads.
        collect(ns, it->code(), false, always, accesses);
      }
      if(accesses.empty())
        continue;

      ++instrumented;
      const source_locationt &location =
        it->source_location().get_file().empty() ? last_location
                                                 : it->source_location();
      goto_programt before;
      const auto set_flag =
        [&](const exprt &flags, const exprt &lvalue, bool value)
      {
        if(use_shadow)
        {
          const char *name = flags == write_flags ? "W" : "R";
          before.add(goto_programt::make_function_call(
            code_function_callt{
              set_field,
              {byte_address(lvalue),
               field_name(name),
               from_integer(value ? 1 : 0, signedbv_typet{32})}},
            location));
          return;
        }
        before.add(goto_programt::make_assignment(
          index_exprt{flags, flag_index(lvalue)},
          value ? exprt{true_exprt{}} : exprt{false_exprt{}},
          location));
      };
      const auto announce = [&](const exprt &flags, const exprt &lvalue)
      { set_flag(flags, lvalue, true); };
      const auto withdraw = [&](const exprt &flags, const exprt &lvalue)
      { set_flag(flags, lvalue, false); };
      // The flag of kind flags of lvalue, as a condition.
      std::size_t tmp_counter = 0;
      const auto flag = [&](const exprt &flags, const exprt &lvalue) -> exprt
      {
        if(!use_shadow)
          return index_exprt{flags, flag_index(lvalue)};
        const char *name = flags == write_flags ? "W" : "R";
        symbolt &tmp = get_fresh_aux_symbol(
          signedbv_typet{32},
          id2string(f.first),
          "race$" + std::to_string(++tmp_counter),
          location,
          "go",
          goto_model.symbol_table);
        before.add(goto_programt::make_decl(tmp.symbol_expr(), location));
        before.add(goto_programt::make_function_call(
          code_function_callt{
            tmp.symbol_expr(),
            get_field,
            {byte_address(lvalue), field_name(name)}},
          location));
        return notequal_exprt{
          tmp.symbol_expr(), from_integer(0, signedbv_typet{32})};
      };
      const auto check =
        [&](const exprt &condition, const exprt &lvalue, const char *what)
      {
        source_locationt annotated = location;
        annotated.set_property_class("data-race");
        (void)lvalue;
        annotated.set_comment(std::string{"data race ("} + what + ")");
        before.add(goto_programt::make_assertion(condition, annotated));
      };
      // Each operation of an access only if the access is made.
      const auto guarded = [&](const exprt &guard, auto &&emit)
      {
        if(guard.is_true())
        {
          emit();
          return;
        }
        goto_programt::targett skip_jump = before.add(
          goto_programt::make_incomplete_goto(not_exprt{guard}, location));
        emit();
        goto_programt::targett skip =
          before.add(goto_programt::make_skip(location));
        skip_jump->complete_goto(skip);
      };
      for(const auto &a : accesses)
        guarded(
          a.guard,
          [&] { announce(a.write ? write_flags : read_flags, a.lvalue); });
      for(const auto &a : accesses)
        guarded(
          a.guard,
          [&] { withdraw(a.write ? write_flags : read_flags, a.lvalue); });
      for(const auto &a : accesses)
      {
        guarded(
          a.guard,
          [&]
          {
            if(a.write)
            {
              const exprt written = flag(write_flags, a.lvalue);
              const exprt read = flag(read_flags, a.lvalue);
              check(
                and_exprt{not_exprt{written}, not_exprt{read}},
                a.lvalue,
                "write");
            }
            else
              check(not_exprt{flag(write_flags, a.lvalue)}, a.lvalue, "read");
          });
      }
      body.destructive_insert(it, before);
    }
    body.update();
  }
  goto_model.goto_functions.update();
  log.status() << "Go data-race check: instrumented " << instrumented
               << " instructions" << messaget::eom;
}
