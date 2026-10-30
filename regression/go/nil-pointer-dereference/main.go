// Go1.27 §Address_operators: "If x is nil, an attempt to evaluate *x will
// cause a run-time panic." §Selectors: "If x is of pointer type and has
// the value nil and x.f denotes a struct field, assigning to or evaluating
// x.f causes a run-time panic."
package main

import "cprover"

type T struct{ f int }

func main() {
	var p *int
	var t *T
	if cprover.Nondet[bool]() {
		p = new(int)
		t = &T{}
	}
	switch cprover.Nondet[int]() {
	case 0:
		*p = 1
	case 1:
		_ = t.f
	case 2:
		t.f = 2
	}
}
