SRC = shell_arith.cpp \
      shell_ast.cpp \
      shell_convert_arith.cpp \
      shell_convert_command.cpp \
      shell_convert_commands.cpp \
      shell_convert_literals.cpp \
      shell_convert_dot.cpp \
      shell_convert_eval.cpp \
      shell_convert_functions.cpp \
      shell_convert_options.cpp \
      shell_convert_params.cpp \
      shell_convert_strings.cpp \
      shell_convert_patterns.cpp \
      shell_convert_subshells.cpp \
      shell_convert_test.cpp \
      shell_convert_words.cpp \
      shell_converter.cpp \
      shell_dialect.cpp \
      shell_language.cpp \
      shell_pattern.cpp \
      shfmt_parser.cpp \
      shfmt_reader.cpp \
      # Empty last line

INCLUDES= -I ..

include ../config.inc
include ../common

CLEANFILES = shell$(LIBEXT)

all: shell$(LIBEXT)

###############################################################################

shell$(LIBEXT): $(OBJ)
	$(LINKLIB)
