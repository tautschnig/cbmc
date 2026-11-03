// Go1.27 §Package_unsafe: conversions between pointers, unsafe.Pointer and
// uintptr, pointer arithmetic, and Add, Slice, SliceData, String and
// StringData.
package main

import (
	"cprover"
	"unsafe"
)

type hdr struct {
	a int32
	b int32
}

func main() {
	// *T <-> unsafe.Pointer
	x := 5
	p := unsafe.Pointer(&x)
	q := (*int)(p)
	*q = 6
	cprover.Assert(x == 6, "through unsafe.Pointer")
	cprover.Assert(p != nil, "not nil")
	var z unsafe.Pointer
	cprover.Assert(z == nil, "zero is nil")

	// type punning
	f := 1.0
	bits := *(*uint64)(unsafe.Pointer(&f))
	cprover.Assert(bits == 0x3ff0000000000000, "float bits")

	// field access through Add / uintptr arithmetic
	h := hdr{1, 2}
	pb := (*int32)(unsafe.Add(unsafe.Pointer(&h), unsafe.Offsetof(h.b)))
	cprover.Assert(*pb == 2, "Add")
	pb2 := (*int32)(unsafe.Pointer(uintptr(unsafe.Pointer(&h)) + unsafe.Offsetof(h.b)))
	cprover.Assert(*pb2 == 2, "uintptr arithmetic")

	// Slice / String / SliceData / StringData
	arr := [3]byte{'a', 'b', 'c'}
	s := unsafe.Slice(&arr[0], 3)
	cprover.Assert(len(s) == 3 && cap(s) == 3 && s[2] == 'c', "Slice")
	str := unsafe.String(&arr[0], 2)
	cprover.Assert(str == "ab", "String")
	cprover.Assert(unsafe.SliceData(s) == &arr[0], "SliceData")
	cprover.Assert(*unsafe.StringData(str) == 'a', "StringData")

	// zero-copy conversions
	b := []byte("hi")
	s2 := unsafe.String(unsafe.SliceData(b), len(b))
	cprover.Assert(s2 == "hi", "bytes to string")
	b2 := unsafe.Slice(unsafe.StringData(s2), len(s2))
	cprover.Assert(len(b2) == 2 && b2[1] == 'i', "string to bytes")
}
