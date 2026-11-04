// Go1.27 §Declarations_and_scope: "The scope of a type identifier declared
// inside a function begins at the identifier in the TypeSpec and ends at
// the end of the innermost containing block." In a generic function such
// a type may depend on the type parameters; each instance has a type of
// its own (as with gc, whose reflect names it after the instance's type
// arguments).
package main

import (
	"cprover"
	"reflect"
)

type stringer interface{ String() string }

type holder[T any] struct{ v T }

func (h holder[T]) String() string { return "holder" }

func pairOf[T any](a, b T) [2]T {
	type pair struct{ x, y T }
	p := pair{a, b}
	return [2]T{p.x, p.y}
}

// A local type that refers to itself.
func length[T any](xs ...T) int {
	type node struct {
		v    T
		next *node
	}
	var head *node
	for _, x := range xs {
		head = &node{x, head}
	}
	n := 0
	for p := head; p != nil; p = p.next {
		n++
	}
	return n
}

// Promoted methods of an embedded generic type.
func describe[T any](v T) string {
	type outer struct{ holder[T] }
	var s stringer = outer{holder[T]{v}}
	return s.String()
}

// tagged returns a value of its local type, and whether x has that type.
func tagged[T any](x any) (any, bool) {
	type tag struct{}
	_, ok := x.(tag)
	return tag{}, ok
}

type Box[T any] struct{}

func (Box[T]) Local() reflect.Type {
	type loc struct{ v T }
	return reflect.TypeOf(loc{})
}

func main() {
	r := pairOf(1, 2)
	cprover.Assert(r[0] == 1 && r[1] == 2, "pair of int")
	cprover.Assert(pairOf("a", "b")[1] == "b", "pair of string")
	cprover.Assert(length(1, 2, 3) == 3, "list of int")
	cprover.Assert(length("x") == 1, "list of string")
	cprover.Assert(describe(1.5) == "holder", "promoted method")

	ti, _ := tagged[int](nil)
	ts, _ := tagged[string](nil)
	_, same := tagged[int](ti)
	cprover.Assert(same, "an instance's local type is the same type")
	_, other := tagged[int](ts)
	cprover.Assert(!other, "each instance has a type of its own")

	t := Box[int]{}.Local()
	cprover.Assert(t.Name() == "loc[int]", "named after the type arguments")
	cprover.Assert(t.String() == "main.loc[int]", "with its package")
}
