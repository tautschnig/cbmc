// KNOWNBUG: Generic function returning heterogeneous tuple [A, B]
// where A and B have different base types (e.g., number and string)
// fails type unification. The tuple literal construction tries to
// unify the element types into a single array type.

function pair<A, B>(a: A, b: B): [A, B] { return [a, b]; }

const p = pair<number, string>(1, "x");
console.assert(p[0] === 1);
console.assert(p[1] === "x");
