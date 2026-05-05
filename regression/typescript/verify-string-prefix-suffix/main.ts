const url: string = "https://example.com/path";
console.assert(url.startsWith("https://") === true);
console.assert(url.startsWith("http://") === false);
console.assert(url.endsWith("/path") === true);
console.assert(url.endsWith(".org") === false);
