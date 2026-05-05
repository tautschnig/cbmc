class Builder {
  value: number;
  constructor(v: number) { this.value = v; }
  add(n: number): number { this.value = this.value + n; return this.value; }
  mul(n: number): number { this.value = this.value * n; return this.value; }
}
const b = new Builder(5);
console.assert(b.add(3) === 8);
console.assert(b.mul(2) === 16);
