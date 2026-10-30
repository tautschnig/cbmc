// Go1.27 §For_range over integers, arrays and pointers to arrays. "For an
// integer value n ... the iteration values 0 through n-1 are produced in
// increasing order." "The range expression x is evaluated before beginning
// the loop": an array is copied. "If at most one iteration variable is
// present, the range loop produces iteration values from 0 up to len(a)-1
// and does not index into the array". With ":=", "each iteration has its
// own new variables [Go 1.22]"; otherwise "the iteration values are
// assigned to the respective variables as in an assignment statement."
package main

import "cprover"

func main() {
	sum := 0
	for i := range 5 {
		sum += i
	}
	cprover.Assert(sum == 10, "range over an integer")
	for range -1 {
		cprover.Assert(false, "no iterations for n <= 0")
	}

	a := [3]int{1, 2, 3}
	total := 0
	for i, v := range a {
		a[2] = 100 // does not affect v: the range is over a copy
		total += i * v
	}
	cprover.Assert(total == 0*1+1*2+2*3, "range over a copy of the array")

	b := [3]int{1, 2, 3}
	total = 0
	for _, v := range &b {
		b[2] = 100 // a pointer is not copied
		total += v
	}
	cprover.Assert(total == 103, "range over a pointer to an array")

	var nilp *[2]int
	count := 0
	for i := range nilp { // does not index: no nil dereference
		count += i + 1
	}
	cprover.Assert(count == 3, "only the length of a nil *[2]int is used")

	var k, v int
	for k, v = range [2]int{7, 8} {
	}
	cprover.Assert(k == 1 && v == 8, "preexisting iteration variables")

	var fs [3]func() int
	for i := range 3 {
		fs[i] = func() int { return i }
	}
	cprover.Assert(fs[0]() == 0 && fs[2]() == 2, "per-iteration variables")

	n := 0
Outer:
	for i := range 3 {
		for j := range 3 {
			if j > i {
				continue Outer
			}
			n++
		}
	}
	cprover.Assert(n == 6, "labelled continue in range loops")
}
