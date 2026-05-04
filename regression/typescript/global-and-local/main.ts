const x: number = 10;
function f(): number {
  const x: number = 20;
  return x;
}
console.assert(x === 10);
console.assert(f() === 20);
