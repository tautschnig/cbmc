int baz(){
  return 0;
}

int bar(){
  return baz();
}

int foo(){
  return 42;
}

int main(){
  foo();
  bar();
  return 0;
}
