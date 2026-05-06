interface HasValue { value: number; }
function getValue<T extends HasValue>(x: T): number {
  return x.value;
}
const a = { value: 42, extra: "hi" };
console.assert(getValue(a) === 42);
