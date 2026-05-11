// Coverage: exercises Array.prototype.filter on an array with a
// nondet element, which takes the symbolic-source filter path (the
// constant-element path evaluates the predicate at conversion time
// and doesn't exercise the per-element predicate call).
const x: number = nondet_number();
__CPROVER_assume(x >= 1 && x <= 5);
const arr: number[] = [x, 2, 3, 4];
const even: number[] = arr.filter((v: number) => v % 2 === 0);
// At least the three constant-even values 2, 4 must be present.
// x may contribute one more.
console.assert(even.length >= 2);
console.assert(even.length <= 4);
