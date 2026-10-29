default: tests.log

# The Go front-end needs cbmc-go-helper (built by `make -C src/go helper`,
# which requires a Go toolchain) next to the cbmc binary.
test:
	@../test.pl -e -p -c ../../../src/cbmc/cbmc

tests.log: ../test.pl
	@../test.pl -e -p -c ../../../src/cbmc/cbmc

clean:
	find . -name '*.out' -execdir $(RM) '{}' \;
	$(RM) tests*.log
