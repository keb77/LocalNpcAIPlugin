#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WhisperComponent.h"
#include "LlamaComponent.h"
#include "KokoroComponent.h"
#include "NpcAiComponent.generated.h"

class UPlayerAiComponent;

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UNpcAiComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UNpcAiComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | General")
    FString Name = TEXT("NPC");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Whisper")
    int32 WhisperPort = 8000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | VAD")
    EVadMode VadMode = EVadMode::Disabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | VAD", meta = (EditCondition = "VadMode != EVadMode::Disabled", EditConditionHides, ClampMin = "0.1"))
    float SecondsOfSilenceBeforeSend = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | VAD", meta = (EditCondition = "VadMode != EVadMode::Disabled", EditConditionHides, ClampMin = "0.1"))
    float MinSpeechDuration = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | VAD", meta = (EditCondition = "VadMode == EVadMode::EnergyBased", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
    float EnergyThreshold = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | VAD", meta = (EditCondition = "VadMode == EVadMode::WebRTC", EditConditionHides, ClampMin = "0", ClampMax = "3"))
    int32 WebRtcVadAggressiveness = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    int32 LlamaPort = 8080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    FString SystemMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    float Temperature = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    float TopP = 0.95;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    int32 MaxTokens = 300;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    int32 Seed = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    float RepeatPenalty = 1.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    bool bStream = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG")
    ERagMode RagMode = ERagMode::Disabled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides))
    int32 EmbeddingPort = 8081;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG", meta = (EditCondition = "RagMode == ERagMode::EmbeddingPlusReranker", EditConditionHides))
    int32 RerankerPort = 8082;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides))
    FString KnowledgePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ClampMin = "1"))
    int32 EmbeddingTopK = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG", meta = (EditCondition = "RagMode == ERagMode::EmbeddingPlusReranker", EditConditionHides, ClampMin = "1"))
    int32 RerankingTopN = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ClampMin = "1"))
    int32 SentencesPerChunk = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | RAG", meta = (EditCondition = "RagMode != ERagMode::Disabled", EditConditionHides, ClampMin = "0"))
    int32 SentenceOverlap = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Actions")
    TArray<FNpcAction> KnownActions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Actions")
    TArray<FNpcObject> KnownObjects;

    UPROPERTY(BlueprintAssignable, Category = "LocalAiNpc | Actions")
    FOnLlamaActionReceived OnActionReceived;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    int32 KokoroPort = 8880;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    FString Voice;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    double Speed = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    double Volume = 1.0;
    
	UFUNCTION(BlueprintCallable, Category = "LocalAiNpc")
	void StartWhisperRecording();

	UFUNCTION(BlueprintCallable, Category = "LocalAiNpc")
	void StopWhisperRecordingAndSendAudio();

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc")
    void SendText(FString Input);

    UPROPERTY()
    bool bIsUsersConversationTurn = true;

private:
    UPROPERTY()
    UWhisperComponent* WhisperComponent; 
    UPROPERTY()
    ULlamaComponent* LlamaComponent;
    UPROPERTY()
    UKokoroComponent* KokoroComponent;

    UFUNCTION()
	void HandleWhisperTranscriptionComplete(const FString& Transcription);
	UFUNCTION()
	void HandleLlamaResponseReceived(const FString& Response);
    UFUNCTION()
    void HandleLlamaChunkReceived(const FString& Chunk, bool bDone);
    UFUNCTION()
    void HandleLlamaActionReceived(const FString& Action, AActor* Object);
	UFUNCTION()
    void HandleKokoroSoundReady(FSoundWaveWithDuration SoundWave);

	bool bIsWhisperRecording = false;

	bool bIsFirstChunk = true;

    UPlayerAiComponent* PlayerAiComponent;

protected:
    virtual void BeginPlay() override;
};