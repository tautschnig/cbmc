// Go1.27 §For_range: "The iteration order over maps is not specified and is
// not guaranteed to be the same from one iteration to the next." A program
// that relies on an order must fail verification. "If a map entry that has
// not yet been reached is removed during iteration, the corresponding
// iteration value will not be produced."
package main

import "cprover"

func main() {
	m := map[int]int{1: 10, 2: 20, 3: 30}
	first := 0
	for k := range m {
		first = k
		break
	}
	cprover.Assert(first >= 1 && first <= 3, "some key comes first")
	cprover.Assert(first == 1, "but not necessarily key 1")

	seen := 0
	for k := range m {
		if k == 1 {
			delete(m, 2)
		}
		seen++
	}
	cprover.Assert(seen == 2 || seen == 3, "a removed entry may not be reached")
}
