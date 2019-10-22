#include "channel.hpp"
#include "gtest/gtest.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <set>

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

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
    EXPECT_TRUE(closed);
}

TEST(ChannelTest, StressTest) {
    int val = 0;

    channel<int> c;

    std::thread r([&c]{
        for(int i = 0; i < 1e6; i++) {
            c.send(i);
        }
        c.close();
    });

    int match = 0;

    for(int i = 0; i < 1e6; i++) {
        c.recv(val);

        if (i == val) match++;
    }

    r.join();

    EXPECT_EQ(match, 1e6);
    EXPECT_TRUE(c.is_closed());
}

TEST(ChannelTest, StressTest3) {
    channel<int> c;

    int thread_count = 1000;
    int insert = 1000;
    std::vector<std::thread*> threads;

    std::atomic_int32_t fail_count = 0;

    std::set<int> all;

    for(int i = 0; i < thread_count * insert; i++) {
        all.insert(i);
    }

    for(int i = 0; i < thread_count; i++) {
        threads.push_back(new std::thread([i, thread_count, insert, &c, &fail_count]{
            for(int j = 0; j < insert; j++) {
                if (!c.send(i * insert + j)) {
                    std::cerr << "insert failed!" << std::endl;
                    fail_count++;
                }
            }
        }));
    }


    int count = 0;
    int completed = 0;
    int val = 0;
    bool is_closed = false;

    for(int i = 0; i < thread_count * insert; i++) {
        if (!c.recv(val)) {
            std::cerr << "recv failed!" << std::endl;
            fail_count++;
        } else {
            count++;
            if (all.erase(val) != 1) {
                std::cerr << "duplicate detecte: " << val << std::endl;
                fail_count++;
            }
        }
    }

    // cleanup
    for(std::thread *& t : threads) {
        t->join();
        t = nullptr;
        delete t;
    }
    threads.empty();

    for(int x : all) {
        std::cerr << x << " ";
    }
    std::cerr << std::endl;

    EXPECT_EQ(c.recv_queue() + c.recv_watchers(), thread_count * insert);
    EXPECT_EQ(c.recv_while_closed(), 0);
    EXPECT_EQ(c.send_queue() + c.send_watchers(), thread_count * insert);

    EXPECT_EQ(fail_count, 0);
    EXPECT_EQ(count, thread_count * insert);
}

TEST(ChannelTest, ReceiveClosed) {
    channel<int> c;

    c.close();

    int val = 0xDEADBEEF;
    bool is_closed = false;
    bool is_error = false;

    select(
        case_receive(std::tie(val, is_closed), c, [&is_closed, &is_error]{
            if (!is_closed) {
                is_error = true;
            }
        })
    );

    ASSERT_FALSE(is_error);
    ASSERT_TRUE(is_closed);
}


TEST(ChannelTest, StressTestSelect) {
    channel<int> c;
    channel<int> to_close;

    int thread_count = 1000;
    int insert = 1000;
    std::vector<std::thread*> threads;

    std::atomic_int32_t fail_count = 0;

    std::set<int> all;

    for(int i = 0; i < thread_count * insert; i++) {
        all.insert(i);
    }

    for(int i = 0; i < thread_count; i++) {
        threads.push_back(new std::thread([i, thread_count, insert, &c, &to_close, &fail_count]{
            for(int j = 0; j < insert; j++) {
                if (!c.send(i * insert + j)) {
                    std::cerr << "insert failed!" << std::endl;
                    fail_count++;
                }
            }
            to_close.send(i);
        }));
    }


    int count = 0;
    int completed = 0;
    int val = 0;
    bool is_closed = false;

    while(!is_closed) {
        select(
            case_receive(std::tie(val, is_closed), c, [&count, &all, &val, &fail_count, &is_closed]{ 
                int c = 0;
                if (!is_closed) {
                    if ((c = all.erase(val)) != 1) {
                        fail_count++;
                    } else {
                        count++;
                    }
                }
            }),
            case_receive(to_close, [&completed, &c, thread_count]{ 
                completed++; 
                if (completed >= thread_count) {
                    c.close();
                }
            })
        );
    }

    // cleanup
    for(std::thread *& t : threads) {
        t->join();
        t = nullptr;
        delete t;
    }
    threads.empty();

    EXPECT_EQ(c.recv_queue() + c.recv_watchers(), thread_count * insert);
    EXPECT_EQ(c.send_queue() + c.send_watchers(), thread_count * insert);
    EXPECT_EQ(all.size(), 0);

    EXPECT_EQ(fail_count, 0);
    EXPECT_EQ(count, thread_count * insert);
}

TEST(ChannelTest, Triangle) {
    channel<int> c, d;

    std::thread r([&c, &d] {
        int val = 0;
        while(c.recv(val)) {
            d.send(val);
        }
        d.close();
    });

    int val = 0;
    int match = 0;

    for(int i = 0; i < 1e6; i++) {
        c.send(i);

        d.recv(val);

        if (i == val) match++;
    }
    c.close();
    r.join();

    EXPECT_EQ(match, 1e6);
    EXPECT_TRUE(c.is_closed());
    EXPECT_TRUE(d.is_closed());
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