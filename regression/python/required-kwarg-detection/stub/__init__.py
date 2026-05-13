from typing import TypedDict, Unpack, Required, NotRequired


CreateThingInput = TypedDict('CreateThingInput', {
    'name': Required[str],
    'size': Required[int],
    'description': NotRequired[str],
})


class Client:
    def create_thing(self, **kwargs: Unpack[CreateThingInput]) -> None:
        pass
