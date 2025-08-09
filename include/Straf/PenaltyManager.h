#pragma once
#include <queue>
#include <string>
#include <optional>
#include <chrono>
#include <memory>

namespace Straf {

struct Penalty {
    std::string label;
    std::chrono::milliseconds duration{10000};
    std::chrono::milliseconds cooldown{60000};
};

class IOverlayRenderer; // Forward declaration

class IPenaltyManager {
public:
    virtual ~IPenaltyManager() = default;
    virtual void Configure(int queueLimit, std::chrono::milliseconds defaultDuration, std::chrono::milliseconds defaultCooldown) = 0;
    virtual void Trigger(const std::string& reason) = 0;
    virtual void Tick() = 0; // call frequently from main loop
    // Returns current star count (active+queued, clamped 1..5 when any active/queued)
    virtual int GetStarCount() const = 0;
};

std::unique_ptr<IPenaltyManager> CreatePenaltyManager(IOverlayRenderer* overlay);

}
