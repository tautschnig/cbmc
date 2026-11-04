// The functions and methods of imported packages that the program cannot
// reach are not converted (the helper prunes them), unless
// --go-keep-unreachable; methods called through interfaces are reached.
package main

import (
	"cprover"

	"example.com/pruned/lib"
)

func main() {
	cprover.Assert(lib.Twice(21) == 42, "twice")
	var s interface{ Area() int } = lib.Disc{R: 2}
	cprover.Assert(s.Area() == 12, "method through an interface")
}
