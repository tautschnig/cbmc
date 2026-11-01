SRC = expr2go.cpp \
      go_builtins.cpp \
      go_channels.cpp \
      go_convert.cpp \
      go_floats.cpp \
      go_interfaces.cpp \
      go_language.cpp \
      go_maps.cpp \
      go_panics.cpp \
      go_parse_tree.cpp \
      go_types.cpp \
      # Empty last line

INCLUDES= -I ..

include ../config.inc
include ../common

CLEANFILES = go$(LIBEXT) ../cbmc/cbmc-go-helper

all: go$(LIBEXT)

###############################################################################

go$(LIBEXT): $(OBJ)
	$(LINKLIB)

# cbmc-go-helper parses and type-checks Go using go/parser and go/types
# (helper/main.go). It needs a Go toolchain and is placed next to the cbmc
# binary, where the Go front-end looks for it.
GO ?= go
HELPER_SRC = helper/go.mod $(shell find helper -name '*.go')

helper: ../cbmc/cbmc-go-helper

../cbmc/cbmc-go-helper: $(HELPER_SRC)
	cd helper && GOTOOLCHAIN=local GOFLAGS=-mod=mod \
	  $(GO) build -trimpath -o ../../cbmc/cbmc-go-helper .

.PHONY: helper
