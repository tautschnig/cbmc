// KNOWNBUG: compose(f, g) returning an arrow function that captures
// f and g then applies them crashes with a goto-symex type-consistency
// error. Named-function references are also not resolved when passed
// as arguments. The issue likely lies in function-pointer capture
// through closure + arrow-function return. Found via the
// integration/typescript-npm compose-function harness.
const compose = (f: (x: number) => number, g: (x: number) => number) =>
  (x: number) => f(g(x));
const h = compose((x: number) => x * 2, (x: number) => x + 3);
console.assert(h(5) === 16);
