// Rate limiter invariant: count <= limit
class RateLimiter {
  count: number;
  limit: number;
  constructor(l: number) { this.count = 0; this.limit = l; }
  allow(): boolean {
    if (this.count >= this.limit) return false;
    this.count = this.count + 1;
    return true;
  }
}
const rl = new RateLimiter(3);
console.assert(rl.allow() === true);
console.assert(rl.allow() === true);
console.assert(rl.allow() === true);
console.assert(rl.allow() === false);
console.assert(rl.count <= rl.limit);
