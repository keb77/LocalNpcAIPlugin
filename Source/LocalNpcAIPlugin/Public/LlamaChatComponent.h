#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LlamaChatComponent.generated.h"

// TODO:
// - check if [Memory] possible (use model to summarize chat history and add it to the prompt)
// - check if RAG possible

USTRUCT(BlueprintType)
struct FChatMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FString Role;

    UPROPERTY(BlueprintReadWrite)
    FString Content;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaResponseReceived, const FString&, ResponseContent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLlamaStreamTokenReceived, const FString&, Token, bool, bDone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaChunkReceived, const FString&, Chunk);

UCLASS(ClassGroup = (LlamaCPP), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API ULlamaChatComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    ULlamaChatComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    FString ModelName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    FString SystemMessage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    float Temperature = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    float TopP = 0.95;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    int32 MaxTokens = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    int32 Seed = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    float RepeatPenalty = 1.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCPP")
    bool bStream = false;

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    void SendChatRequest(FString Message);

    UPROPERTY(BlueprintAssignable, Category = "LlamaCPP")
    FOnLlamaResponseReceived OnResponseReceived;

    UPROPERTY(BlueprintAssignable, Category = "LlamaCPP")
    FOnLlamaStreamTokenReceived OnStreamTokenReceived;

    UPROPERTY(BlueprintAssignable, Category = "LlamaCPP")
    FOnLlamaChunkReceived OnChunkReceived;

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    void ClearChatHistory();

private:
    TArray<FChatMessage> ChatHistory;

    UFUNCTION()
    void HandleStreamChunk(const FString& PartialText, bool bDone);
    FString AccumulatedChunk;
    FCriticalSection ChunkMutex;

    bool bProcessing = false;
    void StartLlamaProcess(const FString& Message);

    FString SanitizeString(const FString& String);

	double LlamaStartTimeBenchmark = 0.0;
	double LlamaEndTimeBenchmark = 0.0;
    int32 LlamaLengthBenchmark = 0;
};