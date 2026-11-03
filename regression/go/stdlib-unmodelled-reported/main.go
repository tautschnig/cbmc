// A standard-library function without a model is reported, not ignored.
package main

import "os"

func main() { _ = os.Getenv("x") }
