// A value whose conversion is unsupported (here, a variable of an
// unmodelled package) stored in an interface variable is reported where
// it is; the rest of the program is verified.
package main

import (
	"cprover"
	"encoding/binary"
)

func order() binary.ByteOrder {
	var o binary.ByteOrder = binary.BigEndian
	return o
}

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n < 100)
	if n > 1000 {
		cprover.Assert(order() != nil, "order")
	}
	cprover.Assert(n < 100, "n")
}
