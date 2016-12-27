#ifndef __CHANNEL_HPP__
#define __CHANNEL_HPP__

#include <queue>
#include <thread>
#include <tuple>
#include <utility>

namespace chan {

using std::queue;
using std::mutex;
using std::condition_variable;
using std::tuple;
using std::unique_lock;
using std::tie;

template<typename ... Ts>
class channel {
private:
  queue<tuple<Ts...>> queue;
  mutex m;
  condition_variable cv;

  bool closed;
  bool sealed;
  size_t dropped_count_;
  unsigned int buffer_size;
public:
  typedef tuple<Ts...> value_type;

  channel(unsigned int buffer_size = 1)
    : closed(false), sealed(false), buffer_size(buffer_size)
  { }

  channel(channel<Ts...> const &) = delete;
  channel(channel<Ts...> &&) = delete;

  ~channel() { close(); }

  void close() {
    // std::cerr << "closing channel..." << std::endl;
    unique_lock<mutex> lock(m);
    if (closed) return;

    // std::cerr << "acquired channel lock." << std::endl;

    closed = true;
    lock.unlock();
    cv.notify_all();
  }
  bool is_closed() {
    unique_lock<mutex> lock(m);
    return closed;
  }
  void seal() {
    // std::cerr << "sealing channel..." << std::endl;
    unique_lock<mutex> lock(m);
    if (closed || sealed) return;

    // std::cerr << "acquired channel lock." << std::endl;
    sealed = true;
    lock.unlock();
    cv.notify_all();
  }
  bool is_sealed() {
    unique_lock<mutex> lock(m);
    return sealed;
  }
  bool is_empty() {
    unique_lock<mutex> lock(m);
    return queue.empty();
  }
  bool is_full() {
    if (buffer_size == 0) return false;

    unique_lock<mutex> lock(m);
    return queue.size() >= buffer_size;
  }
  size_t size() {
    unique_lock<mutex> lock(m);
    return queue.size();
  }
  template<bool wait_while_full = false>
  bool send(Ts const & ... args) {
    size_t d = 0;
    unique_lock<mutex> lock(m);

    // std::cerr << "size: " << queue.size() << std::endl;

    if (wait_while_full && !closed && !sealed && queue.size() >= buffer_size) {
      cv.wait(lock, [&]{ return closed || sealed || queue.size() < buffer_size; });
    }

    if(closed || sealed)
      return false;

    queue.push(tuple<Ts...>(args...));
    if (buffer_size > 0) {
      d = queue.size() - buffer_size;
      while(queue.size() > buffer_size) {
        queue.pop();
      }
    }
    dropped_count_ = d;
    // if (dropped_count_ > 0)
    //   std::cerr << "dropped: " << dropped_count_ << std::endl;

    lock.unlock();
    cv.notify_one();

    return true;
  }

  size_t dropped_count() {
    unique_lock<mutex> lock(m);
    return dropped_count_;
  }

  template<bool wait = true>
  bool recv(Ts & ... out) {
    // std::cerr << "acquiring receive lock..." << std::endl;
    unique_lock<mutex> lock(m);

    if(!sealed && wait) {
      cv.wait(lock, [&]{ return closed || !queue.empty() || (sealed && queue.empty()); });
    }

    if (closed) {
      return false;
    } else if (sealed && queue.empty()) {
      closed = true;
      lock.unlock();
      cv.notify_all();
      return false;
    } else if (queue.empty()) {
      return false;
    }

    tie(out...) = queue.front();
    queue.pop();

    if (queue.empty()) {
      lock.unlock();
      cv.notify_all();
    } else {
      lock.unlock();
      cv.notify_one();
    }

    return true;
  }
};

}

#endif // __CHANNEL_HPP__
