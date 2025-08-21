#include "NpcAiComponent.h"
#include "PlayerAiComponent.h"

UNpcAiComponent::UNpcAiComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNpcAiComponent::StartWhisperRecording()
{
    if (!WhisperComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] WhisperComponent is not initialized."));
        return;
	}

    if (!bIsUsersConversationTurn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] Not the user's turn to speak."));
        return;
    }

    if (bIsWhisperRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] Whisper recording is already active."));
		return;
	}

	bIsUsersConversationTurn = false;
	bIsWhisperRecording = true;

	WhisperComponent->StartRecording();
}

void UNpcAiComponent::StopWhisperRecordingAndSendAudio()
{
    if (!WhisperComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] WhisperComponent is not initialized."));
        return;
    }

    if (!bIsWhisperRecording)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] Whisper recording is not active."));
        return;
	}

    FString AudioPath = WhisperComponent->StopRecording();
	WhisperComponent->TranscribeAudio(AudioPath);

    bIsWhisperRecording = false;
}

void UNpcAiComponent::SendText(FString Input)
{
    if (!LlamaComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] LlamaComponent is not initialized."));
        return;
	}

    if (!bIsUsersConversationTurn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] Not the user's turn to speak."));
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
		WhisperComponent->VadMode = VadMode;
		WhisperComponent->SecondsOfSilenceBeforeSend = SecondsOfSilenceBeforeSend;
		WhisperComponent->MinSpeechDuration = MinSpeechDuration;
		WhisperComponent->EnergyThreshold = EnergyThreshold;
		WhisperComponent->WebRtcVadAggressiveness = WebRtcVadAggressiveness;

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

		LlamaComponent->RagMode = RagMode;
        LlamaComponent->EmbeddingPort = EmbeddingPort;
        LlamaComponent->RerankerPort = RerankerPort;
		LlamaComponent->KnowledgePath = KnowledgePath;
		LlamaComponent->EmbeddingTopK = EmbeddingTopK;
		LlamaComponent->RerankingTopN = RerankingTopN;
		LlamaComponent->SentencesPerChunk = SentencesPerChunk;
        LlamaComponent->SentenceOverlap = SentenceOverlap;

		LlamaComponent->KnownActions = KnownActions;
        LlamaComponent->KnownObjects = KnownObjects;
		
		LlamaComponent->OnResponseReceived.AddDynamic(this, &UNpcAiComponent::HandleLlamaResponseReceived);
        LlamaComponent->OnChunkReceived.AddDynamic(this, &UNpcAiComponent::HandleLlamaChunkReceived);
        LlamaComponent->OnActionReceived.AddDynamic(this, &UNpcAiComponent::HandleLlamaActionReceived);
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
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] Failed to initialize components."));

        bIsUsersConversationTurn = true;

        return;
	}

    APawn* PlayerPawn = GetWorld()->GetFirstPlayerController()->GetPawn();
    if (PlayerPawn)
    {
        PlayerAiComponent = PlayerPawn->FindComponentByClass<UPlayerAiComponent>();
    }
}

void UNpcAiComponent::HandleWhisperTranscriptionComplete(const FString& Transcription)
{
    if (Transcription.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] Received empty transcription. Ignoring."));

        bIsUsersConversationTurn = true;
        return;
	}

    if (PlayerAiComponent && PlayerAiComponent->ChatWidgetInstance)
    {
        PlayerAiComponent->ChatWidgetInstance->AddMessage(PlayerAiComponent->Name, Transcription);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] PlayerAiComponent or ChatWidgetInstance not found. Add PlayerAiComponent to Player Pawn to see chat messages."));
    }

    if (LlamaComponent)
    {
        LlamaComponent->SendChatMessage(Transcription);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] LlamaComponent is not initialized."));

        bIsUsersConversationTurn = true;
	}
}

void UNpcAiComponent::HandleLlamaResponseReceived(const FString& Response)
{
    if (Response.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] Received empty response."));

        if(KokoroComponent)
        {
			KokoroComponent->CreateSoundWave(TEXT("I'm sorry, I didn't understand that. Could you please repeat?"));
        }
        else
        {
			UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] KokoroComponent is not initialized."));
		}

        if (PlayerAiComponent && PlayerAiComponent->ChatWidgetInstance)
        {
            PlayerAiComponent->ChatWidgetInstance->AddMessage(Name, TEXT("I'm sorry, I didn't understand that. Could you please repeat?"));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] PlayerAiComponent or ChatWidgetInstance not found. Add PlayerAiComponent to Player Pawn to see chat messages."));
        }

        bIsUsersConversationTurn = true;
        return;
    }

    if (PlayerAiComponent && PlayerAiComponent->ChatWidgetInstance)
    {
        PlayerAiComponent->ChatWidgetInstance->AddMessage(Name, Response);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] PlayerAiComponent or ChatWidgetInstance not found. Add PlayerAiComponent to Player Pawn to see chat messages."));
    }

    if (KokoroComponent)
    {
        KokoroComponent->CreateSoundWave(Response);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] KokoroComponent is not initialized."));
	}

    bIsUsersConversationTurn = true;
}

void UNpcAiComponent::HandleLlamaChunkReceived(const FString& Chunk, bool bDone)
{
    if (PlayerAiComponent && PlayerAiComponent->ChatWidgetInstance)
    {
        if (bIsFirstChunk)
        {
            PlayerAiComponent->ChatWidgetInstance->AddMessage(Name, Chunk);
            bIsFirstChunk = false;

			UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | NpcAiComponent] First chunk received: %s"), *Chunk);
        }
        else if (bDone)
        {
            PlayerAiComponent->ChatWidgetInstance->RemoveLastMessage();
            bIsFirstChunk = true;

			UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | NpcAiComponent] Last chunk received: %s"), *Chunk);
        }
        else
        {
            PlayerAiComponent->ChatWidgetInstance->AppendToLastMessage(Chunk);

			UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | NpcAiComponent] Chunk received: %s"), *Chunk);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] PlayerAiComponent or ChatWidgetInstance not found. Add PlayerAiComponent to Player Pawn to see chat messages."));
    }

    if (KokoroComponent)
    {
        KokoroComponent->CreateSoundWave(Chunk);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] KokoroComponent is not initialized."));
    }
}

void UNpcAiComponent::HandleLlamaActionReceived(const FString& Action, AActor* Object)
{
    OnActionReceived.Broadcast(Action, Object);
}

void UNpcAiComponent::HandleKokoroSoundReady(FSoundWaveWithDuration SoundWave)
{
    if (!SoundWave.SoundWave)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | NpcAiComponent] Received empty sound wave. Ignoring."));

        return;
    }

	if (KokoroComponent)
    {
        KokoroComponent->PlaySoundWave(SoundWave);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | NpcAiComponent] KokoroComponent is not initialized."));
	}
}
