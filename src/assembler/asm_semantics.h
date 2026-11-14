/*******************************************************************\

Module: Inline assembly: instruction semantics

Author: Kiro

\*******************************************************************/

/// \file
/// The interface between the lowering of inline assembly statements and the
/// architecture-specific instruction semantics. See
/// doc/architectural/inline-assembly.md for the design.

#ifndef CPROVER_ASSEMBLER_ASM_SEMANTICS_H
#define CPROVER_ASSEMBLER_ASM_SEMANTICS_H

#include <util/namespace.h>
#include <util/source_location.h>
#include <util/std_expr.h>

#include <goto-programs/goto_program.h>

#include "asm_operands.h"
#include "asm_template.h"

#include <list>
#include <map>
#include <memory>

class symbol_table_baset;
class asm_semanticst;

/// The state of lowering one inline assembly statement: the goto code being
/// built, the storage of register operands, hardware registers and flags, and
/// labels. Architecture semantics use it to emit the effect of instructions.
class asm_lowering_contextt
{
public:
  asm_lowering_contextt(
    symbol_table_baset &symbol_table,
    const irep_idt &function_id,
    const source_locationt &source_location,
    asm_operandst &operands,
    const asm_semanticst &semantics);

  const namespacet ns;
  asm_operandst &operands;
  const asm_semanticst &semantics;

  const source_locationt &source_location() const
  {
    return location;
  }

  /// A description of why lowering failed, for diagnostics
  std::string failure;

  /// Record a failure reason and return false, for use in `return
  /// ctx.fail(...)`.
  bool fail(const std::string &reason)
  {
    if(failure.empty())
      failure = reason;
    return false;
  }

  /// \name Storage
  /// \{

  /// A fresh block-local variable, declared (with a non-deterministic value)
  /// at the start of the statement.
  symbol_exprt temporary(const typet &type, const std::string &suffix);

  /// A hardware register, identified by its canonical (full-width) name. Its
  /// storage is block-local: the value is non-deterministic unless an operand
  /// with a fixed-register constraint initialises it.
  symbol_exprt hardware_register(const std::string &canonical);

  /// A condition flag (Boolean), non-deterministic initially
  symbol_exprt flag(const std::string &name);

  /// The block-local storage of register operand \p index, a bit-vector of
  /// the architecture's register width
  const symbol_exprt &operand_storage(std::size_t index) const;

  /// A pointer to the start of memory operand \p index, evaluated before the
  /// first instruction
  const symbol_exprt &operand_address(std::size_t index) const;

  /// The value of immediate operand \p index
  const exprt &operand_immediate(std::size_t index) const;

  /// The width, in bits, of the C expression of operand \p index; 0 if it is
  /// not a scalar
  std::size_t operand_width(std::size_t index) const;

  /// \}

  /// \name Code
  /// \{
  void assign(const exprt &lhs, const exprt &rhs);
  void assume(const exprt &condition);
  void add(goto_programt::instructiont instruction);

  /// A memory barrier; \p cumulative for barriers that order accesses of all
  /// observers (ARM `dmb`, Power `sync`)
  void fence(bool ww, bool rr, bool rw, bool wr, bool cumulative);

  /// Make the statement an atomic section
  void mark_atomic()
  {
    atomic = true;
  }

  /// A call to a library function taking `void *` arguments
  void call(const irep_idt &function, const exprt::operandst &arguments);

  /// The functions called via call(), which the caller has to make known
  std::vector<irep_idt> called_functions;

  /// A non-deterministic value of type \p type
  exprt nondet(const typet &type) const;

  /// A memory location of \p width bits at \p address (any pointer or
  /// integer type), as an lvalue of type unsignedbv(width)
  exprt memory(const exprt &address, std::size_t width) const;
  /// \}

  /// \name Control flow
  /// \{

  /// Define the local label \p name at the current position
  void define_label(const std::string &name);

  /// Jump to the target named by \p target (a LOCAL_LABEL, C_LABEL, or
  /// IDENTIFIER naming a label of this statement) if \p condition holds.
  /// \return false if the target is not understood
  bool branch(const asm_tokent &target, const exprt &condition);

  /// Start a region of code that only executes if \p condition holds, for
  /// conditionally executed instructions; end it with end_conditional
  goto_programt::targett begin_conditional(const exprt &condition);
  void end_conditional(goto_programt::targett begin);
  /// \}

  /// Initialise operand storage; must be called before lowering
  /// instructions.
  /// \return false if an operand cannot be modelled
  bool prepare();

  /// Assemble the complete lowering of the statement into \p dest: prologue
  /// (inputs), instructions, epilogue (outputs).
  /// \return false if a branch target could not be resolved
  bool finish(goto_programt &dest);

  /// Symbol holding the label chosen by `asm goto` (0 for fall-through, k+1
  /// for label k), or nil if the statement has no labels
  exprt goto_target;

  /// \name Conversion between C values and bit-vectors
  /// \{

  /// \p value (a C scalar) as unsignedbv(width), truncating or
  /// zero-extending; nil if \p value is not a scalar
  exprt to_bits(const exprt &value, std::size_t width) const;

  /// \p bits (a bit-vector) as a value of C type \p type
  exprt from_bits(const exprt &bits, const typet &type) const;

  /// The width in bits of the scalar type \p type; 0 if not a scalar
  std::size_t width(const typet &type) const;
  /// \}

protected:
  symbol_table_baset &symbol_table;
  irep_idt function_id;
  irep_idt mode;
  source_locationt location;

  goto_programt prologue, body, epilogue;
  std::list<symbol_exprt> declared;
  bool atomic = false;

  std::map<std::string, symbol_exprt> registers;
  std::map<std::string, symbol_exprt> flags;
  std::map<std::size_t, symbol_exprt> storage;
  std::map<std::size_t, symbol_exprt> addresses;
  std::map<std::size_t, exprt> immediates;

  /// named and numeric labels: current definition
  std::map<std::string, goto_programt::targett> label_definitions;
  /// all definitions of numeric labels, in order
  std::map<std::string, std::vector<goto_programt::targett>> numeric_labels;
  struct pending_brancht
  {
    goto_programt::targett instruction;
    std::string label;
    /// for numeric labels: the number of definitions of the label that
    /// preceded the branch
    std::size_t definitions_before;
    bool forward;
    bool numeric;
  };
  std::vector<pending_brancht> pending;
  goto_programt::targett exit_label;
  std::vector<goto_programt::targett> exit_branches;
};

/// The instruction semantics of one architecture
class asm_semanticst
{
public:
  virtual ~asm_semanticst() = default;

  /// A name for diagnostics, e.g. "x86"
  virtual std::string name() const = 0;

  /// The lexical conventions of templates
  virtual const asm_syntaxt &syntax() const = 0;

  /// The width of general-purpose registers in bits
  virtual std::size_t register_width() const = 0;

  /// How a constraint letter is interpreted
  enum class constraint_classt
  {
    UNKNOWN,
    REGISTER,
    MEMORY,
    IMMEDIATE,
    /// register, memory, or immediate
    ANY,
    /// a constraint letter that names one register; the name is returned in
    /// the output parameter
    FIXED_REGISTER,
    /// a letter that is not about the operand location (modifiers like `%`)
    IGNORE
  };

  /// Classify an architecture-specific constraint letter.
  /// \param constraint: the constraint string
  /// \param pos: position of the letter in \p constraint; may be advanced
  ///   for multi-letter constraints
  /// \param [out] fixed_register: the register named by the letter
  virtual constraint_classt classify_constraint(
    const std::string &constraint,
    std::size_t &pos,
    std::string &fixed_register) const;

  /// \return true if \p condition is valid in an `=@cc<condition>` output
  virtual bool is_flag_output_condition(const std::string &condition) const
  {
    return false;
  }

  /// The value of the condition \p condition on the current flags, for
  /// `=@cc` outputs; nil if not understood
  virtual exprt
  condition(const std::string &condition, asm_lowering_contextt &ctx) const
  {
    return nil_exprt{};
  }

  /// Lower \p instruction, appending its effect to \p ctx.
  /// \return false if the instruction is not understood; the caller then
  ///   approximates the whole statement
  virtual bool lower(
    const asm_instructiont &instruction,
    asm_lowering_contextt &ctx) const = 0;
};

/// The semantics for architecture \p arch (as in `config.ansi_c.arch`), or a
/// semantics that understands no instruction if the architecture is not
/// supported.
std::unique_ptr<asm_semanticst> make_asm_semantics(const irep_idt &arch);

/// Semantics that understand no instruction (but see lower_barrier), with
/// general-purpose registers of \p register_width bits
std::unique_ptr<asm_semanticst>
make_asm_generic_semantics(std::size_t register_width);

/// Lower the barrier instructions that are recognised on every architecture
/// (for compatibility with models compiled for one architecture and analysed
/// as another): x86 `mfence`/`lfence`/`sfence`, Power `sync`/`lwsync`/
/// `isync`/`eieio`, ARM `dmb`/`dsb`/`isb`.
/// \return false if \p instruction is not one of them
bool lower_barrier(
  const asm_instructiont &instruction,
  asm_lowering_contextt &ctx);

#endif // CPROVER_ASSEMBLER_ASM_SEMANTICS_H
