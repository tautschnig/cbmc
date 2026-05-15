from typing import Required, NotRequired, TypedDict, Unpack
from re import compile


HumanTaskConfig = TypedDict("HumanTaskConfig", {
    "PreHumanTaskLambdaArn": NotRequired[str],
})


CreateRequest = TypedDict("CreateRequest", {
    "Name": Required[str],
    "HumanTaskConfig": NotRequired[HumanTaskConfig],
})


class Service:
    def create(self, **kwargs: Unpack[CreateRequest]) -> None:
        if "PreHumanTaskLambdaArn" in kwargs["HumanTaskConfig"]:
            assert compile(
                "^arn:aws[a-z\\-]*:lambda:[a-z0-9\\-]*:[0-9]{12}:function:"
            ).search(
                kwargs["HumanTaskConfig"]["PreHumanTaskLambdaArn"]
            ) is not None, "lambda ARN required"
