#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "NpcAiComponent.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "ChatWidget.h"
#include "PlayerAiComponent.generated.h"

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UPlayerAiComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UPlayerAiComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc")
	FString Name = TEXT("You");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc", meta = (ClampMin = "0.0"))
    float InteractionRadius = 1000.f;

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc")
    void StartRecording();

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc")
    void StopRecordingAndSendAudio();

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc")
    void SendText(FString Input);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | UI")
    TSubclassOf<UUserWidget> ChatWidgetClass;
    UPROPERTY()
    UChatWidget* ChatWidgetInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Input")
    UInputMappingContext* PlayerMappingContext;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Input")
    UInputAction* PushToTalkAction;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Input")
    UInputAction* FocusChatAction;

private:
    USphereComponent* InteractionSphere;
    UFUNCTION()
    void OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
        bool bFromSweep, const FHitResult& SweepResult);
    UFUNCTION()
    void OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    TArray<UNpcAiComponent*> NearbyNpcs;
    UFUNCTION()
    UNpcAiComponent* GetClosestNpc();

    UNpcAiComponent* CurrentRecordingNpc;
	bool bIsRecording = false;

	UNpcAiComponent* CurrentTypingNpc;
	bool bIsTyping = false;

    void SetupInput();
    void OnPushToTalkStarted(const FInputActionValue& Value);
    void OnPushToTalkReleased(const FInputActionValue& Value);
    void OnFocusChat(const FInputActionValue& Value);

	FTimerHandle UpdateHintTextTimerHandle;
	void UpdateHintText();
	FString LastHintText;

protected:
    virtual void BeginPlay() override;
};