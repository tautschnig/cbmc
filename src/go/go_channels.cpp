/*******************************************************************\

Module: Go front-end: channels and select

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Channels (Go1.27 §Channel_types, §Send_statements, §Receive_operator,
/// §Close, §Select_statements). Goroutines are CBMC threads
/// (go_convertert::convert_go); a channel is a pointer to a header on the
/// heap (go_type_convertert::chan_type), which operations update in atomic
/// sections. An operation that cannot proceed blocks: it assumes, in its
/// atomic section, that it can (as the C library's pthread_mutex_lock
/// does), so that only the interleavings in which it can proceed continue.
///
/// Buffered channels keep a ring buffer of `cap` values. On an unbuffered
/// channel, a sender deposits its value in `buf[0]` (`count` is 1) with a
/// ticket (`sent`), and completes when a receiver has taken it (`recvd`
/// reaches the ticket); receivers count themselves in `waiting` while they
/// wait, so that a select can tell that a send can proceed.

#include <util/arith_tools.h>
#include <util/c_types.h>
#include <util/config.h>
#include <util/expr_initializer.h>
#include <util/pointer_expr.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "go_convert.h"
#include "go_parse_tree.h"

/// Field \p name of the header of channel \p chan.
static member_exprt field(const exprt &chan, const char *name)
{
  const struct_typet &header =
    to_struct_type(to_pointer_type(chan.type()).base_type());
  return member_exprt{dereference_exprt{chan}, header.get_component(name)};
}

static codet atomic_begin(const source_locationt &location)
{
  codet result{ID_atomic_begin};
  result.add_source_location() = location;
  return result;
}

static codet atomic_end(const source_locationt &location)
{
  codet result{ID_atomic_end};
  result.add_source_location() = location;
  return result;
}

static exprt as_bool(const exprt &flag)
{
  return typecast_exprt::conditional_cast(flag, bool_typet{});
}

static exprt int_constant(int value, const exprt &like)
{
  return from_integer(value, like.type());
}

/// The element of the buffer of \p chan at index \p index.
static dereference_exprt buffer_at(const exprt &chan, const exprt &index)
{
  return dereference_exprt{plus_exprt{
    field(chan, "buf"),
    typecast_exprt::conditional_cast(index, c_index_type())}};
}

symbol_exprt go_convertert::deadlock_variable(const std::string &name)
{
  const irep_idt id = "go::$" + name;
  const signedbv_typet type{config.go.int_width};
  if(!symbol_table.has_symbol(id))
  {
    symbolt symbol{id, type, "go"};
    symbol.base_name = "$" + name;
    symbol.pretty_name = symbol.base_name;
    symbol.is_static_lifetime = true;
    symbol.is_lvalue = true;
    symbol.module = parse_tree.package;
    // main is live from the start.
    symbol.value = from_integer(name == "live" ? 1 : 0, type);
    symbol_table.add(symbol);
  }
  return symbol_exprt{id, type};
}

void go_convertert::block_until(
  const exprt &ready,
  bool may_park,
  const source_locationt &location,
  code_blockt &dest)
{
  block_until([&ready]() { return ready; }, may_park, location, dest);
}

void go_convertert::block_until(
  const std::function<exprt()> &condition,
  bool may_park,
  const source_locationt &location,
  code_blockt &dest)
{
  // Go's runtime reports `fatal error: all goroutines are asleep -
  // deadlock!` when all goroutines are blocked. A goroutine that would
  // block may instead park (see park), and never continue, so that only
  // the executions in which it is blocked forever are affected; otherwise
  // it blocks as before, by assuming in the atomic section that it can
  // proceed. The parking has atomic sections of its own, before the
  // operation's: goto-symex's atomic sections must not span branches that
  // leave and re-enter them.
  if(may_park && deadlock_check)
    park(condition, code_blockt{}, location, dest);
  dest.add(atomic_begin(location));
  dest.add(code_assumet{condition()});
}

void go_convertert::park(
  const std::function<exprt()> &condition,
  const code_blockt &announce,
  const source_locationt &location,
  code_blockt &dest)
{
  // A goroutine that parks counts itself in $parked. Once all live
  // goroutines have parked ($parked = $live), no goroutine changes the
  // state any more; each then confirms that it still cannot proceed, and
  // when all have confirmed, the program is deadlocked. main stays live
  // after it returns, without parking, so that the program then ends
  // rather than deadlocks.
  const symbol_exprt live = deadlock_variable("live");
  const symbol_exprt parked = deadlock_variable("parked");
  const symbol_exprt confirmed = deadlock_variable("confirmed");
  const exprt one = from_integer(1, live.type());

  code_blockt park;
  park.add(atomic_begin(location));
  park.add(code_assumet{not_exprt{condition()}});
  for(const auto &statement : announce.statements())
    park.add(statement);
  park.add(code_frontend_assignt{parked, plus_exprt{parked, one}});
  park.add(atomic_end(location));
  park.add(atomic_begin(location));
  park.add(
    code_assumet{and_exprt{equal_exprt{parked, live}, not_exprt{condition()}}});
  park.add(code_frontend_assignt{confirmed, plus_exprt{confirmed, one}});
  source_locationt property = location;
  property.set_property_class("deadlock");
  property.set_comment("all goroutines are asleep - deadlock!");
  code_assertt deadlock{notequal_exprt{confirmed, live}};
  deadlock.add_source_location() = property;
  park.add(std::move(deadlock));
  park.add(atomic_end(location));
  park.add(code_assumet{false_exprt{}});
  dest.add(code_ifthenelset{
    side_effect_expr_nondett{bool_typet{}, location}, std::move(park)});
}

void go_convertert::goroutine_end(
  const source_locationt &location,
  code_blockt &dest)
{
  // A goroutine that ends is no longer live (see park).
  if(!deadlock_check)
    return;
  const symbol_exprt live = deadlock_variable("live");
  dest.add(atomic_begin(location));
  dest.add(code_frontend_assignt{
    live, minus_exprt{live, from_integer(1, live.type())}});
  dest.add(atomic_end(location));
}

exprt go_convertert::make_chan(
  const typet &type,
  const exprt &size,
  const source_locationt &location,
  code_blockt &dest)
{
  // Go1.27 §Making_slices_maps_and_channels: make(T, n) makes a "buffered
  // channel of type T, buffer size n"; "For slices and channels, if n is
  // negative or larger than m at run time, a run-time panic occurs." The
  // message is gc's (makechan: size out of range).
  const typet int_type = signedbv_typet{config.go.int_width};
  const exprt n = evaluate_once(
    typecast_exprt::conditional_cast(size, int_type), location, dest);
  add_panic(
    dest,
    binary_relation_exprt{n, ID_ge, from_integer(0, int_type)},
    "makechan",
    "makechan: size out of range",
    location);
  const typet &header = to_pointer_type(type).base_type();
  const symbol_exprt chan = new_temporary(type, location, dest);
  dest.add(code_frontend_assignt{
    chan, typecast_exprt{allocation(header, location), type}});
  const struct_typet &components = to_struct_type(header);
  exprt::operandst values;
  for(const auto &component : components.components())
  {
    auto zero = zero_initializer(component.type(), location, ns);
    CHECK_RETURN(zero.has_value());
    values.push_back(*zero);
  }
  dest.add(code_frontend_assignt{
    dereference_exprt{chan}, struct_exprt{std::move(values), header}});
  dest.add(code_frontend_assignt{field(chan, "cap"), n});
  // At least one element: unbuffered channels hand over values in it.
  const typet element = to_pointer_type(field(chan, "buf").type()).base_type();
  const exprt elements = if_exprt{
    equal_exprt{n, from_integer(0, int_type)}, from_integer(1, int_type), n};
  dest.add(code_frontend_assignt{
    field(chan, "buf"),
    allocate_elements(element, elements, true, location, dest)});
  return std::move(chan);
}

/// Whether a send on the non-nil channel \p chan can proceed now: Go1.27
/// §Send_statements: "A send on an unbuffered channel can proceed if a
/// receiver is ready. A send on a buffered channel can proceed if there is
/// room in the buffer. A send on a closed channel proceeds by causing a
/// run-time panic."
static exprt send_ready(const exprt &chan)
{
  const exprt cap = field(chan, "cap");
  const exprt count = field(chan, "count");
  return or_exprt{
    as_bool(field(chan, "closed")),
    if_exprt{
      binary_relation_exprt{cap, ID_gt, int_constant(0, cap)},
      binary_relation_exprt{count, ID_lt, cap},
      and_exprt{
        equal_exprt{count, int_constant(0, count)},
        binary_relation_exprt{
          field(chan, "waiting"), ID_gt, int_constant(0, count)}}}};
}

/// Whether a receive from the non-nil channel \p chan can proceed now:
/// §Receive_operator: "The expression blocks until a value is available."
/// "A receive operation on a closed channel can always proceed
/// immediately".
static exprt recv_ready(const exprt &chan)
{
  const exprt count = field(chan, "count");
  return or_exprt{
    as_bool(field(chan, "closed")),
    binary_relation_exprt{count, ID_gt, int_constant(0, count)}};
}

static exprt send_ready_blocking(const exprt &chan)
{
  // A blocking send on an unbuffered channel deposits its value as soon as
  // the slot is free; receivers then take it.
  const exprt cap = field(chan, "cap");
  const exprt count = field(chan, "count");
  return or_exprt{
    as_bool(field(chan, "closed")),
    if_exprt{
      binary_relation_exprt{cap, ID_gt, int_constant(0, cap)},
      binary_relation_exprt{count, ID_lt, cap},
      equal_exprt{count, int_constant(0, count)}}};
}

void go_convertert::chan_send_step(
  const exprt &chan,
  const exprt &value,
  const exprt &failed,
  const exprt &ticket,
  code_blockt &dest)
{
  // In an atomic section in which the send can proceed: record whether
  // the channel is closed, or else store the value.
  const exprt cap = field(chan, "cap");
  const exprt count = field(chan, "count");
  dest.add(code_frontend_assignt{failed, as_bool(field(chan, "closed"))});
  code_blockt buffered;
  buffered.add(code_frontend_assignt{
    buffer_at(chan, mod_exprt{plus_exprt{field(chan, "head"), count}, cap}),
    value});
  buffered.add(
    code_frontend_assignt{count, plus_exprt{count, int_constant(1, count)}});
  code_blockt unbuffered;
  unbuffered.add(code_frontend_assignt{
    buffer_at(chan, from_integer(0, c_index_type())), value});
  unbuffered.add(code_frontend_assignt{count, int_constant(1, count)});
  const exprt sent = field(chan, "sent");
  unbuffered.add(
    code_frontend_assignt{sent, plus_exprt{sent, int_constant(1, sent)}});
  unbuffered.add(code_frontend_assignt{ticket, sent});
  dest.add(code_ifthenelset{
    not_exprt{failed},
    code_ifthenelset{
      binary_relation_exprt{cap, ID_gt, int_constant(0, cap)},
      std::move(buffered),
      std::move(unbuffered)}});
}

void go_convertert::chan_send_finish(
  const exprt &chan,
  const exprt &failed,
  const exprt &ticket,
  const source_locationt &location,
  code_blockt &dest,
  bool may_park)
{
  // On an unbuffered channel, the send completes when a receiver has
  // taken the value; if the channel is closed before, it panics (and the
  // value is not received).
  const exprt cap = field(chan, "cap");
  const exprt recvd = field(chan, "recvd");
  code_blockt wait;
  const or_exprt taken{
    binary_relation_exprt{recvd, ID_ge, ticket},
    as_bool(field(chan, "closed"))};
  block_until(taken, may_park, location, wait);
  wait.add(
    code_frontend_assignt{failed, binary_relation_exprt{recvd, ID_lt, ticket}});
  wait.add(code_ifthenelset{
    failed,
    code_frontend_assignt{field(chan, "count"), int_constant(0, recvd)}});
  wait.add(atomic_end(location));
  dest.add(code_ifthenelset{
    and_exprt{not_exprt{failed}, equal_exprt{cap, int_constant(0, cap)}},
    std::move(wait)});
  add_panic(
    dest,
    not_exprt{failed},
    "closed-channel",
    "send on closed channel",
    location);
}

void go_convertert::chan_send(
  const exprt &chan,
  const exprt &value,
  const source_locationt &location,
  code_blockt &dest,
  bool may_park)
{
  // "A send on a nil channel blocks forever."
  const exprt not_nil =
    notequal_exprt{chan, null_pointer_exprt{to_pointer_type(chan.type())}};
  block_until(not_nil, may_park, location, dest);
  dest.add(atomic_end(location));
  const symbol_exprt failed = new_temporary(bool_typet{}, location, dest);
  const symbol_exprt ticket =
    new_temporary(field(chan, "sent").type(), location, dest);
  dest.add(code_frontend_assignt{ticket, int_constant(0, ticket)});
  block_until(send_ready_blocking(chan), may_park, location, dest);
  chan_send_step(chan, value, failed, ticket, dest);
  dest.add(atomic_end(location));
  chan_send_finish(chan, failed, ticket, location, dest, may_park);
}

void go_convertert::chan_recv_step(
  const exprt &chan,
  const exprt &value,
  const exprt &ok,
  code_blockt &dest)
{
  // In an atomic section in which the receive can proceed: take the first
  // value, or the zero value if the channel is closed and empty.
  // §Receive_operator: "The value of ok is true if the value received was
  // delivered by a successful send operation to the channel, or false if
  // it is a zero value generated because the channel is closed and empty."
  const exprt cap = field(chan, "cap");
  const exprt count = field(chan, "count");
  const exprt head = field(chan, "head");
  const source_locationt location = value.source_location();
  code_blockt buffered;
  buffered.add(code_frontend_assignt{value, buffer_at(chan, head)});
  buffered.add(code_frontend_assignt{
    head, mod_exprt{plus_exprt{head, int_constant(1, head)}, cap}});
  buffered.add(
    code_frontend_assignt{count, minus_exprt{count, int_constant(1, count)}});
  buffered.add(code_frontend_assignt{ok, true_exprt{}});
  code_blockt unbuffered;
  unbuffered.add(code_frontend_assignt{
    value, buffer_at(chan, from_integer(0, c_index_type()))});
  unbuffered.add(code_frontend_assignt{count, int_constant(0, count)});
  const exprt recvd = field(chan, "recvd");
  unbuffered.add(
    code_frontend_assignt{recvd, plus_exprt{recvd, int_constant(1, recvd)}});
  unbuffered.add(code_frontend_assignt{ok, true_exprt{}});
  code_blockt closed;
  auto zero = zero_initializer(value.type(), location, ns);
  CHECK_RETURN(zero.has_value());
  closed.add(code_frontend_assignt{value, *zero});
  closed.add(code_frontend_assignt{ok, false_exprt{}});
  // On an unbuffered channel, a value deposited by a sender that has not
  // completed is not received once the channel is closed: that send
  // panics.
  const exprt is_buffered =
    binary_relation_exprt{cap, ID_gt, int_constant(0, cap)};
  dest.add(code_ifthenelset{
    and_exprt{
      is_buffered, binary_relation_exprt{count, ID_gt, int_constant(0, count)}},
    std::move(buffered),
    code_ifthenelset{
      and_exprt{
        not_exprt{is_buffered}, not_exprt{as_bool(field(chan, "closed"))}},
      std::move(unbuffered),
      std::move(closed)}});
}

void go_convertert::chan_recv(
  const exprt &chan,
  const exprt &value,
  const exprt &ok,
  const source_locationt &location,
  code_blockt &dest,
  bool may_park)
{
  // "Receiving from a nil channel blocks forever."
  const exprt not_nil =
    notequal_exprt{chan, null_pointer_exprt{to_pointer_type(chan.type())}};
  block_until(not_nil, may_park, location, dest);
  dest.add(atomic_end(location));
  const exprt cap = field(chan, "cap");
  const exprt waiting = field(chan, "waiting");
  const exprt unbuffered = equal_exprt{cap, int_constant(0, cap)};
  // A receiver on an unbuffered channel is ready while it waits.
  code_blockt announce;
  announce.add(atomic_begin(location));
  announce.add(code_frontend_assignt{
    waiting, plus_exprt{waiting, int_constant(1, waiting)}});
  announce.add(atomic_end(location));
  dest.add(code_ifthenelset{unbuffered, std::move(announce)});
  block_until(recv_ready(chan), may_park, location, dest);
  dest.add(code_ifthenelset{
    unbuffered,
    code_frontend_assignt{
      waiting, minus_exprt{waiting, int_constant(1, waiting)}}});
  chan_recv_step(chan, value, ok, dest);
  dest.add(atomic_end(location));
}

exprt go_convertert::convert_receive(const irept &expr)
{
  const source_locationt location = location_of(expr);
  exprt chan = convert_expr(go_field(expr, "X"));
  if(chan.is_nil())
    return nil_exprt{};
  code_blockt block;
  chan = evaluate_once(chan, location, block);
  const typet element = to_pointer_type(field(chan, "buf").type()).base_type();
  symbol_exprt value = new_temporary(element, location, block);
  value.add_source_location() = location;
  const symbol_exprt ok = new_temporary(bool_typet{}, location, block);
  chan_recv(chan, value, ok, location, block);
  // x, ok = <-ch "yields an additional untyped boolean result reporting
  // whether the communication succeeded."
  if(type_converter.underlying(expr.get("_t")).get("k") == "tuple")
  {
    const typet tuple = expr_type(expr);
    const auto &components = to_struct_type(tuple).components();
    return with_prefix(
      struct_exprt{
        {assign_cast(value, components[0].type()),
         typecast_exprt::conditional_cast(ok, components[1].type())},
        tuple},
      block,
      location);
  }
  return with_prefix(value, block, location);
}

void go_convertert::convert_send(const irept &stmt, code_blockt &dest)
{
  // §Send_statements: "Both the channel and the value expression are
  // evaluated before communication begins. Communication blocks until the
  // send can proceed."
  const source_locationt location = location_of(stmt);
  exprt chan = convert_expr(go_field(stmt, "Chan"));
  exprt value = convert_expr(go_field(stmt, "Value"));
  if(chan.is_nil() || value.is_nil())
    return;
  chan = evaluate_once(chan, location, dest);
  const typet element = to_pointer_type(field(chan, "buf").type()).base_type();
  value = evaluate_once(assign_cast(value, element), location, dest);
  chan_send(chan, value, location, dest);
}

void go_convertert::convert_close(const irept &call, code_blockt &dest)
{
  const source_locationt location = location_of(call);
  exprt chan = convert_expr(go_list(call, "Args").front());
  if(chan.is_nil())
    return;
  close_channel(evaluate_once(chan, location, dest), location, dest);
}

void go_convertert::close_channel(
  const exprt &chan,
  const source_locationt &location,
  code_blockt &dest)
{
  // §Close: "Sending to or closing a closed channel causes a run-time
  // panic. Closing the nil channel also causes a run-time panic."
  add_panic(
    dest,
    notequal_exprt{chan, null_pointer_exprt{to_pointer_type(chan.type())}},
    "closed-channel",
    "close of nil channel",
    location);
  const symbol_exprt was = new_temporary(bool_typet{}, location, dest);
  dest.add(atomic_begin(location));
  dest.add(code_frontend_assignt{was, as_bool(field(chan, "closed"))});
  dest.add(code_frontend_assignt{
    field(chan, "closed"), from_integer(1, field(chan, "closed").type())});
  dest.add(atomic_end(location));
  add_panic(
    dest,
    not_exprt{was},
    "closed-channel",
    "close of closed channel",
    location);
}

exprt go_convertert::chan_len(
  const exprt &chan,
  bool is_cap,
  const typet &type,
  const source_locationt &location)
{
  // §Length_and_capacity: len(s) is the "number of elements queued in
  // channel buffer", cap(s) the "channel buffer capacity"; "The length of
  // a nil slice, map or channel is 0. The capacity of a nil slice or
  // channel is 0." (A value handed over on an unbuffered channel is not
  // queued.)
  code_blockt prefix;
  const exprt c = evaluate_once(chan, location, prefix);
  const exprt cap = field(c, "cap");
  if(!is_cap)
  {
    // The length observes the channel's state without synchronising
    // (Go1.27 §Channel_types; the memory model has no edge for it).
    code_blockt observe;
    race_marker(
      "go::$race_observe",
      address_of_exprt{field(c, "count")},
      location,
      observe);
    prefix.add(code_ifthenelset{
      notequal_exprt{c, null_pointer_exprt{to_pointer_type(c.type())}},
      std::move(observe)});
  }
  const exprt value =
    is_cap ? exprt{cap}
           : exprt{if_exprt{
               binary_relation_exprt{cap, ID_gt, int_constant(0, cap)},
               field(c, "count"),
               int_constant(0, cap)}};
  return with_prefix(
    if_exprt{
      equal_exprt{c, null_pointer_exprt{to_pointer_type(c.type())}},
      from_integer(0, type),
      typecast_exprt::conditional_cast(value, type)},
    prefix,
    location);
}

void go_convertert::convert_chan_range(
  const irept &stmt,
  const exprt &chan_value,
  code_blockt &dest,
  const irep_idt &label_object)
{
  // Go1.27 §For_range: "For channels, the iteration values produced are
  // the successive values sent on the channel until the channel is
  // closed. If the channel is nil, the range expression blocks forever."
  const source_locationt location = location_of(stmt);
  code_blockt scope;
  const symbol_exprt chan = new_temporary(chan_value.type(), location, scope);
  scope.add(code_frontend_assignt{chan, chan_value});
  const typet element = to_pointer_type(field(chan, "buf").type()).base_type();
  const std::string prefix =
    "$for" + std::to_string(state.label_counter++) + "$";

  code_blockt body;
  symbol_exprt value = new_temporary(element, location, body);
  value.add_source_location() = location;
  const symbol_exprt ok = new_temporary(bool_typet{}, location, body);
  chan_recv(chan, value, ok, location, body);
  body.add(code_ifthenelset{not_exprt{ok}, code_gotot{prefix + "break"}});
  const irept &key = go_field(stmt, "Key");
  if(key.is_not_nil() && !(key.id() == "Ident" && key.get("Name") == "_"))
  {
    exprt target;
    if(stmt.get("Tok") == ":=")
    {
      typet type =
        convert_type(parse_tree.object(key.get("_obj")).get("type"), key);
      if(type.is_nil())
        return;
      target = declare_local(key.get("_obj"), type, location_of(key), body);
    }
    else
    {
      access_partst parts;
      target = convert_access(key, parts);
      if(target.is_nil())
        return;
      for(auto &code : parts.operands.statements())
        body.add(std::move(code));
      for(auto &code : parts.checks.statements())
        body.add(std::move(code));
    }
    body.add(code_frontend_assignt{target, assign_cast(value, target.type())});
  }
  state.jump_targets.push_back(
    {label_object, true, prefix + "break", prefix + "next"});
  code_blockt statements = convert_block(go_field(stmt, "Body"));
  state.jump_targets.pop_back();
  for(auto &code : statements.statements())
    body.add(std::move(code));
  body.add(code_labelt{prefix + "next", code_skipt{}});
  code_whilet loop{true_exprt{}, std::move(body)};
  loop.add_source_location() = location;
  scope.add(std::move(loop));
  scope.add(code_labelt{prefix + "break", code_skipt{}});
  dest.add(std::move(scope));
}

void go_convertert::convert_select(
  const irept &stmt,
  code_blockt &dest,
  const irep_idt &label_object)
{
  // Go1.27 §Select_statements: "For all the cases in the statement, the
  // channel operands of receive operations and the channel and
  // right-hand-side expressions of send statements are evaluated exactly
  // once, in source order, upon entering the "select" statement." "If one
  // or more of the communications can proceed, a single one that can
  // proceed is chosen via a uniform pseudo-random selection. Otherwise, if
  // there is a default case, that case is chosen. If there is no default
  // case, the "select" statement blocks until at least one of the
  // communications can proceed." The choice is nondeterministic. A select
  // without default case either waits for a case that can proceed, or
  // commits to one case and performs it as a single blocking operation:
  // that lets two goroutines in select statements communicate on an
  // unbuffered channel, where neither has a ready partner before the
  // other commits.
  const source_locationt location = location_of(stmt);
  const std::string prefix =
    "$select" + std::to_string(state.label_counter++) + "$";
  struct caset
  {
    const irept *clause;
    bool is_send = false;
    // Nil for the default case.
    exprt chan = nil_exprt{};
    exprt value = nil_exprt{};
    exprt ok = nil_exprt{};
    exprt failed = nil_exprt{};
    exprt ticket = nil_exprt{};
  };
  std::vector<caset> cases;
  bool has_default = false;
  code_blockt scope;
  for(const auto &clause : go_list(go_field(stmt, "Body"), "List"))
  {
    const irept &comm = go_field(clause, "Comm");
    if(comm.is_nil())
    {
      has_default = true;
      cases.push_back({&clause});
      continue;
    }
    caset c{&clause};
    const irept *recv = nullptr;
    if(comm.id() == "SendStmt")
    {
      c.is_send = true;
      exprt chan = convert_expr(go_field(comm, "Chan"));
      exprt value = convert_expr(go_field(comm, "Value"));
      if(chan.is_nil() || value.is_nil())
        return;
      c.chan = evaluate_once(chan, location, scope);
      const typet element =
        to_pointer_type(field(c.chan, "buf").type()).base_type();
      c.value = evaluate_once(assign_cast(value, element), location, scope);
      c.failed = new_temporary(bool_typet{}, location, scope);
      c.ticket = new_temporary(field(c.chan, "sent").type(), location, scope);
      scope.add(code_frontend_assignt{c.ticket, int_constant(0, c.ticket)});
    }
    else
    {
      recv = comm.id() == "ExprStmt" ? &go_field(comm, "X")
                                     : &go_list(comm, "Rhs").front();
      while(recv->id() == "ParenExpr")
        recv = &go_field(*recv, "X");
      exprt chan = convert_expr(go_field(*recv, "X"));
      if(chan.is_nil())
        return;
      c.chan = evaluate_once(chan, location, scope);
      const typet element =
        to_pointer_type(field(c.chan, "buf").type()).base_type();
      symbol_exprt value = new_temporary(element, location, scope);
      value.add_source_location() = location;
      c.value = value;
      c.ok = new_temporary(bool_typet{}, location, scope);
    }
    cases.push_back(std::move(c));
  }

  // The choice: the index of the case, or -1 for the default case.
  const signedbv_typet index_type{32};
  const symbol_exprt choice = new_temporary(index_type, location, scope);
  const symbol_exprt commit = new_temporary(bool_typet{}, location, scope);
  const auto index = [&](std::size_t i) { return from_integer(i, index_type); };
  const auto not_nil = [](const exprt &chan)
  {
    return notequal_exprt{
      chan, null_pointer_exprt{to_pointer_type(chan.type())}};
  };
  exprt::operandst ready;
  exprt::operandst ready_choice;
  for(std::size_t i = 0; i < cases.size(); ++i)
  {
    const caset &c = cases[i];
    if(c.chan.is_nil())
      continue;
    // "Since communication on nil channels can never proceed".
    const and_exprt can{
      not_nil(c.chan), c.is_send ? send_ready(c.chan) : recv_ready(c.chan)};
    ready.push_back(can);
    ready_choice.push_back(and_exprt{equal_exprt{choice, index(i)}, can});
  }
  scope.add(code_frontend_assignt{
    choice, side_effect_expr_nondett{index_type, location}});
  scope.add(code_frontend_assignt{commit, false_exprt{}});
  // Without a default case, the select may be part of a deadlock if none
  // of its cases can proceed; then it waits on all of them, as a receiver
  // on its unbuffered channels.
  if(!has_default && deadlock_check)
  {
    code_blockt announce;
    for(const auto &c : cases)
    {
      if(c.chan.is_nil() || c.is_send)
        continue;
      const exprt cap = field(c.chan, "cap");
      const exprt waiting = field(c.chan, "waiting");
      announce.add(code_ifthenelset{
        and_exprt{not_nil(c.chan), equal_exprt{cap, int_constant(0, cap)}},
        code_frontend_assignt{
          waiting, plus_exprt{waiting, int_constant(1, waiting)}}});
    }
    const exprt any_ready = disjunction(ready);
    park([&any_ready]() { return any_ready; }, announce, location, scope);
  }
  scope.add(atomic_begin(location));
  if(has_default)
  {
    code_blockt none;
    std::size_t default_index = 0;
    for(std::size_t i = 0; i < cases.size(); ++i)
      if(cases[i].chan.is_nil())
        default_index = i;
    scope.add(code_ifthenelset{
      disjunction(ready),
      code_assumet{disjunction(ready_choice)},
      code_frontend_assignt{choice, index(default_index)}});
  }
  else
  {
    // Either a case that can proceed now, or (commit) any case, performed
    // after the atomic section as a blocking operation (which then is not a
    // deadlock of its own).
    exprt::operandst any_case;
    for(std::size_t i = 0; i < cases.size(); ++i)
      any_case.push_back(equal_exprt{choice, index(i)});
    scope.add(code_frontend_assignt{
      commit, side_effect_expr_nondett{bool_typet{}, location}});
    scope.add(code_ifthenelset{
      commit,
      code_assumet{disjunction(any_case)},
      code_assumet{disjunction(ready_choice)}});
  }
  // The communication of the chosen case, if it can proceed now.
  for(std::size_t i = 0; i < cases.size(); ++i)
  {
    const caset &c = cases[i];
    if(c.chan.is_nil())
      continue;
    code_blockt step;
    if(c.is_send)
      chan_send_step(c.chan, c.value, c.failed, c.ticket, step);
    else
      chan_recv_step(c.chan, c.value, c.ok, step);
    scope.add(code_ifthenelset{
      and_exprt{not_exprt{commit}, equal_exprt{choice, index(i)}},
      std::move(step)});
  }
  scope.add(atomic_end(location));

  // Complete the communication and run the clause.
  state.jump_targets.push_back(
    {label_object, false, prefix + "break", irep_idt{}});
  if(cases.empty())
  {
    // "a select with only nil channels and no default case blocks
    // forever", as does select {} (block_until above).
  }
  for(std::size_t i = 0; i < cases.size(); ++i)
  {
    const caset &c = cases[i];
    code_blockt clause;
    if(c.chan.is_nil())
    {
      // The default case observes that no communication can proceed,
      // without synchronising with the operations that made it so.
      for(const caset &other : cases)
      {
        if(other.chan.is_nil())
          continue;
        code_blockt observe;
        race_marker(
          "go::$race_observe",
          address_of_exprt{field(other.chan, "count")},
          location,
          observe);
        clause.add(code_ifthenelset{
          notequal_exprt{
            other.chan, null_pointer_exprt{to_pointer_type(other.chan.type())}},
          std::move(observe)});
      }
    }
    else
    {
      code_blockt blocking;
      if(c.is_send)
      {
        code_blockt now;
        chan_send_finish(c.chan, c.failed, c.ticket, location, now, false);
        code_blockt later;
        chan_send(c.chan, c.value, location, later, false);
        clause.add(code_ifthenelset{commit, std::move(later), std::move(now)});
      }
      else
      {
        code_blockt later;
        chan_recv(c.chan, c.value, c.ok, location, later, false);
        clause.add(code_ifthenelset{commit, std::move(later)});
        // "If the selected case is a RecvStmt with a short variable
        // declaration or an assignment, the left-hand side expressions are
        // evaluated and the received value (or values) are assigned."
        const irept &comm = go_field(*c.clause, "Comm");
        if(comm.id() == "AssignStmt")
        {
          const auto &lhs = go_list(comm, "Lhs");
          const exprt values[] = {c.value, c.ok};
          for(std::size_t j = 0; j < lhs.size() && j < 2; ++j)
          {
            const irept &target_node = lhs[j];
            if(target_node.id() == "Ident" && target_node.get("Name") == "_")
              continue;
            exprt target;
            if(comm.get("Tok") == ":=")
            {
              typet type = convert_type(
                parse_tree.object(target_node.get("_obj")).get("type"),
                target_node);
              if(type.is_nil())
                return;
              target = declare_local(
                target_node.get("_obj"),
                type,
                location_of(target_node),
                clause);
            }
            else
            {
              access_partst parts;
              target = convert_access(target_node, parts);
              if(target.is_nil())
                return;
              for(auto &code : parts.operands.statements())
                clause.add(std::move(code));
              for(auto &code : parts.checks.statements())
                clause.add(std::move(code));
            }
            clause.add(code_frontend_assignt{
              target, assign_cast(values[j], target.type())});
          }
        }
      }
    }
    for(const auto &statement : go_list(*c.clause, "Body"))
      convert_statement(statement, clause);
    clause.add(code_gotot{prefix + "break"});
    scope.add(
      code_ifthenelset{equal_exprt{choice, index(i)}, std::move(clause)});
  }
  state.jump_targets.pop_back();
  scope.add(code_labelt{prefix + "break", code_skipt{}});
  dest.add(std::move(scope));
}
