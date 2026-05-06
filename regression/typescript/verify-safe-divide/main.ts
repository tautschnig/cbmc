function safeDiv(a: number, b: number): number {
  if (b === 0) return 0;
  return a / b;
}
console.assert(safeDiv(10, 2) === 5);
console.assert(safeDiv(10, 0) === 0);
