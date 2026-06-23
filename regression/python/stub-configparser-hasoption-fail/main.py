import configparser
cp = configparser.ConfigParser()
assert cp.has_option("s", "o") == False
