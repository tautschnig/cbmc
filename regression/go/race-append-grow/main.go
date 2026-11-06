// Go1.27 §Appending_and_copying_slices with the data-race check: a goroutine
// assigns a grown slice to s while main ranges over s (runtime/race's
// TestRaceSliceRangeAppend).
package main

func main() {
	c := make(chan bool, 1)
	s := make([]int, 10)
	go func() {
		s = append(s, 3)
		c <- true
	}()
	for range s {
	}
	<-c
}
