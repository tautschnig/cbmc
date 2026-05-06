function take(s: string, n: number): string {
  return s.substring(0, n);
}
const x: string = take("hello world", 5);
console.assert(x === "hello");
