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
/// The flags are two shared unbounded arrays indexed by the accessed
/// address (object and offset). Accesses in atomic sections (the models of
/// channels, sync and sync/atomic) synchronise: they are not announced.
/// Spike limitations: an access is keyed by its first byte only; accesses
/// of the runtime and sync models are not checked at all.

#include "go_race_check.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
#include <util/string_constant.h>
#include <util/fresh_symbol.h>
#include <util/message.h>
#include <util/pointer_expr.h>
#include <util/pointer_offset_size.h>
#include <util/std_expr.h>
#include <util/symbol_table_base.h>

#include <goto-programs/goto_model.h>

#include <linking/static_lifetime_init.h>

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

/// The accesses of \p expr, which is read (or written if \p write):
/// maximal shared lvalues, and the reads in their addresses.
static void collect(
  const namespacet &ns,
  const exprt &expr,
  bool write,
  std::vector<exprt> &reads,
  std::vector<exprt> &writes)
{
  if(expr.id() == ID_address_of)
  {
    // Only the address: the operands of index expressions and pointers.
    const exprt &object = to_address_of_expr(expr).object();
    if(object.id() == ID_dereference)
      collect(ns, to_dereference_expr(object).pointer(), false, reads, writes);
    else if(object.id() == ID_index)
    {
      collect(ns, address_of_exprt{to_index_expr(object).array()}, false, reads,
              writes);
      collect(ns, to_index_expr(object).index(), false, reads, writes);
    }
    else if(object.id() == ID_member)
      collect(
        ns,
        address_of_exprt{to_member_expr(object).compound()},
        false,
        reads,
        writes);
    return;
  }
  if(expr.id() == ID_side_effect || expr.id() == ID_pointer_object ||
     expr.id() == ID_pointer_offset || expr.id() == ID_object_size)
  {
    return;
  }
  if(is_shared_lvalue(ns, expr))
  {
    // Values of size zero (empty structs, zero-length arrays) have no
    // memory to race on.
    const auto size = pointer_offset_size(expr.type(), ns);
    if(size.has_value() && *size == 0)
      return;
    (write ? writes : reads).push_back(expr);
    // The reads that compute its address.
    collect(ns, address_of_exprt{expr}, false, reads, writes);
    return;
  }
  for(const auto &op : expr.operands())
    collect(ns, op, false, reads, writes);
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
  const array_typet type{
    bool_typet{}, infinity_exprt{unsignedbv_typet{64}}};
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
    goto_model.symbol_table,
    CPROVER_PREFIX "field_decl_global",
    empty_typet{});
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
    auto init = goto_model.goto_functions.function_map.find(
      INITIALIZE_FUNCTION);
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

      std::vector<exprt> reads, writes;
      if(it->is_assign())
      {
        collect(ns, it->assign_lhs(), true, reads, writes);
        collect(ns, it->assign_rhs(), false, reads, writes);
      }
      else if(it->is_function_call())
      {
        if(it->call_lhs().is_not_nil())
          collect(ns, it->call_lhs(), true, reads, writes);
        for(const auto &argument : it->call_arguments())
          collect(ns, argument, false, reads, writes);
        collect(ns, it->call_function(), false, reads, writes);
      }
      else if(it->is_set_return_value())
        collect(ns, it->return_value(), false, reads, writes);
      else if(
        it->is_goto() || it->is_assume() || it->is_assert())
        collect(ns, it->condition(), false, reads, writes);
      else if(it->is_other())
        collect(ns, it->code(), false, reads, writes);
      if(reads.empty() && writes.empty())
        continue;

      ++instrumented;
      const source_locationt &location = it->source_location();
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
      for(const auto &w : writes)
        announce(write_flags, w);
      for(const auto &r : reads)
        announce(read_flags, r);
      for(const auto &w : writes)
        withdraw(write_flags, w);
      for(const auto &r : reads)
        withdraw(read_flags, r);
      const auto check = [&](const exprt &condition, const exprt &lvalue,
                             const char *what)
      {
        source_locationt annotated = location;
        annotated.set_property_class("data-race");
        (void)lvalue;
        annotated.set_comment(std::string{"data race ("} + what + ")");
        before.add(goto_programt::make_assertion(condition, annotated));
      };
      for(const auto &w : writes)
      {
        const exprt written = flag(write_flags, w);
        const exprt read = flag(read_flags, w);
        check(and_exprt{not_exprt{written}, not_exprt{read}}, w, "write");
      }
      for(const auto &r : reads)
        check(not_exprt{flag(write_flags, r)}, r, "read");
      body.destructive_insert(it, before);
    }
    body.update();
  }
  goto_model.goto_functions.update();
  log.status() << "Go data-race check: instrumented " << instrumented
               << " instructions" << messaget::eom;
}
