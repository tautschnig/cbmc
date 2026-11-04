// reflect's panics: a method of the wrong kind, setting an unaddressable
// value, and Interface of an unexported field.
package main

import (
	"cprover"
	"reflect"
)

type T struct{ hidden int }

func panics(f func()) (p any) {
	defer func() { p = recover() }()
	f()
	return nil
}

func main() {
	p := panics(func() { reflect.ValueOf("s").Int() })
	e, ok := p.(*reflect.ValueError)
	cprover.Assert(ok && e.Kind == reflect.String && e.Method == "reflect.Value.Int", "value error")
	cprover.Assert(e.Error() == "reflect: call of reflect.Value.Int on string Value", "message")
	p = panics(func() { reflect.ValueOf(1).SetInt(2) })
	cprover.Assert(p == "reflect: reflect.Value.SetInt using unaddressable value", "unaddressable")
	p = panics(func() { reflect.ValueOf(T{}).Field(0).Interface() })
	cprover.Assert(p != nil, "unexported")
	reflect.ValueOf(0).Len()
}
