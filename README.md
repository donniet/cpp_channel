# cpp_channel

A [golang](https://golang.org) style [channel](https://golang.org/ref/spec#Channel_types) with [select](https://golang.org/ref/spec#Select_statements).  

## usage

Channels can be constructed using the passed type

```
chan::channel<int> c;
```

Then data can be sent and received using the `send` and `recv` functions:

```
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

```
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

```
chan::channel<int> c;
chan::channel<bool> completed;

std::thread worker([&c, &completed]{
  int val;
  bool comp;
  while(!comp) {
    chan::select(
      chan::case_receive(val, c, [&val]{
        std::cout << "got a value: " << val << std::endl;
      }),
      chan::case_receive(comp, completed, [&c, &comp]{
        if (comp) c.close();
      })
    );
  }
});

std::thread timer([&completed]{
  std::this_thread::sleep_for(std::milliseconds(1000));
  completed.send(true);
});

for(int i = 0; c.send(i); i++) { }

timer.join();
worker.join();
```
