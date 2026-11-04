// Values through reflection: reading and setting through pointers,
// fields, elements and interfaces, conversions, and new values.
package main

import (
	"cprover"
	"reflect"
)

type Pair struct {
	A int8
	B string
	c bool
}

type Named int

func main() {
	p := Pair{-3, "b", true}
	v := reflect.ValueOf(p)
	cprover.Assert(v.Field(0).Int() == -3 && v.Field(1).String() == "b" && v.Field(2).Bool(), "read fields")
	cprover.Assert(!v.CanSet() && !v.Field(0).CanAddr(), "not addressable")
	cprover.Assert(v.Field(0).CanInterface() && !v.Field(2).CanInterface(), "unexported")

	e := reflect.ValueOf(&p).Elem()
	cprover.Assert(e.CanSet() && e.Field(0).CanSet() && !e.Field(2).CanSet(), "settable")
	e.Field(0).SetInt(5)
	e.FieldByName("B").SetString("x")
	cprover.Assert(p.A == 5 && p.B == "x", "set fields")
	e.Set(reflect.ValueOf(Pair{A: 1}))
	cprover.Assert(p.A == 1 && p.B == "" && !p.c, "set struct")

	xs := []uint16{1, 2, 3}
	s := reflect.ValueOf(xs)
	s.Index(1).SetUint(7)
	cprover.Assert(xs[1] == 7 && s.Len() == 3 && s.Cap() == 3, "slice elements")
	sub := s.Slice(1, 3)
	cprover.Assert(sub.Len() == 2 && sub.Index(0).Uint() == 7, "slice")
	cprover.Assert(reflect.ValueOf("hey").Index(1).Uint() == 'e', "string index")

	var i any = Named(4)
	iv := reflect.ValueOf(&i).Elem()
	cprover.Assert(iv.Kind() == reflect.Interface && iv.Elem().Int() == 4, "interface element")
	iv.Set(reflect.ValueOf("now a string"))
	cprover.Assert(i == "now a string", "set interface")

	n := reflect.New(reflect.TypeOf(Named(0)))
	n.Elem().SetInt(9)
	cprover.Assert(*n.Interface().(*Named) == 9, "new")
	cprover.Assert(reflect.Zero(reflect.TypeOf(p)).IsZero() && !reflect.ValueOf(p).IsZero(), "zero")

	conv := reflect.ValueOf(Named(300)).Convert(reflect.TypeOf(uint8(0)))
	cprover.Assert(conv.Uint() == 44, "convert truncates")
	f := reflect.ValueOf(2.75).Convert(reflect.TypeOf(0))
	cprover.Assert(f.Int() == 2, "float to int")
	str := reflect.ValueOf([]byte("ab")).Convert(reflect.TypeOf(""))
	cprover.Assert(str.String() == "ab", "bytes to string")

	var nilp *Pair
	cprover.Assert(reflect.ValueOf(nilp).IsNil() && !reflect.ValueOf(nilp).Elem().IsValid(), "nil pointer")
	cprover.Assert(!reflect.ValueOf(nil).IsValid(), "invalid")
	a := reflect.ValueOf(&xs).Elem().Index(0).Addr()
	cprover.Assert(a.Interface().(*uint16) == &xs[0], "addr")
	cprover.Assert(reflect.ValueOf(&p).Pointer() == reflect.ValueOf(&p).Pointer(), "pointer")
	k := reflect.ValueOf(Named(1)).Interface()
	_, isNamed := k.(Named)
	cprover.Assert(isNamed, "interface keeps type")
}
