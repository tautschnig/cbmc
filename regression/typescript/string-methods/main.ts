// ES2024 sec-string.prototype.*
const s: string = "hello world";
console.assert(s.indexOf("world") === 6);
console.assert(s.includes("hello"));
console.assert(s.substring(0, 5) === "hello");
