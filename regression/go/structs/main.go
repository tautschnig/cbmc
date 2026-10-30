// Go1.27 §Struct_types: fields, embedded fields and promotion ("Promoted
// fields act like ordinary fields of a struct."). §Selectors: x.f denotes
// the field at the shallowest depth, through embedded pointers ("p.x is
// (*(*p).T0).x"), and "if the type of x is a defined pointer type and
// (*x).f is a valid selector expression denoting a field (but not a
// method), x.f is shorthand for (*x).f." Structs are values: assignment
// and parameter passing copy them.
package main

import "cprover"

type T0 struct{ x int }
type T1 struct{ y int }
type T2 struct {
	z int
	T1
	*T0
}
type Q *T2

func bump(t T1) T1 {
	t.y++
	return t
}

func main() {
	t := T2{z: 1, T1: T1{y: 2}, T0: &T0{x: 3}}
	cprover.Assert(t.z == 1 && t.y == 2 && t.x == 3, "promoted fields")
	t.y = 20
	t.x = 30
	cprover.Assert(t.T1.y == 20 && t.T0.x == 30, "promoted fields assigned")

	p := &t
	p.z = 10
	cprover.Assert(t.z == 10 && p.y == 20 && p.x == 30, "selectors through *T2")
	var q Q = p
	q.x = 31
	cprover.Assert(t.x == 31, "x.f is (*x).f for a defined pointer type")

	u := t.T1
	u.y = 0
	cprover.Assert(t.y == 20, "struct assignment copies")
	cprover.Assert(bump(u).y == 1 && u.y == 0, "structs are passed by value")

	var zero T2
	cprover.Assert(zero.z == 0 && zero.T0 == nil, "zero value of a struct")

	type Celsius struct{ deg int } // a local type
	type Fahrenheit struct{ deg int }
	c := Celsius{100}
	f := Fahrenheit(c)
	cprover.Assert(f.deg == 100, "conversion between identical struct types")
}
