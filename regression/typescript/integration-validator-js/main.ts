// Simplified validator.js-like checks
function isLength(s: string, min: number, max: number): boolean {
  return s.length >= min && s.length <= max;
}

function isEmpty(s: string): boolean {
  return s.length === 0;
}

console.assert(isLength("hello", 3, 10) === true);
console.assert(isLength("hi", 3, 10) === false);
console.assert(isLength("very long string", 3, 10) === false);
console.assert(isEmpty("") === true);
console.assert(isEmpty("x") === false);
