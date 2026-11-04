// Calls through reflection (functions, methods, variadic functions) and
// types built by reflect (SliceOf, MapOf, slices of arrays).
package main

import (
	"cprover"
	"errors"
	"reflect"
)

type Account struct{ Balance int }

func (a *Account) Deposit(n int) error {
	if n < 0 {
		return errors.New("negative")
	}
	a.Balance += n
	return nil
}

func (a Account) Get() int { return a.Balance }

func sum(xs ...int) (int, bool) {
	t := 0
	for _, x := range xs {
		t += x
	}
	return t, len(xs) > 0
}

func main() {
	acc := &Account{1}
	v := reflect.ValueOf(acc)
	m := v.MethodByName("Deposit")
	cprover.Assert(m.IsValid() && m.Type().NumIn() == 1, "method value")
	out := m.Call([]reflect.Value{reflect.ValueOf(4)})
	cprover.Assert(len(out) == 1 && out[0].IsNil() && acc.Balance == 5, "call method")
	out = m.Call([]reflect.Value{reflect.ValueOf(-1)})
	cprover.Assert(!out[0].IsNil(), "error result")
	g := reflect.ValueOf(*acc).MethodByName("Get").Call(nil)
	cprover.Assert(g[0].Int() == 5, "value method")
	r := reflect.ValueOf(sum).Call([]reflect.Value{reflect.ValueOf(1), reflect.ValueOf(2)})
	cprover.Assert(r[0].Int() == 3 && r[1].Bool(), "variadic call")
	r = reflect.ValueOf(sum).CallSlice([]reflect.Value{reflect.ValueOf([]int{4})})
	cprover.Assert(r[0].Int() == 4, "call slice")
	st := reflect.SliceOf(reflect.TypeOf(Account{}))
	cprover.Assert(st.Kind() == reflect.Slice && st.Elem() == reflect.TypeOf(Account{}), "slice of")
	s := reflect.MakeSlice(st, 1, 1)
	s.Index(0).Field(0).SetInt(3)
	cprover.Assert(s.Interface().([]Account)[0].Balance == 3, "make slice of")
	mt := reflect.MapOf(reflect.TypeOf(""), reflect.TypeOf(0))
	cprover.Assert(mt == reflect.TypeOf(map[string]int(nil)), "map of")
	arr := [3]int{1, 2, 3}
	sl := reflect.ValueOf(&arr).Elem().Slice(1, 3)
	cprover.Assert(sl.Len() == 2 && sl.Index(0).Int() == 2, "array slice")
}
