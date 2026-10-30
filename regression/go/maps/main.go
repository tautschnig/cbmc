// Go1.27 §Map_types: "A map is an unordered group of elements of one type,
// called the element type, indexed by a set of unique keys of another
// type". "The value of an uninitialized map is nil." §Index_expressions:
// "if the map is nil or does not contain such an entry, a[x] is the zero
// value for the element type of M"; for "v, ok = a[x]", "The value of ok is
// true if the key x is present in the map, and false otherwise."
// §Deletion_of_map_elements: "If the map m is nil or the element m[k] does
// not exist, delete is a no-op." §Clear: "clear(m) map[K]T deletes all
// entries, resulting in an empty map (len(m) == 0)".
package main

import "cprover"

func main() {
	var empty map[string]int
	cprover.Assert(empty == nil && len(empty) == 0 && empty["x"] == 0, "nil map")
	delete(empty, "x")

	m := map[string]int{"a": 1, "b": 2}
	cprover.Assert(len(m) == 2 && m["a"] == 1 && m["b"] == 2, "literal")
	v, ok := m["a"]
	cprover.Assert(v == 1 && ok, "present")
	v, ok = m["z"]
	cprover.Assert(v == 0 && !ok, "absent")

	m["c"] = 3
	m["a"] = 10
	cprover.Assert(len(m) == 3 && m["a"] == 10 && m["c"] == 3, "insert, update")
	m["b"] += 5
	m["d"]++
	cprover.Assert(m["b"] == 7 && m["d"] == 1, "op= and ++ start from zero")
	delete(m, "a")
	delete(m, "missing")
	_, ok = m["a"]
	cprover.Assert(len(m) == 3 && !ok, "delete")

	alias := m
	alias["e"] = 5
	cprover.Assert(m["e"] == 5, "map values refer to the same map")

	sum, count := 0, 0
	for _, x := range m {
		sum += x
		count++
	}
	cprover.Assert(count == 4 && sum == 3+7+1+5, "range visits every entry once")

	made := make(map[int]bool, 100)
	cprover.Assert(made != nil && len(made) == 0, "make")
	made[cprover.Nondet[int]()] = true
	clear(made)
	cprover.Assert(len(made) == 0, "clear")
}
