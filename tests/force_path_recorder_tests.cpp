#include "ForcePathRecorder.hpp"

int main()
{
    ForcePathRecorder rec;
    if (rec.size() != 0 || rec.dropped() != 0)
    {
        return 1;
    }

    rec.push(1.0f, 2.0f);
    rec.push(3.0f, 4.0f);
    if (rec.size() != 2)
    {
        return 2;
    }

    ForcePathRecorder::Sample out[4];
    const std::size_t n = rec.pop(out, 1);
    if (n != 1 || out[0].z != 1.0f || out[0].fz != 2.0f)
    {
        return 3;
    }
    if (rec.size() != 1 || rec.sequence() != 1)
    {
        return 4;
    }

    rec.reset();
    for (std::size_t i = 0; i < ForcePathRecorder::kCapacity + 10; ++i)
    {
        rec.push(static_cast<float>(i), static_cast<float>(i) * 0.1f);
    }
    if (rec.size() != ForcePathRecorder::kCapacity)
    {
        return 5;
    }
    if (rec.dropped() != 10)
    {
        return 6;
    }

    ForcePathRecorder::Sample first{};
    if (rec.pop(&first, 1) != 1)
    {
        return 7;
    }
    if (first.z != 10.0f)
    {
        return 8;
    }

    return 0;
}
