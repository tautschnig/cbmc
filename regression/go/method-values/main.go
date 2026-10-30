// Go1.27 §Method_values, with the spec's example: "The expression x is
// evaluated and saved during the evaluation of the method value; the saved
// copy is then used as the receiver in any calls, which may be executed
// later."
//
//	f := t.M  // receiver *t is evaluated and stored in f
//	g := s.M  // receiver *(s.T) is evaluated and stored in g
//	*t = 42   // does not affect stored receivers in f and g
//
// §Method_expressions: "T.M is a function that is callable as a regular
// function with the same arguments as M prefixed by an additional argument
// that is the receiver of the method." "(*T).Mv yields a function value
// representing Mv with signature func(tv *T, a int) int".
package main

import "cprover"

type S struct{ *T }
type T int

func (t T) M() int { return int(t) }

type V struct{ a int }

func (tv V) Mv(a int) int  { return tv.a + a }
func (tp *V) Mp(f int) int { tp.a += f; return tp.a }

type I interface{ Mv(int) int }

func main() {
	t := new(T)
	*t = 1
	s := S{T: t}
	f := t.M
	g := s.M
	*t = 42
	cprover.Assert(f() == 1 && g() == 1, "stored receivers")
	cprover.Assert(t.M() == 42, "new receiver value")

	v := V{1}
	cprover.Assert(V.Mv(v, 7) == 8 && (V).Mv(v, 1) == 2, "T.Mv")
	cprover.Assert((*V).Mv(&v, 2) == 3, "(*T).Mv")
	cprover.Assert((*V).Mp(&v, 5) == 6 && v.a == 6, "(*T).Mp")
	mp := v.Mp // like (&v).Mp
	mp(1)
	cprover.Assert(v.a == 7, "pointer method value refers to v")

	var i I = v
	h := i.Mv
	cprover.Assert(h(1) == 8, "method value of an interface value")
	ie := I.Mv
	cprover.Assert(ie(V{10}, 1) == 11, "method expression of an interface")
}
