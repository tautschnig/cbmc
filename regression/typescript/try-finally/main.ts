let x: number = 0;
try { x = 1; } finally { x = x + 10; }
console.assert(x === 11);
