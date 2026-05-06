class Box<T> {
  value: T;
  constructor(v: T) { this.value = v; }
  get(): T { return this.value; }
}
const b1 = new Box<number>(42);
const b2 = new Box<string>("hi");
console.assert(b1.get() === 42);
console.assert(b2.get() === "hi");
