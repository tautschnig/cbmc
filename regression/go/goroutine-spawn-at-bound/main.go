// A goroutine started in the last iteration that the unwinding bound
// admits gets its arguments as passed (Go1.27 §Go_statements), although the
// spawning path ends at the bound before the goroutine is executed.
package main

import "cprover"

func main() {
	data := make([]int, 10)
	ch := make(chan bool, 5)
	for i := 0; i < 10; i++ {
		ch <- true
		go func(i int) {
			cprover.Assert(i >= 0 && i < 10, "argument as passed")
			data[i] = 42
			<-ch
		}(i)
	}
}
