// Deterministic unit tests for TestPoint / TestProgram (Milestone 6).
#include "TestPoint.hpp"
#include "TestProgram.hpp"

#include <cmath>

// Compare two floats within a deterministic tolerance. // helper
static bool nearEqual(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) <= epsilon; // absolute compare
}

// Compare two TestPoints field-wise. // helper
static bool samePoint(const TestPoint& a, const TestPoint& b)
{
    return nearEqual(a.x, b.x) && nearEqual(a.y, b.y) && nearEqual(a.z, b.z); // all axes
}

// Return 0 on success; non-zero identifies the failing check.
int main()
{
    // Test 1 – Initial empty state.
    {
        TestProgram program; // empty
        if (program.size() != 0) // must be empty
        {
            return 1; // fail
        }
    }

    // Test 2 – Add points in order and read them back.
    {
        TestProgram program; // empty
        const TestPoint p0{1.0f, 2.0f, 3.0f}; // first
        const TestPoint p1{4.0f, 5.0f, 6.0f}; // second
        const TestPoint p2{-1.0f, 0.5f, 10.0f}; // third

        if (!program.add(p0) || !program.add(p1) || !program.add(p2)) // all must succeed
        {
            return 2; // fail
        }

        if (program.size() != 3) // size after three adds
        {
            return 3; // fail
        }

        TestPoint out{}; // scratch
        if (!program.pointAt(0, out) || !samePoint(out, p0)) // first
        {
            return 4; // fail
        }
        if (!program.pointAt(1, out) || !samePoint(out, p1)) // second
        {
            return 5; // fail
        }
        if (!program.pointAt(2, out) || !samePoint(out, p2)) // third
        {
            return 6; // fail
        }
    }

    // Test 3 – Clear empties the program and allows refill.
    {
        TestProgram program; // empty
        if (!program.add(TestPoint{1.0f, 1.0f, 1.0f})) // seed
        {
            return 7; // fail
        }
        if (program.size() != 1) // one point
        {
            return 8; // fail
        }

        program.clear(); // wipe
        if (program.size() != 0) // empty again
        {
            return 9; // fail
        }

        const TestPoint refill{9.0f, 8.0f, 7.0f}; // new first after clear
        if (!program.add(refill)) // must accept again
        {
            return 10; // fail
        }
        if (program.size() != 1) // size after refill
        {
            return 11; // fail
        }

        TestPoint out{}; // scratch
        if (!program.pointAt(0, out) || !samePoint(out, refill)) // order restarts at 0
        {
            return 12; // fail
        }
    }

    // Test 4 – Overflow: add beyond capacity fails and leaves data unchanged.
    {
        TestProgram program; // empty

        // Fill to capacity with distinct points. // fill
        for (std::size_t i = 0; i < kMaxTestPoints; ++i)
        {
            const TestPoint p{
                static_cast<float>(i),
                static_cast<float>(i) + 0.1f,
                static_cast<float>(i) + 0.2f}; // unique
            if (!program.add(p)) // must succeed until full
            {
                return 13; // fail
            }
        }

        if (program.size() != kMaxTestPoints) // full
        {
            return 14; // fail
        }

        // Snapshot last point before rejected add. // snapshot
        TestPoint last_before{}; // buffer
        if (!program.pointAt(kMaxTestPoints - 1, last_before)) // last valid
        {
            return 15; // fail
        }

        const TestPoint overflow_point{999.0f, 998.0f, 997.0f}; // must not be stored
        if (program.add(overflow_point)) // must fail
        {
            return 16; // fail
        }

        if (program.size() != kMaxTestPoints) // size unchanged
        {
            return 17; // fail
        }

        TestPoint last_after{}; // buffer
        if (!program.pointAt(kMaxTestPoints - 1, last_after) || !samePoint(last_after, last_before))
        {
            return 18; // fail
        }

        // First point must also remain unchanged. // integrity
        TestPoint first{}; // buffer
        if (!program.pointAt(0, first) || !nearEqual(first.x, 0.0f))
        {
            return 19; // fail
        }
    }

    // Test 5 – Invalid pointAt indices.
    {
        TestProgram program; // empty
        TestPoint out{}; // scratch

        // Empty program: any index is invalid. // empty access
        if (program.pointAt(0, out)) // must fail
        {
            return 20; // fail
        }

        if (!program.add(TestPoint{1.0f, 2.0f, 3.0f})) // one point
        {
            return 21; // fail
        }

        if (program.pointAt(1, out)) // index == size is invalid
        {
            return 22; // fail
        }

        if (program.pointAt(100, out)) // far out of range
        {
            return 23; // fail
        }

        // Valid index still works. // sanity
        if (!program.pointAt(0, out) || !nearEqual(out.x, 1.0f))
        {
            return 24; // fail
        }
    }

    // All TestProgram tests passed.
    return 0;
}
