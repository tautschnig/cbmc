class Box {
  value: number;
  constructor(v: number) { this.value = v; }
  double(): number { return this.value * 2; }
}
const a = new Box(5);
const b = new Box(10);
console.assert(a.double() === 10);
console.assert(b.double() === 20);
console.assert(a.value + b.value === 15);
