SRC = symex_target.cpp symex_target_equation.cpp basic_symex.cpp \
      symex_main.cpp goto_trace.cpp build_goto_trace.cpp \
      symex_function_call.cpp goto_symex_state.cpp symex_dereference.cpp \
      symex_goto.cpp builtin_functions.cpp slice.cpp symex_other.cpp \
      slice_by_trace.cpp xml_goto_trace.cpp symex_decl.cpp \
      precondition.cpp postcondition.cpp symex_clean_expr.cpp \
      symex_dereference_state.cpp vcd_goto_trace.cpp auto_objects.cpp \
      symex_catch.cpp symex_start_thread.cpp symex_assign.cpp \
      abstract_event_structure.cpp partial_order_concurrency.cpp \
      concrete_event_structure.cpp event_dependencies.cpp \
      memory_model.cpp memory_model_alpha.cpp memory_model_arm_pldi.cpp \
      memory_model_power.cpp memory_model_ppc_pldi.cpp memory_model_pso.cpp \
      memory_model_rmo.cpp memory_model_rmo.cpp memory_model_sc.cpp \
      memory_model_tso.cpp memory_model_arm.cpp

INCLUDES= -I .. -I ../util

include ../config.inc
include ../common

CLEANFILES = goto-symex$(LIBEXT)

all: goto-symex$(LIBEXT)

###############################################################################

goto-symex$(LIBEXT): $(OBJ)
	$(LINKLIB)

