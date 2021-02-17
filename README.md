# CS144 Lab Assignments - 手写TCP - LAB0

> CS 144: Introduction to Computer Networking, Fall 2020
> https://cs144.github.io/
>
> My Repo
> https://github.com/wine99/cs144-20fa
> 

LAB0 在 master 分支，LAB1 - 7 在对应名字的分支。

## webget

### What is webget?

参照 lab0.pdf 2.1 Fetch a Web page, 如下所示。

[![sbYMvD.png](https://s3.ax1x.com/2021/01/24/sbYMvD.png)](https://imgchr.com/i/sbYMvD)

其效果等同于

[![sbTh4A.png](https://s3.ax1x.com/2021/01/24/sbTh4A.png)](https://imgchr.com/i/sbTh4A)

### Write Webget

参考 API 文档 https://cs144.github.io/doc/lab0/class_t_c_p_socket.html。

注意 lab0.pdf 中的几点提示：

+ Please note that in HTTP, each line must be ended with “\r\n” (it’s not sufficient to use just “\n” or endl).
+ Don’t forget to include the “Connection: close” line in your client’s request. This tells the server that it shouldn’t wait around for your client to send any more requests after this one. Instead, the server will send one reply and then will immediately end its outgoing bytestream (the one from the server’s socket to your socket). You’ll discover that your incoming byte stream has ended because your socket will reach “EOF” (end of file) when you have read the entire byte stream coming from the server. That’s how your client will know that the server has finished its reply.
+ Make sure to read and print all the output from the server until the socket reaches “EOF” (end of file) — a single call to read is not enough.

在 GET 请求中写明 `Connection: close` 可以让服务器马上进入连接释放的过程（发一个 FIN 过来），我们在发送完 GET 同样需要 `shutdown(SHUT_WR)`，进入连接释放过程（发一个 FIN 过去）。（不太理解的话，写完 Lab4，就能理解了。）

```cpp
void get_URL(const string &host, const string &path) {
    // Your code here.

    // You will need to connect to the "http" service on
    // the computer whose name is in the "host" string,
    // then request the URL path given in the "path" string.

    // Then you'll need to print out everything the server sends back,
    // (not just one call to read() -- everything) until you reach
    // the "eof" (end of file).

    // cerr << "Function called: get_URL(" << host << ", " << path << ").\n";
    // cerr << "Warning: get_URL() has not been implemented yet.\n";

    TCPSocket sock1;
    sock1.connect(Address(host, "http"));
    sock1.write("GET " + path + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
    while (!sock1.eof()) {
        cout << sock1.read();
    }
    sock1.shutdown(SHUT_WR);
}
```

## An in-memory reliable byte stream

注意下面代码中的 `buffer_size` 为缓冲的内容大小（等于 `_stream.size()`），`capacity` 才是缓冲的大小。只有当 `input_ended` 为真并且 `buffer_size` 为 0 时，才是 EOF。

`byte_stream.hh`:

```cpp
class ByteStream {
  private:
    // Your code here -- add private members as necessary.

    // Hint: This doesn't need to be a sophisticated data structure at
    // all, but if any of your tests are taking longer than a second,
    // that's a sign that you probably want to keep exploring
    // different approaches.

    bool _error = false;  //!< Flag indicating that the stream suffered an error.
    bool _input_ended = false;
    size_t _capacity;
    size_t _buffer_size = 0;
    size_t _bytes_written = 0;
    size_t _bytes_read = 0;
    std::list<char> _stream{};

  public:
```

`byte_stream.cc`:

```cpp
#include "byte_stream.hh"

#include <string>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    size_t write_count = 0;
    for (const char c : data) {
        // not very efficient to do conditional in loop
        if (_capacity - _buffer_size <= 0)
            break;
        else {
            _stream.push_back(c);
            ++_buffer_size;
            ++_bytes_written;
            ++write_count;
        }
    }

    return write_count;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    const size_t peek_length = len > _buffer_size ? _buffer_size : len;
    list<char>::const_iterator it = _stream.begin();
    advance(it, peek_length);
    return string(_stream.begin(), it);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_length = len > _buffer_size ? _buffer_size : len;
    _bytes_read += pop_length;
    _buffer_size -= pop_length;
    while (pop_length--)
        _stream.pop_front();
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    const string result = peek_output(len);
    pop_output(len);
    return result;
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _buffer_size; }

bool ByteStream::buffer_empty() const { return _stream.size() == 0; }

bool ByteStream::eof() const { return _input_ended && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _buffer_size; }
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
