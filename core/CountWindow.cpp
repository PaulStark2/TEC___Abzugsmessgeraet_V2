#include "CountWindow.hpp"

#include <cmath>

void CountWindow::dropOldest()
{
    if (count_ <= 0)
    {
        return;
    }
    total_us_ -= slots_[head_].dt_us;
    if (total_us_ < 0)
    {
        total_us_ = 0;
    }
    head_ = (head_ + 1) % kCapacity;
    --count_;
}

CountWindow::Mean CountWindow::mean() const
{
    Mean out{};
    if (count_ <= 0 || total_us_ <= 0)
    {
        return out;
    }
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_z = 0.0;
    int index = head_;
    for (int n = 0; n < count_; ++n)
    {
        const Slot& slot = slots_[index];
        const double weight = static_cast<double>(slot.dt_us);
        sum_x += static_cast<double>(slot.x) * weight;
        sum_y += static_cast<double>(slot.y) * weight;
        sum_z += static_cast<double>(slot.z) * weight;
        index = (index + 1) % kCapacity;
    }
    const double inv = 1.0 / static_cast<double>(total_us_);
    out.x = static_cast<float>(sum_x * inv);
    out.y = static_cast<float>(sum_y * inv);
    out.z = static_cast<float>(sum_z * inv);
    return out;
}

bool CountWindow::full() const
{
    return total_us_ >= kWindowUs;
}

int CountWindow::totalUs() const
{
    return total_us_;
}

CountWindow::Mean CountWindow::push(float x, float y, float z, float dt_s)
{
    if (!(dt_s > 0.0f))
    {
        return mean();
    }
    int dt_us = static_cast<int>(std::lround(static_cast<double>(dt_s) * 1000000.0));
    if (dt_us <= 0)
    {
        return mean();
    }
    if (dt_us > kWindowUs)
    {
        dt_us = kWindowUs;
    }

    while (count_ > 0 && (total_us_ + dt_us > kWindowUs || count_ >= kCapacity))
    {
        dropOldest();
    }

    const int tail = (head_ + count_) % kCapacity;
    slots_[tail] = Slot{x, y, z, dt_us};
    ++count_;
    total_us_ += dt_us;
    return mean();
}
