// Go1.27 §Package_unsafe: "At run time, if len is negative, or if ptr is
// nil and len is not zero, a run-time panic occurs". Values of type
// unsafe.Pointer in interfaces and as map keys.
package main

import (
	"cprover"
	"unsafe"
)

func panics(f func()) (msg string) {
	defer func() {
		if r := recover(); r != nil {
			msg = r.(error).Error()
		}
	}()
	f()
	return ""
}

func main() {
	var nilp *byte
	cprover.Assert(unsafe.Slice(nilp, 0) == nil, "nil Slice")
	cprover.Assert(unsafe.String(nilp, 0) == "", "empty String")
	cprover.Assert(panics(func() { _ = unsafe.Slice(nilp, 1) }) ==
		"runtime error: unsafe.Slice: ptr is nil and len is not zero", "nil ptr")
	n := -1
	b := byte(1)
	cprover.Assert(panics(func() { _ = unsafe.String(&b, n) }) ==
		"runtime error: unsafe.String: len out of range", "negative len")

	x := 1
	var i any = unsafe.Pointer(&x)
	p, ok := i.(unsafe.Pointer)
	cprover.Assert(ok && p == unsafe.Pointer(&x), "boxed")
	m := map[unsafe.Pointer]int{unsafe.Pointer(&x): 3}
	cprover.Assert(m[p] == 3, "map key")

}
