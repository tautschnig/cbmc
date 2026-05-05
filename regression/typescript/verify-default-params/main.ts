function add(a: number, b: number = 10): number {
  return a + b;
}
console.assert(add(5) === 15);
console.assert(add(5, 3) === 8);
