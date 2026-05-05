class Logger {
  count: number;
  constructor() { this.count = 0; }
  log(): void { this.count = this.count + 1; }
  getCount(): number { return this.count; }
}
class Service {
  logger: Logger;
  constructor(l: Logger) { this.logger = l; }
  doWork(): void { this.logger.log(); }
}
const logger = new Logger();
const svc = new Service(logger);
svc.doWork();
svc.doWork();
console.assert(logger.getCount() === 2);
