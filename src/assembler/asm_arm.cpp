/*******************************************************************\

Module: Inline assembly: AArch32 instruction semantics

Author: Kiro

\*******************************************************************/

/// \file
/// Instruction semantics for AArch32 (ARM and Thumb-2, unified syntax)

#include "asm_arm.h"

#include <util/c_types.h>
#include <util/config.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>

#include "asm_arm_flags.h"
#include "asm_bv_util.h"
#include "asm_semantics.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

using asm_bv::bool_to_bv;
using asm_bv::borrow_sub;
using asm_bv::bv;
using asm_bv::carry_add;
using asm_bv::constant;
using asm_bv::even_parity_low_byte;
using asm_bv::extract;
using asm_bv::is_zero;
using asm_bv::msb;
using asm_bv::overflow_add;
using asm_bv::overflow_sub;
using asm_bv::replace_bits;
using asm_bv::resize;
using asm_bv::sign_extend;

namespace
{
struct a32_operandt
{
  enum class kindt
  {
    REGISTER,
    IMMEDIATE,
    MEMORY,
    NAME
  };

  kindt kind = kindt::REGISTER;
  exprt storage;
  exprt value;
  exprt address;
  exprt lvalue;
  std::size_t natural_width = 0;
  std::string name;
};

/// Instructions by base mnemonic; `s` says whether a flag-setting `s`
/// suffix is allowed
struct a32_instructiont
{
  std::string base;
  bool sets_flags = false;
  std::string condition = "al";
};

const std::vector<std::string> &base_mnemonics()
{
  // longest first, so that e.g. "bic" is not read as "b" + "ic"
  static const std::vector<std::string> bases = []
  {
    std::vector<std::string> b = {
      "mov",    "mvn",    "add",  "adc",   "sub",   "sbc",    "rsb",    "and",
      "orr",    "eor",    "bic",  "cmp",   "cmn",   "tst",    "teq",    "lsl",
      "lsr",    "asr",    "ror",  "b",     "ldr",   "ldrb",   "ldrh",   "ldrsb",
      "ldrsh",  "str",    "strb", "strh",  "ldrex", "ldrexb", "ldrexh", "strex",
      "strexb", "strexh", "nop",  "yield", "wfe",   "wfi",    "sev",    "mrc",
      "mcr",    "clz",    "rev",  "neg",   "cbz",   "cbnz"};
    std::sort(
      b.begin(),
      b.end(),
      [](const std::string &x, const std::string &y)
      { return x.size() > y.size(); });
    return b;
  }();
  return bases;
}

const std::set<std::string> &flag_setting()
{
  static const std::set<std::string> ops = {
    "mov",
    "mvn",
    "add",
    "adc",
    "sub",
    "sbc",
    "rsb",
    "and",
    "orr",
    "eor",
    "bic",
    "lsl",
    "lsr",
    "asr",
    "ror",
    "neg"};
  return ops;
}

bool decode(const std::string &m, a32_instructiont &result)
{
  for(const auto &base : base_mnemonics())
  {
    if(m.compare(0, base.size(), base) != 0)
      continue;
    std::string rest = m.substr(base.size());
    bool s = false;
    if(
      !rest.empty() && rest[0] == 's' && flag_setting().count(base) &&
      (rest.size() == 1 || asm_arm::condition_codes().count(rest.substr(1))))
    {
      s = true;
      rest.erase(0, 1);
    }
    std::string cond = "al";
    if(!rest.empty())
    {
      if(!asm_arm::condition_codes().count(rest))
        continue;
      cond = rest;
    }
    result.base = base;
    result.sets_flags = s;
    result.condition = cond;
    return true;
  }
  return false;
}

class asm_arm_semanticst : public asm_semanticst
{
public:
  asm_arm_semanticst()
  {
    syntax_conventions.line_comments = {"@"};
    syntax_conventions.separators = ";";
  }

  std::string name() const override
  {
    return "AArch32";
  }

  const asm_syntaxt &syntax() const override
  {
    return syntax_conventions;
  }

  std::size_t register_width() const override
  {
    return 32;
  }

  bool reference_widens(const std::string &, std::size_t natural_width)
    const override
  {
    // AArch32 references always read the full 32-bit register
    return 32 > natural_width;
  }

  constraint_classt classify_constraint(
    const std::string &constraint,
    std::size_t &pos,
    std::string &) const override
  {
    switch(constraint[pos])
    {
    case 'l':
    case 'h':
    case 'k':
      return constraint_classt::REGISTER;
    case 'Q':
      return constraint_classt::MEMORY;
    case 'U':
      if(pos + 1 < constraint.size())
      {
        ++pos;
        return constraint_classt::MEMORY;
      }
      return constraint_classt::UNKNOWN;
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
      return constraint_classt::IMMEDIATE;
    case 'P':
      if(pos + 1 < constraint.size())
      {
        ++pos;
        return constraint_classt::IMMEDIATE;
      }
      return constraint_classt::UNKNOWN;
    default:
      // t, w, x: floating-point and vector registers are not modelled
      return constraint_classt::UNKNOWN;
    }
  }

  exprt
  condition(const std::string &cc, asm_lowering_contextt &ctx) const override
  {
    return asm_arm::condition(cc, ctx);
  }

  bool lower(const asm_instructiont &instruction, asm_lowering_contextt &ctx)
    const override;

protected:
  asm_syntaxt syntax_conventions;

  bool parse_operand(
    const asm_token_listt &tokens,
    asm_lowering_contextt &ctx,
    a32_operandt &operand) const;

  exprt
  read(asm_lowering_contextt &ctx, const a32_operandt &op, std::size_t w) const;

  bool write(
    asm_lowering_contextt &ctx,
    const a32_operandt &op,
    std::size_t w,
    const exprt &value) const;

  bool lower_unconditional(
    const a32_instructiont &decoded,
    std::vector<a32_operandt> &ops,
    const asm_instructiont &instruction,
    asm_lowering_contextt &ctx) const;
};

bool register_name(
  const std::string &raw,
  asm_lowering_contextt &ctx,
  a32_operandt &operand)
{
  std::string name = raw;
  for(auto &c : name)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  static const std::map<std::string, std::string> aliases = {
    {"sb", "r9"},
    {"sl", "r10"},
    {"fp", "r11"},
    {"ip", "r12"},
    {"sp", "r13"},
    {"lr", "r14"}};
  auto alias = aliases.find(name);
  if(alias != aliases.end())
    name = alias->second;

  if(name.size() >= 2 && name[0] == 'r')
  {
    const std::string digits = name.substr(1);
    if(
      digits.size() <= 2 &&
      std::all_of(
        digits.begin(),
        digits.end(),
        [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
    {
      const int n = std::stoi(digits);
      if(n <= 14)
      {
        operand.kind = a32_operandt::kindt::REGISTER;
        operand.storage = ctx.hardware_register("r" + std::to_string(n));
        return true;
      }
    }
  }
  return false;
}

bool asm_arm_semanticst::parse_operand(
  const asm_token_listt &tokens,
  asm_lowering_contextt &ctx,
  a32_operandt &operand) const
{
  if(tokens.empty())
    return ctx.fail("empty operand");

  const asm_tokent &first = tokens[0];

  if(
    first.is_punctuation('#') || first.kind == asm_tokent::kindt::NUMBER ||
    first.is_punctuation('-'))
  {
    std::size_t pos = first.is_punctuation('#') ? 1 : 0;
    bool negative = false;
    if(pos < tokens.size() && tokens[pos].is_punctuation('-'))
    {
      negative = true;
      ++pos;
    }
    if(pos + 1 != tokens.size())
      return ctx.fail("unsupported immediate");
    operand.kind = a32_operandt::kindt::IMMEDIATE;
    if(tokens[pos].kind == asm_tokent::kindt::NUMBER)
    {
      operand.value = from_integer(
        negative ? -tokens[pos].number : tokens[pos].number, integer_typet{});
      return true;
    }
    if(
      tokens[pos].kind == asm_tokent::kindt::OPERAND &&
      ctx.operands.operands.at(tokens[pos].index).kind ==
        asm_operandt::kindt::IMMEDIATE)
    {
      operand.value = ctx.operand_immediate(tokens[pos].index);
      if(negative)
        operand.value = unary_minus_exprt{operand.value};
      return true;
    }
    return ctx.fail("unsupported immediate");
  }

  if(first.is_punctuation('['))
  {
    if(!tokens.back().is_punctuation(']'))
      return ctx.fail("unsupported addressing mode (writeback?)");
    std::vector<asm_token_listt> parts{{}};
    for(std::size_t i = 1; i + 1 < tokens.size(); ++i)
    {
      if(tokens[i].is_punctuation(','))
        parts.emplace_back();
      else
        parts.back().push_back(tokens[i]);
    }
    a32_operandt base;
    if(
      !parse_operand(parts[0], ctx, base) ||
      base.kind != a32_operandt::kindt::REGISTER)
    {
      return ctx.fail("unsupported base register");
    }
    exprt address = typecast_exprt{
      resize(read(ctx, base, 32), config.ansi_c.pointer_width),
      pointer_type(char_type())};
    if(parts.size() == 2)
    {
      a32_operandt offset;
      if(
        !parse_operand(parts[1], ctx, offset) ||
        offset.kind != a32_operandt::kindt::IMMEDIATE)
      {
        return ctx.fail("unsupported offset");
      }
      address = plus_exprt{
        address,
        typecast_exprt::conditional_cast(
          offset.value, signedbv_typet{config.ansi_c.pointer_width})};
    }
    else if(parts.size() > 2)
      return ctx.fail("unsupported addressing mode");
    operand.kind = a32_operandt::kindt::MEMORY;
    operand.address = address;
    return true;
  }

  if(tokens.size() == 1 && first.kind == asm_tokent::kindt::OPERAND)
  {
    if(!first.modifier.empty())
      return ctx.fail("unsupported operand modifier %" + first.modifier);
    const asm_operandt &bound = ctx.operands.operands.at(first.index);
    switch(bound.kind)
    {
    case asm_operandt::kindt::REGISTER:
      operand.kind = a32_operandt::kindt::REGISTER;
      operand.storage = ctx.operand_storage(first.index);
      return true;
    case asm_operandt::kindt::MEMORY:
      operand.kind = a32_operandt::kindt::MEMORY;
      operand.address = ctx.operand_address(first.index);
      operand.lvalue = dereference_exprt{ctx.operand_address(first.index)};
      operand.natural_width = ctx.operand_width(first.index);
      return true;
    case asm_operandt::kindt::IMMEDIATE:
      operand.kind = a32_operandt::kindt::IMMEDIATE;
      operand.value = ctx.operand_immediate(first.index);
      return true;
    case asm_operandt::kindt::FLAG_OUTPUT:
      break;
    }
    return ctx.fail("unsupported operand");
  }

  if(tokens.size() == 1 && first.is_identifier())
  {
    if(register_name(first.text, ctx, operand))
      return true;
    operand.kind = a32_operandt::kindt::NAME;
    operand.name = first.text;
    return true;
  }

  if(
    tokens.size() == 1 && (first.kind == asm_tokent::kindt::LOCAL_LABEL ||
                           first.kind == asm_tokent::kindt::C_LABEL))
  {
    operand.kind = a32_operandt::kindt::NAME;
    return true;
  }

  return ctx.fail("unsupported operand");
}

exprt asm_arm_semanticst::read(
  asm_lowering_contextt &ctx,
  const a32_operandt &op,
  std::size_t w) const
{
  switch(op.kind)
  {
  case a32_operandt::kindt::REGISTER:
    return w == 32 ? op.storage : extract(op.storage, 0, w);
  case a32_operandt::kindt::IMMEDIATE:
    if(op.value.type().id() == ID_integer)
      return constant(
        numeric_cast_v<mp_integer>(to_constant_expr(op.value)), w);
    return typecast_exprt::conditional_cast(op.value, bv(w));
  case a32_operandt::kindt::MEMORY:
    if(op.lvalue.is_not_nil() && op.natural_width == w)
      return ctx.to_bits(op.lvalue, w);
    return ctx.memory(op.address, w);
  case a32_operandt::kindt::NAME:
    break;
  }
  UNREACHABLE;
}

bool asm_arm_semanticst::write(
  asm_lowering_contextt &ctx,
  const a32_operandt &op,
  std::size_t w,
  const exprt &value) const
{
  switch(op.kind)
  {
  case a32_operandt::kindt::REGISTER:
    ctx.assign(op.storage, resize(resize(value, w), 32));
    return true;
  case a32_operandt::kindt::MEMORY:
    if(op.lvalue.is_not_nil() && op.natural_width == w)
      ctx.assign(op.lvalue, ctx.from_bits(resize(value, w), op.lvalue.type()));
    else
      ctx.assign(ctx.memory(op.address, w), resize(value, w));
    return true;
  case a32_operandt::kindt::IMMEDIATE:
  case a32_operandt::kindt::NAME:
    break;
  }
  return ctx.fail("invalid destination");
}

bool asm_arm_semanticst::lower(
  const asm_instructiont &instruction,
  asm_lowering_contextt &ctx) const
{
  const std::string &m = instruction.mnemonic;

  if(!instruction.prefixes.empty())
    return ctx.fail("unexpected prefix");

  // In unified syntax every instruction in an IT block carries its own
  // condition suffix, so IT itself has no effect here.
  if(
    m.size() >= 2 && m.size() <= 4 && m[0] == 'i' && m[1] == 't' &&
    std::all_of(
      m.begin() + 2, m.end(), [](char c) { return c == 't' || c == 'e'; }))
  {
    return true;
  }

  if(m == "cpsid" || m == "cpsie" || m == "pld" || m == "pldw")
    return true;

  if(m == "dmb" || m == "dsb" || m == "isb")
    return false; // see lower_barrier

  a32_instructiont decoded;
  if(!decode(m, decoded))
    return ctx.fail("unsupported instruction " + m);

  if(decoded.base == "b" || decoded.base == "cbz" || decoded.base == "cbnz")
  {
    if(instruction.operands.empty() || instruction.operands.back().size() != 1)
      return ctx.fail("unsupported branch");
    const asm_tokent &target = instruction.operands.back().back();
    if(decoded.base == "b")
    {
      if(instruction.operands.size() != 1)
        return ctx.fail("unsupported branch");
      return ctx.branch(target, asm_arm::condition(decoded.condition, ctx));
    }
    a32_operandt reg;
    if(
      instruction.operands.size() != 2 ||
      !parse_operand(instruction.operands[0], ctx, reg) ||
      reg.kind != a32_operandt::kindt::REGISTER)
    {
      return ctx.fail("unsupported " + decoded.base);
    }
    const exprt zero = is_zero(read(ctx, reg, 32));
    return ctx.branch(
      target, decoded.base == "cbz" ? zero : exprt{not_exprt{zero}});
  }

  std::vector<a32_operandt> ops;
  for(const auto &tokens : instruction.operands)
  {
    a32_operandt op;
    if(!parse_operand(tokens, ctx, op))
      return false;
    ops.push_back(op);
  }

  if(decoded.condition == "al")
    return lower_unconditional(decoded, ops, instruction, ctx);

  const auto begin =
    ctx.begin_conditional(asm_arm::condition(decoded.condition, ctx));
  if(!lower_unconditional(decoded, ops, instruction, ctx))
    return false;
  ctx.end_conditional(begin);
  return true;
}

bool asm_arm_semanticst::lower_unconditional(
  const a32_instructiont &decoded,
  std::vector<a32_operandt> &ops,
  const asm_instructiont &instruction,
  asm_lowering_contextt &ctx) const
{
  const std::string &b = decoded.base;

  auto is_reg = [&](std::size_t i)
  { return i < ops.size() && ops[i].kind == a32_operandt::kindt::REGISTER; };
  auto is_mem = [&](std::size_t i)
  { return i < ops.size() && ops[i].kind == a32_operandt::kindt::MEMORY; };
  auto temp = [&](const exprt &value, const std::string &suffix)
  {
    const symbol_exprt t = ctx.temporary(value.type(), suffix);
    ctx.assign(t, value);
    return t;
  };

  if(
    b == "nop" || b == "yield" || b == "wfe" || b == "wfi" || b == "sev" ||
    b == "mcr")
  {
    return true;
  }

  if(b == "mrc")
  {
    // mrc p15, op1, Rt, CRn, CRm, op2: Rt receives a system register
    if(ops.size() < 3 || !is_reg(2))
      return ctx.fail("unsupported mrc");
    return write(ctx, ops[2], 32, ctx.nondet(bv(32)));
  }

  if(b == "mov" || b == "mvn")
  {
    if(ops.size() != 2 || !is_reg(0))
      return ctx.fail("unsupported " + b);
    const symbol_exprt v = temp(read(ctx, ops[1], 32), "v");
    const symbol_exprt r = b == "mvn" ? temp(bitnot_exprt{v}, "r") : v;
    if(decoded.sets_flags)
    {
      asm_arm::set_nz(ctx, r, 32);
      if(ops[1].kind == a32_operandt::kindt::IMMEDIATE)
        ctx.assign(ctx.flag("c"), ctx.nondet(bool_typet{}));
    }
    return write(ctx, ops[0], 32, r);
  }

  if(b == "clz" || b == "rev" || b == "neg")
  {
    if(ops.size() != 2 || !is_reg(0))
      return ctx.fail("unsupported " + b);
    const symbol_exprt v = temp(read(ctx, ops[1], 32), "v");
    const exprt r = b == "clz"
                      ? exprt{count_leading_zeros_exprt{v, true, bv(32)}}
                    : b == "rev" ? exprt{bswap_exprt{v, 8}}
                                 : exprt{unary_minus_exprt{v}};
    const symbol_exprt result = temp(r, "r");
    if(b == "neg" && decoded.sets_flags)
      asm_arm::set_sub_flags(ctx, constant(0, 32), v, result, 32);
    return write(ctx, ops[0], 32, result);
  }

  static const std::set<std::string> data = {
    "add",
    "adc",
    "sub",
    "sbc",
    "rsb",
    "and",
    "orr",
    "eor",
    "bic",
    "lsl",
    "lsr",
    "asr",
    "ror"};

  if(data.count(b))
  {
    // two-operand form: Rd, Rm means Rd, Rd, Rm
    if(ops.size() == 2)
      ops.insert(ops.begin() + 1, ops[0]);
    if(ops.size() != 3 || !is_reg(0) || !is_reg(1))
      return ctx.fail("unsupported operands of " + instruction.mnemonic);

    const symbol_exprt x = temp(read(ctx, ops[1], 32), "a");
    const symbol_exprt y = temp(read(ctx, ops[2], 32), "b");
    const exprt carry = bool_to_bv(ctx.flag("c"), 32);
    exprt r;
    if(b == "add")
      r = plus_exprt{x, y};
    else if(b == "adc")
      r = plus_exprt{plus_exprt{x, y}, carry};
    else if(b == "sub")
      r = minus_exprt{x, y};
    else if(b == "sbc") // x - y - !C
      r = minus_exprt{minus_exprt{x, y}, minus_exprt{constant(1, 32), carry}};
    else if(b == "rsb")
      r = minus_exprt{y, x};
    else if(b == "and")
      r = bitand_exprt{x, y};
    else if(b == "orr")
      r = bitor_exprt{x, y};
    else if(b == "eor")
      r = bitxor_exprt{x, y};
    else if(b == "bic")
      r = bitand_exprt{x, bitnot_exprt{y}};
    else
    {
      // shifts by register use the bottom byte; shifts of 32 or more
      // produce 0 (or the sign) and are not modelled exactly here
      const exprt n = bitand_exprt{y, constant(255, 32)};
      const exprt big = binary_relation_exprt{n, ID_ge, constant(32, 32)};
      if(b == "lsl")
        r = if_exprt{big, constant(0, 32), shl_exprt{x, n}};
      else if(b == "lsr")
        r = if_exprt{big, constant(0, 32), lshr_exprt{x, n}};
      else if(b == "asr")
        r = typecast_exprt{
          ashr_exprt{
            typecast_exprt{x, signedbv_typet{32}},
            if_exprt{big, constant(31, 32), n}},
          bv(32)};
      else
      {
        const exprt k = bitand_exprt{n, constant(31, 32)};
        r = if_exprt{
          is_zero(k),
          x,
          bitor_exprt{
            lshr_exprt{x, k}, shl_exprt{x, minus_exprt{constant(32, 32), k}}}};
      }
    }

    const symbol_exprt result = temp(r, "r");
    if(decoded.sets_flags)
    {
      if(b == "add")
        asm_arm::set_add_flags(ctx, x, y, constant(0, 32), result, 32);
      else if(b == "adc")
        asm_arm::set_add_flags(ctx, x, y, carry, result, 32);
      else if(b == "sub")
        asm_arm::set_sub_flags(ctx, x, y, result, 32);
      else if(b == "rsb")
        asm_arm::set_sub_flags(ctx, y, x, result, 32);
      else if(b == "sbc")
      {
        // the flags of x + ~y + C
        asm_arm::set_add_flags(ctx, x, bitnot_exprt{y}, carry, result, 32);
      }
      else
      {
        // logical operations and shifts: C comes from the shifter, which
        // is not modelled; V is unaffected
        asm_arm::set_nz(ctx, result, 32);
        if(
          ops[2].kind == a32_operandt::kindt::IMMEDIATE || b == "lsl" ||
          b == "lsr" || b == "asr" || b == "ror")
        {
          ctx.assign(ctx.flag("c"), ctx.nondet(bool_typet{}));
        }
      }
    }
    return write(ctx, ops[0], 32, result);
  }

  if(b == "cmp" || b == "cmn" || b == "tst" || b == "teq")
  {
    if(ops.size() != 2 || !is_reg(0))
      return ctx.fail("unsupported " + b);
    const symbol_exprt x = temp(read(ctx, ops[0], 32), "a");
    const symbol_exprt y = temp(read(ctx, ops[1], 32), "b");
    if(b == "cmp")
      asm_arm::set_sub_flags(ctx, x, y, temp(minus_exprt{x, y}, "r"), 32);
    else if(b == "cmn")
    {
      asm_arm::set_add_flags(
        ctx, x, y, constant(0, 32), temp(plus_exprt{x, y}, "r"), 32);
    }
    else
    {
      asm_arm::set_nz(
        ctx,
        temp(
          b == "tst" ? exprt{bitand_exprt{x, y}} : exprt{bitxor_exprt{x, y}},
          "r"),
        32);
      if(ops[1].kind == a32_operandt::kindt::IMMEDIATE)
        ctx.assign(ctx.flag("c"), ctx.nondet(bool_typet{}));
    }
    return true;
  }

  static const std::map<std::string, std::pair<std::size_t, bool>> loads = {
    {"ldr", {32, false}},
    {"ldrb", {8, false}},
    {"ldrh", {16, false}},
    {"ldrsb", {8, true}},
    {"ldrsh", {16, true}},
    {"ldrex", {32, false}},
    {"ldrexb", {8, false}},
    {"ldrexh", {16, false}}};
  auto load = loads.find(b);
  if(load != loads.end())
  {
    if(ops.size() != 2 || !is_reg(0) || !is_mem(1))
      return ctx.fail("unsupported operands of " + instruction.mnemonic);
    if(b.compare(0, 5, "ldrex") == 0)
      ctx.mark_atomic();
    const exprt v = read(ctx, ops[1], load->second.first);
    return write(
      ctx,
      ops[0],
      32,
      load->second.second ? sign_extend(v, load->second.first, 32)
                          : resize(v, 32));
  }

  static const std::map<std::string, std::size_t> stores = {
    {"str", 32}, {"strb", 8}, {"strh", 16}};
  auto store = stores.find(b);
  if(store != stores.end())
  {
    if(ops.size() != 2 || !is_reg(0) || !is_mem(1))
      return ctx.fail("unsupported operands of " + instruction.mnemonic);
    return write(ctx, ops[1], store->second, read(ctx, ops[0], store->second));
  }

  static const std::map<std::string, std::size_t> exclusive_stores = {
    {"strex", 32}, {"strexb", 8}, {"strexh", 16}};
  auto xstore = exclusive_stores.find(b);
  if(xstore != exclusive_stores.end())
  {
    // strex Rd, Rt, [Rn]: Rd := 0 on success, which it always is inside the
    // atomic section (see doc/architectural/inline-assembly.md)
    if(ops.size() != 3 || !is_reg(0) || !is_reg(1) || !is_mem(2))
      return ctx.fail("unsupported operands of " + instruction.mnemonic);
    ctx.mark_atomic();
    return write(
             ctx, ops[2], xstore->second, read(ctx, ops[1], xstore->second)) &&
           write(ctx, ops[0], 32, constant(0, 32));
  }

  return ctx.fail("unsupported instruction " + instruction.mnemonic);
}
} // namespace

std::unique_ptr<asm_semanticst> make_asm_arm_semantics()
{
  return std::make_unique<asm_arm_semanticst>();
}
