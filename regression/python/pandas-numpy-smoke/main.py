# Smoke test for pandas and numpy stubs.


# numpy
import numpy as np
arr = np.zeros(5)
arr2 = np.ones(3)
arr3 = np.array([1, 2, 3])

# numpy method calls
s = arr.sum()
m = arr2.mean()

# numpy.random
r = np.random.rand(4)

# numpy.linalg
from numpy.linalg import inv, norm


# pandas
import pandas as pd
df = pd.DataFrame({"a": [1, 2, 3], "b": [4, 5, 6]})
sr = pd.Series([10, 20, 30])

# pandas API surface
df2 = df.head()
df3 = df.dropna()
sr2 = df["a"]

# Read functions
df4 = pd.read_csv("x.csv")
