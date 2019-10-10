#include "channel.hpp"
#include "gtest/gtest.h"

#include <thread>
#include <chrono>
#include <iostream>

TEST(ChannelTest, SendRecv) {
    chan::channel<int> c;

    c.send(5);
    int r;
    c.recv(r);

    EXPECT_EQ(r, 5);
}

TEST(ChannelTest, SendRecvThread) {
    chan::channel<int> c;

    int val = 0;

    std::thread r([&c, &val]{ c.recv(val); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    c.send(6);

    r.join();

    EXPECT_EQ(val, 6);
}

TEST(ChannelTest, Select) {
    chan::channel<int> c;
    
    int val = 0;

    c.send(7);

    chan::select(
        chan::case_receive(val, c)
    );

    EXPECT_EQ(val, 7);
}

TEST(ChannelTest, SelectAction) {
    chan::channel<int> c;
    
    int val = 0;

    c.send(7);

    chan::select(
        chan::case_receive(val, c, [&val]{
            val++;
        })
    );

    EXPECT_EQ(val, 8);
}

TEST(ChannelTest, SelectThread) {
    chan::channel<int> c;
    
    int val = 0;

    std::thread r([&c, &val]{
        chan::select(
            chan::case_receive(val, c)
        );
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    c.send(8);
    
    r.join();

    EXPECT_EQ(val, 8);
}

TEST(ChannelTest, SelectThreadAction) {
    chan::channel<int> c;
    
    int val = 0;

    std::thread r([&c, &val]{
        chan::select(
            chan::case_receive(val, c, [&val]{
                val++;
            })
        );
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    c.send(8);
    
    r.join();

    EXPECT_EQ(val, 9);
}

TEST(ChannelTest, SelectDefault) {
    int val = 0;

    chan::select(
        chan::case_default([&val]{
            val = 1;
        })
    );

    EXPECT_EQ(val, 1);
}

int main(int argc, char ** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}