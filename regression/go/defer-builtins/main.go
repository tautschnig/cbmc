// Go1.27 §Defer_statements: deferred calls of the built-in functions
// delete and close; their arguments are evaluated when the defer statement
// executes.
package main

import "cprover"

func work(m map[string]int, k string, ch chan int) {
	defer delete(m, k)
	defer close(ch)
	k = "other"
	cprover.Assert(m["a"] == 1, "not deleted yet")
}

func main() {
	m := map[string]int{"a": 1, "other": 2}
	ch := make(chan int)
	work(m, "a", ch)
	_, present := m["a"]
	cprover.Assert(!present && m["other"] == 2, "deleted the key at defer")
	_, ok := <-ch
	cprover.Assert(!ok, "closed")
}
