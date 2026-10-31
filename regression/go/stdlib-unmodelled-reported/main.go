// A standard-library function without a model is reported, not ignored.
package main

import "strings"

func main() { _ = strings.ToUpper("x") }
