// Misuse of sync: unlocking an unlocked mutex is a fatal error (which
// deferred functions cannot recover from); a negative WaitGroup counter
// panics.
package main

import (
	"cprover"
	"sync"
)

func main() {
	if cprover.Nondet[bool]() {
		var mu sync.Mutex
		defer func() { recover() }()
		mu.Unlock()
	} else {
		var wg sync.WaitGroup
		wg.Done()
	}
}
