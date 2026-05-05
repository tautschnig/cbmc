interface Item { id: number; value: number; }
const items: Item[] = [
  { id: 1, value: 10 },
  { id: 2, value: 20 },
  { id: 3, value: 30 }
];
const total: number = items.reduce(
  (sum: number, item: Item): number => sum + item.value, 0);
console.assert(total === 60);
