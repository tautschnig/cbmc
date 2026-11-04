// reflect with package unsafe: SliceHeader and StringHeader, NewAt,
// UnsafeAddr, Pointer and UnsafePointer.
package main

import (
	"cprover"
	"reflect"
	"unsafe"
)

type T struct {
	a int
	B string
}

func s2b(s string) (b []byte) {
	sh := (*reflect.StringHeader)(unsafe.Pointer(&s))
	bh := (*reflect.SliceHeader)(unsafe.Pointer(&b))
	bh.Data = sh.Data
	bh.Len = sh.Len
	bh.Cap = sh.Len
	return b
}

func main() {
	xs := []int{1, 2, 3}
	h := (*reflect.SliceHeader)(unsafe.Pointer(&xs))
	cprover.Assert(h.Len == 3 && h.Cap == 3, "slice header")
	b := s2b("hi")
	cprover.Assert(len(b) == 2 && b[1] == 'i', "string to bytes")
	x := 5
	v := reflect.NewAt(reflect.TypeOf(0), unsafe.Pointer(&x)).Elem()
	v.SetInt(6)
	cprover.Assert(x == 6, "new at")
	t := T{1, "b"}
	f := reflect.ValueOf(&t).Elem().Field(0)
	p := (*int)(unsafe.Pointer(f.UnsafeAddr()))
	*p = 9
	cprover.Assert(t.a == 9, "unsafe addr")
	w := reflect.NewAt(f.Type(), unsafe.Pointer(f.UnsafeAddr())).Elem()
	w.SetInt(10)
	cprover.Assert(t.a == 10, "unexported field write")
	cprover.Assert(reflect.ValueOf(&t).Pointer() == uintptr(unsafe.Pointer(&t)), "pointer")
	cprover.Assert(reflect.ValueOf(xs).UnsafePointer() == unsafe.Pointer(&xs[0]), "slice data")
}
