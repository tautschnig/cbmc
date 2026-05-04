// TSH: Narrowing.md — null narrowing
function len(s: string | null): number {
  if (s === null) return 0;
  return s.length;
}
console.assert(len("hello") === 5);
console.assert(len(null) === 0);
