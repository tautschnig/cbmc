const s: string = "  Hello World  ";
const trimmed: string = s.trim();
const upper: string = trimmed.toUpperCase();
console.assert(trimmed === "Hello World");
console.assert(upper === "HELLO WORLD");
