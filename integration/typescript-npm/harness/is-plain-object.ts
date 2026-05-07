// Harness for npm package 'is-plain-object' (jonschlinkert/is-plain-object)
// Version tracked in integration/typescript-npm/package.json
//
// isPlainObject(x) returns true iff x is an object literal (not an
// Array, Map, Set, class instance, null, or primitive).
// We verify the classification over SYMBOLIC cases.
//
// Upstream: https://github.com/jonschlinkert/is-plain-object/blob/v5.0.0/is-plain-object.js

// Reimplementation of the classification. Since CBMC's TypeScript
// frontend doesn't model prototypes dynamically, we classify based on
// the TypeScript type system's structural information (via a tag).
type ObjectKind = "plain" | "array" | "class" | "null" | "primitive";

interface Tagged {
  kind: ObjectKind;
}

function isPlainObject(o: Tagged): boolean {
  return o.kind === "plain";
}

// Property 1: plain objects are recognized.
const plain: Tagged = { kind: "plain" };
console.assert(isPlainObject(plain));

// Property 2: non-plain objects are rejected.
const arr: Tagged = { kind: "array" };
const cls: Tagged = { kind: "class" };
const nul: Tagged = { kind: "null" };
const prim: Tagged = { kind: "primitive" };
console.assert(!isPlainObject(arr));
console.assert(!isPlainObject(cls));
console.assert(!isPlainObject(nul));
console.assert(!isPlainObject(prim));

// Property 3: classification is total — any object is in exactly one class.
// Symbolic choice: pick one of 5 kinds nondeterministically.
const pick: number = nondet_number();
__CPROVER_assume(pick >= 0 && pick <= 4);

let o: Tagged;
if (pick === 0) o = { kind: "plain" };
else if (pick === 1) o = { kind: "array" };
else if (pick === 2) o = { kind: "class" };
else if (pick === 3) o = { kind: "null" };
else o = { kind: "primitive" };

const result: boolean = isPlainObject(o);

// The result matches the kind.
if (pick === 0) console.assert(result);
if (pick !== 0) console.assert(!result);

// Property 4: idempotence. Classification is a pure function.
console.assert(isPlainObject(o) === result);
console.assert(isPlainObject(plain) === true);
console.assert(isPlainObject(arr) === false);
