class Builder {
  value: number;
  constructor() { this.value = 0; }
  add(n: number): number { this.value = this.value + n; return this.value; }
}
const b = new Builder();
console.assert(b.add(5) === 5);
console.assert(b.add(3) === 8);
console.assert(b.value === 8);
