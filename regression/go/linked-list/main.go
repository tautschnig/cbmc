// Go1.27 §Struct_types: "A struct type T may not contain a field of type
// T", but "T5 struct{ f *T5 } // T5 contains T5 as component of a pointer"
// is valid. A list built from heap variables (§Allocation).
package main

import "cprover"

type Node struct {
	value int
	next  *Node
}

func push(head *Node, v int) *Node {
	return &Node{value: v, next: head}
}

func sum(n *Node) int {
	s := 0
	for ; n != nil; n = n.next {
		s += n.value
	}
	return s
}

func main() {
	var list *Node
	for i := 1; i <= 3; i++ {
		list = push(list, i)
	}
	cprover.Assert(list.value == 3 && list.next.next.value == 1, "order")
	cprover.Assert(sum(list) == 6, "traversal")
	cprover.Assert(list.next.next.next == nil, "terminated by nil")
}
