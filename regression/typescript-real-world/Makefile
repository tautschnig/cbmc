default: tests.log

CBMC_EXE = ../../../build/bin/cbmc

test:
	@../test.pl -e -p -c "$(CBMC_EXE)"

tests.log: test
