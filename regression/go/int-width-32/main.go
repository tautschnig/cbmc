// Go1.27 §Numeric_types: with --32, CBMC targets 386, where gc's int is
// 32 bits; int arithmetic wraps at 2^31 (Go1.27 §Integer_overflow).
package main

import "cprover"

func main() {
	var x int = 1<<31 - 1
	x++
	cprover.Assert(x < 0, "int is 32 bits and wraps")
	var u uint = 0
	u--
	cprover.Assert(u == 4294967295, "uint is 32 bits")
}
