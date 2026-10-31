// Panics of the standard library are those of its models: strings.Repeat
// panics if count is negative (as its documentation says).
package main

import (
	"cprover"
	"strings"
)

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(n >= -1 && n <= 2)
	_ = strings.Repeat("ab", n)
}
