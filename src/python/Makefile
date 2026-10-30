SRC = expr2python.cpp \
      python_converter.cpp \
      python_language.cpp \
      python_parse_tree.cpp \
      # Empty last line

INCLUDES= -I ..

include ../config.inc
include ../common

CLEANFILES = python$(LIBEXT)

all: python$(LIBEXT)

###############################################################################

python$(LIBEXT): $(OBJ)
	$(LINKLIB)
