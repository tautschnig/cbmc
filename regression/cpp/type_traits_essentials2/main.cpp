template<typename _Tp, _Tp __v>
struct integral_constant
{
	static constexpr _Tp                  value = __v;
	typedef _Tp                           value_type;
	constexpr operator value_type() const { return value; }
};

#ifdef __GNUC__
union __type
{
	unsigned char __data[1];
	struct __attribute__((__aligned__)) { } __align;
};
#endif

int main(int argc, char* argv[])
{
  return 0;
}
