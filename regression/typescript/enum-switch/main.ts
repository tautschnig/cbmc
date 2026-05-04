enum Dir { Up = 1, Down = 2, Left = 3, Right = 4 }
function opposite(d: Dir): Dir {
  switch (d) {
    case Dir.Up: return Dir.Down;
    case Dir.Down: return Dir.Up;
    case Dir.Left: return Dir.Right;
    default: return Dir.Left;
  }
}
console.assert(opposite(Dir.Up) === 2);
console.assert(opposite(Dir.Left) === 4);
