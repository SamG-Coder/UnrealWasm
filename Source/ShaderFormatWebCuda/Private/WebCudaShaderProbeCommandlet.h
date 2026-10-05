#pragma once
#include "Commandlets/Commandlet.h"
#include "WebCudaShaderProbeCommandlet.generated.h"

// Runs the real IShaderFormat boundary against captured UE-preprocessed source.
UCLASS()
class UWebCudaShaderProbeCommandlet : public UCommandlet {
    GENERATED_BODY()
public:
    UWebCudaShaderProbeCommandlet();
    int32 Main(const FString& Params) override;
};
