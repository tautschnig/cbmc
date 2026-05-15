# Stage 1 of the re-precision plan, second half: detect a
# regex-no-match condition where the empty-string subject
# arrives via a nested-Dict kwarg passed by the user, and the
# stub method's body asserts a non-ε-accepting regex against
# that nested kwarg path.
#
# Mirrors the boto3 SageMaker create_labeling_job pattern in
# python-verification-benchmarks: the user passes
#   HumanTaskConfig={'PreHumanTaskLambdaArn': '', ...}
# and the stub asserts
#   compile("^arn:aws...lambda:...:function:")
#       .search(kwargs["HumanTaskConfig"]["PreHumanTaskLambdaArn"])
#       is not None

from typing import Required, NotRequired, TypedDict, Unpack
from stub import Service


client = Service()
client.create(
    Name="job-001",
    HumanTaskConfig={
        "PreHumanTaskLambdaArn": "",  # bug: empty fails the regex
    },
)
