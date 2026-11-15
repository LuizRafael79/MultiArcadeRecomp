#include "ymfm_timer.hpp"

namespace multiarcaderecomp::chips {

void YmfmTimerBank::set_timer(std::uint32_t timer, std::int32_t duration, std::uint64_t now) {
    if (timer >= timer_count) return;
    if (duration < 0) {
        m_active[timer] = false;
        return;
    }
    m_active[timer] = true;
    m_deadlines[timer] = now + static_cast<std::uint32_t>(duration);
}

void YmfmTimerBank::set_busy_end(std::uint32_t clocks, std::uint64_t now) {
    m_busy_until = now + clocks;
}

bool YmfmTimerBank::is_busy(std::uint64_t now) const {
    return now < m_busy_until;
}

void YmfmTimerBank::expire_due(std::uint64_t now,
    const std::function<void(std::uint32_t)>& callback) {
    for (std::uint32_t timer = 0; timer < timer_count; ++timer) {
        if (!m_active[timer] || m_deadlines[timer] > now) continue;
        m_active[timer] = false;
        if (callback) callback(timer);
    }
}

} // namespace multiarcaderecomp::chips
