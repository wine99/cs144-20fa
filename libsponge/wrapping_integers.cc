#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    return WrappingInt32{static_cast<uint32_t>(n) + isn.raw_value()};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // understand the following code first

    // uint32_t a = 1;
    // uint32_t b = static_cast<uint32_t>((1UL << 32) - 1UL); // 4294967295
    // uint32_t x = a - b;
    // int32_t  y = a - b;
    // 2 2
    // uint32_t x = b - a;
    // int32_t  y = b - a;
    // 4294967294 -2

    // uint32_t a = 1;
    // uint32_t b = 0;
    // uint32_t x = a - b;
    // int32_t  y = a - b;
    // 1 1
    // uint32_t x = b - a;
    // int32_t  y = b - a;
    // 4294967295 -1

    // STEP ranges from -UINT32_MAX/2 to UINT32_MAX/2
    // in most cases, just adding STEP to CHECKPOINT will get the absolute seq
    // but if after adding, the absolute seq is negative, it should add another (1UL << 32)
    // (this means the checkpoint is near 0 so the new seq should always go bigger
    // eg. test/unwrap.cc line 25)
    int32_t steps = n - wrap(checkpoint, isn);
    int64_t res = checkpoint + steps;
    return res >= 0 ? checkpoint + steps : res + (1UL << 32);
}
