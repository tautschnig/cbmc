// ES2024 sec-try-statement
let caught: boolean = false;
try {
  throw new Error("test");
} catch (e) {
  caught = true;
}
console.assert(caught === true);
