/**
 * @file ring_buffer.cpp
 * @brief Lock-free SPSC ring buffer implementation
 *
 * Memory ordering rationale (SPSC lock-free pattern):
 *
 *  Producer (write):
 *    - Reads _tail with memory_order_acquire  → sees all consumer stores
 *    - Writes _head with memory_order_release → makes new data visible to consumer
 *
 *  Consumer (read):
 *    - Reads _head with memory_order_acquire  → sees all producer stores
 *    - Writes _tail with memory_order_release → makes freed space visible to producer
 *
 *  This is the minimal correct ordering for SPSC on weakly-ordered architectures.
 *  Using memory_order_seq_cst would also be correct but adds unnecessary overhead.
 */

#include "ring_buffer.h"

void RingBuffer::init(size_t capacity) {
    if (capacity > MAX_CAPACITY) capacity = MAX_CAPACITY;

    // Round down to nearest power of 2 (minimum 2)
    size_t p = 1;
    while (p <= capacity / 2) p <<= 1;  // largest power-of-2 <= capacity
    if (p < 2) p = 2;

    _capacity = p;
    _mask     = p - 1;

    _head.store(0, std::memory_order_relaxed);
    _tail.store(0, std::memory_order_relaxed);
    memset(_buffer, 0, sizeof(_buffer));
}

size_t RingBuffer::write(const q31_t* data, size_t count) {
    // Acquire tail so we correctly see how much space the consumer has freed
    const size_t tail  = _tail.load(std::memory_order_acquire);
    const size_t head  = _head.load(std::memory_order_relaxed); // only we write _head
    const size_t avail = _capacity - 1 - (head - tail);         // available space

    if (count > avail) count = avail;
    if (count == 0)    return 0;

    size_t pos = head & _mask;
    size_t firstChunk = std::min(count, _capacity - pos);
    memcpy(&_buffer[pos], data, firstChunk * sizeof(q31_t));
    if (firstChunk < count) {
        memcpy(&_buffer[0], data + firstChunk, (count - firstChunk) * sizeof(q31_t));
    }
    // Release: make written data + new _head visible to consumer atomically
    _head.store(head + count, std::memory_order_release);
    return count;
}

size_t RingBuffer::read(q31_t* data, size_t count) {
    // Acquire head so we correctly see data the producer has published
    const size_t head  = _head.load(std::memory_order_acquire);
    const size_t tail  = _tail.load(std::memory_order_relaxed); // only we write _tail
    const size_t avail = head - tail;

    if (count > avail) count = avail;
    if (count == 0)    return 0;

    size_t pos = head & _mask;
    size_t firstChunk = std::min(count, _capacity - pos);
    memcpy(&_buffer[pos], data, firstChunk * sizeof(q31_t));
    if (firstChunk < count) {
        memcpy(&_buffer[0], data + firstChunk, (count - firstChunk) * sizeof(q31_t));
    }

    // Release: make new _tail (freed slots) visible to producer
    _tail.store(tail + count, std::memory_order_release);
    return count;
}

size_t RingBuffer::available() const {
    // acquire both to get a consistent snapshot (approximate on multi-core)
    const size_t head = _head.load(std::memory_order_acquire);
    const size_t tail = _tail.load(std::memory_order_acquire);
    return head - tail;
}

size_t RingBuffer::space() const {
    return _capacity - 1 - available();
}

void RingBuffer::reset() {
    // Only safe to call when both producer and consumer are suspended
    _head.store(0, std::memory_order_seq_cst);
    _tail.store(0, std::memory_order_seq_cst);
}