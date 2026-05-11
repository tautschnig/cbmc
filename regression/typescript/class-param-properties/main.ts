// ES2024 §15.7.13: Parameter-property shorthand in constructors.
// `constructor(public x: number) {}` declares x as a field AND
// emits an implicit `this.x = x` assignment.
class P {
  constructor(public x: number, public y: number) {}
  sum(): number { return this.x + this.y; }
}
const p = new P(3, 4);
console.assert(p.sum() === 7);
console.assert(p.x === 3);
console.assert(p.y === 4);

// private and readonly also work.
class Q {
  constructor(private z: number, public readonly label: number) {}
  get_z(): number { return this.z; }
}
const q = new Q(10, 20);
console.assert(q.get_z() === 10);
console.assert(q.label === 20);
