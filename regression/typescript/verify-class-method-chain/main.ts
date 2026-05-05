class Builder {
  value: number;
  constructor() { this.value = 0; }
  add(n: number): number { this.value = this.value + n; return this.value; }
  mul(n: number): number { this.value = this.value * n; return this.value; }
}
const b = new Builder();
b.add(5);
b.mul(3);
console.assert(b.value === 15);
