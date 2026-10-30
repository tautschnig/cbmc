// Go1.27 §Type_constraints: "A type constraint is an interface that defines
// the set of permissible type arguments for the respective type parameter
// and controls the operations supported by values of that type parameter".
// §Arithmetic_operators: "The operands are represented as values of the type
// argument that the type parameter is instantiated with, and the operation
// is computed with the precision of that type argument."
package main

import "cprover"

func Dot[F ~float32 | ~float64](v1, v2 []F) F {
	var s F
	for i, x := range v1 {
		y := v2[i]
		s += x * y
	}
	return s
}

func Half[T ~float32 | ~float64](x T) T { return x * 0.5 } // 0.5 has type T

func Wrap[T ~int8 | ~int16](x T) T { return x + 100 }

type Stringer interface{ String() string }

type Name string

func (n Name) String() string { return string(n) }

// Methods of the constraint are called on the type argument.
func Join[T Stringer](xs []T) string {
	s := ""
	for _, x := range xs {
		s += x.String()
	}
	return s
}

type S struct{ v int }

func (s *S) Set(v int) { s.v = v }

// A constraint with a type element and a method: PT is *T.
func New[T any, PT interface {
	*T
	Set(int)
}]() T {
	var x T
	PT(&x).Set(5)
	return x
}

func Is[T any](x any) bool {
	switch x.(type) {
	case T:
		return true
	}
	return false
}

func Len[T ~string | ~[]byte](x T) int { return len(x) }

func Sum[T ~int](n T) T {
	var s T
	for i := range n { // i has type T
		s += i
	}
	return s
}

type MyFloat float64

func main() {
	x := float32(0.1)
	d := Dot([]float32{x}, []float32{x})
	cprover.Assert(d == x*x, "float32 precision")
	cprover.Assert(float64(d) != float64(x)*float64(x), "not float64")
	cprover.Assert(Half(MyFloat(3)) == 1.5, "constant of type T")
	cprover.Assert(Wrap(int8(100)) == -56 && Wrap(int16(100)) == 200,
		"overflow of the type argument")

	cprover.Assert(Join([]Name{"a", "b"}) == "ab", "method constraint")
	var s Stringer = Name("i")
	cprover.Assert(Join([]Stringer{s}) == "i", "interface type argument")

	cprover.Assert(New[S]().v == 5, "pointer method constraint")
	cprover.Assert(Is[int](3) && !Is[string](3), "case T in a type switch")
	cprover.Assert(Is[any](3) && !Is[any](nil), "case any")
	cprover.Assert(Len("abc") == 3 && Len([]byte{1}) == 1, "len")
	cprover.Assert(Sum(4) == 6, "range over T")
}
