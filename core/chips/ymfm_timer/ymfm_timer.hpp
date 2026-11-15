#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace multiarcaderecomp::chips {

// Host-clocked timer and busy-line state used by ymfm device interfaces.
class YmfmTimerBank {
public:
    static constexpr std::size_t timer_count = 4;

    void set_timer(std::uint32_t timer, std::int32_t duration, std::uint64_t now);
    void set_busy_end(std::uint32_t clocks, std::uint64_t now);
    bool is_busy(std::uint64_t now) const;
    void expire_due(std::uint64_t now, const std::function<void(std::uint32_t)>& callback);

private:
    std::array<std::uint64_t, timer_count> m_deadlines{};
    std::array<bool, timer_count> m_active{};
    std::uint64_t m_busy_until = 0;
};

} // namespace multiarcaderecomp::chips
