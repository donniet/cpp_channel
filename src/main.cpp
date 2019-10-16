#include "channel.hpp"
#include <iostream>

using namespace chan;

void foo() {
    channel<int> c;

    int val = 0;

    c.send(7);
    c.recv(val);
    std::cout << "value: " << val << std::endl;
}

int main(int ac, char ** av) {
    foo();


    return 0;
}