#pragma yanc prname sapho_demo
#include "solar_math.hpp"

void main(void) {
    int s = 0;
    for (int i = 1; i <= 10; i = i + 1) s = add(s, i);
    out(0, s);
    out(0, sizeof(int));
}
