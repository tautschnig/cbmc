// Go1.27 §Type_assertions, with the spec's example:
//
//	var x interface{} = 7          // x has dynamic type int and value 7
//	i := x.(int)                   // i has type int and value 7
//
// "If T is an interface type, x.(T) asserts that the dynamic type
// of x implements the interface T." For "v, ok = x.(T)", "The value of ok is
// true if the assertion holds. Otherwise it is false and the value of v is
// the zero value for type T."
package main

import "cprover"

type I interface{ M() int }
type J interface{ N() int }
type A struct{ v int }

func (a A) M() int { return a.v }

type MyInt int

func main() {
	var x interface{} = 7
	i := x.(int)
	cprover.Assert(i == 7, "x.(int)")
	_, ok := x.(MyInt)
	cprover.Assert(!ok, "MyInt is a different type than int")
	s, ok := x.(string)
	cprover.Assert(!ok && s == "", "zero value when the assertion fails")

	var y any = A{3}
	m, ok := y.(I)
	cprover.Assert(ok && m.M() == 3, "assertion to an implemented interface")
	_, ok = y.(J)
	cprover.Assert(!ok, "not implemented")
	var none any
	_, ok = none.(I)
	cprover.Assert(!ok, "nil asserts nothing")
	a := m.(A)
	cprover.Assert(a.v == 3, "from one interface to the dynamic type")
}
