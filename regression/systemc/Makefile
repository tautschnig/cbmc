default: tests.log

CBMC = ../../../src/cbmc/cbmc
#$W/svncprover/trunk/src/cbmc/cbmc


test:
	@../test.pl -c $(CBMC)

tests.log: ../test.pl
	@../test.pl -c $(CBMC)

show:
	@for dir in *; do \
		if [ -d "$$dir" ]; then \
			vim -o "$$dir/*.c" "$$dir/*.out"; \
		fi; \
	done;
