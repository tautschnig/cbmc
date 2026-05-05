// Null-safe function with explicit check
function safeLength(s: string | null): number {
  if (s === null) return 0;
  return s.length;
}
console.assert(safeLength("hi") === 2);
