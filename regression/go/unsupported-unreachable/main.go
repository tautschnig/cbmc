// A function with an unsupported construct that is not called does not
// affect verification.
package main

import (
	"cprover"
	"os"
)

func upper(s string) string { return os.Getenv(s) }

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(n < 100 && n > -100)
	cprover.Assert(2*n%2 == 0, "even")
}
