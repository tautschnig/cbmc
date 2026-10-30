// Go1.27 §Interface_types: "A variable of interface type can store a value
// of any type that is in the type set of the interface. Such a type is said
// to implement the interface. The value of an uninitialized
// variable of interface type is nil." Method calls on interface values call
// the method of the dynamic type (§Method_sets).
package main

import "cprover"

type Shape interface {
	Area() int
	Scale(int)
}

type Sq struct{ s int }

func (q *Sq) Area() int   { return q.s * q.s }
func (q *Sq) Scale(f int) { q.s *= f }

type Rect struct{ w, h int }

func (r *Rect) Area() int   { return r.w * r.h }
func (r *Rect) Scale(f int) { r.w *= f; r.h *= f }

func total(shapes []Shape) int {
	t := 0
	for _, s := range shapes {
		t += s.Area()
	}
	return t
}

func main() {
	var s Shape
	cprover.Assert(s == nil, "zero value is nil")
	sq := &Sq{2}
	s = sq
	s.Scale(3)
	cprover.Assert(sq.s == 6 && s.Area() == 36, "dynamic type *Sq")
	shapes := []Shape{&Sq{1}, &Rect{2, 3}, sq}
	cprover.Assert(total(shapes) == 1+6+36, "dynamic dispatch")

	var a any = 5
	var b any = "five"
	var c interface{} = [2]int{1, 2}
	cprover.Assert(a != nil && b != nil && c != nil, "any holds values")

	var which Shape = &Rect{1, 1}
	if cprover.Nondet[bool]() {
		which = &Sq{3}
	}
	area := which.Area()
	cprover.Assert(area == 1 || area == 9, "either dynamic type")
}
