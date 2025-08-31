#include "Straf/PenaltyManager.h"
#include "Straf/Overlay.h"
#include "Straf/Logging.h"
#include <memory>
#include <queue>
#include <unordered_map>

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
        
        // Debounce check - prevent penalties too close together
        if (lastStraf_ + debounceDuration_ > now) {
            LogInfo("Penalty debounced: %s (too soon, last was %.1fs ago)", 
                reason.c_str(), 
                std::chrono::duration<double>(now - lastStraf_).count());
            return;
        }
        
        // Check if we've already penalized this exact phrase recently
        auto phraseIt = recentPhrases_.find(reason);
        if (phraseIt != recentPhrases_.end() && phraseIt->second + phraseCooldown_ > now) {
            LogInfo("Penalty skipped: '%s' (phrase recently penalized %.1fs ago)", 
                reason.c_str(),
                std::chrono::duration<double>(now - phraseIt->second).count());
            return;
        }
        
        // Record this phrase and timestamp
        recentPhrases_[reason] = now;
        lastStraf_ = now;
        
        // Progressive penalty duration - repeat offenses get longer penalties
        int currentTotal = GetStarCount();
        auto duration = CalculateProgressiveDuration(currentTotal);
        
        // Queue penalty if space available
        if ((int)queue_.size() < queueLimit_) {
            queue_.push(Penalty{reason, duration, defaultCooldown_});
            LogInfo("Penalty applied: '%s' duration=%.1fs (total stars will be %d, queue=%zu)", 
                reason.c_str(), 
                std::chrono::duration<double>(duration).count(),
                GetStarCount(),
                queue_.size());
            overlay_->UpdateStatus(GetStarCount(), reason);
        } else {
            LogInfo("Penalty queue full - dropped: '%s'", reason.c_str());
        }
    }

    void Tick() override {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        
        // Periodically clean up old phrase entries (every 30 seconds)
        static auto lastCleanup = now;
        if (now - lastCleanup > std::chrono::seconds{30}) {
            CleanupOldPhrases();
            lastCleanup = now;
        }
        
        if (current_) {
            if (start_ + current_->duration <= now) {
                lastEnd_ = now;
                current_.reset();
                LogInfo("Penalty ended naturally");
                
                int remainingStars = GetStarCount();
                if (remainingStars > 0) {
                    // Still have queued penalties - keep overlay visible but update status
                    overlay_->UpdateStatus(remainingStars, "");
                    LogInfo("Penalty ended but {} stars remaining - keeping overlay visible", remainingStars);
                } else {
                    // No more penalties - hide overlay
                    overlay_->Hide();
                    overlay_->UpdateStatus(0, "");
                    LogInfo("All penalties finished - hiding overlay");
                }
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
    // Calculate progressive penalty duration - more stars = longer penalties
    std::chrono::milliseconds CalculateProgressiveDuration(int currentStars) {
        // Base duration increases with current penalty level
        // 1 star: 5s, 2 stars: 8s, 3 stars: 12s, 4 stars: 18s, 5+ stars: 25s
        const std::chrono::milliseconds baseDurations[] = {
            std::chrono::milliseconds{5000},   // 0 stars -> 5s
            std::chrono::milliseconds{8000},   // 1 star -> 8s  
            std::chrono::milliseconds{12000},  // 2 stars -> 12s
            std::chrono::milliseconds{18000},  // 3 stars -> 18s
            std::chrono::milliseconds{25000}   // 4+ stars -> 25s
        };
        
        int index = std::min(currentStars, 4);
        return baseDurations[index];
    }
    
    // Clean up old phrase entries to prevent memory bloat
    void CleanupOldPhrases() {
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        auto maxAge = phraseCooldown_ * 2; // Keep phrases for double the cooldown period
        
        for (auto it = recentPhrases_.begin(); it != recentPhrases_.end();) {
            if (it->second + maxAge < now) {
                it = recentPhrases_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    IOverlayRenderer* overlay_;
    int queueLimit_{5};
    std::chrono::milliseconds defaultDuration_{10000};
    std::chrono::milliseconds defaultCooldown_{60000};
    std::chrono::milliseconds debounceDuration_{3000};     // 3s between any penalties
    std::chrono::milliseconds phraseCooldown_{15000};      // 15s before same phrase can be penalized again

    std::optional<Penalty> current_{};
    std::queue<Penalty> queue_{};
    std::chrono::steady_clock::time_point start_{};
    std::chrono::steady_clock::time_point lastEnd_{};
    std::chrono::steady_clock::time_point lastStraf_{};
    
    // Track recent phrases to prevent repeat penalties
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> recentPhrases_;
};

std::unique_ptr<IPenaltyManager> CreatePenaltyManager(IOverlayRenderer* overlay){ 
    return std::make_unique<PenaltyManager>(overlay); 
}

}
