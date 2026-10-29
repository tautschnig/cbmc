default: test

test:
	@../test.pl -e -p -c "../../../src/cbmc/cbmc"

tests.log: ../test.pl test

clean:
	find . -name '*.out' -execdir $(RM) '{}' \;
	$(RM) tests.log
