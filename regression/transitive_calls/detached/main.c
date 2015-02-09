int baz(){
  return 0;
}

int bar(){
  return baz();
}

int quux(){
  return 9;
}

int foo(){
  return quux();
}

int main(){
  return 0;
}
