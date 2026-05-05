const words: string[] = ["hello", "world"];
const joined: string = words.reduce((a: string, b: string): string => a + " " + b, "");
console.assert(joined === " hello world");
