// copy and append of elements with pointers (pointers, strings, function
// values) need no loop, and the copies point to what the originals do.
package main

import "cprover"

type T struct {
	s string
	p *int
	f func() int
}

func main() {
	x, y := 1, 2
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n <= 4)
	a := make([]*int, n)
	if n >= 2 {
		a[0] = &x
		a[1] = &y
	}
	b := make([]*int, 5)
	k := copy(b[1:], a)
	if n >= 2 {
		if *b[1] != 1 || *b[2] != 2 {
			panic("pointers")
		}
		*b[2] = 7
		if y != 7 {
			panic("alias")
		}
	}
	if k > 0 && b[0] != nil {
		panic("untouched")
	}
	ts := []T{{"a", &x, func() int { return 3 }}, {"bc", &y, nil}}
	us := append([]T{{}}, ts...)
	if us[1].f() != 3 || us[2].s != "bc" || *us[2].p != y || us[2].f != nil {
		panic("structs")
	}
	if n == 4 && *b[2] == 7 {
		panic("expected failure")
	}
}
