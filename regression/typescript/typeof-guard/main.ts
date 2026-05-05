function process(x: number | string): number {
  if (typeof x === "number") return x * 2;
  return x.length;
}
console.assert(process(5) === 10);
console.assert(process("hi") === 2);
