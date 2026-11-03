// cprover.Nondet of types with references, as CBMC's object factory builds
// C inputs (--max-nondet-tree-depth, default 5): pointers are nil or point
// to new arbitrary values, maps have at most 5 entries with distinct keys,
// slices of such elements have at most 5, and interface values hold nil
// or an arbitrary value of one of the program's types that implement the
// interface (Go1.27 §Interface_types).
package main

import "cprover"

type Node struct {
	val  int
	next *Node
}

type Shape interface{ Area() int }

type Sq struct{ s int }

func (q Sq) Area() int { return q.s * q.s }

type Rect struct{ w, h int }

func (r *Rect) Area() int { return r.w * r.h }

func length(n *Node) int {
	c := 0
	for ; n != nil; n = n.next {
		c++
	}
	return c
}

func main() {
	p := cprover.Nondet[*int]()
	if p != nil {
		*p = 3
		cprover.Assert(*p == 3, "write through pointer")
	}
	l := cprover.Nondet[*Node]()
	cprover.Assert(length(l) <= 5, "list bounded by depth")
	cprover.Assert(length(l) < 5, "list can be long")
	m := cprover.Nondet[map[int]int]()
	cprover.Assert(len(m) <= 5, "map bounded")
	cprover.Assert(m == nil || len(m) < 3, "map can have entries")
	s := cprover.Nondet[Shape]()
	if s != nil {
		_, isSq := s.(Sq)
		_, isRect := s.(*Rect)
		cprover.Assert(isSq || isRect, "known dynamic types")
		cprover.Assert(isSq, "can be a rect")
	}
	ss := cprover.Nondet[[]string]()
	cprover.Assert(len(ss) <= 5, "slice of strings bounded")
}
