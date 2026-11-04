// A dynamic type whose comparison the front-end does not support (arrays
// of more than 64 structs with blank fields) fails only where interface
// values of that type are compared; other comparisons and reflection on
// the type work.
package main

import (
	"cprover"
	"reflect"
)

type padded struct {
	_ int
	X int
}

type table [65]padded

func main() {
	var t any = table{}
	var u any = 1
	cprover.Assert(u == any(1), "other comparison")
	cprover.Assert(reflect.TypeOf(t).Len() == 65, "reflection")
	if cprover.Nondet[bool]() {
		_ = t == any(table{})
	}
}
