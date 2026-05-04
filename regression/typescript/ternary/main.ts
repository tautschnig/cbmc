// ES2024 sec-conditional-operator: Conditional Operator (?:)
const x: number = 5;
const result: number = x > 3 ? 10 : 20;
console.assert(result === 10);
const y: number = 1;
const other: number = y > 3 ? 10 : 20;
console.assert(other === 20);
