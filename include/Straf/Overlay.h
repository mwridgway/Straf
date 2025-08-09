#pragma once
#include <string>

namespace Straf {

class IOverlayRenderer {
public:
    virtual ~IOverlayRenderer() = default;
    virtual bool Initialize() = 0;
    virtual void ShowPenalty(const std::string& label) = 0;
    virtual void Hide() = 0;
};

}
