#include "Straf/PenaltyManager.h"
#include "Straf/Overlay.h"
#include "Straf/Logging.h"
#include <memory>
#include <queue>

namespace Straf {

class PenaltyManager : public IPenaltyManager {
public:
    explicit PenaltyManager(IOverlayRenderer* overlay) : overlay_(overlay) {}

    void Configure(int queueLimit, std::chrono::milliseconds defaultDuration, std::chrono::milliseconds defaultCooldown) override {
        queueLimit_ = queueLimit;
        defaultDuration_ = defaultDuration;
        defaultCooldown_ = defaultCooldown;
    }

    void Trigger(const std::string& reason) override {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        // Cooldown check (per label simplified)
        if (lastEnd_ + defaultCooldown_ > now) {
            // Queue if space
            if ((int)queue_.size() < queueLimit_) {
                queue_.push(Penalty{reason, defaultDuration_, defaultCooldown_});
                LogInfo("Penalty queued: %s (queue=%zu)", reason.c_str(), queue_.size());
                overlay_->UpdateStatus(GetStarCount(), reason);
            } else {
                LogInfo("Penalty dropped (queue full)");
            }
            return;
        }
        // Start immediately if idle
        current_ = Penalty{reason, defaultDuration_, defaultCooldown_};
        start_ = now;
        overlay_->ShowPenalty(reason);
        overlay_->UpdateStatus(GetStarCount(), reason);
        LogInfo("Penalty started: %s", reason.c_str());
    }

    void Tick() override {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        if (current_) {
            if (start_ + current_->duration <= now) {
                overlay_->Hide();
                lastEnd_ = now;
                current_.reset();
                LogInfo("Penalty ended");
                overlay_->UpdateStatus(GetStarCount(), "");
            }
            return;
        }
        // Start next if available and cooldown passed
        if (!queue_.empty() && lastEnd_ + defaultCooldown_ <= now) {
            current_ = queue_.front();
            queue_.pop();
            start_ = now;
            overlay_->ShowPenalty(current_->label);
            overlay_->UpdateStatus(GetStarCount(), current_->label);
            LogInfo("Penalty started from queue: %s (remaining=%zu)", current_->label.c_str(), queue_.size());
        }
    }

    int GetStarCount() const override {
        int active = current_.has_value() ? 1 : 0;
        int queued = static_cast<int>(queue_.size());
        int total = active + queued;
        if (total <= 0) return 0;
        if (total > 5) return 5;
        return total;
    }

private:
    IOverlayRenderer* overlay_;
    int queueLimit_{5};
    std::chrono::milliseconds defaultDuration_{10000};
    std::chrono::milliseconds defaultCooldown_{60000};

    std::optional<Penalty> current_{};
    std::queue<Penalty> queue_{};
    std::chrono::steady_clock::time_point start_{};
    std::chrono::steady_clock::time_point lastEnd_{};
};

std::unique_ptr<IPenaltyManager> CreatePenaltyManager(IOverlayRenderer* overlay){ 
    return std::make_unique<PenaltyManager>(overlay); 
}

}
