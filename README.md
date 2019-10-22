# cpp_channel

A [header-only](https://github.com/donniet/cpp_channel/blob/master/include/channel.hpp) [golang](https://golang.org)-style [channel](https://golang.org/ref/spec#Channel_types) with [select](https://golang.org/ref/spec#Select_statements).  

## usage

Channels are type-safe and simply constructed.

```cpp
chan::channel<int> c;
```

Then data can be sent and received using the `send` and `recv` functions:

```cpp
chan::channel<int> c;
bool success = false;
success = c.send(5);
ASSERT_TRUE(success);
int val = 0;
success = c.recv(val);
ASSERT_TRUE(success);
ASSERT_EQ(val, 5);
```

The true value of a channel is for passing data between threads:

```cpp
chan::channel<int> c;

std::thread worker([&c]{
  int val;
  while(c.recv(val)) {
    std::cout << "got a value: " << val << std::endl;
  }
});

for(int i = 0; i < 100; i++) {
  c.send(i);
}
c.close();
worker.join();
```

Note that the channel must be passed by reference, it is not copy constructable, and may not be able to be move constructable (see TODOs)

You can also wait on multiple channels using a select:

```cpp
using namespace chan;

channel<int> c;
channel<bool> completed;

std::thread worker([&c, &completed]{
  int val = 0;
  bool comp = false;
  while(!comp) {
    select(
      // note that the captured variables are passed by reference.
      // this is important because if passed by value the value would
      // be captured before the case has completed.
      case_receive(val, c, [&val]{
        std::cout << "got a value: " << val << std::endl;
      }),
      case_receive(comp, completed, [&c, &comp]{
        if (comp) c.close();
      })
    );
  }
});

std::thread timer([&completed]{
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  completed.send(true);
});

for(int i = 0; c.send(i); i++) { 
  // will end once the timer thread sends the complete signal
}

timer.join();
worker.join();
```

## TODOs
- set a maximum channel buffer size
- allow sends in select statements
- explicitly delete copy constructor
- research if channels can be move constructable
- research a shared_ptr specialization for channels
