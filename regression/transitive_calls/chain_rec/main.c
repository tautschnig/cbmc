int a();
int b();
int c();
int d();
int _1();
int _2();
int _3();
int _4();

int _1(){
  b();
  return 0;
}
int _2(){
  c();
  return 0;
}
int _3(){
  d();
  return 0;
}
int _4(){
  a();
  return 0;
}
int a(){
  _1();
  return 0;
}
int b(){
  _2();
  return 0;
}
int c(){
  _3();
  return 0;
}
int d(){
  _4();
  return 0;
}

int main(){
  a();
  return 0;
}
