function safeLength(s: string): number {
  if (s === "") return 0;
  return s.length;
}
console.assert(safeLength("hello") === 5);
console.assert(safeLength("") === 0);
