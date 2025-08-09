#include "Straf/Overlay.h"
#include "Straf/Logging.h"
#include <windows.h>

namespace Straf {

class OverlayStub : public IOverlayRenderer {
public:
    bool Initialize() override {
        LogInfo("OverlayStub initialized (no real window)");
        return true;
    }
    void ShowPenalty(const std::string& label) override {
        LogInfo("Penalty shown: %s", label.c_str());
        // Real implementation would render via D3D11 to a layered window.
    }
    void Hide() override {
        LogInfo("Overlay hidden");
    }
};

IOverlayRenderer* CreateOverlayStub(){ return new OverlayStub(); }

}
