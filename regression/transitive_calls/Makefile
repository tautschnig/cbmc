MAKEFLAGS=-j

# Change for your environment
CPROVER_ROOT=/home/kareem/doc/cbmc
GOTO_IN=$(CPROVER_ROOT)/src/goto-instrument/goto-instrument
GOTO_CC=$(CPROVER_ROOT)/src/goto-cc/goto-cc

# The goto binary that each C file will be compiled into
TEST_GOTO=test.goto

STDERR=2>/dev/null

DIRS=$(shell find . -type d | grep -v "^.$$")

# Each dir will get a results.json
JSON_RESULTS=$(patsubst %,%/result.json,$(DIRS))

# Each dir will get a $(TEST_GOTO)
GOTOS=$(patsubst %,%/$(TEST_GOTO),$(DIRS))

default: test

%/result.json: %/$(TEST_GOTO) %/expected.json $(GOTO_IN)
	@echo computing transitive calls for $(patsubst %/,%,$(dir $<))
	@$(GOTO_IN) --transitive-calls $< > $@ 2>/dev/null


%/$(TEST_GOTO): %/main.c
	@echo compiling $(patsubst %/,%,$(dir $<))
	@$(GOTO_CC) -o $@ -pthread $< 2>/dev/null

test: $(patsubst ./%,test_%,$(DIRS))

test_%: %/result.json %/expected.json
	@./result_checker.rb $(patsubst test_%,%,$@) $?

.SECONDAY: $(GOTOS)

clean:
	-rm $(GOTOS)
