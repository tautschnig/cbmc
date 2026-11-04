// Maps and slices through reflection: lookups, updates, iteration, new
// maps and slices, appending and copying.
package main

import (
	"cprover"
	"reflect"
)

func main() {
	m := map[string]int{"a": 1}
	mv := reflect.ValueOf(m)
	cprover.Assert(mv.Len() == 1 && mv.MapIndex(reflect.ValueOf("a")).Int() == 1, "lookup")
	cprover.Assert(!mv.MapIndex(reflect.ValueOf("b")).IsValid(), "missing")
	mv.SetMapIndex(reflect.ValueOf("b"), reflect.ValueOf(2))
	cprover.Assert(m["b"] == 2 && mv.Len() == 2, "store")
	mv.SetMapIndex(reflect.ValueOf("a"), reflect.Value{})
	_, present := m["a"]
	cprover.Assert(!present && len(mv.MapKeys()) == 1, "delete")
	sum := 0
	for it := mv.MapRange(); it.Next(); {
		sum += int(it.Value().Int()) + len(it.Key().String())
	}
	cprover.Assert(sum == 3, "range")

	var keys map[any]bool = map[any]bool{1: true}
	kv := reflect.ValueOf(keys)
	cprover.Assert(kv.MapIndex(reflect.ValueOf(1)).Bool(), "interface keys")

	mt := reflect.TypeOf(map[int]string{})
	nm := reflect.MakeMap(mt)
	nm.SetMapIndex(reflect.ValueOf(3), reflect.ValueOf("c"))
	cprover.Assert(nm.Interface().(map[int]string)[3] == "c", "make map")

	st := reflect.TypeOf([]int{})
	s := reflect.MakeSlice(st, 1, 1)
	s.Index(0).SetInt(1)
	s = reflect.Append(s, reflect.ValueOf(2), reflect.ValueOf(3))
	xs := s.Interface().([]int)
	cprover.Assert(len(xs) == 3 && xs[0] == 1 && xs[2] == 3, "append")
	s = reflect.AppendSlice(s, reflect.ValueOf([]int{4}))
	cprover.Assert(s.Len() == 4 && s.Index(3).Int() == 4, "append slice")
	dst := make([]int, 2)
	n := reflect.Copy(reflect.ValueOf(dst), s)
	cprover.Assert(n == 2 && dst[0] == 1 && dst[1] == 2, "copy")

	var p *[]int = &xs
	pv := reflect.ValueOf(p).Elem()
	pv.SetLen(2)
	cprover.Assert(len(xs) == 2, "set len")
	arr := [2]string{"x", "y"}
	av := reflect.ValueOf(&arr).Elem()
	av.Index(1).SetString("z")
	cprover.Assert(arr[1] == "z" && av.Len() == 2, "array")
}
