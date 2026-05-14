# PySpec-style stub module: defines the TypedDict and a class
# whose method takes **kwargs: Unpack[TypedDict]. This is the
# pattern boto3 stubs use; the TypedDict scanner in
# process_imported_module runs only on imported modules, so the
# regression test exercises that import path.
from typing import Required, NotRequired, TypedDict, Unpack


CreateRequest = TypedDict('CreateRequest', {
    'Name': Required[str],
    'Description': NotRequired[str],
    'Count': NotRequired[int],
})


class Service:
    def create(self, **kwargs: Unpack[CreateRequest]) -> None:
        pass
