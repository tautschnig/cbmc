// Go1.27 §Map_types: "If the key type is an interface type, these
// comparison operators must be defined for the dynamic key values; failure
// will cause a run-time panic." Also in an empty map, and for lookups.
package main

import "cprover"

func panics(f func()) (p bool) {
	defer func() { p = recover() != nil }()
	f()
	return false
}

func main() {
	m := map[any]int{}
	cprover.Assert(panics(func() { m[[]int{1}] = 1 }), "insert")
	cprover.Assert(panics(func() { _ = m[map[int]int{}] }), "lookup")
	var nilMap map[any]int
	cprover.Assert(panics(func() { _ = nilMap[func() {}] }), "nil map")
	m["k"] = 2
	cprover.Assert(!panics(func() { _ = m[nil] }) && m["k"] == 2,
		"comparable keys")
}
