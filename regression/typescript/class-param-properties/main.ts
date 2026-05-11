// KNOWNBUG: TypeScript parameter-property shorthand crashes the
// frontend. Use explicit property declaration + constructor
// assignment instead.
class P {
  constructor(public x: number, public y: number) {}
  sum(): number { return this.x + this.y; }
}
const p = new P(3, 4);
console.assert(p.sum() === 7);
