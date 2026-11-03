// Locking a locked sync.Mutex that no goroutine unlocks deadlocks.
package main

import "sync"

func main() {
	var mu sync.Mutex
	mu.Lock()
	mu.Lock() // deadlock
}
