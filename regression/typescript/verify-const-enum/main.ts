const enum Direction { Up = 0, Down = 1, Left = 2, Right = 3 }
function opposite(d: Direction): Direction {
  switch (d) {
    case Direction.Up: return Direction.Down;
    case Direction.Down: return Direction.Up;
    case Direction.Left: return Direction.Right;
    default: return Direction.Left;
  }
}
console.assert(opposite(Direction.Up) === Direction.Down);
console.assert(opposite(Direction.Down) === Direction.Up);
console.assert(opposite(Direction.Left) === Direction.Right);
console.assert(opposite(Direction.Right) === Direction.Left);
