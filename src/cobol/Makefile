SRC = cobol_language.cpp \
      cobol_scanner.cpp \
      cobol_typecheck.cpp \
      cobol_entry_point.cpp \
      expr2cobol.cpp \
      # Empty last line

INCLUDES= -I ..

include ../config.inc
include ../common

CLEANFILES = cobol$(LIBEXT)

all: cobol$(LIBEXT)

###############################################################################

cobol$(LIBEXT): $(OBJ)
	$(LINKLIB)
