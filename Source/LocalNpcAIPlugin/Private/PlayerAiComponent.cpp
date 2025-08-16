#include "PlayerAiComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"

UPlayerAiComponent::UPlayerAiComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    InteractionSphere->SetupAttachment(this);
    InteractionSphere->InitSphereRadius(InteractionRadius);
    InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    InteractionSphere->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
    InteractionSphere->SetGenerateOverlapEvents(true);
}

void UPlayerAiComponent::BeginPlay()
{
    Super::BeginPlay();

    SetupInput();

    if (InteractionSphere)
    {
        InteractionSphere->SetSphereRadius(InteractionRadius);

        InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &UPlayerAiComponent::OnBeginOverlap);
        InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &UPlayerAiComponent::OnEndOverlap);

        TArray<AActor*> OverlappingActors;
        InteractionSphere->GetOverlappingActors(OverlappingActors);

        for (AActor* Actor : OverlappingActors)
        {
            if (UNpcAiComponent* NpcComp = Actor->FindComponentByClass<UNpcAiComponent>())
            {
                NearbyNpcs.AddUnique(NpcComp);
                UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerAiComponent] NPC already in range at start: %s"), *NpcComp->Name);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerAiComponent] InteractionSphere is not initialized in PlayerAiComponent."));
    }

    if (ChatWidgetClass)
    {
        ChatWidgetInstance = CreateWidget<UChatWidget>(GetWorld(), ChatWidgetClass);
        if (ChatWidgetInstance)
        {
            ChatWidgetInstance->AddToViewport();

			GetWorld()->GetTimerManager().SetTimer(UpdateHintTextTimerHandle, this, &UPlayerAiComponent::UpdateHintText, 0.25f, true, 0.0f);
        }
        else
        {
			UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerAiComponent] Failed to create ChatWidgetInstance in PlayerAiComponent."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerAiComponent] ChatWidgetClass is not set in PlayerAiComponent."));
	}
}

void UPlayerAiComponent::OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
    bool bFromSweep, const FHitResult& SweepResult)
{
    if (!OtherActor || OtherActor == GetOwner())
    {
        return;
    }

    if (UNpcAiComponent* NpcComp = OtherActor->FindComponentByClass<UNpcAiComponent>())
    {
        NearbyNpcs.AddUnique(NpcComp);
        UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerAiComponent] NPC entered interaction range: %s"), *NpcComp->Name);
    }
}

void UPlayerAiComponent::OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor || OtherActor == GetOwner())
    {
        return;
    }

    if (UNpcAiComponent* NpcComp = OtherActor->FindComponentByClass<UNpcAiComponent>())
    {
        if (NearbyNpcs.Remove(NpcComp) > 0)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerAiComponent] NPC left interaction range: %s"), *NpcComp->Name);
		}
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | PlayerAiComponent] NPC not found in nearby list: %s"), *NpcComp->Name);
		}
    }
}

UNpcAiComponent* UPlayerAiComponent::GetClosestNpc()
{
    UNpcAiComponent* ClosestNpc = nullptr;
    float ClosestDistanceSq = TNumericLimits<float>::Max();

    if (!GetOwner())
    {
        return nullptr;
    }

    const FVector PlayerLocation = GetOwner()->GetActorLocation();

    for (UNpcAiComponent* NpcComp : NearbyNpcs)
    {
        if (!IsValid(NpcComp) || !IsValid(NpcComp->GetOwner()))
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(PlayerLocation, NpcComp->GetOwner()->GetActorLocation());
        if (DistSq < ClosestDistanceSq)
        {
            ClosestDistanceSq = DistSq;
            ClosestNpc = NpcComp;
        }
    }

    return ClosestNpc;
}

void UPlayerAiComponent::StartRecording()
{
    if (bIsRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] Already recording."));
        return;
    }

	CurrentRecordingNpc = GetClosestNpc();

    if (!CurrentRecordingNpc)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] No NPCs in range to start recording."));
        return;
    }

    if (!CurrentRecordingNpc->bIsUsersConversationTurn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] Not the user's turn to speak."));
        return;
	}

    if (ChatWidgetInstance)
    {
        FString Hint = FString::Printf(TEXT("Talking to %s..."), *CurrentRecordingNpc->Name);
        ChatWidgetInstance->SetHintText(Hint);
        LastHintText = Hint;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] ChatWidgetInstance is not initialized. Cannot update hint text."));
    }

	bIsRecording = true;

    CurrentRecordingNpc->StartWhisperRecording();
}

void UPlayerAiComponent::StopRecordingAndSendAudio()
{
    if (!bIsRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] Not currently recording."));

        bIsRecording = false;
        CurrentRecordingNpc = nullptr;
        return;
	}

    if (!CurrentRecordingNpc)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] No NPCs currently recording."));

        bIsRecording = false;
        CurrentRecordingNpc = nullptr;
        return;
    }

	bIsRecording = false;

    CurrentRecordingNpc->StopWhisperRecordingAndSendAudio();

    CurrentRecordingNpc = nullptr;
}

void UPlayerAiComponent::SendText(FString Input)
{
    if (!bIsTyping)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] Not currently typing."));

        bIsTyping = false;
        CurrentTypingNpc = nullptr;
        return;
	}

    if (!CurrentTypingNpc)
    {
		UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] Not typing to any NPC."));

        bIsTyping = false;
        CurrentTypingNpc = nullptr;
		return;
    }

    if (Input.IsEmpty())
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | PlayerAiComponent] Empty input text. Skipping send."));

        bIsTyping = false;
        CurrentTypingNpc = nullptr;
        return;
	}

    if (ChatWidgetInstance)
    {
		ChatWidgetInstance->AddMessage(Name, Input);
    }

    bIsTyping = false;

	CurrentTypingNpc->SendText(Input);

	CurrentTypingNpc = nullptr;
}

void UPlayerAiComponent::SetupInput()
{
    if (!GetOwner())
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerAiComponent] Owner is not valid in PlayerAiComponent."));
        return;
	}

    if (APlayerController* PC = Cast<APlayerController>(GetOwner()->GetInstigatorController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (PlayerMappingContext)
            {
                Subsystem->AddMappingContext(PlayerMappingContext, 0);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] PlayerMappingContext is not set in PlayerAiComponent."));
			}
        }
        else
        {
			UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerAiComponent] EnhancedInputLocalPlayerSubsystem not found for PlayerController."));
        }

        if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PC->InputComponent))
        {
            if (PushToTalkAction)
            {
                EIC->BindAction(PushToTalkAction, ETriggerEvent::Started, this, &UPlayerAiComponent::OnPushToTalkStarted);
                EIC->BindAction(PushToTalkAction, ETriggerEvent::Completed, this, &UPlayerAiComponent::OnPushToTalkReleased);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] PushToTalkAction is not set in PlayerAiComponent."));
            }

            if (FocusChatAction)
            {
                EIC->BindAction(FocusChatAction, ETriggerEvent::Started, this, &UPlayerAiComponent::OnFocusChat);
            }
			else
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] FocusChatAction is not set in PlayerAiComponent."));
            }
        }
        else
        {
			UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerAiComponent] EnhancedInputComponent not found for PlayerController."));
        }
    }
    else 
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | PlayerAiComponent] PlayerController is not valid in PlayerAiComponent."));
	}
}

void UPlayerAiComponent::OnPushToTalkStarted(const FInputActionValue& Value)
{
    StartRecording();
}

void UPlayerAiComponent::OnPushToTalkReleased(const FInputActionValue& Value)
{
    StopRecordingAndSendAudio();
}

void UPlayerAiComponent::OnFocusChat(const FInputActionValue& Value)
{
    if (ChatWidgetInstance)
    {
		CurrentTypingNpc = GetClosestNpc();

        if (!CurrentTypingNpc)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] No NPCs in range to type to."));
            return;
        }

        if (!CurrentTypingNpc->bIsUsersConversationTurn)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] Not the user's turn to type."));
            return;
        }

        ChatWidgetInstance->FocusChatInput();
        bIsTyping = true;

        FString Hint = FString::Printf(TEXT("Talking to %s..."), *CurrentTypingNpc->Name);
		ChatWidgetInstance->SetHintText(Hint);
        LastHintText = Hint;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] ChatWidgetInstance is not initialized. Cannot focus chat input."));
	}
}

void UPlayerAiComponent::UpdateHintText()
{
    if (!ChatWidgetInstance)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | PlayerAiComponent] ChatWidgetInstance is not initialized. Cannot update hint text."));
        return;
	}

    if (!bIsTyping && !bIsRecording)
    {
		UNpcAiComponent* ClosestNpc = GetClosestNpc();
        if (ClosestNpc)
		{
            FString Hint;
            if (ClosestNpc->bIsUsersConversationTurn)
            {
                Hint = FString::Printf(TEXT("Hold V to talk or Enter to type to %s"), *ClosestNpc->Name);
            }
            else
            {
                Hint = FString::Printf(TEXT("Talking to %s..."), *ClosestNpc->Name);
			}

            if (Hint != LastHintText)
            {
                ChatWidgetInstance->SetHintText(Hint);
                LastHintText = Hint;
            }
        }
        else
        {
			FString Hint = TEXT("No NPC in range to talk to");
            if (Hint != LastHintText)
            {
                ChatWidgetInstance->SetHintText(Hint);
                LastHintText = Hint;
			}
        }
    }
}