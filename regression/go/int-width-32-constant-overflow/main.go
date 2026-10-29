// Go1.27 §Constants, §Representability: with --32 (GOARCH=386) int is 32
// bits, so 1<<40 is not representable in int: a compile-time error,
// reported by go/types with the target's sizes.
package main

var x int = 1 << 40

func main() {}
