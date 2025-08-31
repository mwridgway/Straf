#pragma once
#include <string>
#include <memory>

namespace Straf {

class IOverlayRenderer {
public:
    virtual ~IOverlayRenderer() = default;
    virtual bool Initialize() = 0;
    virtual void ShowPenalty(const std::string& label) = 0;
    // Update the overlay status (e.g., number of stars and label). Stars clamped to [0,5].
    virtual void UpdateStatus(int stars, const std::string& label) = 0;
    virtual void Hide() = 0;
};

// Factory functions for pluggable overlays
// Classic: existing GTA-like stars banner
std::unique_ptr<IOverlayRenderer> CreateOverlayClassic();
// Bar: alternative style (bottom bar with status)
std::unique_ptr<IOverlayRenderer> CreateOverlayBar();
// Vignette: elegant rounded vignette style
std::unique_ptr<IOverlayRenderer> CreateOverlayVignette();
// Default selection based on environment (STRAF_NO_OVERLAY, STRAF_OVERLAY_STYLE)
// STRAF_OVERLAY_STYLE: "classic" (default), "bar", or "vignette"
std::unique_ptr<IOverlayRenderer> CreateOverlayStub();

}
