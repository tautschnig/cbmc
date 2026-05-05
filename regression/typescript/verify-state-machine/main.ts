enum State { Idle = 0, Running = 1, Done = 2 }
function transition(s: State, event: number): State {
  if (s === State.Idle && event === 1) return State.Running;
  if (s === State.Running && event === 2) return State.Done;
  return s;
}
let state: State = State.Idle;
state = transition(state, 1);
console.assert(state === State.Running);
state = transition(state, 2);
console.assert(state === State.Done);
