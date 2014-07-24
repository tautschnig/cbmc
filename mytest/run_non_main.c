/* It is possible to run a non main function like foo() below at startup */
/* How: use the "function" command at the interpreter mode */

int foo()
{
  int a;
  a = 1;
  return a;
}

/* you would still have to provide the main() funciton; */
/* otherwise it won't be compiled */;
int main()
{
  return 0;
}
