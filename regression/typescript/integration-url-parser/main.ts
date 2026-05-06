// URL-like parser
class URL {
  protocol: string;
  host: string;
  path: string;
  constructor(url: string) {
    this.protocol = "";
    this.host = "";
    this.path = "";
    // Parse protocol
    const idx: number = url.indexOf("://");
    if (idx >= 0) {
      this.protocol = url.substring(0, idx);
      const rest: string = url.substring(idx + 3);
      const slash: number = rest.indexOf("/");
      if (slash >= 0) {
        this.host = rest.substring(0, slash);
        this.path = rest.substring(slash);
      } else {
        this.host = rest;
      }
    }
  }
}

const u = new URL("https://example.com/path");
console.assert(u.protocol === "https");
console.assert(u.host === "example.com");
console.assert(u.path === "/path");
