#include "NpcAiComponent.h"

UNpcAiComponent::UNpcAiComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNpcAiComponent::StartWhisperRecording()
{
    if (!WhisperComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] WhisperComponent is not initialized."));
        return;
	}

    if (!bIsUsersConversationTurn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc] Not the user's turn to speak."));
        return;
    }

	bIsUsersConversationTurn = false;

	WhisperComponent->StartRecording();
}

void UNpcAiComponent::StopWhisperRecordingAndSendAudio()
{
    if (!WhisperComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] WhisperComponent is not initialized."));
        return;
    }

    FString AudioPath = WhisperComponent->StopRecording();
	WhisperComponent->TranscribeAudio(AudioPath);
}

void UNpcAiComponent::SendText(FString Input)
{
    if (!LlamaComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] LlamaComponent is not initialized."));
        return;
	}

    if (!bIsUsersConversationTurn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc] Not the user's turn to speak."));
        return;
    }

    bIsUsersConversationTurn = false;

	LlamaComponent->SendChatMessage(Input);
}

void UNpcAiComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();

    WhisperComponent = NewObject<UWhisperComponent>(this, UWhisperComponent::StaticClass(), TEXT("WhisperComponent"));
    if (WhisperComponent)
    {
        WhisperComponent->RegisterComponent();
        WhisperComponent->Port = WhisperPort;

        WhisperComponent->OnTranscriptionComplete.AddDynamic(this, &UNpcAiComponent::HandleWhisperTranscriptionComplete);
    }

    LlamaComponent = NewObject<ULlamaComponent>(this, ULlamaComponent::StaticClass(), TEXT("LlamaComponent"));
    if (LlamaComponent)
    {
        LlamaComponent->RegisterComponent();
        LlamaComponent->Port = LlamaPort;
        LlamaComponent->SystemMessage = SystemMessage;
        LlamaComponent->Temperature = Temperature;
        LlamaComponent->TopP = TopP;
        LlamaComponent->MaxTokens = MaxTokens;
        LlamaComponent->Seed = Seed;
        LlamaComponent->RepeatPenalty = RepeatPenalty;
        LlamaComponent->bStream = bStream;

		LlamaComponent->OnResponseReceived.AddDynamic(this, &UNpcAiComponent::HandleLlamaResponseReceived);
        LlamaComponent->OnChunkReceived.AddDynamic(this, &UNpcAiComponent::HandleLlamaResponseReceived);
    }

    KokoroComponent = NewObject<UKokoroComponent>(this, UKokoroComponent::StaticClass(), TEXT("KokoroComponent"));
    if (KokoroComponent)
    {
        KokoroComponent->RegisterComponent();
        KokoroComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
        KokoroComponent->Port = KokoroPort;
        KokoroComponent->Voice = Voice;
        KokoroComponent->Speed = Speed;
        KokoroComponent->Volume = Volume;

		KokoroComponent->OnSoundReady.AddDynamic(this, &UNpcAiComponent::HandleKokoroSoundReady);
    }

    if (!WhisperComponent || !LlamaComponent || !KokoroComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] Failed to initialize components."));

        bIsUsersConversationTurn = true;

        return;
	}
}

void UNpcAiComponent::HandleWhisperTranscriptionComplete(const FString& Transcription)
{
    if (Transcription.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc] Received empty transcription. Ignoring."));

        bIsUsersConversationTurn = true;

        return;
	}

    if (LlamaComponent)
    {
        LlamaComponent->SendChatMessage(Transcription);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] LlamaComponent is not initialized."));

        bIsUsersConversationTurn = true;
	}
}

void UNpcAiComponent::HandleLlamaResponseReceived(const FString& Response)
{
    if (Response.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc] Received empty response."));

        if(KokoroComponent)
        {
			KokoroComponent->CreateSoundWave(TEXT("I'm sorry, I didn't understand that. Could you please repeat?"));
        }
        else
        {
			UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] KokoroComponent is not initialized."));

			bIsUsersConversationTurn = true;
		}

        return;
    }
    if (KokoroComponent)
    {
        KokoroComponent->CreateSoundWave(Response);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] KokoroComponent is not initialized."));

        bIsUsersConversationTurn = true;
	}
}

void UNpcAiComponent::HandleKokoroSoundReady(FSoundWaveWithDuration SoundWave)
{
    if (!SoundWave.SoundWave)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc] Received empty sound wave. Ignoring."));

        bIsUsersConversationTurn = true;

        return;
    }

	if (KokoroComponent)
    {
        KokoroComponent->PlaySoundWave(SoundWave);

        bIsUsersConversationTurn = true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc] KokoroComponent is not initialized."));

        bIsUsersConversationTurn = true;
	}
}
