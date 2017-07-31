#include <cassert>

template<class T>
class myclass
{
public:
  explicit myclass(T _m) : m(_m) {}

  myclass<T> &operator+= (int z) { m = z+2; return *this; }
//  myclass<T> operator= (const myclass<T> &e) { m = e.m+2; return *this; }
  bool operator== (const myclass<T> &e) { return (m == e.m); }

protected:
  T m;  
};

/*template<class T>
  myclass<T> myclass<T>::operator= (const myclass<T> &e) { m = e.m+2; return *this; }*/

int main(int argc, char** argv)
{
  myclass<int> x(0);
  myclass<int> y(1);
  //x = y;
  x += 1;
  assert(x == y);
}
