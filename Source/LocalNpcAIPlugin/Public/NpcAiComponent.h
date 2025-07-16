#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NpcAiComponent.generated.h"

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UNpcAiComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNpcAiComponent();
};