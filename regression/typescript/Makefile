default: test

test:
	@../test.pl -e -p -c "../../../build/bin/cbmc"

test-only-failed:
	@../test.pl -e -p -c "../../../build/bin/cbmc" -C

show:
	@for dir in *; do \
		if [ -d "$$dir" ]; then \
			vim -p "$$dir"/*.ts "$$dir"/test.desc; \
		fi \
	done

clean:
	find -name '*.out' -execdir $(RM) '{}' \;
	find -name '*.gb' -execdir $(RM) '{}' \;
	$(RM) tests.log
