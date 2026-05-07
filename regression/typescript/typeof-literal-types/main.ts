// ES2024 sec-typeof-operator: literal types must still return their
// runtime base type.
console.assert(typeof 42 === "number");
console.assert(typeof 3.14 === "number");
console.assert(typeof "x" === "string");
console.assert(typeof "hello" === "string");
console.assert(typeof true === "boolean");
console.assert(typeof false === "boolean");
console.assert(typeof undefined === "undefined");
console.assert(typeof null === "object"); // JS quirk

// Narrowing via typeof-literal (tests the result is a valid string)
function f(x: number | string): string {
  if (typeof x === "number") return "num";
  return "str";
}
console.assert(f(42) === "num");
console.assert(f("x") === "str");
