function Injectable(config: { singleton: boolean }): any {
  return (target: any) => target;
}

@Injectable({ singleton: true })
class Service {
  value: number;
  constructor() { this.value = 42; }
  getValue(): number { return this.value; }
}

const s = new Service();
console.assert(s.getValue() === 42);
