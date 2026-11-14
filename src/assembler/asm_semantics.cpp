/*******************************************************************\

Module: Inline assembly: instruction semantics

Author: Kiro

\*******************************************************************/

/// \file
/// Architecture-independent parts of the inline assembly semantics

#include "asm_semantics.h"

#include <util/config.h>

#include "asm_aarch64.h"
#include "asm_arm.h"
#include "asm_x86.h"

asm_semanticst::constraint_classt asm_semanticst::classify_constraint(
  const std::string &,
  std::size_t &,
  std::string &) const
{
  return constraint_classt::UNKNOWN;
}

bool lower_barrier(
  const asm_instructiont &instruction,
  asm_lowering_contextt &ctx)
{
  const std::string &m = instruction.mnemonic;

  if(!instruction.prefixes.empty())
    return false;

  if(m == "mfence" || m == "lfence" || m == "sfence") // x86
  {
    if(!instruction.operands.empty())
      return false;
    // keep calling the library model, as tools rely on the call
    ctx.call("__asm_" + m, {});
    return true;
  }

  if(m == "sync" || m == "hwsync") // Power
  {
    ctx.fence(true, true, true, true, true);
    return true;
  }

  if(m == "lwsync") // Power: everything but store-load
  {
    ctx.fence(true, true, true, false, true);
    return true;
  }

  if(m == "eieio") // Power: orders stores
  {
    ctx.fence(true, false, false, false, true);
    return true;
  }

  if(m == "isync" || m == "isb") // Power, ARM: needs a branch to order
  {
    ctx.fence(false, false, false, false, false);
    return true;
  }

  if(m == "dmb" || m == "dsb") // ARM
  {
    std::string option;
    if(
      instruction.operands.size() == 1 && instruction.operands[0].size() == 1 &&
      instruction.operands[0][0].is_identifier())
    {
      option = instruction.operands[0][0].text;
    }
    else if(!instruction.operands.empty())
      return false;

    const bool ld =
      option.size() >= 2 && option.compare(option.size() - 2, 2, "ld") == 0;
    const bool st =
      option.size() >= 2 && option.compare(option.size() - 2, 2, "st") == 0;

    if(ld) // orders loads against later loads and stores
      ctx.fence(false, true, true, false, true);
    else if(st) // orders stores against stores
      ctx.fence(true, false, false, false, true);
    else
      ctx.fence(true, true, true, true, true);
    return true;
  }

  return false;
}

/// Semantics for architectures without instruction support: only the
/// barriers of lower_barrier are understood.
class asm_generic_semanticst : public asm_semanticst
{
public:
  explicit asm_generic_semanticst(std::size_t _register_width)
    : register_width_bits(_register_width)
  {
    syntax_conventions.line_comments = {"#"};
  }

  std::string name() const override
  {
    return "generic";
  }

  const asm_syntaxt &syntax() const override
  {
    return syntax_conventions;
  }

  std::size_t register_width() const override
  {
    return register_width_bits;
  }

  bool
  lower(const asm_instructiont &, asm_lowering_contextt &ctx) const override
  {
    return ctx.fail("no instruction semantics for this architecture");
  }

protected:
  std::size_t register_width_bits;
  asm_syntaxt syntax_conventions;
};

std::unique_ptr<asm_semanticst>
make_asm_generic_semantics(std::size_t register_width)
{
  return std::make_unique<asm_generic_semanticst>(register_width);
}

std::unique_ptr<asm_semanticst> make_asm_semantics(const irep_idt &arch)
{
  if(arch == "x86_64" || arch == "x32")
    return make_asm_x86_semantics(true);
  else if(arch == "i386")
    return make_asm_x86_semantics(false);
  else if(arch == "arm64")
    return make_asm_aarch64_semantics();
  else if(arch == "arm" || arch == "armel" || arch == "armhf")
    return make_asm_arm_semantics();
  else
  {
    std::size_t width = config.ansi_c.pointer_width;
    if(width == 0)
      width = 64;
    return std::make_unique<asm_generic_semanticst>(width);
  }
}
