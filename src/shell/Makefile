SRC = shell_arith.cpp \
      shell_ast.cpp \
      shell_dialect.cpp \
      shell_language.cpp \
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
