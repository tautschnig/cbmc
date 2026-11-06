// A comparison with a short string constant needs no loop: the length and
// the bytes are compared one by one, also through an interface value.
package main

import "cprover"

func main() {
	s := "sync: negative " + "WaitGroup counter"
	if s != "sync: negative WaitGroup counter" {
		panic("equal strings")
	}
	if s == "sync: negative WaitGroup counteR" || s == "sync" {
		panic("different strings")
	}
	b := []byte{'a', 'b', 'c'}
	i := cprover.Nondet[int]()
	cprover.Assume(0 <= i && i < 3)
	b[i] = cprover.Nondet[byte]()
	u := string(b)
	if (u == "abc") != (b[i] == "abc"[i]) {
		panic("exact")
	}
	if u == "abc" {
		panic("expected failure")
	}
	var e any = s
	if e != "sync: negative WaitGroup counter" {
		panic("interface")
	}
	var f any = 3
	if f == "sync: negative WaitGroup counter" {
		panic("interface of another type")
	}
}
