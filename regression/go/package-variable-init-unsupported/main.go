// Go1.27 §Package_initialization: the initialisation of a package
// variable of an unsupported type fails a property, as package
// initialisation runs before main.
package main

import (
	"cprover"
	"unsafe"
)

var x int

var p = unsafe.Pointer(&x)

func main() {
	cprover.Assert(x == 0, "x")
}
