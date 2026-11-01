// Go1.27 §Channel_types: "Otherwise, the channel is buffered and
// communication succeeds without blocking if the buffer is not full
// (sends) or not empty (receives)." §Receive_operator: "A receive
// operation on a closed channel can always proceed immediately, yielding
// the element type's zero value after any previously sent values have been
// received."
package main

import "cprover"

func main() {
	b := make(chan string, 2)
	b <- "a"
	b <- "b"
	cprover.Assert(len(b) == 2 && cap(b) == 2, "len and cap")
	cprover.Assert(<-b == "a", "first in, first out")
	close(b)
	v, ok := <-b
	cprover.Assert(v == "b" && ok, "buffered value after close")
	v, ok = <-b
	cprover.Assert(v == "" && !ok, "zero value")
	var nilch chan int
	cprover.Assert(len(nilch) == 0 && cap(nilch) == 0, "nil channel")
}
