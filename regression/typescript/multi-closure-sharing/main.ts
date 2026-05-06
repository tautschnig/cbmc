function make() {
  let x: number = 0;
  function inc(): number { x = x + 1; return x; }
  function get(): number { return x; }
  const a: number = inc();
  const b: number = inc();
  const c: number = get();
  return { a: a, b: b, c: c };
}
const r = make();
console.assert(r.a === 1);
console.assert(r.b === 2);
console.assert(r.c === 2);
