enum Direction { North = 0, South = 1, East = 2, West = 3 }
function opposite(d: Direction): Direction {
  if (d === Direction.North) return Direction.South;
  if (d === Direction.South) return Direction.North;
  if (d === Direction.East) return Direction.West;
  return Direction.East;
}
console.assert(opposite(Direction.North) === Direction.South);
console.assert(opposite(Direction.East) === Direction.West);
