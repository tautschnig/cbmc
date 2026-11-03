// "a select with only nil channels and no default case blocks forever",
// as does select {}: a deadlock, while the other goroutine is blocked too.
package main

func main() {
	a := make(chan int)
	go func() { <-a }()
	select {}
}
