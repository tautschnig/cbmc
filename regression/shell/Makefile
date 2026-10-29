default: test

# Requires shfmt (https://github.com/mvdan/sh) on the PATH, or its path
# in the CBMC_SHFMT environment variable.
test:
	@../test.pl -e -p -c "../../../build/bin/cbmc"

clean:
	find . -name '*.out' -execdir $(RM) '{}' \;
