// A package variable of an unsupported type is not an error: its uses
// fail a property where they are reached, here in a branch that is not
// taken, so that the rest of the program is verified.
package main

import "cprover"

// A type that refers to itself other than through a struct or a
// function type, which is not supported.
type rec []rec

var p rec

var counter int

func touch() bool { return p == nil }

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n < 100)
	if n > 1000 {
		cprover.Assert(touch(), "p is nil")
	}
	counter += n
	cprover.Assert(counter < 100, "counter")
}
