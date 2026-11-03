// Go1.27 §Package_initialization: the initialisation of a package
// variable of an unsupported type fails a property, as package
// initialisation runs before main.
package main

import "cprover"

// A type that refers to itself other than through a struct or a
// function type, which is not supported.
type rec []rec

var x int

var p = make(rec, x)

func main() {
	cprover.Assert(x == 0, "x")
}
