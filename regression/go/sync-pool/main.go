// The model of sync.Pool: Get returns an item that was put and not yet
// got, or the result of New (nil without New), and never the same item
// twice.
package main

import (
	"cprover"
	"sync"
)

type buffer struct{ id int }

func main() {
	made := 0
	p := sync.Pool{New: func() any { made++; return &buffer{100 + made} }}
	a, b := &buffer{1}, &buffer{2}
	p.Put(a)
	p.Put(b)
	p.Put(nil) // ignored
	x := p.Get().(*buffer)
	y := p.Get().(*buffer)
	cprover.Assert(x != y, "an item is got once")
	cprover.Assert(x == a || x == b || x.id == 100+made || x.id == 99+made,
		"put or new")
	cprover.Assert(made <= 2, "New is called only if needed")

	var q sync.Pool
	cprover.Assert(q.Get() == nil, "empty pool without New")
	q.Put(a)
	if v := q.Get(); v != nil {
		cprover.Assert(v.(*buffer) == a, "the item put")
	}
}
