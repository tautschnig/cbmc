// Types built by reflect: SliceOf (also for a type whose slice type the
// program does not mention), MapOf, ArrayOf, and slices of arrays.
package main

import (
	"cprover"
	"reflect"
)

type Point struct{ X, Y int }

func main() {
	st := reflect.SliceOf(reflect.TypeOf(Point{}))
	cprover.Assert(st.Kind() == reflect.Slice && st.Elem() == reflect.TypeOf(Point{}), "slice of")
	cprover.Assert(st.String() == "[]main.Point", "slice of string")
	s := reflect.MakeSlice(st, 1, 1)
	s.Index(0).Field(1).SetInt(3)
	cprover.Assert(s.Index(0).Interface().(Point).Y == 3, "make slice of")
	mt := reflect.MapOf(reflect.TypeOf(""), reflect.TypeOf(0))
	cprover.Assert(mt == reflect.TypeOf(map[string]int(nil)), "map of")
	at := reflect.ArrayOf(2, reflect.TypeOf(""))
	cprover.Assert(at == reflect.TypeOf([2]string{}), "array of")
	arr := [3]int{1, 2, 3}
	sl := reflect.ValueOf(&arr).Elem().Slice(1, 3)
	cprover.Assert(sl.Len() == 2 && sl.Cap() == 2 && sl.Index(0).Int() == 2, "array slice")
	sl.Index(1).SetInt(7)
	cprover.Assert(arr[2] == 7, "shares the array")
}
