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
#include <type_traits>
#include <thread>

namespace chan {

template<typename T> class channel;

namespace detail {
    // returns true if available, false if waiting
    // either way this returns immediately and does not wait
    // returns zero if value is set.

    // these functions shouldn't be used outside of the library
    template<typename T>
    int __recv_or_notify(channel<T> & c, T & msg, std::function<bool(const T & msg, bool is_closed)> notifier) {
        std::unique_lock<std::mutex> lock(c.m);

        if (c.queue.size() > 0) {
            msg = c.queue.front();
            c.queue.pop_front();

            return 0;
        } else if (c.is_closed_) {
            //TODO: should we set msg to T()? to mimic the "zero value" in golang?
            return 0;
        }

        ++c.wait_id;

        auto pos = c.wait_list.insert(c.wait_list.end(), {c.wait_id, notifier});
        c.wait_list_index[c.wait_id] = pos;
        return c.wait_id;
    }


    // this function should not be used outside of the library
    template<typename T>
    bool __unnotify(channel<T> & c, int id) {
        std::unique_lock<std::mutex> lock(c.m);

        auto i = c.wait_list_index.find(id);
        if (i == c.wait_list_index.end()) {
            return false;
        }

        c.wait_list.erase(i->second);
        return true;
    }
}

template<typename T>
struct scoped_count {
private:
    T & p;
public:
    scoped_count(T & p) : p(++p) { }
    ~scoped_count() { --p; }
};

template<typename T>
class channel {
    friend int detail::__recv_or_notify<T>(channel<T> &, T &, std::function<bool(const T &, bool)>);
    friend bool detail::__unnotify<T>(channel<T> &, int);
private:
    std::list<T> queue;
    std::mutex m;
    std::condition_variable cv;

    bool is_closed_;

    int receivers_;

    typedef std::list<std::pair<int, std::function<bool(const T & msg, bool is_closed)>>> wait_list_type;

    int wait_id;
    wait_list_type wait_list;
    std::map<int, typename wait_list_type::iterator> wait_list_index;


    // assumes lock
    void empty_wait_list() {
        T zero;

        while(wait_list.size() > 0) {
            auto notify = wait_list.front();
            wait_list.pop_front();
            wait_list_index.erase(notify.first);

            notify.second(zero, is_closed_);
        }
    }

public:
    void close() {
        std::unique_lock<std::mutex> lock(m);

        if (!is_closed_) {
            is_closed_ = true;
            // queue is empty iff wait_list is not empty
            empty_wait_list();

            if (queue.size() == 0) {
                lock.unlock();
                cv.notify_all();
            }
        }
    }
    
    bool send(const T & msg) {
        std::unique_lock<std::mutex> lock(m);

        // in golang send on a closed channel panics, but we will return false
        if (is_closed_) {
            empty_wait_list();
            return false;
        }

        // waiters get first priority
        while (wait_list.size() > 0) {
            auto notify = wait_list.front();
            wait_list.pop_front();
            wait_list_index.erase(notify.first);
        
            // if the receiver returns true we can exit, otherwise go onto the next waiter
            if (notify.second(msg, is_closed_)) {
                return true;
            }
        }

        queue.push_back(msg);

        lock.unlock();
        cv.notify_one();

        return true;
    }

    template<bool wait = true>
    bool recv(T & msg) {
        std::unique_lock<std::mutex> lock(m);

        scoped_count<int> ref(receivers_);

        cv.wait(lock, [this]{ return !wait || queue.size() > 0 || is_closed_; });

        // TODO: there is a chance that after descruction this could result in a segmentation fault
        //   we should find a way to use the receivers_ reference count to ensure no more receivers before 
        //   finishing destruction.  maybe we need another condition variable?
        // this means that the channel must be closed
        if (queue.size() == 0) {
            return false;
        }

        msg = queue.front();
        queue.pop_front();

        if (is_closed_ && queue.size() == 0) {
            lock.unlock();
            cv.notify_all();
        }

        return true;
    }
    bool is_closed() {
        std::unique_lock<std::mutex> lock(m);

        return is_closed_;
    }

    channel() 
        : wait_id(0), is_closed_(false), receivers_(0) 
    { }

    ~channel() {
        // empty the queue
        std::unique_lock<std::mutex> lock(m);
        queue.clear();

        // implement close under the same lock to prevent race conditions:
        if (!is_closed_) {
            is_closed_ = true;
            // queue is empty iff wait_list is not empty
            empty_wait_list();
        }

        lock.unlock();
        cv.notify_all();
    } 
};

template<typename T>
class receiver {
public:
    T * dat;
    bool * closed;
    channel<T> * chan;
    std::function<void()> action;

    typedef T value_type;

    receiver(T & d, channel<T> & c, std::function<void()> action) 
        : dat(&d), closed(nullptr), chan(&c), action(action) 
    { }

    receiver(std::tuple<T&,bool&> p, channel<T> & c, std::function<void()> action) 
        : dat(&std::get<0>(p)), closed(&std::get<1>(p)), chan(&c), action(action) 
    { }

    receiver(channel<T> & c, std::function<void()> action) 
        : dat(nullptr), closed(nullptr), chan(&c), action(action) 
    { }
    
    receiver(T & d, channel<T> & c) 
        : dat(&d), closed(nullptr), chan(&c), action() 
    { }

    receiver(std::tuple<T&,bool&> p, channel<T> & c) 
        : dat(&std::get<0>(p)), closed(&std::get<1>(p)), chan(&c), action() 
    { }

    receiver(channel<T> & c) 
        : dat(nullptr), closed(nullptr), chan(&c), action() 
    { }

    // default receiver
    receiver(std::function<void()> action = nullptr) 
        : dat(nullptr), closed(nullptr), chan(nullptr), action(action) 
    { }
};

template<typename T> receiver<T> case_receive(T & dat, channel<T> & c, std::function<void()> action = nullptr) {
    return receiver<T>(dat, c, action);
}
template<typename T> receiver<T> case_receive(channel<T> & c, std::function<void()> action = nullptr) {
    return receiver<T>(c, action);
}
template<typename T> receiver<T> case_receive(std::tuple<T&, bool&> p, channel<T> & c, std::function<void()> action = nullptr) {
    return receiver<T>(p, c, action);
}


template<>
class receiver<void> {
public:
    typedef void value_type;

    std::function<void()> action;
};

receiver<void> case_default(std::function<void()> action = nullptr) {
    return receiver<void>{action};
}

namespace detail {
    template<typename ... Ts>
    class selector;

    template<> class selector<> {
    public:
        std::mutex m;
        std::condition_variable cv;

        std::function<void()> selected_action;
        bool completed;

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

        bool get_completed() {
            std::unique_lock<std::mutex> lock(m);

            return completed;
        }
    };

    template<>
    class selector<void> : public selector<> {
    public:
        std::function<void()> action;

        selector(receiver<void> const & def) : selector<>() {
            action = def.action;
        }
    };

    template<typename T, typename ... Ts>
    class selector<T, Ts...> : public selector<Ts...> {
    public:
        T * dat;
        bool * closed;
        channel<T> * chan;
        std::function<void()> action;

        int wait_id;

        selector(receiver<T> const & r, receiver<Ts> const & ... rs)
            : selector<Ts...>(rs...), dat(r.dat), closed(r.closed), chan(r.chan), action(r.action), wait_id(0)
        { 
            if (this->completed)
                return;

            // no need to lock because constructors are called sequentially
            T d;

            // have to force it to look for the T instantiation
            wait_id = detail::__recv_or_notify<T>(*chan, d, std::bind(&selector<T, Ts...>::notify, this, std::placeholders::_1, std::placeholders::_2));

            if(wait_id == 0) {
                notify(d, chan->is_closed());
            } 
        }

        ~selector() {
            detail::__unnotify(*chan, wait_id);
        }

        bool notify(T const & val, bool is_closed) {
            std::unique_lock<std::mutex> lock(this->m);

            // only allow one case to be selected
            if (this->completed) 
                return false;

            if (dat != nullptr)
                *dat = val;

            if (closed != nullptr) {
                *closed = is_closed;
            }

            this->selected_action = action;
            this->completed = true;

            lock.unlock();
            this->cv.notify_all();

            return true;
        }
    };

    template<bool has_default, typename ... Ts>
    struct select_inner {};

    template<typename ... Ts>
    struct select_inner<true, Ts...> {
        static void select(receiver<Ts> const & ... rs) {
            selector<Ts...> s(rs...);

            // if it completed, execute the action
            if (s.get_completed()) {
                s.wait_and_action();
                return;
            }

            // or else execute the default action
            selector<void> * ps = dynamic_cast<selector<void>*>(&s);
            ps->action();
        }
    };

    template<typename ... Ts>
    struct select_inner<false, Ts...> {
        static void select(receiver<Ts> const & ... rs) {
            selector<Ts...> s(rs...);

            s.wait_and_action();
        }
    };
} // namespace detail

template<typename ... Ts>
void select(receiver<Ts> const & ... rs) {
    // this ensures that we are only casting to the default type if we have that case.
    detail::select_inner<
        std::is_base_of<detail::selector<void>, detail::selector<Ts...>>::value,
        Ts...
    >::select(rs...);
}


} // namespace chan