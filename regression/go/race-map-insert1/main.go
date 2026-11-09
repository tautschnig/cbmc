package main

func main() {
	m := make(map[int]int)
	var k int
	ch := make(chan bool, 1)
	go func() {
		m[k] = 1
		ch <- true
	}()
	k = 2
	<-ch
}
