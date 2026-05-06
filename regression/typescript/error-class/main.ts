const e = new Error("something failed");
console.assert(e.message.length === 16);
const e2 = new Error("x");
console.assert(e2.message.length === 1);
