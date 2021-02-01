#ifdef __GNUC__
int in_other_section(void);

int __attribute__((__section__(".init.text"))) in_other_section(void)
{
  return 42;
}

int __attribute__((__section__(".init.text"))) also_in_other_section(void)
{
  return 42;
}
#endif
