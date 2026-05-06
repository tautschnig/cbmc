function safeFunc(x: number): number {
  if (x < 0) throw "negative";
  return x * 2;
}
const a: number = safeFunc(5);
console.assert(a === 10);
