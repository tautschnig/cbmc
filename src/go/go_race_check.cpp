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

#include <algorithm>
#include <cstdlib>
#include <map>
#include <unordered_set>
#include <vector>

static const char *const write_flags_id = "go::$race_w";
static const char *const read_flags_id = "go::$race_r";

/// How the accesses of a function are instrumented.
enum class modet
{
  /// Not at all (not Go, or the check's own functions).
  NONE,
  /// Every access is announced and checked: the program, the runtime
  /// helpers (cprover/rt) and the front-end's generated functions.
  CHECKED,
  /// The models of sync and sync/atomic, which synchronise: the accesses
  /// through parameters in their atomic sections are announced without
  /// checks (they race with plain accesses, not with each other), and
  /// outside atomic sections only rt.RaceRead/RaceWrite are accesses (as
  /// gc's annotations of the real implementations).
  MODEL
};

static modet mode_of(const irep_idt &function_id)
{
  const std::string id = id2string(function_id);
  if(id.rfind("go::", 0) != 0 || id == "go::$race_fields")
    return modet::NONE;
  if(id.rfind("go::sync.", 0) == 0 || id.rfind("go::sync/atomic.", 0) == 0)
    return modet::MODEL;
  return modet::CHECKED;
}

/// The local variables whose address is taken (SPIKE: set by go_race_check
/// for the duration of the pass).
static std::unordered_set<irep_idt> address_taken;

/// Adds the symbols whose address \p expr takes to address_taken.
static void find_address_taken(const exprt &expr)
{
  expr.visit_pre(
    [](const exprt &e)
    {
      if(e.id() != ID_address_of)
        return;
      const exprt *object = &to_address_of_expr(e).object();
      while(object->id() == ID_member || object->id() == ID_index)
        object = &object->operands().front();
      if(object->id() == ID_symbol)
        address_taken.insert(to_symbol_expr(*object).get_identifier());
    });
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
  if(s->is_type || s->type.id() == ID_code)
    return false;
  // A local whose address is taken may be accessed through the address by
  // another goroutine.
  if(address_taken.count(id) != 0)
    return true;
  return s->is_static_lifetime && !s->is_thread_local;
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

/// If \p lhs, assigned in an atomic section, is the effect of a channel
/// operation that gc's race detector takes as an access of the channel
/// (runtime/chan.go: closechan writes it, chansend reads it), the channel,
/// a symbol that holds it during the section, else nil. These are, in
/// go_channels.cpp, `c.closed = 1` of close, and the write of the value
/// into the buffer of a send; \p write is set to the kind of access.
static exprt chan_of_operation(const exprt &lhs, bool &write)
{
  const auto chan_of_field = [](const exprt &expr, const char *name) -> exprt
  {
    if(
      expr.id() != ID_member ||
      to_member_expr(expr).get_component_name() != name)
    {
      return nil_exprt{};
    }
    const exprt &compound = to_member_expr(expr).compound();
    if(compound.id() != ID_dereference)
      return nil_exprt{};
    const exprt &chan = to_dereference_expr(compound).pointer();
    if(chan.id() != ID_symbol || !is_go_chan(chan.type()))
      return nil_exprt{};
    return chan;
  };
  const exprt closed = chan_of_field(lhs, "closed");
  if(closed.is_not_nil())
  {
    write = true;
    return closed;
  }
  if(lhs.id() != ID_dereference)
    return nil_exprt{};
  exprt pointer = skip_typecasts(to_dereference_expr(lhs).pointer());
  if(pointer.id() == ID_plus && pointer.operands().size() == 2)
    pointer = skip_typecasts(to_plus_expr(pointer).op0());
  write = false;
  return chan_of_field(pointer, "buf");
}

/// Whether \p lvalue is accessed through a parameter: `*addr`, `x.v` of a
/// method of a pointer receiver x, the accesses of sync/atomic's model.
static bool is_through_parameter(const namespacet &ns, const exprt &lvalue)
{
  if(lvalue.id() == ID_member)
    return is_through_parameter(ns, to_member_expr(lvalue).compound());
  if(lvalue.id() == ID_index)
    return is_through_parameter(ns, to_index_expr(lvalue).array());
  if(lvalue.id() != ID_dereference)
    return false;
  const exprt pointer = skip_typecasts(to_dereference_expr(lvalue).pointer());
  if(pointer.id() != ID_symbol)
    return false;
  const symbolt *symbol;
  return !ns.lookup(to_symbol_expr(pointer).get_identifier(), symbol) &&
         symbol->is_parameter;
}

static void leaves_of(
  const namespacet &ns,
  const exprt &lvalue,
  std::size_t &budget,
  std::vector<exprt> &leaves);

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
    // The entries a map search reads (go_maps.cpp, map_find) are covered by
    // the operation's read of the map's header.
    if(expr.id() == ID_dereference)
    {
      exprt pointer = skip_typecasts(to_dereference_expr(expr).pointer());
      if(pointer.id() == ID_plus && pointer.operands().size() == 2)
        pointer = skip_typecasts(to_plus_expr(pointer).op0());
      if(pointer.type().get_bool(ID_C_go_map_entries))
        return;
    }
    // The fields of a channel's header are the runtime's (go_channels.cpp);
    // the operations on the channel are accesses of it (chan_of_operation).
    if(
      expr.id() == ID_member &&
      to_member_expr(expr).compound().id() == ID_dereference &&
      is_go_chan(
        to_dereference_expr(to_member_expr(expr).compound()).pointer().type()))
    {
      collect(
        ns,
        to_dereference_expr(to_member_expr(expr).compound()).pointer(),
        false,
        guard,
        accesses);
      return;
    }
    const exprt map = map_of_part(expr);
    if(map.is_not_nil())
    {
      accesses.push_back({dereference_exprt{map}, write, guard});
      collect(ns, map, false, guard, accesses);
      return;
    }
    // Values of size zero (empty structs, zero-length arrays) have no
    // memory to race on; nor do functions (`*(f.code)`).
    if(expr.type().id() == ID_code)
      return;
    const auto size = pointer_offset_size(expr.type(), ns);
    if(size.has_value() && *size == 0)
      return;
    // Values of size zero do not get here; scalar parts each get a flag.
    std::size_t budget = 64;
    std::vector<exprt> leaves;
    leaves_of(ns, expr, budget, leaves);
    for(const auto &leaf : leaves)
      accesses.push_back({leaf, write, guard});
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

/// The locations of an access of \p lvalue, appended to \p leaves: the
/// first bytes of its scalar parts (struct members, array elements), so
/// that `s = S{}` conflicts with `s.f.x = 1`. Arrays of more than
/// max_elements elements, or beyond \p budget leaves, are keyed by their
/// first byte (SPIKE: a race on a later element of a large array, as on
/// BigKey's [1000]*int, is missed).
static void leaves_of(
  const namespacet &ns,
  const exprt &lvalue,
  std::size_t &budget,
  std::vector<exprt> &leaves)
{
  static const std::size_t max_elements = 32;
  const typet &type = lvalue.type();
  if(budget > 1 && (type.id() == ID_struct || type.id() == ID_struct_tag))
  {
    const struct_typet &struct_type =
      type.id() == ID_struct_tag ? ns.follow_tag(to_struct_tag_type(type))
                                 : to_struct_type(type);
    std::vector<exprt> members;
    for(const auto &component : struct_type.components())
    {
      if(component.get_is_padding())
        continue;
      const auto size = pointer_offset_size(component.type(), ns);
      if(size.has_value() && *size == 0)
        continue;
      members.push_back(member_exprt{lvalue, component});
    }
    if(!members.empty() && members.size() <= budget)
    {
      budget -= members.size();
      for(const auto &member : members)
      {
        ++budget;
        leaves_of(ns, member, budget, leaves);
      }
      return;
    }
  }
  if(budget > 1 && type.id() == ID_array)
  {
    const auto &array_type = to_array_type(type);
    const auto size = numeric_cast<mp_integer>(array_type.size());
    if(
      size.has_value() && *size > 0 && *size <= max_elements && *size <= budget)
    {
      const std::size_t n = numeric_cast_v<std::size_t>(*size);
      budget -= n;
      for(std::size_t i = 0; i < n; ++i)
      {
        ++budget;
        leaves_of(
          ns,
          index_exprt{lvalue, from_integer(i, array_type.index_type())},
          budget,
          leaves);
      }
      return;
    }
  }
  if(budget > 0)
    --budget;
  leaves.push_back(lvalue);
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

  // Set by shadow memory when it cannot resolve an object
  // (SHADOW_MEMORY_INCOMPLETE in goto-symex/shadow_memory.h).
  const symbol_exprt incomplete{
    CPROVER_PREFIX "shadow_memory_incomplete", bool_typet{}};
  if(!goto_model.symbol_table.has_symbol(incomplete.get_identifier()))
  {
    symbolt symbol{incomplete.get_identifier(), bool_typet{}, "C"};
    symbol.base_name = incomplete.get_identifier();
    symbol.is_static_lifetime = true;
    symbol.is_lvalue = true;
    symbol.value = false_exprt{};
    goto_model.symbol_table.add(symbol);
  }

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
      body.insert_before(
        body.instructions.begin(),
        goto_programt::make_assignment(incomplete, false_exprt{}));
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

  address_taken.clear();
  for(const auto &f : goto_model.goto_functions.function_map)
  {
    for(const auto &instruction : f.second.body.instructions)
    {
      instruction.apply([](const exprt &e) { find_address_taken(e); });
    }
  }

  std::size_t instrumented = 0;
  for(auto &f : goto_model.goto_functions.function_map)
  {
    const modet mode = mode_of(f.first);
    if(mode == modet::NONE)
      continue;
    const bool checked = mode == modet::CHECKED;
    goto_programt &body = f.second.body;

    // The instrumentation of accesses at location, made only if their guards
    // hold, with checks if with_checks.
    const auto instrument = [&](
                              const std::vector<accesst> &accesses,
                              bool with_checks,
                              const source_locationt &location)
    {
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
      // The flag of kind flags of lvalue, as a condition.
      const auto flag = [&](const exprt &flags, const exprt &lvalue) -> exprt
      {
        if(!use_shadow)
          return index_exprt{flags, flag_index(lvalue)};
        const char *name = flags == write_flags ? "W" : "R";
        symbolt &tmp = get_fresh_aux_symbol(
          signedbv_typet{32},
          id2string(f.first),
          "race$",
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
      const auto check = [&](const exprt &condition, const char *what)
      {
        source_locationt annotated = location;
        annotated.set_property_class("data-race");
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
      const auto flags_of = [&](const accesst &a)
      { return a.write ? write_flags : read_flags; };
      // Setting a flag updates the shadow memory (or the flag array) of the
      // object as a whole: an update of another goroutine in between would
      // be lost, and so a flag that was reset could be set again.
      const auto atomically = [&](auto &&emit)
      {
        before.add(goto_programt::make_atomic_begin(location));
        emit();
        before.add(goto_programt::make_atomic_end(location));
      };
      atomically(
        [&]
        {
          for(const auto &a : accesses)
            guarded(a.guard, [&] { set_flag(flags_of(a), a.lvalue, true); });
        });
      atomically(
        [&]
        {
          for(const auto &a : accesses)
            guarded(a.guard, [&] { set_flag(flags_of(a), a.lvalue, false); });
        });
      if(with_checks)
      {
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
                check(and_exprt{not_exprt{written}, not_exprt{read}}, "write");
              }
              else
                check(not_exprt{flag(write_flags, a.lvalue)}, "read");
            });
        }
      }
      if(use_shadow)
      {
        // Shadow memory that cannot resolve an accessed object (its value
        // set is unknown, or the object has no shadow memory) loses the
        // flags: say so rather than miss a race.
        source_locationt annotated = location;
        annotated.set_property_class("data-race");
        annotated.set_comment(
          "data race check: shadow memory does not resolve the accessed "
          "object");
        before.add(
          goto_programt::make_assertion(not_exprt{incomplete}, annotated));
      }
      ++instrumented;
      return before;
    };
    // Jumps to the instruction at target must reach its instrumentation (as
    // the branch of a dispatcher of function values to the call does): the
    // instrumentation takes its place, and it follows. The result is the
    // instruction.
    const auto insert_before =
      [&body](goto_programt::targett target, goto_programt &before)
    {
      const auto inserted = before.instructions.size();
      body.insert_before_swap(target, before);
      std::advance(target, inserted);
      return target;
    };

    // For each instruction, the ATOMIC_BEGIN of the outermost atomic
    // section it is in (end() if none), by a forward data flow: sections
    // may have several exits on different paths (a panic path, say), so
    // that their extent is not a range of instructions.
    std::map<const goto_programt::instructiont *, goto_programt::targett>
      section_of;
    {
      using statet = std::pair<std::size_t, goto_programt::targett>;
      std::map<const goto_programt::instructiont *, statet> before_state;
      std::vector<std::pair<goto_programt::targett, statet>> work;
      if(!body.instructions.empty())
        work.push_back(
          {body.instructions.begin(), {0, body.instructions.end()}});
      while(!work.empty())
      {
        auto [target, in] = work.back();
        work.pop_back();
        if(!before_state.emplace(&*target, in).second)
          continue;
        statet out = in;
        if(target->is_atomic_begin())
        {
          if(out.first++ == 0)
            out.second = target;
        }
        else if(target->is_atomic_end() && out.first > 0)
        {
          if(--out.first == 0)
            out.second = body.instructions.end();
        }
        for(const auto &successor : body.get_successors(target))
          work.push_back({successor, out});
      }
      for(const auto &entry : before_state)
        section_of.emplace(entry.first, entry.second.second);
    }
    const auto section_at = [&](goto_programt::targett target)
    {
      auto found = section_of.find(&*target);
      return found == section_of.end() ? body.instructions.end()
                                       : found->second;
    };
    // The accesses that are made just before each section: channel
    // operations as gc's race detector sees them, and atomic operations.
    std::map<
      const goto_programt::instructiont *,
      std::pair<goto_programt::targett, std::vector<accesst>>>
      section_accesses;
    // Generated instructions (e.g., of map operations) may have no location:
    // their checks get the last one before them.
    source_locationt last_location;
    for(auto it = body.instructions.begin(); it != body.instructions.end();
        ++it)
    {
      if(!it->source_location().get_file().empty())
        last_location = it->source_location();
      if(it->is_atomic_begin() || it->is_atomic_end())
        continue;
      const goto_programt::targett section = section_at(it);
      if(section != body.instructions.end())
      {
        auto &entry = section_accesses[&*section];
        entry.first = section;
        if(it->is_assign())
        {
          if(checked)
          {
            bool write = false;
            const exprt chan = chan_of_operation(it->assign_lhs(), write);
            if(chan.is_not_nil())
            {
              entry.second.push_back(
                {dereference_exprt{chan}, write, true_exprt{}});
            }
          }
          else
          {
            std::vector<accesst> accesses;
            collect(ns, it->assign_lhs(), true, true_exprt{}, accesses);
            collect(ns, it->assign_rhs(), false, true_exprt{}, accesses);
            for(const auto &a : accesses)
              if(is_through_parameter(ns, a.lvalue))
                entry.second.push_back(a);
          }
        }
        continue;
      }
      std::vector<accesst> accesses;
      const exprt always = true_exprt{};
      // rt.RaceRead/RaceWrite (go_convert.cpp): calls of go::$race_read and
      // go::$race_write with the variable's address.
      const irep_idt callee =
        it->is_function_call() && it->call_function().id() == ID_symbol
          ? to_symbol_expr(it->call_function()).get_identifier()
          : irep_idt{};
      if(
        (callee == "go::$race_read" || callee == "go::$race_write") &&
        it->call_arguments().size() == 1)
      {
        collect(
          ns,
          dereference_exprt{skip_typecasts(it->call_arguments().front())},
          callee == "go::$race_write",
          always,
          accesses);
      }
      else if(!checked)
        continue;
      else if(it->is_assign())
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
      // One access per location and guard: a write covers a read.
      {
        std::vector<accesst> unique;
        for(const auto &a : accesses)
        {
          auto same = std::find_if(
            unique.begin(),
            unique.end(),
            [&](const accesst &u)
            { return u.lvalue == a.lvalue && u.guard == a.guard; });
          if(same == unique.end())
            unique.push_back(a);
          else
            same->write = same->write || a.write;
        }
        accesses.swap(unique);
      }

      goto_programt before = instrument(
        accesses,
        true,
        it->source_location().get_file().empty() ? last_location
                                                 : it->source_location());
      it = insert_before(it, before);
    }
    for(auto &entry : section_accesses)
    {
      if(entry.second.second.empty())
        continue;
      goto_programt before = instrument(
        entry.second.second, checked, entry.second.first->source_location());
      insert_before(entry.second.first, before);
    }
    body.update();
  }
  goto_model.goto_functions.update();
  log.status() << "Go data-race check: instrumented " << instrumented
               << " instructions" << messaget::eom;
}
