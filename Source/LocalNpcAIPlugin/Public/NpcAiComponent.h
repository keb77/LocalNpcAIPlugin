#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WhisperComponent.h"
#include "LlamaComponent.h"
#include "KokoroComponent.h"
#include "NpcAiComponent.generated.h"

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UNpcAiComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UNpcAiComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Whisper")
    int32 WhisperPort = 8000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Llama")
    int32 LlamaPort = 8080;

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
    void HandleKokoroSoundReady(FSoundWaveWithDuration SoundWave);

	bool bIsUsersConversationTurn = true;

    void BeginPlay() override;
};