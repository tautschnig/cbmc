// Go1.27 §Package_unsafe: unsafe.Sizeof and unsafe.Alignof are constants
// unless the argument's type depends on type parameters; in each instance
// of a generic function, the front-end uses gc's size of the instance's
// type.
package main

import (
	"cprover"
	"unsafe"
)

func size[T any]() uintptr {
	var x T
	return unsafe.Sizeof(x)
}

func align[T any]() uintptr {
	var x T
	return unsafe.Alignof(x)
}

func main() {
	cprover.Assert(size[int32]() == 4 && size[[3]int64]() == 24, "sizes")
	cprover.Assert(size[string]() == 16 && size[struct {
		a byte
		b int64
	}]() == 16, "layout")
	cprover.Assert(align[int16]() == 2 && align[[2]int64]() == 8, "alignments")
}
