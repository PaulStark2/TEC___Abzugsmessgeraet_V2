#pragma once

#include "TestPoint.hpp"

#include <cstddef>

// Maximum number of points a TestProgram can hold (roadmap: fixed capacity, no heap).
// No concrete limit is specified in the roadmap; 32 is a small STM32-friendly constant.
constexpr std::size_t kMaxTestPoints = 32;

// TestProgram stores a fixed-capacity list of TestPoints without dynamic allocation.
class TestProgram
{
public:
    // Construct an empty program. // ctor
    TestProgram()
        : count_(0) // start empty
    {
    }

    // Append one point if capacity remains. // returns false on overflow
    bool add(const TestPoint& point)
    {
        // Reject when the fixed buffer is already full. // overflow
        if (count_ >= kMaxTestPoints)
        {
            return false; // not stored
        }

        // Store the point at the next free slot. // append
        points_[count_] = point; // copy POD
        ++count_; // advance size
        return true; // stored
    }

    // Remove all stored points. // reset
    void clear()
    {
        count_ = 0; // logical empty; buffer contents unused until overwritten
    }

    // Return the number of currently stored points. // size
    std::size_t size() const
    {
        return count_; // current count
    }

    // Copy the point at index into out. Returns false if index is out of range. // safe access
    bool pointAt(std::size_t index, TestPoint& out) const
    {
        // Reject invalid indices. // bounds check
        if (index >= count_)
        {
            return false; // no point
        }

        out = points_[index]; // copy POD
        return true; // success
    }

private:
    // Fixed storage for up to kMaxTestPoints points (no heap). // buffer
    TestPoint points_[kMaxTestPoints];

    // Number of valid points currently stored in points_. // count
    std::size_t count_;
};
