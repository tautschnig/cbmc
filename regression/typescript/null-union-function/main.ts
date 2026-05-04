// TSH: Narrowing.md — null handling
function safeDiv(a: number, b: number | null): number {
  if (b === null) return 0;
  return a / b;
}
console.assert(safeDiv(10, 2) === 5);
console.assert(safeDiv(10, null) === 0);
