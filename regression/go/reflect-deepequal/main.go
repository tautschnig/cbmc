// reflect.DeepEqual on nested values.
package main

import (
	"cprover"
	"reflect"
)

type Node struct {
	Value int
	Next  *Node
	Tags  map[string][]int
	Any   any
}

func main() {
	a := Node{1, &Node{Value: 2}, map[string][]int{"x": {1, 2}}, []string{"s"}}
	b := Node{1, &Node{Value: 2}, map[string][]int{"x": {1, 2}}, []string{"s"}}
	cprover.Assert(reflect.DeepEqual(a, b), "equal")
	b.Next.Value = 3
	cprover.Assert(!reflect.DeepEqual(a, b), "pointee differs")
	b.Next.Value = 2
	b.Tags["x"][1] = 5
	cprover.Assert(!reflect.DeepEqual(a, b), "map element differs")
	b.Tags["x"][1] = 2
	b.Any = []string{"t"}
	cprover.Assert(!reflect.DeepEqual(a, b), "interface differs")
	cprover.Assert(!reflect.DeepEqual([]int(nil), []int{}), "nil and empty")
	cprover.Assert(!reflect.DeepEqual(1, int64(1)), "types differ")
	var x, y error
	cprover.Assert(reflect.DeepEqual(x, y), "nil interfaces")
	f := 0.5
	cprover.Assert(reflect.DeepEqual(&f, &f), "same pointer")
}
