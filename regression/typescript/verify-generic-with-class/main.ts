class Wrapper {
  v: number;
  constructor(v: number) { this.v = v; }
}
function getValue<T>(w: T): T { return w; }
const w = new Wrapper(42);
const r = getValue<Wrapper>(w);
console.assert(r.v === 42);
