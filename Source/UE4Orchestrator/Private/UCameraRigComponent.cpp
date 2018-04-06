#include "UCameraRigComponent.h"

////////////////////////////////////////////////////////////////////////////////

UCameraRigComponent::UCameraRigComponent()
{
    Init();
}

void
UCameraRigComponent::TickComponent(float dt, enum ELevelTick tt, FTickFn* fn)
{
    LOG("%s", "UCameraRigComponent::TickComponent");

    check(this->pawn);
}

////////////////////////////////////////////////////////////////////////////////
/*
 *  Initialize the camera component rig.
 *
 *  We currently support the following post process capture types:
 *    1.  WorldNormal
 *    2.  SceneDepth
 *    3.  PNG
 *    4.  HDR
 */
void
UCameraRigComponent::Init()
{
    LOG("%s", "UCameraRigComponent::Init()");

    // Define a lut to keep track of our modes -> material asset paths.
    static TMap<FString, FString>* lut = nullptr;
    if (lut == nullptr)
    {
        lut = new TMap<FString, FString>();
        lut->Add(T("WorldNormal"), TEXT("/Engine/BufferVisualization/WorldNormal"));
        lut->Add(T("SceneDepth"), TEXT("/Engine/BufferVisualization/SceneDepthWorldUnits"));
        lut->Add(T("PNG"), TEXT(""));
        lut->Add(T("HDR"), TEXT(""));
    }

    if (materials == nullptr)
    {
        materials = new TMap<FString, UMaterial*>();
        for (auto& e : *lut)
        {
            FString key = e.Key;
            FString mp  = e.Value;
            ConstructorHelpers::FObjectFinder<UMaterial> m(*mp);

            if (m.Object != NULL)
            {
                materials->Add(key, (UMaterial*)m.Object);
                LOG("Added material with key: %s", *key);
            }
            else
                LOG("Material with key %s does not exist!", *key);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

UCameraRigComponent*
UCameraRigComponent::Create(APawn* pawn, TArray<FString> modes)
{
    LOG("%s", "UCameraRigComponent::Create()");

    UWorld* w = URCHTTP::Get().GetWorld();

    UCameraRigComponent* rig = NewObject<UCameraRigComponent>();
    rig->pawn = pawn;
    rig->Init();

    return rig;
}