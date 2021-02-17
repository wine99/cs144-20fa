# CS144 Lab Assignments - 手写TCP - LAB4

> CS 144: Introduction to Computer Networking, Fall 2020
> https://cs144.github.io/
>
> My Repo
> https://github.com/wine99/cs144-20fa

## 任务

本节实现 TCPConnection 类，实例化这个类将作为一个完整的 TCP 连接中的一个 peer（可以充当任意一方，Server 或 Client）。前面两个实验分别实现的 TCPSender 和 TCPReceiver 并不能作为一个独立的 Server 或 Client，这两个类的实例是用作 TCPConnection 实例的内部成员，即下图。

[![ygfFsI.png](https://s3.ax1x.com/2021/02/17/ygfFsI.png)](https://imgchr.com/i/ygfFsI)

## Sender 和 Receiver 的作用

- 收到报文段时
  - 通知 _receiver：根据报文段的 seqno、SYN、FIN 和 payload，以及当前状态，更新 ackno；收集数据
  - 通知 \_sender：根据报文段的 ackno 以及当前状态，更新 next_seqno；更新 window_size
- 发送报文段时
  - \_sender 负责填充 payload、seqno、SYN、FIN，注意有可能既没有 payload 也没有 S、F 标识（empty segment），这和 Lab3 实现的 \_sender 的 ack_received() 逻辑不同
  - \_receiver 负责填充 ackno、window size

## FSM

结合 Lab2、Lab3 讲义中的 TCPSender 和 TCPReceiver 的状态转换图，tcp_state.cc 中 TCPConnection 的各状态与 sender、receiver 状态的对应关系，以及下面的 TCPConnection 的状态转换图，理解整个 TCP 连接。

[![yg5iQJ.jpg](https://s3.ax1x.com/2021/02/17/yg5iQJ.jpg)](https://imgchr.com/i/yg5iQJ)

## Edge case

在实现过程中，需要额外关注收到报文段时 TCPSender 和 TCPConnection 的逻辑的不同之处。这些细节来源于

1. Lab2 中的 receiver 只关心收到数据和数据有关的标识；Lab3 中 sender 只关心收到的 ackno 和 win，不处理也不知道收到的数据和其他信息，在 \_stream_in() 没有数据时可能不会做任何动作（我的 Lab3 实现是这样的），而在 Lab4 中可能还需要发一个空的 ACK 报文段
2. 连接建立和释放过程中的各种特殊情况
   1. 发完 SYN 后马上收到 RST
   2. 发完 SYN 后马上收到 FIN
   3. Simultaneous open
   4. Simultaneous shutdown
   5. ...

实验给出的测试套非常完备，覆盖了各种特殊情况，Simultaneous open 和 Simultaneous shutdown 的情况见下图。按照讲义所说，如果你的 Lab2 和 Lab3 实现非常 robust，Lab4 的大部分工作是 wire up 前面两个类的接口，但也有可能你需要修改前两个实验的实现。

下图出处：[TCP State Transitions](http://ttcplinux.sourceforge.net/documents/one/tcpstate/tcpstate.html)

[![yg7ih6.jpg](https://s3.ax1x.com/2021/02/17/yg7ih6.jpg)](https://imgchr.com/i/yg7ih6)[![yg7A1O.jpg](https://s3.ax1x.com/2021/02/17/yg7A1O.jpg)](https://imgchr.com/i/yg7A1O)

## 实现

我的实验四的函数框架参考了 [这篇博客](https://www.cnblogs.com/kangyupl/p/stanford_cs144_labs.html)，但实现不同。我在网上浏览过的几个实现，均改动了 Lab2、Lab3 的函数签名，让 Lab2、Lab3 的实现变得不太干净。我的最终实现没有入侵 Lab3 和 Lab2 的代码，细节逻辑全部在 TCPConnection 类中完成。

注意如果 tests 文件夹中的测试全部通过但是 txrx.sh 中的测试不通过，并且不通过的原因是结果的哈希值不同，去掉所有的自己添加的打印语句，再进行测试。

实验四刚开始时一度想要放弃，但最终花费的时间居然比实验三要少（实验三零零碎碎花了六天左右，实验四大概花费了集中的两天半时间）。通过全部测试的时候，还感觉有点懵逼，怎么就通过了，我真的把细节都处理完了？第一次意识到，复杂的项目中，完备的测试比“充满自信”的实现代码可靠多了，也不得不感慨课程质量之高以及讲师和助教付出的心血。

[![y2kzRO.png](https://s3.ax1x.com/2021/02/17/y2kzRO.png)](https://imgchr.com/i/y2kzRO)

## 代码

### 添加的成员变量

```cpp
class TCPConnection {
  private:
    size_t _time_since_last_segment_received{0};
    bool _active{true};

    void send_sender_segments();
    void clean_shutdown();
    void unclean_shutdown();
```

### 实现代码

```cpp
#include "tcp_connection.hh"

#include <iostream>

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

bool TCPConnection::active() const { return _active; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active)
        return;
    _time_since_last_segment_received = 0;
    // State: closed
    if (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0) {
        if (!seg.header().syn)
            return;
        _receiver.segment_received(seg);
        connect();
        return;
    }
    // State: syn sent
    if (_sender.next_seqno_absolute() > 0 && _sender.bytes_in_flight() == _sender.next_seqno_absolute() &&
        !_receiver.ackno().has_value()) {
        if (seg.payload().size())
            return;
        if (!seg.header().ack) {
            if (seg.header().syn) {
                // simultaneous open
                _receiver.segment_received(seg);
                _sender.send_empty_segment();
            }
            return;
        }
        if (seg.header().rst) {
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _active = false;
            return;
        }
    }
    _receiver.segment_received(seg);
    _sender.ack_received(seg.header().ackno, seg.header().win);
    // Lab3 behavior: fill_window() will directly return without sending any segment.
    // See tcp_sender.cc line 42
    if (_sender.stream_in().buffer_empty() && seg.length_in_sequence_space())
        _sender.send_empty_segment();
    if (seg.header().rst) {
        _sender.send_empty_segment();
        unclean_shutdown();
        return;
    }
    send_sender_segments();
}

size_t TCPConnection::write(const string &data) {
    if (!data.size())
        return 0;
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    send_sender_segments();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active)
        return;
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS)
        unclean_shutdown();
    send_sender_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_sender_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_sender_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            _sender.send_empty_segment();
            unclean_shutdown();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_sender_segments() {
    TCPSegment seg;
    while (!_sender.segments_out().empty()) {
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        _segments_out.push(seg);
    }
    clean_shutdown();
}

void TCPConnection::unclean_shutdown() {
    // When this being called, _sender.stream_out() should not be empty.
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
    TCPSegment seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    seg.header().ack = true;
    if (_receiver.ackno().has_value())
        seg.header().ackno = _receiver.ackno().value();
    seg.header().win = _receiver.window_size();
    seg.header().rst = true;
    _segments_out.push(seg);
}

void TCPConnection::clean_shutdown() {
    if (_receiver.stream_out().input_ended()) {
        if (!_sender.stream_in().eof())
            _linger_after_streams_finish = false;
        else if (_sender.bytes_in_flight() == 0) {
            if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
                _active = false;
            }
        }
    }
}
```

## 性能优化

### 分析

由于没有做过 profiling，性能分析的工作抄了上面提到的博客的作业。

>  修改 `sponge/etc/cflags.cmake` 中的编译参数，将`-g`改为`-Og -pg`，使生成的程序具有分析程序可用的链接信息。
>
> ```bash
> make -j8
> ./apps/tcp_benchmark
> gprof ./apps/tcp_benchmark > prof.txt
> ```

[![y2nkwQ.png](https://s3.ax1x.com/2021/02/17/y2nkwQ.png)](https://imgchr.com/i/y2nkwQ)

如讲义中所说，很可能需要改动 ByteStream 或 StreamReassembler。调优方法是利用 buffer.h 中提供的 BufferList。实际上测试代码中就有用到 BufferList，简而言之它是一个 deque\<Buffer\>，而 Buffer 则在整个实现与测试代码中被大量使用，例如 payload() 就是一个 Buffer 实例。

### 改动

把 ByteStream 类中字节流的容器由 Lab0 最初的 `std::list<char> _stream{};` 改为 `BufferList _stream{};`。

byte_stream.cc 改动的函数：

```cpp
size_t ByteStream::write(const string &data) {
    size_t write_count = data.size();
    if (write_count > _capacity - _buffer_size)
        write_count = _capacity - _buffer_size;
    _stream.append(BufferList(move(string().assign(data.begin(), data.begin() + write_count)))); 
    _buffer_size += write_count;
    _bytes_written += write_count;
    return write_count;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    const size_t peek_length = len > _buffer_size ? _buffer_size : len;
    string str = _stream.concatenate();
    return string().assign(str.begin(), str.begin() + peek_length);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_length = len > _buffer_size ? _buffer_size : len;
    _stream.remove_prefix(pop_length);
    _bytes_read += pop_length;
    _buffer_size -= pop_length;
}
```

### 改动后的 benchmark

[![y2KfoQ.png](https://s3.ax1x.com/2021/02/17/y2KfoQ.png)](https://imgchr.com/i/y2KfoQ)

## webget revisited

直接按照讲义中的步骤，把 Linux 自带的 TCPSocket，换成我们自己的实现。

```cpp
void get_URL(const string &host, const string &path) {
    CS144TCPSocket sock1{};
    sock1.connect(Address(host, "http"));
    sock1.write("GET " + path + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
    while (!sock1.eof()) {
        cout << sock1.read();
    }
    sock1.shutdown(SHUT_WR);
    sock1.wait_until_closed();
}
```

替换后 webget 依然 work（不知道为什么 WSL 替换后连接建立不起来，但在云主机上测试后没有问题），至此，手写 TCP 正式完成。

---

BELOW IS THE ORIGINAL README OF THIS LAB.

---
For build prereqs, see [the CS144 VM setup instructions](https://web.stanford.edu/class/cs144/vm_howto).

## Sponge quickstart

To set up your build directory:

	$ mkdir -p <path/to/sponge>/build
	$ cd <path/to/sponge>/build
	$ cmake ..

**Note:** all further commands listed below should be run from the `build` dir.

To build:

    $ make

You can use the `-j` switch to build in parallel, e.g.,

    $ make -j$(nproc)

To test (after building; make sure you've got the [build prereqs](https://web.stanford.edu/class/cs144/vm_howto) installed!)

    $ make check_lab0

or

	$ make check_lab1

etc.

The first time you run a `make check`, it may run `sudo` to configure two
[TUN](https://www.kernel.org/doc/Documentation/networking/tuntap.txt) devices for use during testing.

### build options

You can specify a different compiler when you run cmake:

    $ CC=clang CXX=clang++ cmake ..

You can also specify `CLANG_TIDY=` or `CLANG_FORMAT=` (see "other useful targets", below).

Sponge's build system supports several different build targets. By default, cmake chooses the `Release`
target, which enables the usual optimizations. The `Debug` target enables debugging and reduces the
level of optimization. To choose the `Debug` target:

    $ cmake .. -DCMAKE_BUILD_TYPE=Debug

The following targets are supported:

- `Release` - optimizations
- `Debug` - debug symbols and `-Og`
- `RelASan` - release build with [ASan](https://en.wikipedia.org/wiki/AddressSanitizer) and
  [UBSan](https://developers.redhat.com/blog/2014/10/16/gcc-undefined-behavior-sanitizer-ubsan/)
- `RelTSan` - release build with
  [ThreadSan](https://developer.mozilla.org/en-US/docs/Mozilla/Projects/Thread_Sanitizer)
- `DebugASan` - debug build with ASan and UBSan
- `DebugTSan` - debug build with ThreadSan

Of course, you can combine all of the above, e.g.,

    $ CLANG_TIDY=clang-tidy-6.0 CXX=clang++-6.0 .. -DCMAKE_BUILD_TYPE=Debug

**Note:** if you want to change `CC`, `CXX`, `CLANG_TIDY`, or `CLANG_FORMAT`, you need to remove
`build/CMakeCache.txt` and re-run cmake. (This isn't necessary for `CMAKE_BUILD_TYPE`.)

### other useful targets

To generate documentation (you'll need `doxygen`; output will be in `build/doc/`):

    $ make doc

To lint (you'll need `clang-tidy`):

    $ make -j$(nproc) tidy

To run cppcheck (you'll need `cppcheck`):

    $ make cppcheck

To format (you'll need `clang-format`):

    $ make format

To see all available targets,

    $ make help
