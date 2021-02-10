#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

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
