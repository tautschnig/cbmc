// The model of sync.Map: the methods behave as on a map[any]any, and
// Range visits every key present when it starts exactly once.
package main

import (
	"cprover"
	"sync"
)

func main() {
	var m sync.Map
	_, ok := m.Load("a")
	cprover.Assert(!ok, "empty")
	m.Store("a", 1)
	m.Store("b", 2)
	v, ok := m.Load("a")
	cprover.Assert(ok && v == 1, "stored")
	actual, loaded := m.LoadOrStore("a", 3)
	cprover.Assert(loaded && actual == 1, "LoadOrStore loads")
	actual, loaded = m.LoadOrStore("c", 3)
	cprover.Assert(!loaded && actual == 3, "LoadOrStore stores")
	prev, loaded := m.Swap("c", 4)
	cprover.Assert(loaded && prev == 3, "Swap")
	cprover.Assert(!m.CompareAndSwap("c", 3, 5), "CompareAndSwap fails")
	cprover.Assert(m.CompareAndSwap("c", 4, 5), "CompareAndSwap swaps")
	cprover.Assert(!m.CompareAndDelete("x", nil), "no entry")
	cprover.Assert(m.CompareAndDelete("c", 5), "CompareAndDelete")
	v, loaded = m.LoadAndDelete("b")
	cprover.Assert(loaded && v == 2, "LoadAndDelete")
	m.Delete("b")
	m.Store("d", 4)

	sum, n := 0, 0
	m.Range(func(k, v any) bool {
		sum += v.(int)
		n++
		return true
	})
	cprover.Assert(n == 2 && sum == 5, "Range visits a and d")

	n = 0
	m.Range(func(k, v any) bool { n++; return false })
	cprover.Assert(n == 1, "Range stops")

	m.Clear()
	_, ok = m.Load("a")
	cprover.Assert(!ok, "cleared")
}
