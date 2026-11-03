// --max-nondet-tree-depth bounds the references from an arbitrary value
// (here: lists of at most 2 nodes), and --min-null-tree-depth makes
// references above that depth non-nil (here: the list has a first node).
package main

import "cprover"

type Node struct {
	val  int
	next *Node
}

func main() {
	l := cprover.Nondet[*Node]()
	n := 0
	for p := l; p != nil; p = p.next {
		n++
	}
	cprover.Assert(n >= 1, "not nil")
	cprover.Assert(n <= 2, "bounded")
	cprover.Assert(n == 1, "two nodes possible")
}
