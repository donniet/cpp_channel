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

    // these functions shouldn't be used outside of the library.  They are external to the class to allow
    // friend-like access into the class for the selects below
    template<typename T>
    int __recv_or_notify(channel<T> & c, std::function<bool(const T & msg, bool is_closed)> notifier) {
        std::unique_lock<std::mutex> lock(c.m);

        T msg;
        bool send_closed = false;
        bool no_wait = false;

        // is there something in the queue already?
        if (c.queue.size() > 0) {
            msg = c.queue.front();
            c.queue.pop_front();
            send_closed = false; // only return closed if we didn't send back an output

            no_wait = true;

        } else if (c.is_closed_) {
            //TODO: should we set msg to T()? to mimic the "zero value" in golang?
            send_closed = true;

            no_wait = true;
        }

        // notify now
        if (no_wait) {
            if (!notifier(msg, send_closed)) {
                // the notifier has finished, we need to put this back onto the queue or else we'll lose it!
                // TODO: maybe there's a better way to do this-- but I think that the channel should act as
                // if the item has been removed while the action happens (if it happens) which means
                // that pushing it back on might be the only way.
                c.queue.push_front(msg);

                // we can probably return here, but control will flow to the if(no_wait) statement below
            } else {
#ifndef NDEBUG 
                if (send_closed) {
                    c.receive_while_closed_++;
                } else {
                    c.receive_queue_++;
                }
#endif
            }
        }

        // this looks a bit complicated, but basically if we got something from the queue
        // but now it's empty and closed, we have to notify people.
        if (!send_closed && no_wait && c.queue.size() == 0 && c.is_closed_) {
            lock.unlock();
            c.cond_send.notify_all();
            c.cond_recv.notify_all();
        }

        // if we found something in the queue we can just exit here
        if (no_wait) 
            return 0;

        // if we get here we are a waiter.
        ++c.wait_id;

        auto pos = c.recv_wait_list.insert(c.recv_wait_list.end(), {c.wait_id, notifier});
        c.recv_wait_list_index[c.wait_id] = pos;

        return c.wait_id;
    }

    template<typename T>
    int __send_or_notify(channel<T> & c, std::function<bool(T & msg, bool is_closed)> notifier) {
        std::unique_lock<std::mutex> lock(c.m);

        T msg;

        if (c.is_closed_) {
            // tell the notifier that we are closed
            notifier(msg, true);
            return 0;
        }

        if (c.queue.size() < c.capacity_ + c.receivers_) {
            // get an item from the notifier
            if (!notifier(msg, false)) {
                // notifier doesn't want to give us one, just return
                return 0;
            }

            // add the item to the queue and set the no_wait flag
            c.queue.push_back(msg);

            lock.unlock();
            c.cond_recv.notify_one();
            return 0;
        }

        ++c.wait_id;
        auto pos = c.send_wait_list.insert(c.send_wait_list.end(), {c.wait_id, notifier});
        c.send_wait_list_index[c.wait_id] = pos;

        return c.wait_id;
    }


    // this function should not be used outside of the library
    template<typename T>
    bool __unnotify(channel<T> & c, int id) {
        std::unique_lock<std::mutex> lock(c.m);

        auto i = c.recv_wait_list_index.find(id);
        if (i != c.recv_wait_list_index.end()) {
            c.recv_wait_list.erase(i->second);
            c.recv_wait_list_index.erase(i);
            return true;
        }
        auto j = c.send_wait_list_index.find(id);
        if (j != c.send_wait_list_index.end()) {
            c.send_wait_list.erase(j->second);
            c.send_wait_list_index.erase(j);
            return true;
        }

        return false;
    }

    // use a list as the queue for complex types
    template<typename T, bool trivial = false>
    class __channel_queue {
    protected:
        std::list<T> queue;
    };

    // use a deque for trivial types to speed things up a but
    template<typename T>
    class __channel_queue<T, true> {
    protected:
        std::deque<T> queue;
    };

    template<typename T>
    class __channel_base 
        : public __channel_queue<T, std::is_trivial<T>::value>
    { };
}

template<typename T>
class channel 
    // this will use a deque for simple data types, but a list for complex ones
    : public detail::__channel_base<T> 
{
    friend int detail::__recv_or_notify<T>(channel<T> &, std::function<bool(const T &, bool)>);
    friend int detail::__send_or_notify<T>(channel<T> &, std::function<bool(T &, bool)>);
    friend bool detail::__unnotify<T>(channel<T> &, int);
private:
    std::mutex m;
    std::condition_variable cond_recv;
    std::condition_variable cond_send;

    bool is_closed_;

    int receivers_;
    int senders_;

    typedef std::list<std::pair<int, std::function<bool(const T & msg, bool is_closed)>>> recv_wait_list_type;
    typedef std::list<std::pair<int, std::function<bool(T & msg, bool is_closed)>>> send_wait_list_type;

    int wait_id;
    recv_wait_list_type recv_wait_list;
    send_wait_list_type send_wait_list;
    std::map<int, typename recv_wait_list_type::iterator> recv_wait_list_index;
    std::map<int, typename send_wait_list_type::iterator> send_wait_list_index;

    int capacity_;


#ifndef NDEBUG
    int send_watchers_;
    int send_queue_;
    int receive_watcher_;
    int receive_queue_;
    int receive_while_closed_;
#endif

    // assumes lock
    void empty_recv_wait_list() {
        T zero;

        while(recv_wait_list.size() > 0) {
            auto notify = recv_wait_list.front();
            recv_wait_list.pop_front();
            recv_wait_list_index.erase(notify.first);

            // we only empty the list if we are closed
            notify.second(zero, true);
        }
    }

    // assumes lock
    void empty_send_wait_list() {
        T zero;

        while(send_wait_list.size() > 0) {
            auto notify = send_wait_list.front();
            send_wait_list.pop_front();
            send_wait_list_index.erase(notify.first);

            // we only empty the list if we are closed
            notify.second(zero, true);
        }
    }

public:
    typedef T value_type;

    auto size() {
        std::unique_lock<std::mutex> lock(m);

        return this->queue.size();
    }

    void close() {
        std::unique_lock<std::mutex> lock(m);

        if (!is_closed_) {
            is_closed_ = true;
            // queue is empty iff recv_wait_list is not empty
            empty_recv_wait_list();
            empty_send_wait_list();

            if (this->queue.size() == 0) {
                lock.unlock();
                cond_recv.notify_all();
                cond_send.notify_all();
            }
        }
    }
    
    template<bool wait = true>
    bool send(const T & msg) {
        std::unique_lock<std::mutex> lock(m);

        // in golang send on a closed channel panics, but we will return false
        if (is_closed_) {
            // the wait list has to be empty if is_closed_ is set, see close()
            // if (recv_wait_list.size() > 0) 
            //     empty_recv_wait_list();
            return false;
        } 

        senders_++;

        // waiters get first priority
        while (recv_wait_list.size() > 0) {
            auto notify = recv_wait_list.front();
            recv_wait_list.pop_front();
            recv_wait_list_index.erase(notify.first);
        
            // if the receiver returns true we can exit, otherwise go onto the next waiter
            // the second param should be false (is_closed) because 
            if (notify.second(msg, false)) {
#ifndef NDEBUG
                send_watchers_++;
                receive_watcher_++;
#endif
                senders_--;
                return true;
            }
        }

#ifndef NDEBUG
        send_queue_++;
#endif

        if (this->queue.size() >= capacity_ + receivers_) {
            // no space and not waiting
            if (!wait) {
                senders_--;
                return false;
            }

            cond_send.wait(lock, [this]{ return is_closed_ || this->queue.size() < capacity_ + receivers_; });
            
            // have to check closed again
            if (is_closed_) {
                senders_--;
                return false;
            }
        }

        this->queue.push_back(msg);

        senders_--;

        lock.unlock();
        cond_recv.notify_one();

        return true;
    }

    template<bool wait = true>
    bool recv(T & msg) {
        bool ret = true;

        std::unique_lock<std::mutex> lock(m);

        // keep track of receivers.  This allows us to wait for all receivers to 
        // go away before we free the memory
        receivers_++;

        // waiters get first priority
        while(send_wait_list.size() > 0) {
            auto notify = send_wait_list.front();
            send_wait_list.pop_front();
            send_wait_list_index.erase(notify.first);

            if (notify.second(msg, false)) {
#ifndef NDEBUG
                send_watchers_++;
                receive_watcher_++;
#endif
                receivers_--;
                return true;
            }
        }

        // notify a sender that a receiver is waiting
        if (wait) {
            if(senders_ > 0 && this->queue.size() == 0) {
                // I think in this case we should notify while under a lock because the next statement will
                // unlock immediately 
                // TODO: write a test case for this somehow.
                // lock.unlock();
                cond_send.notify_one();
                // lock.lock();
            }

            cond_recv.wait(lock, [this]{ return this->queue.size() > 0 || is_closed_; });
        }

        if (this->queue.size() > 0) {
            // if there's something to send send it
            msg = this->queue.front();
            this->queue.pop_front();

#ifndef NDEBUG
            receive_queue_++;
#endif
        } else {
            // otherwise we are either closed or not waiting around
            ret = false;
        }

        receivers_--;

        // if we got something, but it was the last thing and we are closed, notify everybody.
        if (ret && is_closed_ && this->queue.size() == 0) {
            lock.unlock();
            cond_send.notify_all();
            cond_recv.notify_all();
        }

        return ret;
    }
    bool is_closed() {
        std::unique_lock<std::mutex> lock(m);

        return this->queue.size() == 0 && is_closed_;
    }

    size_t capacity() const {
        // capacity cannot be changed so no need to lock it
        return capacity_;
    }

    channel() 
        : wait_id(0), is_closed_(false), receivers_(0), senders_(0), capacity_(std::numeric_limits<decltype(capacity_)>::max()) 
#ifndef NDEBUG
            , send_queue_(0), send_watchers_(0), receive_queue_(0), receive_watcher_(0), receive_while_closed_(0)
#endif
    { }

    channel(size_t capacity) : channel() {
        capacity_ = capacity;
    }

#ifndef NDEBUG
    int send_watchers() {
        std::unique_lock<std::mutex> lock(m);

        return send_watchers_;
    }
    int send_queue() {
        std::unique_lock<std::mutex> lock(m);

        return send_queue_;
    }
    int recv_watchers() {
        std::unique_lock<std::mutex> lock(m);

        return receive_watcher_;
    }
    int recv_queue() {
        std::unique_lock<std::mutex> lock(m);

        return receive_queue_;
    }
    int recv_while_closed() {
        std::unique_lock<std::mutex> lock(m);

        return receive_while_closed_;
    }
#endif

    ~channel() {
        // empty the queue
        std::unique_lock<std::mutex> lock(m);
        this->queue.clear();

        // implement close under the same lock to prevent race conditions:
        if (!is_closed_) {
            is_closed_ = true;
            // queue is empty iff recv_wait_list is not empty
            empty_recv_wait_list();
            empty_send_wait_list();
        }

        lock.unlock();
        cond_send.notify_all();
        cond_recv.notify_all();

        // receivers notify when they end if the channel is closed, this will
        // wait for all receivers to finish before completing desctruction
        cond_send.wait(lock, [this]{ return receivers_ == 0; });
    } 
};



template<typename T>
class statement {
public:
    T * dat;
    const T * sen;
    bool * closed;
    channel<T> * chan;
    std::function<void()> action;

    typedef T value_type;

    statement(T & d, channel<T> & c, std::function<void()> action = nullptr) 
        : dat(&d), sen(nullptr), closed(nullptr), chan(&c), action(action)
    { }

    statement(const T & d, channel<T> & c, std::function<void()> action = nullptr) 
        : dat(nullptr), sen(&d), closed(nullptr), chan(&c), action(action)
    { }

    statement(std::tuple<T&,bool&> p, channel<T> & c, std::function<void()> action = nullptr) 
        : dat(&std::get<0>(p)), sen(nullptr), closed(&std::get<1>(p)), chan(&c), action(action)
    { }

    statement(std::tuple<const T&,bool&> p, channel<T> & c, std::function<void()> action = nullptr) 
        : dat(nullptr), sen(&std::get<0>(p)), closed(&std::get<1>(p)), chan(&c), action(action)
    { }

    statement(channel<T> & c, std::function<void()> action = nullptr) 
        : dat(nullptr), sen(nullptr), closed(nullptr), chan(&c), action(action)
    { }

    // default statement
    statement(std::function<void()> action = nullptr) 
        : dat(nullptr), sen(nullptr), closed(nullptr), chan(nullptr), action(action)
    { }
};

// NOTE: if the action passed into any receive case captures data received from channel (dat and is_closed boolean)
// those must be passed by reference, or else their value at the time of instantiation will be captured, instead
// of their value at the time of execution of the action (after the channel has received)
template<typename T> statement<T> case_receive(T & dat, channel<T> & c, std::function<void()> action = nullptr) {
    return statement<T>(dat, c, action);
}
template<typename T> statement<T> case_send(const T & dat, channel<T> & c, std::function<void()> action = nullptr) {
    return statement<T>(dat, c, action);
}
template<typename T> statement<T> case_receive(std::tuple<T&, bool&> p, channel<T> & c, std::function<void()> action = nullptr) {
    return statement<T>(p, c, action);
}
template<typename T> statement<T> case_send(std::tuple<const T&, bool&> p, channel<T> & c, std::function<void()> action = nullptr) {
    return statement<T>(p, c, action);
}
template<typename T> statement<T> case_receive(channel<T> & c, std::function<void()> action = nullptr) {
    return statement<T>(c, action);
}


template<>
class statement<void> {
public:
    typedef void value_type;

    std::function<void()> action;

        // default statement
    statement(std::function<void()> action = nullptr) 
        : action(action) 
    { }
};

statement<void> case_default(std::function<void()> action = nullptr) {
    return statement<void>{action};
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

        selector(statement<void> const & def) : selector<>() {
            action = def.action;
        }
    };

    template<typename T, typename ... Ts>
    class selector<T, Ts...> : public selector<Ts...> {
    public:
        T * dat;
        const T * sen;
        bool * closed;
        channel<T> * chan;
        std::function<void()> action;

        int wait_id;

        selector(statement<T> const & r, statement<Ts> const & ... rs)
            : selector<Ts...>(rs...), dat(r.dat), sen(r.sen), closed(r.closed), chan(r.chan), action(r.action), wait_id(0)
        { 
            // no need to lock because constructors are called sequentially

            // we could be completed already if a previous constructor pulled a value from a channel
            if (this->completed)
                return;

            if (sen != nullptr) {
                wait_id = detail::__send_or_notify<T>(*chan, std::bind(&selector<T, Ts...>::notify_send, this, std::placeholders::_1, std::placeholders::_2));
            } else {
                // have to force it to look for the T instantiation
                wait_id = detail::__recv_or_notify<T>(*chan, std::bind(&selector<T, Ts...>::notify_recv, this, std::placeholders::_1, std::placeholders::_2));
            }
        }

        ~selector() {
            // no need to lock here either because destructors are called sequentially
            if (wait_id != 0)
                detail::__unnotify(*chan, wait_id);
        }

        bool notify_send(T & val, bool is_closed) {
            std::unique_lock<std::mutex> lock(this->m);

            if (this->completed)
                return false;

            if (closed != nullptr) {
                *closed = is_closed;
            }

            // if it's not closed send the data
            if (!is_closed) {
                val = *sen;
            }

            this->selected_action = action;
            this->completed = true;

            lock.unlock();
            this->cv.notify_all();

            return true;
        }

        bool notify_recv(T const & val, bool is_closed) {
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
    struct select_inner;

    template<typename ... Ts>
    struct select_inner<true, Ts...> {
        static void select(statement<Ts> const & ... rs) {
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
        static void select(statement<Ts> const & ... rs) {
            selector<Ts...> s(rs...);

            s.wait_and_action();
        }
    };
} // namespace detail

template<typename ... Ts>
void select(statement<Ts> const & ... rs) {
    // this ensures that we are only casting to the default type if we have that case.
    detail::select_inner<
        std::is_base_of<detail::selector<void>, detail::selector<Ts...>>::value,
        Ts...
    >::select(rs...);
}


} // namespace chan