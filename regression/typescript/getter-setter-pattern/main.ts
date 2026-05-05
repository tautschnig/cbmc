class Temperature {
  celsius: number;
  constructor(c: number) { this.celsius = c; }
  getFahrenheit(): number { return this.celsius * 9 / 5 + 32; }
  setCelsius(c: number): void { this.celsius = c; }
}
const t = new Temperature(100);
console.assert(t.celsius === 100);
t.setCelsius(0);
console.assert(t.celsius === 0);
