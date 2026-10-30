// Go1.27 §Function_declarations: "If the function declaration specifies type
// parameters, the function name denotes a generic function". §Instantiations:
// "When using a generic function or method, type arguments may be provided
// explicitly, or they may be partially or completely inferred from the
// context in which the function is used." "Instantiating a generic type,
// function, or method results in a non-generic type, function, or method,
// respectively."
package main

import "cprover"

func Max[T ~int | ~float64](x, y T) T {
	if x > y {
		return x
	}
	return y
}

func Map[S ~[]E, E, F any](s S, f func(E) F) []F {
	r := make([]F, 0, len(s))
	for _, x := range s {
		r = append(r, f(x))
	}
	return r
}

func Double[T ~int](x T) T { return 2 * x }

func Zero[T any]() T {
	var z T
	return z
}

// cprover.Nondet is generic as well, and instantiated with T's argument.
func Arbitrary[T ~int | ~string]() T { return cprover.Nondet[T]() }

func Even[T ~int](n T) bool {
	if n == 0 {
		return true
	}
	return Odd(n - 1)
}

func Odd[T ~int](n T) bool {
	if n == 0 {
		return false
	}
	return Even(n - 1)
}

var calls int

// Each instance is a function of its own, but they share package variables.
func Count[T any](T) int {
	calls++
	return calls
}

type Celsius int

type Ints []int

type Alias = int

var top = Max(3, 4) // package-level initialisation

func main() {
	cprover.Assert(top == 4, "package-level instance")
	cprover.Assert(Max[int](1, 2) == 2, "explicit")
	cprover.Assert(Max(2.5, 1) == 2.5, "inferred float64")
	cprover.Assert(Max(Celsius(5), 7) == Celsius(7), "inferred named type")

	strs := Map(Ints{1, 2}, func(x int) string { return "s" })
	cprover.Assert(len(strs) == 2 && strs[1] == "s", "inferred S, E, F")
	doubled := Map[Ints](Ints{1, 2}, Double[int]) // partial type arguments
	cprover.Assert(doubled[1] == 4, "instance as function value")

	var f func(Celsius) Celsius = Double // inferred from the assignment
	cprover.Assert(f(21) == 42, "inferred by assignment")

	cprover.Assert(Zero[int]() == 0 && Zero[string]() == "", "zero values")
	cprover.Assert(Zero[*int]() == nil && Zero[[]int]() == nil, "nil zeros")

	cprover.Assert(Even(4) && Odd(Celsius(3)), "mutual recursion")

	if n := Arbitrary[Celsius](); n > 3 {
		cprover.Assert(n >= 4, "nondeterministic Celsius")
	}
	cprover.Assert(len(Arbitrary[string]()) >= 0, "nondeterministic string")

	Count[Alias](0) // the same instance as Count[int]
	Count("x")
	cprover.Assert(Count[int](0) == 3, "shared package state")
}
