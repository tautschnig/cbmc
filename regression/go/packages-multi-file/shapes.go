package main

type Shape interface{ Area() int }

type Rect struct{ W, H int }

func (r Rect) Area() int { return r.W * r.H }

const limit = 10

var scaled = limit * 2

func init() { order = append(order, "shapes.go") }
