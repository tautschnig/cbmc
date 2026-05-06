class Box<T> {
  value: T;
  constructor(v: T) { this.value = v; }
  get(): T { return this.value; }
  set(v: T): void { this.value = v; }
}
const b = new Box<number>(42);
console.assert(b.get() === 42);
b.set(100);
console.assert(b.get() === 100);
