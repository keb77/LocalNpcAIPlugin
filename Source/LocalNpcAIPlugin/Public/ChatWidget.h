#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChatWidget.generated.h"

UCLASS()
class LOCALNPCAIPLUGIN_API UChatWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintImplementableEvent, Category = "LocalAiNpc")
    void AddMessage(const FString& Name, const FString& MessageText);

    UFUNCTION(BlueprintImplementableEvent, Category = "LocalAiNpc")
    void AppendToLastMessage(const FString& Text);

    UFUNCTION(BlueprintImplementableEvent, Category = "LocalAiNpc")
    void RemoveLastMessage();

	UFUNCTION(BlueprintImplementableEvent, Category = "LocalAiNpc")
    void SetHintText(const FString& HintText);

    UFUNCTION(BlueprintImplementableEvent, Category = "LocalAiNpc")
    void FocusChatInput();

	UFUNCTION(BlueprintCallable, Category = "LocalAiNpc")
    void SendMessage(const FString& MessageText);
};