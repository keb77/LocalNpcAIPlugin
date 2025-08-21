#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LlamaComponent.generated.h"

USTRUCT()
struct FChatMessage
{
    GENERATED_BODY()
    UPROPERTY()
    FString Role;
    UPROPERTY()
    FString Content;
};

UENUM(BlueprintType)
enum class ERagMode : uint8
{
    Disabled               UMETA(DisplayName = "Disabled"),
    Embedding              UMETA(DisplayName = "Embedding"),
    EmbeddingPlusReranker  UMETA(DisplayName = "Embedding + Reranker")
};

USTRUCT()
struct FKnowledgeEntry
{
    GENERATED_BODY()
    UPROPERTY() 
    FString Text;
    UPROPERTY() 
    TArray<float> Embedding;
};

USTRUCT(BlueprintType)
struct FNpcAction
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHasTargetObject = false;
};

USTRUCT(BlueprintType)
struct FNpcObject
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    AActor* ActorRef;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLlamaResponseReceived, const FString&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLlamaStreamTokenReceived, const FString&, Token, bool, bDone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLlamaChunkReceived, const FString&, Chunk, bool, bDone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLlamaActionReceived, const FString&, Action, AActor*, Object);

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
    int32 MaxTokens = 300;

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

private:
    TArray<FChatMessage> ChatHistory;

    void SendRequest(FString InSystemMessage);
    void SendRequestStreaming(FString InSystemMessage);
    FString CreateJsonRequest(FString InSystemMessage);

    UFUNCTION()
    void HandleStreamChunk(const FString& PartialText, bool bDone);
    FString AccumulatedChunk;
    FCriticalSection ChunkMutex;

    FString SanitizeString(const FString& String);

    double ChunkStartTimeBenchmark = 0.0;

    TArray<FKnowledgeEntry> Knowledge;
    TArray<float> EmbedText(const FString& Text);
	void GenerateKnowledge();
    float ComputeCosineSimilarity(const TArray<float>& A, const TArray<float>& B);
    TArray<FString> GetTopKDocuments(const TArray<float>& QueryEmbedding);
    TArray<FString> RerankDocuments(const FString& Query, const TArray<FString>& Documents);

	void HandleNpcAction(const FString& ActionCommand);
	FString BuildActionsSystemMessage();

protected:
	virtual void BeginPlay() override;
};