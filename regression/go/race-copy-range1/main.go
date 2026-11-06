// copy accesses only the elements it copies: a write of another element
// does not race with it, a write of a copied one does.
package main

func main() {
	a := make([]int, 10)
	b := make([]*int, 5)
	p := make([]*int, 10)
	done := make(chan bool)
	go func() {
		a[7] = 1 // not copied into: no race
		p[9] = nil
		done <- true
	}()
	copy(a[:5], a[5:7])
	copy(p[2:7], b)
	<-done
	go func() {
		a[3] = 2 // copied into: a race
		done <- true
	}()
	copy(a[2:], a)
	<-done
}
