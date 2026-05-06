// 530 CORE milestone - comprehensive test
class Config<T> {
  value: T;
  max: number;
  constructor(v: T, m: number) { this.value = v; this.max = m; }
  exceeds(n: number): boolean { return n > this.max; }
}
interface Result { ok: boolean; value: number; }

function validate(n: number): Result {
  if (n < 0) return { ok: false, value: 0 };
  return { ok: true, value: n * 2 };
}

const c = new Config<number>(10, 100);
console.assert(c.value === 10);
console.assert(c.exceeds(50) === false);
console.assert(c.exceeds(150) === true);

const r: Result = validate(42);
console.assert(r.ok === true);
console.assert(r.value === 84);
