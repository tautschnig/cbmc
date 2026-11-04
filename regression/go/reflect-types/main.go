// Types through reflection: kinds, names, elements, fields and tags,
// methods, and the relations between types.
package main

import (
	"cprover"
	"reflect"
)

type Celsius float64

func (c Celsius) String() string { return "C" }
func (c *Celsius) Set(x float64) { *c = Celsius(x) }

type Base struct{ ID int }

type Record struct {
	Base
	Name  string `json:"name,omitempty" db:"n"`
	tags  []string
	Score map[string]Celsius
}

type Stringer interface{ String() string }

func main() {
	t := reflect.TypeOf(Record{})
	cprover.Assert(t.Kind() == reflect.Struct && t.Name() == "Record" && t.PkgPath() == "main", "named struct")
	cprover.Assert(t.String() == "main.Record", "string")
	cprover.Assert(t.NumField() == 4, "fields")
	f := t.Field(1)
	cprover.Assert(f.Name == "Name" && f.Tag.Get("json") == "name,omitempty" && f.Tag.Get("db") == "n", "tag")
	cprover.Assert(f.IsExported() && !t.Field(2).IsExported() && t.Field(2).PkgPath == "main", "exported")
	cprover.Assert(t.Field(0).Anonymous && t.Field(0).Type == reflect.TypeOf(Base{}), "embedded")
	id, ok := t.FieldByName("ID")
	cprover.Assert(ok && len(id.Index) == 2 && id.Index[0] == 0 && id.Index[1] == 0, "promoted")
	m := t.Field(3).Type
	cprover.Assert(m.Kind() == reflect.Map && m.Key().Kind() == reflect.String && m.Elem() == reflect.TypeOf(Celsius(0)), "map")
	cprover.Assert(m.String() == "map[string]main.Celsius", "map string")
	cprover.Assert(reflect.TypeOf([3]int{}).Len() == 3 && reflect.TypeOf([3]int{}).Elem().Kind() == reflect.Int, "array")
	c := reflect.TypeOf(Celsius(0))
	cprover.Assert(c.Kind() == reflect.Float64 && c.Size() == 8 && c.Bits() == 64, "basic")
	cprover.Assert(c.NumMethod() == 1 && c.Method(0).Name == "String", "methods")
	pc := reflect.TypeOf((*Celsius)(nil))
	cprover.Assert(pc.NumMethod() == 2 && pc.Elem() == c && reflect.PointerTo(c) == pc, "pointer methods")
	s := reflect.TypeOf((*Stringer)(nil)).Elem()
	cprover.Assert(s.Kind() == reflect.Interface && c.Implements(s) && pc.Implements(s), "implements")
	cprover.Assert(!reflect.TypeOf(0).Implements(s), "not implements")
	cprover.Assert(c.ConvertibleTo(reflect.TypeOf(0)) && !c.AssignableTo(reflect.TypeOf(0.0)), "convertible")
	cprover.Assert(reflect.TypeOf([]byte(nil)).ConvertibleTo(reflect.TypeOf("")), "bytes to string")
	fn := reflect.TypeOf(func(int, ...string) bool { return false })
	cprover.Assert(fn.Kind() == reflect.Func && fn.NumIn() == 2 && fn.IsVariadic() && fn.Out(0).Kind() == reflect.Bool, "func")
	cprover.Assert(reflect.TypeFor[error]().Kind() == reflect.Interface, "TypeFor")
	ch := reflect.TypeOf(make(<-chan int))
	cprover.Assert(ch.ChanDir() == reflect.RecvDir && ch.String() == "<-chan int", "chan")
	cprover.Assert(reflect.TypeOf(nil) == nil, "nil")
	cprover.Assert(reflect.Ptr == reflect.Pointer && reflect.Struct.String() == "struct", "kinds")
}
