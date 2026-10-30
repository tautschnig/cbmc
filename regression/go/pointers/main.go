// Go1.27 §Address_operators: "For an operand x of type T, the address
// operation &x generates a pointer of type *T to x." "the pointer
// indirection *x denotes the variable of type T pointed to by x."
// §Pointer_types: "The value of an uninitialized pointer is nil."
// §Allocation: "The built-in function new creates a new, initialized
// variable and returns a pointer to it."
package main

import "cprover"

func escape() *int {
	v := 41 // outlives escape (Go1.27 §Variables)
	return &v
}

func set(p *int, v int) { *p = v }

func main() {
	var p *int
	cprover.Assert(p == nil, "zero value of a pointer is nil")

	x := 1
	p = &x
	*p = 2
	cprover.Assert(x == 2, "assignment through a pointer")
	set(&x, 3)
	cprover.Assert(x == 3 && *p == 3, "pointer passed to a function")

	q := escape()
	*q++
	cprover.Assert(*q == 42, "a local variable outlives its function")
	cprover.Assert(escape() != escape(), "each call creates a new variable")

	n := new(int)
	cprover.Assert(*n == 0, "new(T) is zero-initialised")
	m := new(x + 4)
	cprover.Assert(*m == 7, "new(expr) is initialised to expr")
	cprover.Assert(n != m && p == &x, "pointer equality")

	pp := &p
	**pp = 5
	cprover.Assert(x == 5, "pointer to pointer")

	// "Pointers to distinct zero-size variables may or may not be equal."
	a, b := new(struct{}), new(struct{})
	cprover.Assert(a == a, "a pointer equals itself")
	_ = a == b // either outcome is allowed
}
