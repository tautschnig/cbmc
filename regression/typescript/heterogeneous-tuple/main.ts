// ES2024: tuple type `[A, B]` with heterogeneous element types now
// works for NON-GENERIC use. Generic monomorphization with two
// distinct type parameters is still a limitation (tracked separately).
//
// Our array literal handler now detects tuple _type `[T, U, ...]` and
// emits a tuple-struct (with _0, _1 fields) when element types differ,
// avoiding the earlier type-unification failure.

// Non-generic heterogeneous tuple (works):
const p: [number, string] = [1, "x"];
console.assert(p[0] === 1);
console.assert(p[1] === "x");

// Function returning heterogeneous tuple (works):
function make_pair(a: number, b: string): [number, string] {
  return [a, b];
}
const q = make_pair(2, "y");
console.assert(q[0] === 2);
console.assert(q[1] === "y");

// Multi-element tuple:
const triple: [number, string, boolean] = [42, "z", true];
console.assert(triple[0] === 42);
console.assert(triple[1] === "z");
console.assert(triple[2] === true);
