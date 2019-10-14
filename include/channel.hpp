#pragma once

#include <list>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>
#include <tuple>
#include <iostream>
#include <map>
#include <deque>

namespace chan {

template<typename T>
class channel {
private:
    std::deque<T> queue;
    std::mutex m;
    std::condition_variable cv;

    typedef std::list<std::pair<int, std::function<bool(const T & msg)>>> wait_list_type;

    int wait_id;
    wait_list_type wait_list;
    std::map<int, typename wait_list_type::iterator> wait_list_index;

public:
    // returns true if available, false if waiting
    // either way this returns immediately and does not wait
    // returns zero if value is set.
    int recv_or_notify(T & msg, std::function<bool(const T & msg)> notifier) {
        std::unique_lock<std::mutex> lock(m);

        if (queue.size() > 0) {
            msg = queue.front();
            queue.pop_front();

            return 0;
        }

        ++wait_id;

        auto pos = wait_list.insert(wait_list.end(), {wait_id, notifier});
        wait_list_index[wait_id] = pos;
        return wait_id;
    }
    bool unnotify(int id) {
        std::unique_lock<std::mutex> lock(m);

        auto i = wait_list_index.find(id);
        if (i == wait_list_index.end()) {
            return false;
        }

        wait_list.erase(i->second);
        return true;
    }
    bool send(const T & msg) {
        std::unique_lock<std::mutex> lock(m);

        // waiters get first priority
        while (wait_list.size() > 0) {
            auto notify = wait_list.front();
            wait_list.pop_front();
            wait_list_index.erase(notify.first);
        
            // if the receiver returns true we can exit, otherwise go onto the next waiter
            if (notify.second(msg)) {
                return true;
            }
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

    channel() : wait_id(0) { }    
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

class defaulter {
public:
    std::function<void()> action;
};

defaulter case_default(std::function<void()> action = nullptr) {
    return defaulter{action};
}

template<typename ... Ts>
class selector;

template<> class selector<> {
public:
    std::mutex m;
    std::condition_variable cv;

    std::function<void()> selected_action;
    bool completed;

    selector(defaulter def) : selector() {
        if(!completed) {
            selected_action = def.action;
            completed = true;
        }
    }

    selector() 
        : selected_action(nullptr), completed(false) 
    { }

    void wait_and_action() {
        std::unique_lock<std::mutex> lock(m);

        cv.wait(lock, [this]{ return completed; });

        if (selected_action) {
            selected_action();
        }
    }
};

template<typename T, typename ... Ts>
class selector<T, Ts...> : public selector<Ts...> {
public:
    T * dat;
    channel<T> * chan;
    std::function<void()> action;

    int wait_id;

    selector(defaulter def, receiver<T> const & r, receiver<Ts> const & ... rs)
        : selector<T, Ts...>(r, rs...)
    { 
        if (!this->completed) {
            this->selected_action = def.action;
            this->completed = true;
        }
    }

    selector(receiver<T> const & r, receiver<Ts> const & ... rs)
        : selector<Ts...>(rs...), dat(r.dat), chan(r.chan), action(r.action), wait_id(0)
    { 
        if (this->completed)
            return;

        // no need to lock because constructors are called sequentially
        T d;

        wait_id = chan->recv_or_notify(d, std::bind(&selector<T, Ts...>::notify, this, std::placeholders::_1));

        if(wait_id == 0) {
            notify(d);
        }
    }

    ~selector() {
        chan->unnotify(wait_id);
    }

    bool notify(T const & val) {
        std::unique_lock<std::mutex> lock(this->m);

        // only allow one case to be selected
        if (this->completed) 
            return false;

        if (dat != nullptr)
            *dat = val;

        this->selected_action = action;
        this->completed = true;

        lock.unlock();
        this->cv.notify_all();

        return true;
    }
};

template<typename ... Ts>
void select(receiver<Ts> const & ... rs) {
    selector<Ts...> s(rs...);

    s.wait_and_action();
}

template<typename ... Ts>
void select(receiver<Ts> const & ... rs, defaulter const & def) {
    selector<Ts...> s(def, rs...);

    s.wait_and_action();
}

void select(defaulter const & def) {
    selector<> s(def);

    s.wait_and_action();
}



// template<typename ... Ts>
// void select_inner(std::condition_variable &, receiver<Ts> & ... rs);

// template<typename ... Ts>
// bool select_inner(
//     bool * completed, 
//     std::mutex * m, 
//     std::condition_variable * cv, 
//     const receiver<Ts> & ... rs);

// template<typename ... Ts>
// void select(const receiver<Ts> & ... rs) {
//     std::mutex m;
//     std::unique_lock<std::mutex> lk(m);
//     bool completed = false;

//     std::condition_variable to_notify;

//     if (select_inner(&completed, &m, &to_notify, rs...)) {
//         return;
//     }

//     to_notify.wait(lk, [&completed]{ return completed; });
// }

// template<typename T>
// void notifier(
//     bool * completed, 
//     std::mutex * m, 
//     std::condition_variable * cv, 
//     const std::function<void()> & action, 
//     T * const & dat, 
//     const T & msg) 
// {
//     std::unique_lock<std::mutex> lk(*m);

//     if (dat != nullptr)
//         *dat = msg;

//     // TODO: should this come after the action or before?
//     *completed = true;

//     // TODO: exception handling will be a nightmare...
//     if (action) {
//         action();
//     }

//     lk.unlock();
//     cv->notify_all();
// }

// template<typename T, typename ... Ts>
// bool select_inner(
//     bool * completed, 
//     std::mutex * m,  
//     std::condition_variable * cv, 
//     const receiver<T> & r, 
//     const receiver<Ts> & ... rs) 
// {
//     // default case
//     if (r.chan == nullptr) {
//         if (r.action) {
//             r.action();
//         }
//         return true;
//     }

//     T temp;

//     if (r.chan->recv_or_notify(temp, std::bind(notifier<T>, completed, m, cv, r.action, r.dat, std::placeholders::_1))) {
//         if (r.dat != nullptr) {
//             *r.dat = temp;
//         }

//         if (r.action) {
//             r.action();
//         }
//         return true;
//     }

//     return select_inner(completed, m, cv, rs...);
// }

// // tail case
// template<> bool select_inner(
//     bool *,
//     std::mutex *, 
//     std::condition_variable *)
// {
//     return false;
// }

} //namespace chan


// void use_select() {
//     select(
//         receiver(msg, chan, []{}),

//     )
// }