interface Printable {
  value: number;
}
function getValue(p: Printable): number {
  return p.value;
}
const obj: Printable = { value: 42 };
console.assert(getValue(obj) === 42);
