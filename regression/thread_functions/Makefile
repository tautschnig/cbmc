TARGETS=$(patsubst %,\
%/result.json,\
$(shell find . -type d | grep -v "^.$$"))

EXECS=$(patsubst %,\
%/test,\
$(shell find . -type d | grep -v "^.$$"))

CPROVER_ROOT=/home/kareem/doc/cbmc
GOTO-IN=$(CPROVER_ROOT)/src/goto-instrument/goto-instrument
GOTO-CC=$(CPROVER_ROOT)/src/goto-cc/goto-cc

default: $(TARGETS)

%/result.json: %/test %/expected.json
	echo foo

%/test: %/main.c
	gcc -o $@ -pthread $<

.SECONDAY: $(EXECS)
