// Operations the model of reflect does not support are reported.
package main

import "reflect"

func main() {
	t := reflect.TypeOf(func(int) int { return 0 })
	reflect.MakeFunc(t, func(args []reflect.Value) []reflect.Value { return args })
}
