#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LlamaChatComponent.generated.h"

// TODO: 
// - add stream support
// - check if other prompt parameters are needed/useful
// - sanitize input and output strings (e.g. remove [action], *action*, ...)
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
    int32 MaxTokens = 200;

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    void SendChatRequest(FString Message);

    UPROPERTY(BlueprintAssignable, Category = "LlamaCPP")
    FOnLlamaResponseReceived OnResponseReceived;

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    void ClearChatHistory();

private:
    TArray<FChatMessage> ChatHistory;
};