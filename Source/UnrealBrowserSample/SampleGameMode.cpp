#include "SampleGameMode.h"
#include "../../shared/Scene.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "PrimitiveSceneProxy.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HighResScreenshot.h"
#include "UnrealClient.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "RHI.h"

ASampleGameMode::ASampleGameMode(){ PrimaryActorTick.bCanEverTick=true; DefaultPawnClass=nullptr; }
void ASampleGameMode::BeginPlay(){
 Super::BeginPlay();
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/FixtureCube.FixtureCube"));
 auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/FixtureMaterial.FixtureMaterial"));
 if(!Mesh||!Mat){UE_LOG(LogTemp,Fatal,TEXT("Missing fixture assets. Run scripts/native.ps1 Assets before launching."));return;}
 for(int i=0;i<SampleBoxCount;i++){
   const float* b=SampleBoxes+i*12;
   auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(b[0],b[1],b[2])*100,FRotator::ZeroRotator);
   Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
   Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
   Actor->SetActorScale3D(FVector(b[3],b[4],b[5]));
   auto* M=UMaterialInstanceDynamic::Create(Mat,this);
   M->SetVectorParameterValue(TEXT("Color"),FLinearColor(b[6],b[7],b[8]));
   M->SetScalarParameterValue(TEXT("Roughness"),b[9]); M->SetScalarParameterValue(TEXT("Metallic"),b[10]);
   Actor->GetStaticMeshComponent()->SetMaterial(0,M);
 }
 FVector Pos(SampleCamera[0]*100,SampleCamera[1]*100,SampleCamera[2]*100);
 FVector Target(SampleCamera[3]*100,SampleCamera[4]*100,SampleCamera[5]*100);
 auto* Cam=GetWorld()->SpawnActor<ACameraActor>(Pos,(Target-Pos).Rotation());
 Cam->GetCameraComponent()->SetFieldOfView(SampleCamera[6]);
 Cam->GetCameraComponent()->SetAspectRatio(16.f/9.f);
 GetWorld()->GetFirstPlayerController()->SetViewTarget(Cam);
 auto* Light=GetWorld()->SpawnActor<APointLight>(FVector(SampleLight[0],SampleLight[1],SampleLight[2])*100,FRotator::ZeroRotator);
 Light->PointLightComponent->SetMobility(EComponentMobility::Movable);
 Light->PointLightComponent->SetIntensityUnits(ELightUnits::Lumens);
 Light->PointLightComponent->SetIntensity(14000);
 Light->PointLightComponent->SetAttenuationRadius(2500);
 Light->PointLightComponent->SetSourceRadius(12);
 auto* PP=GetWorld()->SpawnActor<APostProcessVolume>(); PP->bUnbound=true;
 PP->Settings.bOverride_AutoExposureMethod=true; PP->Settings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
 PP->Settings.bOverride_AutoExposureBias=true; PP->Settings.AutoExposureBias=-4;
 PP->Settings.bOverride_VignetteIntensity=true; PP->Settings.VignetteIntensity=0;
 PP->Settings.bOverride_BloomIntensity=true; PP->Settings.BloomIntensity=0;
 PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP->Settings.AutoExposureApplyPhysicalCameraExposure=false;
 Output=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("artifacts/native"));
 FParse::Value(FCommandLine::Get(),TEXT("SampleOutput="),Output);
 IFileManager::Get().MakeDirectory(*Output,true);
 LastTime=FPlatformTime::Seconds();
}
void ASampleGameMode::Tick(float Delta){
 Super::Tick(Delta); double Now=FPlatformTime::Seconds(); double Ms=(Now-LastTime)*1000.;LastTime=Now;Frames++;
 if(Frames>300&&Frames<=600) Samples.Add(Ms);
 if(Frames==600){
   Samples.Sort(); double Sum=0;for(double S:Samples) Sum+=S;
   auto Report=MakeShared<FJsonObject>();
   Report->SetStringField(TEXT("lane"),TEXT("Native Unreal 5.8.2 / editor -game / DX12 SM6"));
   Report->SetStringField(TEXT("gpu"),GRHIAdapterName);
   Report->SetStringField(TEXT("timing"),TEXT("Wall-clock game-frame intervals; 300 warmup + 300 measured. Not GPU-only time."));
   Report->SetNumberField(TEXT("meanMs"),Sum/Samples.Num());
   Report->SetNumberField(TEXT("p50Ms"),Samples[150]);Report->SetNumberField(TEXT("p95Ms"),Samples[284]);
   Report->SetNumberField(TEXT("width"),1280);Report->SetNumberField(TEXT("height"),720);
   Report->SetBoolField(TEXT("rhiAtomicUInt64"),GRHISupportsAtomicUInt64);
   int32 NaniteMeshes=0,ValidData=0;
   for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It){auto* C=It->GetStaticMeshComponent();if(C->HasValidNaniteData())ValidData++;if(C->SceneProxy&&C->SceneProxy->IsNaniteMesh())NaniteMeshes++;}
   Report->SetNumberField(TEXT("meshesWithNaniteData"),ValidData);
   Report->SetNumberField(TEXT("activeNaniteSceneProxies"),NaniteMeshes);
   for(const TCHAR* Name:{TEXT("r.Nanite"),TEXT("r.Lumen.HardwareRayTracing"),TEXT("r.DynamicGlobalIlluminationMethod"),TEXT("r.ReflectionMethod"),TEXT("r.RayTracing"),TEXT("r.ScreenPercentage")}){
     if(auto* Var=IConsoleManager::Get().FindConsoleVariable(Name))Report->SetNumberField(Name,Var->GetFloat());
   }
   Report->SetStringField(TEXT("featureEvidence"),TEXT("Includes live Nanite scene-proxy counts and RHI capabilities. Separate Nanite cluster and Lumen overview captures validate the native visualization paths."));
   if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite.Visualize")))Report->SetStringField(TEXT("naniteVisualization"),V->GetString());
   if(auto* V=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.Visualize")))Report->SetNumberField(TEXT("lumenVisualization"),V->GetInt());
   FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
   FFileHelper::SaveStringToFile(Json,*(Output/TEXT("report.json")));
   FScreenshotRequest::RequestScreenshot(Output/TEXT("scene.png"),false,false);
 }
 if(Frames==630&&FParse::Param(FCommandLine::Get(),TEXT("SampleBenchmark")))FGenericPlatformMisc::RequestExit(false);
}
