/**
 * @file ring_buffer.h
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) ring buffer
 *
 * Designed for audio I/O: I2S DMA → ring buffer → DSP pipeline → ring buffer → I2S DMA
 *
 * Thread-safety model:
 *  - Exactly ONE producer (writer) and ONE consumer (reader) — on different cores.
 *  - _head is written only by the producer; _tail only by the consumer.
 *  - std::atomic with acquire/release ordering provides the necessary memory
 *    barriers for the ESP32's dual-core Xtensa, replacing the insufficient
 *    `volatile` keyword which only prevents compiler re-ordering, not CPU re-ordering.
 *
 * Design choices:
 *  - Static backing array (no heap allocation).
 *  - Power-of-2 capacity for fast modulo via bitwise AND.
 *  - Full slot wasted to distinguish empty (head==tail) from full (head-tail==cap).
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include "config.h"
#include "dsp_types.h"

class RingBuffer {
public:
    /**
     * Initialize ring buffer.
     * @param capacity Desired capacity; rounded down to nearest power of 2.
     *                 Usable capacity is (rounded_capacity - 1) samples.
     */
    void init(size_t capacity);

    /**
     * Write samples — call from producer side only.
     * @return Number of samples actually written (≤ count).
     */
    size_t write(const q31_t* data, size_t count);

    /**
     * Read samples — call from consumer side only.
     * @return Number of samples actually read (≤ count).
     */
    size_t read(q31_t* data, size_t count);

    /** Samples available for reading (safe to call from either side). */
    size_t available() const;

    /** Free slots for writing (safe to call from either side). */
    size_t space() const;

    /** Reset to empty — only call when both producer and consumer are idle. */
    void reset();

    bool isEmpty() const { return _head.load(std::memory_order_acquire) ==
                                  _tail.load(std::memory_order_acquire); }

    bool isFull()  const { return space() == 0; }

private:
    static constexpr size_t MAX_CAPACITY = RING_BUFFER_SIZE * DSP_NUM_CHANNELS;

    q31_t  _buffer[MAX_CAPACITY];

    // Separate cache lines to avoid false sharing on dual-core ESP32.
    // Xtensa LX7 cache line = 32 bytes; pad each atomic to its own line.
    alignas(32) std::atomic<size_t> _head{0};  // Written by producer only
    alignas(32) std::atomic<size_t> _tail{0};  // Written by consumer only

    size_t _capacity = 0;
    size_t _mask     = 0;
};

#endif // RING_BUFFER_H