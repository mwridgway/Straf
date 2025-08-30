#pragma once
#include <functional>
#include <memory>

namespace Straf {
class ITray {
public:
    virtual ~ITray() = default;
    virtual void Run(std::function<void()> onExit) = 0;
};
std::unique_ptr<ITray> CreateTray();
}
