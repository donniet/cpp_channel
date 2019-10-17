#include "channel.hpp"
#include "gtest/gtest.h"

#include <thread>
#include <chrono>
#include <iostream>

using namespace chan;

TEST(ChannelTest, Close) {
    channel<int> c;

    EXPECT_FALSE(c.is_closed());

    c.close();

    EXPECT_TRUE(c.is_closed());
}

TEST(ChannelTest, SendRecv) {
    channel<int> c;

    c.send(5);
    int r;
    c.recv(r);

    EXPECT_EQ(r, 5);

    c.send(6);
    c.send(7);
    c.send(8);

    c.recv(r);

    EXPECT_EQ(r, 6);
    
    c.recv(r);

    EXPECT_EQ(r, 7);

    c.recv(r);

    EXPECT_EQ(r, 8);

    c.send(9);
    c.send(10);
    c.close();

    c.recv(r);
    EXPECT_EQ(r, 9);
    c.recv(r);
    EXPECT_EQ(r, 10);
    r = 0;
    EXPECT_FALSE(c.recv(r));
    EXPECT_EQ(r, 0);
    EXPECT_TRUE(c.is_closed());
}

TEST(ChannelTest, SendRecvThread) {
    channel<int> c;

    int val = 0;

    std::thread r([&c, &val]{ c.recv(val); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    c.send(6);

    r.join();

    EXPECT_EQ(val, 6);

    std::thread p([&c, &val]{
        for(int i = 7; i < 10; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            c.send(i);
        }
    });

    for(int i = 7; i < 10; i++) {
        c.recv(val);
        EXPECT_EQ(val, i);
    }

    p.join();
}

TEST(ChannelTest, Select) {
    bool ok = true;
    int val = 0;
    channel<int> c;
    

    c.send(7);

    select(
        case_receive(val, c)
    );

    EXPECT_EQ(val, 7);

    c.close();

    select(
        case_receive(std::tie(val, ok), c)
    );

    EXPECT_TRUE(ok);
}

TEST(ChannelTest, SelectAction) {
    channel<int> c;
    
    int val = 0;

    c.send(7);

    select(
        case_receive(val, c, [&val]{
            val++;
        })
    );

    EXPECT_EQ(val, 8);
}

TEST(ChannelTest, SelectThread) {
    channel<int> c;
    
    int val = 0;

    std::thread r([&c, &val]{
        select(
            case_receive(val, c)
        );
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    c.send(8);
    
    r.join();

    EXPECT_EQ(val, 8);
}


TEST(ChannelTest, SelectThreadCases) {
    channel<int> c, d;
    
    int val = 0;

    std::thread r([&c, &d, &val]{
        select(
            case_receive(val, c),
            case_receive(val, d)
        );
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    c.send(8);
    d.send(9);
    
    r.join();

    EXPECT_EQ(val, 8);
}

TEST(ChannelTest, SelectThreadWithDefault) {
    channel<int> c;
    
    int val = 0;

    std::thread r([&c, &val]{
        select(
            case_receive(val, c),
            case_default([&val]{
                val = 10;
            })
        );
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    c.send(9);
    
    r.join();

    EXPECT_EQ(val, 10);
}

TEST(ChannelTest, SelectThreadAction) {
    channel<int> c;
    
    int val = 0;

    std::thread r([&c, &val]{
        select(
            case_receive(val, c, [&val]{
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

    select(
        case_default([&val]{
            val = 1;
        })
    );

    EXPECT_EQ(val, 1);

    channel<int> c;
    val = 0;

    select(
        case_receive(val, c),
        case_default([&val]{
            val = 2;
        })
    );

    EXPECT_EQ(val, 2);

    val = 0;
    bool closed = false;
    c.close();

    select(
        case_receive(std::tie(val, closed), c, [&val]{
            val = 3;
        }),
        case_default([&val]{
            val = 4;
        })
    );

    EXPECT_EQ(val, 3);
}


// TODO: allow default case to come at any position
// TEST(ChannelTest, SelectDefaultCasePosition) {
//     channel<int> c;
//     int val = 0;

//     select(
//         case_default([&val]{
//             val = 1;
//         }),
//         case_receive(val, c)
//     );

//     EXPECT_EQ(val, 1);
// }

TEST(ChannelTest, SelectDefaultCaseSend) {
    channel<int> c;
    int val = 0;

    c.send(2);

    select(
        case_receive(val, c),
        case_default([&val]{
            val = 1;
        })
    );

    EXPECT_EQ(val, 2);
}



int main(int argc, char ** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}