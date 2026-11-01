// Go1.27 §Send_statements: "A send on a closed channel proceeds by causing
// a run-time panic." §Close: "Closing the nil channel also causes a
// run-time panic."
package main

import "cprover"

func main() {
	ch := make(chan int, 1)
	close(ch)
	switch cprover.Nondet[int]() {
	case 0:
		ch <- 1
	case 1:
		close(ch)
	case 2:
		var nilch chan int
		close(nilch)
	case 3:
		n := cprover.Nondet[int]()
		_ = make(chan int, n)
	}
}
