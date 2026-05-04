// TSH: Enums
enum Direction { Up, Down, Left, Right }
const d: Direction = Direction.Down;
console.assert(d === 1);
console.assert(Direction.Up === 0);
console.assert(Direction.Right === 3);
