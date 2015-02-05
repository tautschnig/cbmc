MAKEFLAGS=-j 1

# Change for your environment
CPROVER_ROOT=/home/kareem/doc/cbmc
GOTO_IN=$(CPROVER_ROOT)/src/goto-instrument/goto-instrument
GOTO_CC=$(CPROVER_ROOT)/src/goto-cc/goto-cc

# The goto binary that each C file will be compiled into
TEST_GOTO=test.goto

STDERR=2>/dev/null

# Each dir will get a results.json
JSON_RESULTS=$(patsubst %,\
%/result.json,\
$(shell find . -type d | grep -v "^.$$"))

# Each dir will get a $(TEST_GOTO)
GOTOS=$(patsubst %,\
%/$(TEST_GOTO),\
$(shell find . -type d | grep -v "^.$$"))

default: $(JSON_RESULTS)

%/result.json: %/$(TEST_GOTO) %/expected.json $(GOTO_IN)
	@echo thread-functions for $(patsubst %/,%,$(dir $<))
	@$(GOTO_IN) --thread-functions $< $(STDERR)
#@$(GOTO_IN) --thread-functions $< $(STDERR) > $@


%/$(TEST_GOTO): %/main.c
	@echo compiling $(patsubst %/,%,$(dir $<))
	@$(GOTO_CC) -o $@ -pthread $<

.SECONDAY: $(GOTOS)

clean:
	-rm $(GOTOS)
