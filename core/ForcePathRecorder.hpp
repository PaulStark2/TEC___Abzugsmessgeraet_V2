#pragma once

#include <cstddef>
#include <cstdint>

// Non-blocking (z, fz) ring buffer. Overflow drops the oldest sample.
class ForcePathRecorder
{
public:
    static constexpr std::size_t kCapacity = 256;

    struct Sample
    {
        float z = 0.0f;
        float fz = 0.0f;
        std::uint16_t iteration = 0;
    };

    void reset()
    {
        head_ = 0;
        count_ = 0;
        dropped_ = 0;
        sequence_ = 0;
        iteration_ = 0;
    }

    void setIteration(std::uint16_t iteration)
    {
        iteration_ = iteration;
    }

    std::uint16_t iteration() const
    {
        return iteration_;
    }

    void push(float z, float fz)
    {
        if (count_ == kCapacity)
        {
            head_ = (head_ + 1) % kCapacity;
            --count_;
            ++dropped_;
        }
        const std::size_t index = (head_ + count_) % kCapacity;
        samples_[index].z = z;
        samples_[index].fz = fz;
        samples_[index].iteration = iteration_;
        ++count_;
    }

    std::size_t size() const
    {
        return count_;
    }

    std::uint32_t dropped() const
    {
        return dropped_;
    }

    std::uint32_t sequence() const
    {
        return sequence_;
    }

    // Copy up to max_out samples in FIFO order and remove them.
    std::size_t pop(Sample* out, std::size_t max_out)
    {
        std::size_t n = 0;
        while (n < max_out && count_ > 0)
        {
            out[n] = samples_[head_];
            head_ = (head_ + 1) % kCapacity;
            --count_;
            ++n;
        }
        if (n > 0)
        {
            ++sequence_;
        }
        return n;
    }

    std::size_t popSameIteration(Sample* out, std::size_t max_out)
    {
        if (count_ == 0 || max_out == 0)
        {
            return 0;
        }
        const std::uint16_t it = samples_[head_].iteration;
        std::size_t n = 0;
        while (n < max_out && count_ > 0 && samples_[head_].iteration == it)
        {
            out[n] = samples_[head_];
            head_ = (head_ + 1) % kCapacity;
            --count_;
            ++n;
        }
        if (n > 0)
        {
            ++sequence_;
        }
        return n;
    }

private:
    Sample samples_[kCapacity]{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::uint32_t dropped_ = 0;
    std::uint32_t sequence_ = 0;
    std::uint16_t iteration_ = 0;
};
