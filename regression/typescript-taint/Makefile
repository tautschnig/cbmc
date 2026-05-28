default: tests.log

CBMC_EXE = ../../../build/bin/cbmc
GOTO_ANALYZER_EXE = ../../../build/bin/goto-analyzer

test:
	@../test.pl -e -p -c "../chain.sh $(CBMC_EXE) $(GOTO_ANALYZER_EXE)"

tests.log: test
