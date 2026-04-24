# Limitation: ClassName.attr not supported (only instance.attr)
class Config:
    max_retries: int = 3

assert Config.max_retries == 3
