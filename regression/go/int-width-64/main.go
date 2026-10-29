// Go1.27 §Numeric_types: int has an implementation-specific size; gc on
// a 64-bit target uses 64 bits (config.go.int_width).
package main

import "cprover"

func main() {
	var x int = 1<<31 - 1
	x++
	cprover.Assert(x > 0, "int is wider than 32 bits")
	var u uintptr = 1 << 40
	cprover.Assert(u > 0, "uintptr holds 64-bit values")
}
