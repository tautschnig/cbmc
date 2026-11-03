// Go1.27 §Conversions to a named type whose underlying type is that of the
// value (S(d) of a []int, E(len(d)) of an int) give a value of the named
// type, which is its dynamic type when stored in an interface.
package main

import "cprover"

type I interface{ Len() int }

type S []int

func (s S) Len() int { return len(s) }

type E int

func (e E) Error() string { return "e" }

func main() {
	d := []int{1, 2}
	var x I = S(d)
	cprover.Assert(x.Len() == 2, "len")
	var err error = E(len(d))
	_, ok := err.(E)
	cprover.Assert(ok, "named int")
}
