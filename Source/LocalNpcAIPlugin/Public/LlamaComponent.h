#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LlamaComponent.generated.h"

USTRUCT(BlueprintType)
struct FChatMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FString Role;

    UPROPERTY(BlueprintReadWrite)
    FString Content;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaResponseReceived, const FString&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLlamaStreamTokenReceived, const FString&, Token, bool, bDone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLlamaChunkReceived, const FString&, Chunk, bool, bDone);

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API ULlamaComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULlamaComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    int32 Port = 8080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    FString SystemMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    float Temperature = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    float TopP = 0.95;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    int32 MaxTokens = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    int32 Seed = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    float RepeatPenalty = 1.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    bool bStream = false;

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc | Llama")
    void SendChatMessage(FString Message);

    UPROPERTY(BlueprintAssignable, Category = "LocalAiNpc | Llama")
    FOnLlamaResponseReceived OnResponseReceived;

    UPROPERTY(BlueprintAssignable, Category = "LocalAiNpc | Llama")
    FOnLlamaStreamTokenReceived OnStreamTokenReceived;

    UPROPERTY(BlueprintAssignable, Category = "LocalAiNpc | Llama")
    FOnLlamaChunkReceived OnChunkReceived;

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc | Llama")
    void ClearChatHistory();

private:
    TArray<FChatMessage> ChatHistory;

	FString CreateJsonRequest();

    UFUNCTION()
    void HandleStreamChunk(const FString& PartialText, bool bDone);
    FString AccumulatedChunk;
    FCriticalSection ChunkMutex;

    FString SanitizeString(const FString& String);

	double ChunkStartTimeBenchmark = 0.0;
};