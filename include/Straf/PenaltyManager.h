#pragma once
#include <queue>
#include <string>
#include <optional>
#include <chrono>

namespace Straf {

struct Penalty {
    std::string label;
    std::chrono::milliseconds duration{10000};
    std::chrono::milliseconds cooldown{60000};
};

class IPenaltyManager {
public:
    virtual ~IPenaltyManager() = default;
    virtual void Configure(int queueLimit, std::chrono::milliseconds defaultDuration, std::chrono::milliseconds defaultCooldown) = 0;
    virtual void Trigger(const std::string& reason) = 0;
    virtual void Tick() = 0; // call frequently from main loop
};

}
