const email: string = "user@example.com";
console.assert(email.includes("@") === true);
console.assert(email.includes(".com") === true);
console.assert(email.includes("xyz") === false);
