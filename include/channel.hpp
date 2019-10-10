#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>
#include <tuple>
#include <iostream>

namespace chan {

template<typename T>
class channel {
private:
    std::deque<T> queue;
    std::mutex m;
    std::condition_variable cv;

    std::deque<std::function<void(const T & msg)>> wait_list;
public:
    // returns true if available, false if waiting
    // either way this returns immediately and does not wait
    bool recv_or_notify(T & msg, std::function<void(const T & msg)> notifier) {
        std::unique_lock<std::mutex> lock(m);

        if (queue.size() > 0) {
            msg = queue.front();
            queue.pop_front();

            return true;
        }

        wait_list.push_back(notifier);
        return false;
    }
    bool send(const T & msg) {
        std::unique_lock<std::mutex> lock(m);

        // waiters get first priority
        if (wait_list.size() > 0) {
            std::function<void(const T &)> notify = wait_list.front();
            notify(msg);
            wait_list.pop_front();
            return true;
        }

        queue.push_back(msg);

        lock.unlock();
        cv.notify_one();

        return true;
    }
    bool recv(T & msg) {
        std::unique_lock<std::mutex> lock(m);

        cv.wait(lock, [this]{ return queue.size() > 0; });

        msg = queue.front();
        queue.pop_front();

        return true;
    }

    
};

template<typename T>
class receiver {
public:
    T * dat;
    channel<T> * chan;
    std::function<void()> action;

    receiver(T & d, channel<T> & c, std::function<void()> action) : dat(&d), chan(&c), action(action) {}
    receiver(channel<T> & c, std::function<void()> action) : dat(nullptr), chan(&c), action(action) {}
    receiver(T & d, channel<T> & c) : dat(&d), chan(&c), action() {}
    receiver(channel<T> & c) : dat(nullptr), chan(&c), action() {}
    receiver(std::function<void()> action = nullptr) : dat(nullptr), chan(nullptr), action(action) {}
};

template<typename T> receiver<T> case_receive(T & dat, channel<T> & c, std::function<void()> action = nullptr) {
    return receiver<T>(dat, c, action);
}
template<typename T> receiver<T> case_receive(channel<T> & c, std::function<void()> action = nullptr) {
    return receiver<T>(c, action);
}
receiver<char> case_default(std::function<void()> action = nullptr) {
    return receiver<char>(action);
}





// template<typename ... Ts>
// void select_inner(std::condition_variable &, receiver<Ts> & ... rs);

template<typename ... Ts>
bool select_inner(
    bool * completed, 
    std::mutex * m, 
    std::condition_variable * cv, 
    const receiver<Ts> & ... rs);

template<typename ... Ts>
void select(const receiver<Ts> & ... rs) {
    std::mutex m;
    std::unique_lock<std::mutex> lk(m);
    bool completed = false;

    std::condition_variable to_notify;

    if (select_inner(&completed, &m, &to_notify, rs...)) {
        return;
    }

    to_notify.wait(lk, [&completed]{ return completed; });
}

template<typename T>
void notifier(
    bool * completed, 
    std::mutex * m, 
    std::condition_variable * cv, 
    const std::function<void()> & action, 
    T * const & dat, 
    const T & msg) 
{
    std::unique_lock<std::mutex> lk(*m);

    if (dat != nullptr)
        *dat = msg;

    // TODO: should this come after the action or before?
    *completed = true;

    // TODO: exception handling will be a nightmare...
    if (action) {
        action();
    }

    lk.unlock();
    cv->notify_all();
}

template<typename T, typename ... Ts>
bool select_inner(
    bool * completed, 
    std::mutex * m,  
    std::condition_variable * cv, 
    const receiver<T> & r, 
    const receiver<Ts> & ... rs) 
{
    // default case
    if (r.chan == nullptr) {
        if (r.action) {
            r.action();
        }
        return true;
    }

    T temp;

    if (r.chan->recv_or_notify(temp, std::bind(notifier<T>, completed, m, cv, r.action, r.dat, std::placeholders::_1))) {
        if (r.dat != nullptr) {
            *r.dat = temp;
        }

        if (r.action) {
            r.action();
        }
        return true;
    }

    return select_inner(completed, m, cv, rs...);
}

// tail case
template<> bool select_inner(
    bool *,
    std::mutex *, 
    std::condition_variable *)
{
    return false;
}

} //namespace chan


// void use_select() {
//     select(
//         receiver(msg, chan, []{}),

//     )
// }