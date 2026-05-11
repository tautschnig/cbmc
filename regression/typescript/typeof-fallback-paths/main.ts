// Coverage: exercise typeof's runtime-fallback branches for cases
// where the operand's static type isn't immediately obvious (e.g.
// after an `as` cast).
const n: number = 42;
const s: string = "x";
const b: boolean = true;
console.assert(typeof n === "number");
console.assert(typeof s === "string");
console.assert(typeof b === "boolean");

// Function typeof
function f(): number { return 1; }
console.assert(typeof f === "function");

// Object
const o = { a: 1 };
console.assert(typeof o === "object");
