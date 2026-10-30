// Go1.27 §Index_expressions: "Assigning to an element of a nil map causes a
// run-time panic."
package main

import "cprover"

func main() {
	var m map[string]int
	if cprover.Nondet[bool]() {
		m = map[string]int{}
	}
	m["k"] = 1
}
