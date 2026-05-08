// KNOWNBUG: Generic functions with TWO OR MORE type parameters are
// not correctly monomorphized. Only the first type parameter gets
// substituted; the second keeps its parameter name, which fails
// when concrete argument types don't match.
//
// ES2024 sec-generic-function-definitions (TSH: Generics).
//
// Non-generic heterogeneous tuple DOES work (see heterogeneous-tuple),
// and single-type-parameter generics DO work (see generic-function
// etc.). Only the multi-type-param case remains.
//
// Fixing would require extending generic_functions handling in
// typescript_converter_call.cpp to substitute each type parameter
// independently.
function pair<A, B>(a: A, b: B): [A, B] { return [a, b]; }

const p = pair<number, string>(1, "x");
console.assert(p[0] === 1);
console.assert(p[1] === "x");
