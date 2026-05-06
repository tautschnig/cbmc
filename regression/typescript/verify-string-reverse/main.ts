const arr: string[] = "abc".split("");
const rev: string[] = arr.reverse();
const result: string = rev.join("");
console.assert(result === "cba");
