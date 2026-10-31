// Go1.27 §Packages: "A package in turn is constructed from one or more
// source files that together declare constants, types, variables and
// functions belonging to the package and which are accessible in all files
// of the same package." The files given to cbmc form the package, as for
// go run; init functions run "in the order they appear in the source,
// possibly in multiple files, as presented to the compiler"
// (§Package_initialization).
package main

import "cprover"

var order []string

func init() { order = append(order, "main.go") }

func main() {
	var s Shape = Rect{2, 3}
	cprover.Assert(s.Area() == 6, "type and method of another file")
	cprover.Assert(limit == 10 && scaled == 20, "constant and variable")
	cprover.Assert(len(order) == 2 && order[0] == "main.go" &&
		order[1] == "shapes.go", "init functions in file order")
}
