// Go1.27 §Composite_literals: "For struct literals without keys, the
// element list must contain an element for each struct field in the order
// in which the fields are declared." With keys, "Each key must be a valid
// field selector [Go 1.27] for a (possibly promoted) field of the struct"
// and "Omitted fields get the zero value for that field." "Taking the
// address of a composite literal generates a pointer to a unique variable
// initialized with the literal's value."
package main

import "cprover"

type Object struct{ name, color int }
type Point3D struct {
	Object
	x, y, z int
}

func main() {
	origin := Point3D{}
	cprover.Assert(origin == Point3D{}, "zero value")

	p := Point3D{Object{1, 2}, 3, 4, 5}
	cprover.Assert(p.name == 1 && p.color == 2 && p.z == 5, "positional")

	q := Point3D{y: 7, name: 8} // name is promoted from Object [Go 1.27]
	cprover.Assert(q.y == 7 && q.name == 8 && q.x == 0 && q.color == 0,
		"keyed, with a promoted field and omitted fields")

	r1, r2 := &Point3D{x: 1}, &Point3D{x: 1}
	cprover.Assert(r1 != r2 && *r1 == *r2, "each &T{} is a unique variable")

	pts := [2]*Object{{1, 2}, {}} // elided &Object
	cprover.Assert(pts[0].color == 2 && pts[1].name == 0, "elided &T")
}
