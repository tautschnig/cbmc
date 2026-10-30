// Go1.27 §Instantiations: "Instantiating a generic type, function, or method
// results in a non-generic type, function, or method, respectively." Each
// instance has its own checks, named after its type arguments; identical
// type arguments (here an alias, §Alias_declarations) give the same instance.
package main

import "cprover"

func Div[T ~int | ~uint8](a, b T) T { return a / b }

type Alias = int

func main() {
	b := cprover.Nondet[uint8]()
	if b != 0 {
		_ = Div(uint8(7), b)
	}
	_ = Div[Alias](7, int(b))
	_ = Div(1, 1)
}
