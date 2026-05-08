// ES2024 §21.1.2.12/13: Number.parseInt, Number.parseFloat.
console.assert(Number.parseInt("42") === 42);
console.assert(Number.parseInt("-42") === -42);
console.assert(Number.parseInt("0") === 0);
console.assert(Number.parseInt("1A", 16) === 26);
console.assert(Number.parseInt("ff", 16) === 255);
console.assert(Number.parseInt("1010", 2) === 10);
console.assert(Number.parseFloat("3.14") === 3.14);
console.assert(Number.parseFloat("-3.14") === -3.14);
console.assert(Number.parseFloat("0") === 0);
console.assert(Number.parseFloat("100") === 100);
