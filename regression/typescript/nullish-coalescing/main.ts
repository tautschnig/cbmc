// ES2024 sec-nullish-coalescing: ??
const x: number = 42;
const y: number = x ?? 0;
console.assert(y === 42);
