#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SampleGameMode.generated.h"
UCLASS()
class UNREALBROWSERSAMPLE_API ASampleGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    ASampleGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    int32 Frames=0;
    double LastTime=0;
    TArray<double> Samples;
    FString Output;
};
