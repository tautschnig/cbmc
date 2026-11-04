// The functions of imported packages that the program cannot reach are
// not converted (the helper prunes them), unless --go-keep-unreachable.
package main

import (
	"cprover"

	"example.com/pruned/lib"
)

func main() {
	cprover.Assert(lib.Twice(21) == 42, "twice")
}
