#pragma once
#include <string>
#include <memory>

namespace Straf {

class IOverlayRenderer {
public:
    virtual ~IOverlayRenderer() = default;
    virtual bool Initialize() = 0;
    virtual void ShowPenalty(const std::string& label) = 0;
    virtual void Hide() = 0;
};

std::unique_ptr<IOverlayRenderer> CreateOverlayStub();

}
