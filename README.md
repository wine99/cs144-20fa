# CS144 Lab Assignments - 手写TCP - LAB3

> CS 144: Introduction to Computer Networking, Fall 2020
> https://cs144.github.io/
>
> My Repo
> https://github.com/wine99/cs144-20fa

## 总体思路

tick 不需要我们来调用，参数的意义是距离上次 tick 被调用过去的时间，也不需要我们来设定。我们只需要在 tick 中实现，通过参数判断过去了多少时间，需要执行何种操作即可。

注意根据文档，我们要不需要实现选择重传，而是类似回退 N，需要存储已发送并且未被确认的段，进行累计确认，超时时只要重传这些段中最早的那一个即可。

TCPReceiver 调用 unwrap 时的 checkpoint 是上一个接收到的报文段的 absolute_seqno，TCPSender 调用 unwrap 时的 checkpoint 是 `_next_seqno`。

我的实现中计时器开关的处理：

- 发送新报文段时若计时器未打开，开启
- ack_received() 中，如果有报文段被正确地确认，重置计时器和 RTO，如果所有报文段均被确认（bytes in flight == 0），关闭计时器
- tick() 中，若计时器为关闭状态，直接返回，否则累加计时然后处理超时

## 添加的成员变量

```cpp
class TCPSender {
  private:
    bool _syn_sent = false;
    bool _fin_sent = false;
    uint64_t _bytes_in_flight = 0;
    uint16_t _receiver_window_size = 0;
    uint16_t _receiver_free_space = 0;
    uint16_t _consecutive_retransmissions = 0;
    unsigned int _rto = 0;
    unsigned int _time_elapsed = 0;
    bool _timer_running = false;
    std::queue<TCPSegment> _segments_outstanding{};

    // See test code send_window.cc line 113 why the commented code is wrong.
    bool ack_valid(uint64_t abs_ackno) {
        return abs_ackno <= _next_seqno &&
               //  abs_ackno >= unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno) +
               //          _segments_outstanding.front().length_in_sequence_space();
               abs_ackno >= unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno);
    }

  public:
    void send_segment(TCPSegment &seg);
};
```

- `send_segment(TCPSegment &seg)` 只在 `fill_window()` 中被调用，重传只需要 `_segments_out.push(_segments_outstanding.front())`
- `_receiver_window_size` 保存收到有效（有效的含义见上面 `ack_valid()`）确认报文段时，报文段携带的接收方窗口大小
- `_receiver_free_space` 是在 `_receiver_window_size` 的基础上，再减去已发送的报文段可能占用的空间（`_bytes_in_flight`）

## `fill_window()` 实现

- 如果 SYN 未发送，发送然后返回
- 如果 SYN 未被应答，返回
- 如果 FIN 已经发送，返回
- 如果 _stream 暂时没有内容但并没有 EOF，返回
- 如果 `_receiver_window_size` 不为 0
  1. 当 `receiver_free_space` 不为 0，尽可能地填充 payload
  2. 如果 _stream 已经 EOF，且 `_receiver_free_space` 仍不为 0，填上 FIN（fin 也会占用 _receiver_free_space）
  3. 如果 `_receiver_free_space` 还不为 0，且 _stream 还有内容，回到步骤 1 继续填充
- 如果 `_receiver_window_size` 为 0，则需要发送零窗口探测报文
  - 如果 `_receiver_free_space` 为 0
    - 如果 _stream 已经 EOF，发送仅携带 FIN 的报文
    - 如果 _stream 还有内容，发送仅携带一位数据的报文
  - 之所以还需要判断 `_receiver_free_space` 为 0，是因为这些报文段在此处应该只发送一次，后续的重传由 tick() 函数控制，而当发送了零窗口报文段后 `_receiver_free_space` 的值就会从原来的与 `_receiver_window_size` 相等的 0 变成 -1

```cpp
void TCPSender::fill_window() {
    if (!_syn_sent) {
        _syn_sent = true;
        TCPSegment seg;
        seg.header().syn = true;
        send_segment(seg);
        return;
    }
    if (!_segments_outstanding.empty() && _segments_outstanding.front().header().syn)
        return;
    if (!_stream.buffer_size() && !_stream.eof())
        return;
    if (_fin_sent)
        return;

    if (_receiver_window_size) {
        while (_receiver_free_space) {
            TCPSegment seg;
            size_t payload_size = min({_stream.buffer_size(),
                                       static_cast<size_t>(_receiver_free_space),
                                       static_cast<size_t>(TCPConfig::MAX_PAYLOAD_SIZE)});
            seg.payload() = _stream.read(payload_size);
            if (_stream.eof() && static_cast<size_t>(_receiver_free_space) > payload_size) {
                seg.header().fin = true;
                _fin_sent = true;
            }
            send_segment(seg);
            if (_stream.buffer_empty())
                break;
        }
    } else if (_receiver_free_space == 0) {
        // The zero-window-detect-segment should only be sent once (retransmition excute by tick function).
        // Before it is sent, _receiver_free_space is zero. Then it will be -1.
        TCPSegment seg;
        if (_stream.eof()) {
            seg.header().fin = true;
            _fin_sent = true;
            send_segment(seg);
        } else if (!_stream.buffer_empty()) {
            seg.payload() = _stream.read(1);
            send_segment(seg);
        }
    }
}

void TCPSender::send_segment(TCPSegment &seg) {
    seg.header().seqno = wrap(_next_seqno, _isn);
    _next_seqno += seg.length_in_sequence_space();
    _bytes_in_flight += seg.length_in_sequence_space();
    if (_syn_sent)
        _receiver_free_space -= seg.length_in_sequence_space();
    _segments_out.push(seg);
    _segments_outstanding.push(seg);
    if (!_timer_running) {
        _timer_running = true;
        _time_elapsed = 0;
    }
}
```

## `ack_received()` 实现

代码比较直白，注意进行累计确认之后，如果还有未被确认的报文段，`_receiver_free_space` 的值应为：收到的确认号绝对值 + 窗口大小 - 首个未确认报文的序号绝对值 - 未确认报文段的长度总和。

```cpp
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    if (!ack_valid(abs_ackno)) {
        // cout << "invalid ackno!\n";
        return;
    }
    _receiver_window_size = window_size;
    _receiver_free_space = window_size;
    while (!_segments_outstanding.empty()) {
        TCPSegment seg = _segments_outstanding.front();
        if (unwrap(seg.header().seqno, _isn, _next_seqno) + seg.length_in_sequence_space() <= abs_ackno) {
            _bytes_in_flight -= seg.length_in_sequence_space();
            _segments_outstanding.pop();
            // Do not do the following operations outside while loop.
            // Because if the ack is not corresponding to any segment in the segment_outstanding,
            // we should not restart the timer.
            _time_elapsed = 0;
            _rto = _initial_retransmission_timeout;
            _consecutive_retransmissions = 0;
        } else {
            break;
        }
    }
    if (!_segments_outstanding.empty()) {
        _receiver_free_space = static_cast<uint16_t>(
            abs_ackno + static_cast<uint64_t>(window_size) -
            unwrap(_segments_outstanding.front().header().seqno, _isn, _next_seqno) - _bytes_in_flight);
    }

    if (!_bytes_in_flight)
        _timer_running = false;
    // Note that test code will call it again.
    fill_window();
}
```

## tick() 实现

注意，窗口大小为 0 时不需要增加 RTO。但是发送 SYN 时，窗口为初始值也为 0，而 SYN 超时是需要增加 RTO 的。

```cpp
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer_running)
        return;
    _time_elapsed += ms_since_last_tick;
    if (_time_elapsed >= _rto) {
        _segments_out.push(_segments_outstanding.front());
        if (_receiver_window_size || _segments_outstanding.front().header().syn) {
            ++_consecutive_retransmissions;
            _rto <<= 1;
        }
        _time_elapsed = 0;
    }
}
```

## 其他代码

```cpp
#include <algorithm>

TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _rto{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg);
}
```


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
