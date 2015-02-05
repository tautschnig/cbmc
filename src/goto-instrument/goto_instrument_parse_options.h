/*******************************************************************\

Module: Command Line Parsing

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_GOTO_INSTRUMENT_PARSEOPTIONS_H
#define CPROVER_GOTO_INSTRUMENT_PARSEOPTIONS_H

#include <util/ui_message.h>
#include <util/parse_options.h>

#include <langapi/language_ui.h>
#include <goto-programs/goto_functions.h>

#define GOTO_INSTRUMENT_OPTIONS \
  "(all)" \
  "(document-claims-latex)(document-claims-html)" \
  "(document-properties-latex)(document-properties-html)" \
  "(dump-c)(dump-cpp)(use-system-headers)(dot)(xml)" \
  "(bounds-check)(no-bounds-check)" \
  "(pointer-check)(memory-leak-check)(no-pointer-check)" \
  "(remove-pointers)" \
  "(no-simplify)" \
  "(assert-to-assume)" \
  "(div-by-zero-check)(no-div-by-zero-check)" \
  "(undefined-shift-check)" \
  "(no-assertions)(no-assumptions)(uninitialized-check)" \
  "(nan-check)(no-nan-check)" \
  "(race-check)(scc)(one-event-per-cycle)" \
  "(minimum-interference)" \
  "(mm):(my-events)(unwind):" \
  "(max-var):(max-po-trans):(ignore-arrays)" \
  "(cfg-kill)(no-dependencies)(force-loop-duplication)" \
  "(call-graph)" \
  "(no-po-rendering)(render-cluster-file)(render-cluster-function)" \
  "(nondet-volatile)(isr):(check-invariant):" \
  "(stack-depth):(nondet-static)" \
  "(function-enter):(function-exit):(branch):" \
  "(learn)(learn-functions):(learn-word-length)(fl):(cs):" \
  "(signed-overflow-check)(unsigned-overflow-check)(float-overflow-check)" \
  "(show-goto-functions)(show-value-sets)" \
  "(show-local-may-alias)(show-global-may-alias)" \
  "(show-local-bitvector-analysis)(show-custom-bitvector-analysis)" \
  "(show-escape-analysis)(escape-analysis)" \
  "(custom-bitvector-analysis)" \
  "(show-struct-alignment)(interval-analysis)(show-intervals)" \
  "(show-uninitialized)(show-locations)" \
  "(full-slice)(reachability-slice)" \
  "(inline)" \
  "(show-claims)(show-properties)(property):" \
  "(show-symbol-table)(show-points-to)(show-rw-set)" \
  "(cav11)" \
  "(show-natural-loops)(accelerate)(havoc-loops)" \
  "(error-label):(string-abstraction)" \
  "(verbosity):(version)(xml-ui)(show-loops)" \
  "(accelerate)(constant-propagator)" \
  "(transitive-calls)(spawned-functions)" \
  "(event-functions)(output-event-source-locations)" \
  "(k-induction):(step-case)(base-case)" \
  "(show-call-sequences)(check-call-sequence):(call-sequence-bound):" \
  "(interpreter)(show-reaching-definitions)(count-eloc)" \
  "(list-symbols)(list-undefined-functions)(cfg-dot)" \
  "(z3)(add-library)(show-dependence-graph)(information-leak-check)" \
  "(horn)(skip-loops):(apply-code-contracts)(static-cycles)(egraph-dump)" \
  "(fake-static-cycles)"

class goto_instrument_parse_optionst:
  public parse_options_baset,
  public language_uit
{
public:
  virtual int doit();
  virtual void help();

  goto_instrument_parse_optionst(int argc, const char **argv):
    parse_options_baset(GOTO_INSTRUMENT_OPTIONS, argc, argv),
    language_uit("goto-instrument", cmdline),
    function_pointer_removal_done(false),
    partial_inlining_done(false),
    remove_returns_done(false)
  {
  }
  
protected:
  virtual void register_languages();

  void get_goto_program();
  void instrument_goto_program();
    
  void eval_verbosity();
  
  void do_function_pointer_removal();
  void do_partial_inlining();
  void do_remove_returns();
  
  bool function_pointer_removal_done;
  bool partial_inlining_done;
  bool remove_returns_done;
  
  goto_functionst goto_functions;

  /* Performs program transformations that must take place before the
   * static-cycles analysis.
   */
  int prepare_for_static_cycles(
      namespacet &ns, goto_functionst &goto_functions);
};

#endif
