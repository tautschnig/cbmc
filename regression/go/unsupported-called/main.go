// A function with an unsupported construct (here a call of a
// standard-library function without a model) only fails a property when
// called: as in packages, where most programs use only some functions, it
// does not keep the program from being verified.
package main

import (
	"cprover"
	"os"
)

func upper(s string) string { return os.Getenv(s) }

func twice(n int) int { return 2 * n }

func main() {
	n := cprover.Nondet[int]()
	if n > 100 {
		_ = upper("x")
	}
	cprover.Assume(n < 100 && n > -100)
	cprover.Assert(twice(n)%2 == 0, "even")
}
