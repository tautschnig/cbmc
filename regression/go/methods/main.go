// Go1.27 §Method_declarations: "A method is a function with a receiver."
// With the spec's example: Scale with receiver *Point. §Calls: "If x is
// addressable and &x's method set contains m, x.m() is shorthand for
// (&x).m()". §Method_sets: "The method set of a pointer to a defined type T
// ... is the set of all methods declared with receiver *T or T."
// §Struct_types: promoted methods of embedded fields.
package main

import "cprover"

type Point struct{ x, y int }

func (p *Point) Scale(factor int) {
	p.x *= factor
	p.y *= factor
}

func (p Point) Sum() int { return p.x + p.y }

type Celsius int // methods on any defined type

func (c Celsius) Fahrenheit() int { return int(c)*9/5 + 32 }

type Labelled struct {
	Point
	label string
}

type Ref struct{ *Point } // an embedded pointer

func main() {
	var p Point
	p.x, p.y = 1, 2
	p.Scale(3) // (&p).Scale(3)
	cprover.Assert(p.x == 3 && p.y == 6, "pointer method on an addressable value")
	cprover.Assert(p.Sum() == 9, "value method")
	q := &p
	cprover.Assert(q.Sum() == 9, "value method through a pointer: (*q).Sum()")
	q.Scale(2)
	cprover.Assert(p.x == 6, "pointer method through a pointer")

	cprover.Assert(Celsius(100).Fahrenheit() == 212, "method of a non-struct type")

	l := Labelled{Point{1, 1}, "a"}
	l.Scale(5)
	cprover.Assert(l.Sum() == 10 && l.x == 5, "promoted methods")
	r := Ref{&p}
	r.Scale(0)
	cprover.Assert(p.x == 0 && r.Sum() == 0, "promoted through an embedded pointer")

	pts := []Point{{1, 2}, {3, 4}}
	pts[1].Scale(10) // slice elements are addressable
	cprover.Assert(pts[1].Sum() == 70, "pointer method on a slice element")
}
