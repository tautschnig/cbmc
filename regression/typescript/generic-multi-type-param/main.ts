// ES2024 sec-generic-function-definitions (TSH: Generics).
// Multi-type-parameter generics: the monomorphizer now substitutes
// each type parameter independently.

// Two type parameters
function pair<A, B>(a: A, b: B): [A, B] { return [a, b]; }
const p = pair<number, string>(1, "x");
console.assert(p[0] === 1);
console.assert(p[1] === "x");

// Two type parameters where one is string and the other is number
const p2 = pair<string, number>("a", 42);
console.assert(p2[0] === "a");
console.assert(p2[1] === 42);

// Three type parameters
function triple<A, B, C>(a: A, b: B, c: C): A {
  return a;
}
const r = triple<number, string, boolean>(7, "y", true);
console.assert(r === 7);

// Different call-site instantiations produce distinct specializations
function id2<X, Y>(x: X, y: Y): X { return x; }
const a = id2<number, string>(10, "s");
const b = id2<string, number>("t", 20);
console.assert(a === 10);
console.assert(b === "t");
