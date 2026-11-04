// Sets as maps with values of type struct{}, and slices of struct{}: their
// elements have size zero (Go1.27 §Size_and_alignment_guarantees).
package main

import "cprover"

func main() {
	set := map[int]struct{}{}
	k := cprover.Nondet[int]()
	set[k] = struct{}{}
	set[1] = struct{}{}
	_, ok := set[k]
	cprover.Assert(ok && len(set) >= 1 && len(set) <= 2, "set")
	s := make([]struct{}, 3)
	i := cprover.Nondet[uint]()
	cprover.Assume(i < 3)
	s[i] = struct{}{}
	cprover.Assert(len(s) == 3, "slice")
}
