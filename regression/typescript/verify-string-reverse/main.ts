function reverseStr(s: string): string {
  const arr: string[] = s.split("");
  const rev: string[] = arr.reverse();
  return rev.join("");
}
console.assert(reverseStr("abc") === "cba");
