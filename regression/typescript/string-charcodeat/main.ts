// ES2024 §22.1.3.2: String.prototype.charCodeAt returns the UTF-16
// code unit at the given index, or NaN if out of range.
const s: string = "hello";
console.assert(s.charCodeAt(0) === 104);  // 'h'
console.assert(s.charCodeAt(1) === 101);  // 'e'
console.assert(s.charCodeAt(4) === 111);  // 'o'
console.assert(Number.isNaN(s.charCodeAt(99)));  // out of range
