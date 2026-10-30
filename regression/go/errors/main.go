// Go1.27 §Errors: "The predeclared type error is defined as"
//
//	type error interface {
//		Error() string
//	}
//
// "It is the conventional interface for representing an error condition,
// with the nil value representing no error."
package main

import "cprover"

type NotFound struct{ key string }

func (e *NotFound) Error() string { return "not found: " + e.key }

func lookup(m map[string]int, k string) (int, error) {
	v, ok := m[k]
	if !ok {
		return 0, &NotFound{k}
	}
	return v, nil
}

func main() {
	m := map[string]int{"a": 1}
	v, err := lookup(m, "a")
	cprover.Assert(err == nil && v == 1, "no error")
	_, err = lookup(m, "b")
	cprover.Assert(err != nil && err.Error() == "not found: b", "an error")
	nf, ok := err.(*NotFound)
	cprover.Assert(ok && nf.key == "b", "the dynamic error type")
}
