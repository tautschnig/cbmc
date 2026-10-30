// Go1.27 §Index_expressions: for strings, "if x is out of range at run
// time, a run-time panic occurs". §Slice_expressions: "For arrays or
// strings, the indices are in range if 0 <= low <= high <= len(a)" and "If
// the indices are out of range at run time, a run-time panic occurs."
package main

import "cprover"

func main() {
	s := "abc"
	i := cprover.Nondet[int]()
	switch cprover.Nondet[int]() {
	case 0:
		_ = s[i]
	case 1:
		_ = s[i:]
	case 2:
		_ = s[1:i]
	}
}
