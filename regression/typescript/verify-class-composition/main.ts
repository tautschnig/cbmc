// Class composition (value-based, no shared references)
class Logger {
  count: number;
  constructor() { this.count = 0; }
  log(): void { this.count = this.count + 1; }
  getCount(): number { return this.count; }
}
const logger = new Logger();
logger.log();
logger.log();
logger.log();
console.assert(logger.getCount() === 3);
