// Go1.27 §Type_parameter_declarations: "A type parameter list declares the
// type parameters of a generic function, method, or type declaration."
// §Method_declarations: "If the receiver base type is a generic type, the
// receiver specification must declare corresponding type parameters for the
// method to use." Each instance of a generic type is a type of its own, with
// its own methods, which interface values dispatch to.
package main

import "cprover"

type Pair[A, B any] struct {
	a A
	b B
}

func (p Pair[A, B]) Swap() Pair[B, A] { return Pair[B, A]{a: p.b, b: p.a} }

func (p *Pair[First, _]) SetFirst(a First) { p.a = a }

type Node[T any] struct {
	val  T
	next *Node[T]
}

func (n *Node[T]) Len() int {
	if n == nil {
		return 0
	}
	return 1 + n.next.Len()
}

func Push[T any](n *Node[T], v T) *Node[T] { return &Node[T]{val: v, next: n} }

// Fields and methods of embedded instances are promoted.
type Stack[T any] struct {
	*Node[T]
	name string
}

type Getter[T any] interface{ Get() T }

type Const[T any] struct{ c T }

func (c Const[T]) Get() T { return c.c }

func Use[T any](g Getter[T]) T { return g.Get() }

type Stringer interface{ String() string }

type Box[T any] struct{ v T }

func (Box[T]) String() string { return "box" }

func main() {
	p := Pair[int, string]{a: 1, b: "x"}
	q := p.Swap()
	cprover.Assert(q.a == "x" && q.b == 1, "swap")
	p.SetFirst(5) // (&p).SetFirst(5)
	cprover.Assert(p.a == 5, "pointer receiver")
	cprover.Assert(p == Pair[int, string]{5, "x"}, "equality of instances")

	set := (*Pair[int, string]).SetFirst
	set(&p, 9)
	swap := p.Swap
	p.a = 10
	cprover.Assert(swap().b == 9 && p.Swap().b == 10, "method values")

	m := map[Pair[int, int]]string{{1, 2}: "a"}
	cprover.Assert(m[Pair[int, int]{1, 2}] == "a", "instances as map keys")

	var n *Node[string]
	n = Push(n, "a")
	n = Push(n, "b")
	cprover.Assert(n.Len() == 2 && n.val == "b", "recursive generic type")

	s := Stack[string]{Node: n, name: "s"}
	cprover.Assert(s.Len() == 2 && s.val == "b", "promoted from an instance")

	cprover.Assert(Use[int](Const[int]{4}) == 4, "generic interface type")
	var str Stringer = Box[float64]{1.5}
	cprover.Assert(str.String() == "box", "dispatch to an instance's method")
	_, isInt := str.(Box[int])
	cprover.Assert(!isInt, "Box[float64] is not Box[int]")
}
