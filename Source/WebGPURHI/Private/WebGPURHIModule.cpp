#include "Modules/ModuleManager.h"
#include "WebGPUDevice.h"
#include "IWebGPURHIModule.h"

// Resource backend under construction. Do not register an FDynamicRHI factory
// until the required command contexts, pipelines and shader format exist.
class FWebGPURHIModule final : public IWebGPURHIModule
{
public:
    UEWebGPU::Device& GetDevice() override { Device.Initialize(); return Device; }
    bool SupportsDynamicReloading() override { return false; }
    // Async browser callbacks carry the module's Device pointer. Keep the module
    // loaded for the application's lifetime, including browser shutdown.
    bool SupportsAutomaticShutdown() override { return false; }
private:
    UEWebGPU::Device Device;
};
IMPLEMENT_MODULE(FWebGPURHIModule, WebGPURHI)
