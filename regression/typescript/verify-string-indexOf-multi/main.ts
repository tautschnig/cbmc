const s: string = "hello world hello";
console.assert(s.indexOf("hello") === 0);
console.assert(s.indexOf("world") === 6);
console.assert(s.indexOf("xyz") === -1);
