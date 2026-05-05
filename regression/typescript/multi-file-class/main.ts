import { Counter } from "./counter";
const c = new Counter();
c.increment();
c.increment();
console.assert(c.getCount() === 2);
