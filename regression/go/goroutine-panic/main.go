// Go1.27 §Handling_panics: a panic that a goroutine does not recover
// terminates the program; recovering in the goroutine stops it.
package main

import "cprover"

func work(p *int) {
	defer func() { recover() }()
	*p = 1
}

func main() {
	var p *int
	go work(p) // recovered
	go func() {
		var q *int
		if cprover.Nondet[bool]() {
			q = new(int)
		}
		*q = 2 // not recovered
	}()
}
