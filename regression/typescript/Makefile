default: test

# Start the parse daemon before running tests and stop it after.
# The daemon caches the TypeScript compiler across invocations via
# the Language Service, giving an 18x speedup on the full suite.
TS_SERVER = ../../scripts/cbmc_ts_server

test:
	@$(TS_SERVER) stop >/dev/null 2>&1 || true
	@socket=$$($(TS_SERVER) start) && \
	  CBMC_TS_SERVER_SOCKET="$$socket" \
	    ../test.pl -e -p -c "../../../build/bin/cbmc"; \
	  rc=$$?; \
	  $(TS_SERVER) stop; \
	  exit $$rc

test-only-failed:
	@$(TS_SERVER) stop >/dev/null 2>&1 || true
	@socket=$$($(TS_SERVER) start) && \
	  CBMC_TS_SERVER_SOCKET="$$socket" \
	    ../test.pl -e -p -c "../../../build/bin/cbmc" -C; \
	  rc=$$?; \
	  $(TS_SERVER) stop; \
	  exit $$rc

# Run without the daemon (useful for profiling / reproducibility).
test-no-daemon:
	@../test.pl -e -p -c "../../../build/bin/cbmc"

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
