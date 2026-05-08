// Symbolic-input review: Array element access and includes with
// symbolic arguments. Works well because our array model supports
// symbolic indexing natively (it's just array_typet).
const a: number[] = [10, 20, 30, 40, 50];

// Symbolic index access
const i: number = nondet_number();
__CPROVER_assume(i >= 0 && i < 5);
const v: number = a[i];
console.assert(v === 10 || v === 20 || v === 30 || v === 40 || v === 50);

// Symbolic includes target
const y: number = nondet_number();
__CPROVER_assume(y >= 0 && y <= 100);
const has: boolean = a.includes(y);
if (y === 10 || y === 20 || y === 30 || y === 40 || y === 50) {
  console.assert(has);
} else {
  console.assert(!has);
}

// Note: slice/splice/fill/indexOf with symbolic start/end/count/target
// are a known limitation — they fall through to nondet for non-
// constant arguments. Programs needing these should pass constants.
