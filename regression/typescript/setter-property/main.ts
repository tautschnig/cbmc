class Temperature {
  _celsius: number;
  constructor(c: number) { this._celsius = c; }
  get celsius(): number { return this._celsius; }
  set celsius(v: number) { this._celsius = v; }
}
const t = new Temperature(0);
console.assert(t.celsius === 0);
t.celsius = 100;
console.assert(t.celsius === 100);
t.celsius = 37;
console.assert(t.celsius === 37);
