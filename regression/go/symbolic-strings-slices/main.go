// Strings and slices of arbitrary length and contents (cprover.Nondet).
// Go1.27 §String_concatenation and §Slice_expressions, checked for all
// strings of at most 3 bytes; §Length_and_capacity: "At any time the
// following relationship holds: 0 <= len(s) <= cap(s)".
package main

import "cprover"

func main() {
	s := cprover.Nondet[string]()
	cprover.Assume(len(s) <= 3)
	t := s + "x"
	cprover.Assert(len(t) == len(s)+1, "length of the concatenation")
	cprover.Assert(t[len(s)] == 'x', "the appended byte")
	cprover.Assert(t[:len(s)] == s, "the prefix is s")
	if len(s) > 0 {
		cprover.Assert(t[0] == s[0], "the first byte is s's")
	}

	x := cprover.Nondet[[]int]()
	cprover.Assert(0 <= len(x) && len(x) <= cap(x), "0 <= len <= cap")
	if len(x) == 2 { // a guard rather than an assumption, so that the
		// test also runs with the Go toolchain, where Nondet gives nil
		y := append(x, 5)
		cprover.Assert(len(y) == 3 && y[2] == 5 && y[0] == x[0], "append")
	}
}
