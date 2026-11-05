// Go1.27 §Assignability: a value of type stateFn, a function type that
// refers to itself, is assignable to a variable of its underlying type
// (`state := lexText` has type func(*lexer) stateFn), and vice versa. The
// call whose result is assigned (`state = state(l)`) runs once, as in
// text/template/parse's lexer.
package main

import "cprover"

type lexer struct {
	inside bool
	item   int
	calls  int
}

type stateFn func(*lexer) stateFn

func (l *lexer) emitItem(i int) stateFn {
	l.item = i
	return nil
}

func lexText(l *lexer) stateFn { l.calls++; return lexLeftDelim }

func lexLeftDelim(l *lexer) stateFn {
	l.calls += 10
	l.inside = true
	return l.emitItem(1)
}

func lexInside(l *lexer) stateFn { l.calls += 100; return l.emitItem(2) }

func (l *lexer) nextItem() int {
	l.item = -1
	state := lexText
	if l.inside {
		state = lexInside
	}
	for {
		state = state(l)
		if state == nil {
			return l.item
		}
	}
}

func main() {
	l := &lexer{}
	cprover.Assert(l.nextItem() == 1, "the left delimiter")
	cprover.Assert(l.nextItem() == 2, "then inside the action")
	cprover.Assert(l.calls == 111, "each state function runs once")
}
