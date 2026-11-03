// Two goroutines that wait for each other deadlock.
package main

func main() {
	a, b := make(chan int), make(chan int)
	go func() { <-a; b <- 1 }()
	<-b // deadlock: nobody sends on a
	a <- 1
}
