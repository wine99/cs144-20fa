# CS144 Lab Assignments - 手写TCP - LAB2

> CS 144: Introduction to Computer Networking, Fall 2020
> https://cs144.github.io/
>
> My Repo
> https://github.com/wine99/cs144-20fa
>

## Translating between 64-bit indexes and 32-bit seqnos

[![ydf2M4.png](https://s3.ax1x.com/2021/02/09/ydf2M4.png)](https://imgchr.com/i/ydf2M4)

注意 wrapping_integers.hh 中的这三个方法：

```cpp
inline int32_t operator-(WrappingInt32 a, WrappingInt32 b) { return a.raw_value() - b.raw_value(); }

//! \brief The point `b` steps past `a`.
inline WrappingInt32 operator+(WrappingInt32 a, uint32_t b) { return WrappingInt32{a.raw_value() + b}; }

//! \brief The point `b` steps before `a`.
inline WrappingInt32 operator-(WrappingInt32 a, uint32_t b) { return a + -b; }
```

下面的两个加减，分别是在 WrappingInt32 上加上或减去一个 uint32_t，得到的结果仍然是一个 WrappingInt32，其意义是分别为把 a 这个 WrappingInt32 向前或向后移动 |b| 个单位距离。

而第一个减法重载，是两个 WrappingInt32 相减，得到的是一个 int32_t，要想理解其意义，首先要弄懂下面的代码：

```cpp
// 2 ^ 32 = 4294967296

uint32_t a = 1;
uint32_t b = 0;
uint32_t x = a - b;
int32_t  y = a - b;
// x=1 y=1
uint32_t x = b - a;
int32_t  y = b - a;
// x=4294967295 y=-1

uint32_t a = 1;
uint32_t b = static_cast<uint32_t>((1UL << 32) - 1UL);
uint32_t x = a - b;
int32_t  y = a - b;
// x=2 y=2
uint32_t x = b - a;
int32_t  y = b - a;
// x=4294967294 y=-2
```

上面的运算说明：`c(int32_t) = a(uint32_t) - b(uint32_t)` 的 c 的绝对值的意义是从 b 走到 a 需要花费的最少步数，如果 c 是正数，则向数轴的正方向走，否则向反方向走。之所以存在最小步数一说，是因为往反方向走穿越 0 会到 2^32 - 1，往正方向走穿越 2^32 - 1 会回到 0。这个步数也一定不会超过 2^31 步。第一个减法也是这样的意义，代表了从 WrappingInt32 b 走到 WrappingInt32 a 最少需要的步数。

理解了这三个运算符重载后，就可以编写 Wrapping_integers.cc 了：

```cpp
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) { return isn + static_cast<uint32_t>(n); }

uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // STEP ranges from -UINT32_MAX/2 to UINT32_MAX/2
    // in most cases, just adding STEP to CHECKPOINT will get the absolute seq
    // but if after adding, the absolute seq is negative, it should add another (1UL << 32)
    // (this means the checkpoint is near 0 so the new seq should always go bigger
    // eg. test/unwrap.cc line 25)
    int32_t steps = n - wrap(checkpoint, isn);
    int64_t res = checkpoint + steps;
    return res >= 0 ? checkpoint + steps : res + (1UL << 32);
}
```

首先看把 64 位的 index 转换为 WrappingInt32 的 wrap 函数。根据上面的表格，显然，只需要利用加法重载，把 ISN 往前走 n(=index) 步就可以了。

然后看 unwrap 函数，起作用是把新接收到的报文段的 seqno（WrappingInt32）转换为 64 位的 index。seqno 转 index 的结果显然不唯一，我们想要的是与上一次收到的报文段的 index（checkpoint）最接近的那个转换结果。于是，可以先用刚写好的 wrap 函数把 checkpoint 变为 WrappingInt32，然后利用第一个减法重载，找出这个转换后的 seqno 最少需要走几步可以到新报文的 seqno，然后把这个步数加到 checkpoint 上。注意这里有一个特殊情况（见代码注释），因为我们有可能是往数轴的反方向走的，可能走完之后 res 的值是个负数，这时候需要在加上一个 2^32。

## Implementing the TCP receiver

注意测试里面的特殊情况，例如

- SYN with DATA
- SYN with DATA with FIN
- SYN + FIN
- SYN + DATA with FIN + DATA
- ...

然后根据未通过的测试，一步步完善逻辑，测试全部通过后再重新简化代码，最终得到解法如下（部分思路见注释）：

```cpp
void TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader header = seg.header();
    if (header.syn && _syn)
        return;
    if (header.syn) {
        _syn = true;
        _isn = header.seqno.raw_value();
    }
    // note that fin flag seg can carry payload
    if (_syn && header.fin)
        _fin = true;
    size_t absolute_seqno = unwrap(header.seqno, WrappingInt32(_isn), _checkpoint);
    _reassembler.push_substring(seg.payload().copy(), header.syn ? 0 : absolute_seqno - 1, header.fin);
    _checkpoint = absolute_seqno;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // Return the corresponding 32bit seqno of _reassembler.first_unassembled().
    // Translate from Stream Index to Absolute Sequence Number is simple, just add 1.
    // If outgoing ack is corresponding to FIN
    // (meaning FIN is received and all segs are assembled),
    // add another 1.
    size_t shift = 1;
    if (_fin && _reassembler.unassembled_bytes() == 0)
        shift = 2;
    if (_syn)
        return wrap(_reassembler.first_unassembled() + shift, WrappingInt32(_isn));
    return {};
}

size_t TCPReceiver::window_size() const {
    // equal to first_unacceptable - first_unassembled
    return _capacity - stream_out().buffer_size();
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
