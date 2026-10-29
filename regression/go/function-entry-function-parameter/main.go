// With --function, parameters get arbitrary values. For parameters of
// function type that is not supported yet (Go1.27 §Function_types: a
// function value is nil or refers to a function) and must be reported.
package main

func apply(f func(int) int, x int) int {
	return f(x)
}

func main() {}
