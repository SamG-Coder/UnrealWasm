#pragma once
#include "Modules/ModuleInterface.h"
namespace UEWebGPU { class Device; }
class IWebGPURHIModule : public IModuleInterface
{
public:
    // Initialization is asynchronous. The browser bootstrap must wait for the
    // device before attempting resource creation; it must not block its thread.
    virtual UEWebGPU::Device& GetDevice() = 0;
};
