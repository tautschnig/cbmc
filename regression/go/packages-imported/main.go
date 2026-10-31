// Go1.27 §Import_declarations: "An import declaration states that the
// source file containing the declaration depends on functionality of the
// imported package". User packages (here of the module example.com/shapes)
// are converted from source. §Package_initialization: "Given the list of
// all packages, sorted by import path, in each step the first uninitialized
// package in the list for which all imported packages (if any) are already
// initialized is initialized."
package main

import (
	"cprover"

	"example.com/shapes/geom"
	"example.com/shapes/internal/counter"
)

func init() { counter.Order = append(counter.Order, "main") }

func main() {
	var s geom.Shape = geom.Rect{W: 2, H: 3}
	cprover.Assert(s.Area() == 6, "dispatch to a method of another package")
	cprover.Assert(counter.N == 1, "a variable of another package")
	var sq geom.Shape = &geom.Square{Side: 3}
	cprover.Assert(sq.Area() == 9, "a type of another file of that package")
	cprover.Assert(geom.Unit.Area() == 1, "initialised by an init function")
	cprover.Assert(geom.Max(2, 5) == 5, "a generic function")
	f := geom.Max[float64]
	cprover.Assert(f(1.5, 0) == 1.5, "an instance as a function value")
	cprover.Assert(len(counter.Order) == 2 && counter.Order[0] == "c" &&
		counter.Order[1] == "main", "packages initialised first")
}
