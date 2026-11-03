// Go1.27 §Composite_literals: "It is an error to specify multiple elements
// with the same field selector or constant key value." The elements of a map
// literal with constant keys are added without searching the map, so that
// the literal (here, of a package variable, as in package time) needs no
// unwinding; a non-constant key is inserted as by an assignment.
package main

import "cprover"

var units = map[string]int{"ns": 1, "us": 1000, "ms": 1000000, "s": 1e9,
	"m": 60e9, "h": 3600e9}

func main() {
	cprover.Assert(len(units) == 6, "six entries")
	k := "s"
	m := map[string]int{"a": 1, k: 2, "s": 3}
	cprover.Assert(len(m) == 2, "a repeated non-constant key")
}
