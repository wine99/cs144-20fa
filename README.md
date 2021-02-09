# CS144 Lab Assignments - 手写TCP - LAB1

> CS 144: Introduction to Computer Networking, Fall 2020
> https://cs144.github.io/
>
> My Repo
> https://github.com/wine99/cs144-20fa
> 

## 任务

[![sLrj9e.png](https://s3.ax1x.com/2021/01/25/sLrj9e.png)](https://imgchr.com/i/sLrj9e)

TCP 接受方接收到乱序且可能重叠的报文段，StreamReassembler 需要将收到的报文段按情况送入 ByteStream (lab0 实现的），或丢弃，或暂存（在合适的时候重组送入 ByteStream）。

注意点：
+ 报文段包含索引、长度、内容，lab1 的索引从 0 开始增长，不会溢出绕回。
+ 任何报文段，包括新收到的和暂存的，只要可以，就应该立刻送入 ByteStream（可能需要手动重组和去重叠）。
+ 容量的限制如下图所示。

[![sLc0U0.png](https://s3.ax1x.com/2021/01/25/sLc0U0.png)](https://imgchr.com/i/sLc0U0)

## 思路

+ 用 set 来暂存报文段，按照报文段的 index 大小对比重载 < 运算符。
+ 用 _eof 来保存是否已经收到过有 EOF 标识的段。
+ 收到新段时，通过对比新段的 index、length 和 _first_unacceptable，_first_unassembled 对新段进行必要的剪切，然后处理重叠（调用 _handle_overlap）。
+ 处理重叠的逻辑：遍历每个暂存段，如果与新段产生重叠，合并暂存段与新段（调用 _merge_seg，总是往新段上合并，合并后删除重叠的暂存段）。
+ 合并段的方法：分类讨论，两个段总共会有四种不同的重叠情况，分别处理。
+ 处理完重叠后，调用 _stitch_output：遍历所有暂存段，将可合并的暂存段接入 ByteStream 中。
+ 最后，当 _eof 为真且 unassembled_bytes 为 0 时，调用 ByteSteram 的 end_input。

## 代码

stream_reassembler.hh：

```cpp
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    ByteStream _output;  //!< The reassembled in-order byte stream
    size_t _capacity;    //!< The maximum number of bytes
    size_t _first_unread = 0;
    size_t _first_unassembled = 0;
    size_t _first_unacceptable;
    bool _eof = false;
    struct seg {
        size_t index;
        size_t length;
        std::string data;
        bool operator<(const seg t) const { return index < t.index; }
    };
    std::set<seg> _stored_segs = {};

    void _add_new_seg(seg &new_seg, const bool eof);
    void _handle_overlap(seg &new_seg);
    void _stitch_output();
    void _stitch_one_seg(const seg &new_seg);
    void _merge_seg(seg &new_seg, const seg &other);

  public:
```

stream_reassembler.cc：

注意 _add_new_seg 中关于 EOF 的处理细节。

```cpp
#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _first_unacceptable(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    _first_unread = _output.bytes_read();
    _first_unacceptable = _first_unread + _capacity;
    seg new_seg = {index, data.length(), data};
    _add_new_seg(new_seg, eof);
    _stitch_output();
    if (empty() && _eof)
        _output.end_input();
}

void StreamReassembler::_add_new_seg(seg &new_seg, const bool eof) {
    // check capacity limit, if unmeet limit, return
    // cut the bytes in NEW_SEG that will overflow the _CAPACITY
    // note that the EOF should also be cut
    // cut the bytes in NEW_SEG that are already in _OUTPUT
    // _HANDLE_OVERLAP()
    // update _EOF
    if (new_seg.index >= _first_unacceptable)
        return;
    bool eof_of_this_seg = eof;
    if (int overflow_bytes = new_seg.index + new_seg.length - _first_unacceptable; overflow_bytes > 0) {
        int new_length = new_seg.length - overflow_bytes;
        if (new_length <= 0)
            return;
        eof_of_this_seg = false;
        new_seg.length = new_length;
        new_seg.data = new_seg.data.substr(0, new_seg.length);
    }
    if (new_seg.index < _first_unassembled) {
        int new_length = new_seg.length - (_first_unassembled - new_seg.index);
        if (new_length <= 0)
            return;
        new_seg.length = new_length;
        new_seg.data = new_seg.data.substr(_first_unassembled - new_seg.index, new_seg.length);
        new_seg.index = _first_unassembled;
    }
    _handle_overlap(new_seg);
    // if EOF was received before, it should remain valid
    _eof = _eof || eof_of_this_seg;
}

void StreamReassembler::_handle_overlap(seg &new_seg) {
    for (auto it = _stored_segs.begin(); it != _stored_segs.end();) {
        auto next_it = ++it;
        --it;
        if ((new_seg.index >= it->index && new_seg.index < it->index + it->length) ||
            (it->index >= new_seg.index && it->index < new_seg.index + new_seg.length)) {
            _merge_seg(new_seg, *it);
            _stored_segs.erase(it);
        }
        it = next_it;
    }
    _stored_segs.insert(new_seg);
}

void StreamReassembler::_stitch_output() {
    // _FIRST_UNASSEMBLED is the expected next index_FIRST_UNASSEMBLED
    // compare _STORED_SEGS.begin()->index with
    // if equals, then _STITCH_ONE_SEG() and erase this seg from set
    // continue compare until not equal or empty
    while (!_stored_segs.empty() && _stored_segs.begin()->index == _first_unassembled) {
        _stitch_one_seg(*_stored_segs.begin());
        _stored_segs.erase(_stored_segs.begin());
    }
}

void StreamReassembler::_stitch_one_seg(const seg &new_seg) {
    // write string of NEW_SEG into _OUTPUT
    // update _FIRST_UNASSEMBLED
    _output.write(new_seg.data);
    _first_unassembled += new_seg.length;
    // both way of updating _FIRST_UNASSEMBLED is ok
    // _first_unassembled = _output.bytes_written();
}

void StreamReassembler::_merge_seg(seg &new_seg, const seg &other) {
    size_t n_index = new_seg.index;
    size_t n_end = new_seg.index + new_seg.length;
    size_t o_index = other.index;
    size_t o_end = other.index + other.length;
    string new_data;
    if (n_index <= o_index && n_end <= o_end) {
        new_data = new_seg.data + other.data.substr(n_end - o_index, n_end - o_end);
    } else if (n_index <= o_index && n_end >= o_end) {
        new_data = new_seg.data;
    } else if (n_index >= o_index && n_end <= o_end) {
        new_data =
            other.data.substr(0, n_index - o_index) + new_seg.data + other.data.substr(n_end - o_index, n_end - o_end);
    } else /* if (n_index >= o_index && n_end <= o_end) */ {
        new_data = other.data.substr(0, n_index - o_index) + new_seg.data;
    }
    new_seg.index = n_index < o_index ? n_index : o_index;
    new_seg.length = (n_end > o_end ? n_end : o_end) - new_seg.index;
    new_seg.data = new_data;
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t unassembled_bytes = 0;
    for (auto it = _stored_segs.begin(); it != _stored_segs.end(); ++it)
        unassembled_bytes += it->length;
    return unassembled_bytes;
}

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }
```

## VS Code 调试方法

测试样例对应的源文件在 ./tests 文件夹中，如下图所示。

[![sO3UZd.png](https://s3.ax1x.com/2021/01/25/sO3UZd.png)](https://imgchr.com/i/sO3UZd)

对应的可执行文件在 ./build/tests 中。

通常 VSC 的 launch.json 中自动生成了一个名为 debug current file 的 launch targe，它有一个前置任务 C/C++: g++ build active file。仿照该 lanuch targe，可以写一个名为 debug lab test 的 launch targe。如下所示。

launch.json:

```json
{
    // Use IntelliSense to learn about possible attributes.
    // Hover to view descriptions of existing attributes.
    // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
    "version": "0.2.0",
    "configurations": [
        {
            "name": "debug lab test",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/tests/${fileBasenameNoExtension}",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "miDebuggerPath": "/usr/bin/gdb"
        },
        {
            "name": "debug webget",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/apps/webget",
            "args": ["cs144.keithw.org", "/hello"],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            // "preLaunchTask": "C/C++: g++ build active file",
            "preLaunchTask": "build project",
            "miDebuggerPath": "/usr/bin/gdb"
        },
        {
            "name": "debug current file",
            "type": "cppdbg",
            "request": "launch",
            "program": "${fileDirname}/${fileBasenameNoExtension}",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "C/C++: g++ build active file",
            "miDebuggerPath": "/usr/bin/gdb"
        }
    ]
}
```

tasks.json:

```json
{
    "tasks": [
        {
            "type": "shell",
            "label": "C/C++: g++ build active file",
            "command": "/usr/bin/g++",
            "args": [
                "-g",
                "${file}",
                "-o",
                "${fileDirname}/${fileBasenameNoExtension}"
            ],
            "options": {
                "cwd": "${workspaceFolder}"
            },
            "problemMatcher": [
                "$gcc"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            }
        },
        {
            "type": "shell",
            "label": "build project",
            "command": "cd build && make -j8",
            "args": [],
        },
    ],
    "version": "2.0.0"
}
```

然后就可以在测试源代码中打断点调试。

[![sOGQgK.png](https://s3.ax1x.com/2021/01/25/sOGQgK.png)](https://imgchr.com/i/sOGQgK)


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
