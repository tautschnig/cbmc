function hasKind(obj: { kind: string; value: number }): boolean {
  return "kind" in obj;
}
function hasFoo(obj: { kind: string; value: number }): boolean {
  return "foo" in obj;
}
const x = { kind: "x", value: 42 };
console.assert(hasKind(x) === true);
console.assert(hasFoo(x) === false);
