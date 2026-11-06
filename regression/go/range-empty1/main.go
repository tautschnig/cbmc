// A range loop without iteration variables and with an empty body does
// nothing but evaluate the range expression: no loop is left to unwind, and
// the evaluation races with a concurrent write.
package main

func main() {
	s := make([]int, 100)
	for range s {
	}
	for range "héllo, wörld" {
	}
	n := len(s)
	for range n {
	}
	var a [50]int
	for _ = range a {
	}
	done := make(chan bool)
	go func() {
		s = nil
		done <- true
	}()
	for range s {
	}
	<-done
}
