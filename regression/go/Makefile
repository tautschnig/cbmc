default: tests.log

# The Go front-end needs cbmc-go-helper (built by `make -C src/go helper`,
# which requires a Go toolchain) next to the cbmc binary.
GO ?= go
# The capacities of append that the front-end uses for gc
# (src/go/helper/runtime/rt.go), checked against the Go toolchain, with and
# without optimisation (see CMakeLists.txt).
GO_TEST = cd ../../src/go/helper && GOTOOLCHAIN=local GOFLAGS=-mod=mod \
	  $(GO) test -count=1
# The same for TinyGo, if it is installed.
TINYGO ?= tinygo
TINYGO_TEST = if command -v $(TINYGO) > /dev/null; then \
	  cd ../../src/go/helper && GOTOOLCHAIN=local GOFLAGS=-mod=mod \
	  $(TINYGO) test -opt=z ./runtime && $(TINYGO) test -opt=0 ./runtime; fi

test:
	@../test.pl -e -p -c ../../../src/cbmc/cbmc
	@$(GO_TEST) ./runtime
	@$(GO_TEST) -gcflags=all=-N ./runtime
	@$(TINYGO_TEST)

tests.log: ../test.pl
	@../test.pl -e -p -c ../../../src/cbmc/cbmc
	@$(GO_TEST) ./runtime
	@$(GO_TEST) -gcflags=all=-N ./runtime
	@$(TINYGO_TEST)

clean:
	find . -name '*.out' -execdir $(RM) '{}' \;
	$(RM) tests*.log
