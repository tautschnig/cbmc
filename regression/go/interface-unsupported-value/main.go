// A value whose conversion is unsupported (here, a variable of an
// unmodelled package) stored in an interface variable is reported where
// it is; the rest of the program is verified.
package main

import (
	"cprover"
	"io"
	"os"
)

func order() io.Writer {
	var o io.Writer = os.Stdout
	return o
}

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n < 100)
	if n > 1000 {
		cprover.Assert(order() != nil, "order")
	}
	cprover.Assert(n < 100, "n")
}
