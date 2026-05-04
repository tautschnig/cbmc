function add(a: number, b: number, c: number = 10): number {
  return a + b + c;
}
console.assert(add(1, 2) === 13);
console.assert(add(1, 2, 3) === 6);
