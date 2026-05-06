const secure: string = "https://example.com";
const insecure: string = "http://example.com";
console.assert(secure.startsWith("https://") === true);
console.assert(insecure.startsWith("https://") === false);
