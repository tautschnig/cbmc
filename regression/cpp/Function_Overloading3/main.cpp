#include <cassert>

class t1{
    public:
        t1 (int n) :value(n) {}
    public:
        int value;
        int operator[](int n) { return n*value; }
};

int operator+(t1 left, int right) {return left.value+right;}

int main()
{
    t1 t(10);
    int t_1 = t + 5;
    int t_2 = t[5];
    assert(t_1 == 15);
    assert(t_2 == 50);
    return 0;
}
