#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

namespace sf
{
/**
    Single-producer / single-consumer lock-free ring buffer.

    The audio thread is the only producer and the GUI thread the only consumer.
    Neither side ever blocks: when the ring is full `push` fails and the value is
    dropped. That is the right trade-off here, because every user of this class
    carries visualisation data, where a fresh stream with an occasional gap beats a
    stalled audio thread.

    Capacity must be a power of two so the wrap is a mask rather than a modulo.
*/
template <typename T, size_t Capacity>
class LockFreeFifo
{
    static_assert (Capacity >= 2, "Capacity must be at least 2");
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /** Producer thread. Returns false (and drops `v`) when the ring is full. */
    bool push (const T& v) noexcept
    {
        const auto w    = writePos.load (std::memory_order_relaxed);
        const auto next = (w + 1) & kMask;

        if (next == readPos.load (std::memory_order_acquire))
            return false;

        buffer[w] = v;
        writePos.store (next, std::memory_order_release);
        return true;
    }

    /** Consumer thread. Returns false when the ring is empty. */
    bool pop (T& out) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);

        if (r == writePos.load (std::memory_order_acquire))
            return false;

        out = buffer[r];
        readPos.store ((r + 1) & kMask, std::memory_order_release);
        return true;
    }

    /** Consumer thread. Discards everything currently queued. */
    void clear() noexcept
    {
        readPos.store (writePos.load (std::memory_order_acquire), std::memory_order_release);
    }

    /** Approximate — the other side may be moving its cursor concurrently. */
    size_t getNumReady() const noexcept
    {
        const auto w = writePos.load (std::memory_order_acquire);
        const auto r = readPos.load (std::memory_order_acquire);
        return (w - r) & kMask;
    }

    static constexpr size_t capacity() noexcept { return Capacity - 1; }

private:
    static constexpr size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer {};
    std::atomic<size_t> writePos { 0 };
    std::atomic<size_t> readPos  { 0 };
};

/**
    Lock-free hand-off of a whole snapshot object from one thread to another.

    Three slots: the writer fills a slot that the reader is provably not holding,
    then publishes it with a release store. The reader acquires the published index
    and may keep using that slot until its next `read()`. Nothing is ever copied
    under a lock and neither side blocks.

    Use this for payloads too large to stream through a FIFO — spectra, collision
    maps, rendered curve tables.
*/
template <typename T>
class TripleBuffer
{
public:
    /** Writer thread: mutate a slot, then publish it. */
    template <typename Fn>
    void write (Fn&& fill)
    {
        const auto cur  = published.load (std::memory_order_relaxed);
        const auto prev = lastWritten;

        int slot = 0;
        for (int i = 0; i < 3; ++i)
            if (i != cur && i != prev)
                slot = i;

        std::forward<Fn> (fill) (slots[(size_t) slot]);

        lastWritten = cur;
        published.store (slot, std::memory_order_release);
    }

    /** Reader thread: the most recently published snapshot. Never null. */
    const T& read() noexcept
    {
        readIndex = published.load (std::memory_order_acquire);
        return slots[(size_t) readIndex];
    }

    /** Reader thread: the snapshot already acquired by the last `read()`. */
    const T& current() const noexcept { return slots[(size_t) readIndex]; }

private:
    std::array<T, 3> slots {};
    std::atomic<int> published { 0 };
    int lastWritten = 0;
    int readIndex   = 0;
};
} // namespace sf
