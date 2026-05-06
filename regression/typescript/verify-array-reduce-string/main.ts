const words: string[] = ["Hello", "World"];
const joined: string = words.reduce(
  (acc: string, w: string): string => acc + w, "");
console.assert(joined === "HelloWorld");
