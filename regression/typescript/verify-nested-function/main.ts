function outer(x: number): number {
  function inner(y: number): number { return y * 2; }
  return inner(x) + 1;
}
console.assert(outer(5) === 11);
console.assert(outer(0) === 1);
