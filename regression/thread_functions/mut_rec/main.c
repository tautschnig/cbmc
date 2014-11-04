int foo();

int bar(){
  foo();
  return 0;
}

int foo(){
  bar();
  return 0;
}

int main(){
  foo();
  return 0;
}
