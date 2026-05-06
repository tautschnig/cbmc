// Simplified version of vercel/ms for verification
function parseMs(str: string): number {
  if (str === "1s") return 1000;
  if (str === "1m") return 60000;
  if (str === "1h") return 3600000;
  if (str === "1d") return 86400000;
  return 0;
}

function format(ms: number): string {
  if (ms >= 86400000) return "day";
  if (ms >= 3600000) return "hr";
  if (ms >= 60000) return "min";
  if (ms >= 1000) return "sec";
  return "ms";
}

console.assert(parseMs("1s") === 1000);
console.assert(parseMs("1m") === 60000);
console.assert(format(1000) === "sec");
console.assert(format(60000) === "min");
console.assert(format(500) === "ms");
