// Operations the model of reflect does not support are reported.
package main

import "reflect"

func twice(x int) int { return 2 * x }

func main() {
	reflect.ValueOf(twice).Call([]reflect.Value{reflect.ValueOf(1)})
}
