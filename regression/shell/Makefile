default: test

# The shell front-end parses with shfmt (https://github.com/mvdan/sh): use
# $CBMC_SHFMT if set, else shfmt on the PATH, and skip the tests if neither
# is available.
SHFMT ?= $(or $(CBMC_SHFMT),$(shell command -v shfmt 2>/dev/null))

test:
ifeq ($(SHFMT),)
	@echo "shfmt not found: skipping regression/shell"
else
	@CBMC_SHFMT="$(SHFMT)" ../test.pl -e -p -c "../../../src/cbmc/cbmc"
endif

tests.log: ../test.pl test

clean:
	find . -name '*.out' -execdir $(RM) '{}' \;
	$(RM) tests.log
