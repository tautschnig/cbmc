class Config:
    name: str = "default"
    value: int = 0

c = Config()
d: dict = {"config": c}
x = d["config"]
