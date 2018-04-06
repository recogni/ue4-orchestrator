#pragma once

#include "UE4Orchestrator.h"
#include "UCameraRigComponent.generated.h"

UCLASS()
class UE4ORCHESTRATOR_API UCameraRigComponent : public USceneComponent
{
    GENERATED_BODY()

  private:
    UCameraRigComponent();
    void Init();

  public:
    static UCameraRigComponent* Create(APawn* pawn, TArray<FString> modes);
    virtual void TickComponent(float dt, enum ELevelTick tt, FTickFn* fn) override;
  private:
    TMap<FString, USceneCaptureComponent2D*>    components;
    TMap<FString, UMaterial*>*                  materials;
    APawn*                                      pawn;
};
