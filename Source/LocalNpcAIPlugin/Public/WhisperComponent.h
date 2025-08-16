#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "WhisperComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWhisperTranscriptionComplete, const FString&, Transcription);

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UWhisperComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UWhisperComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAiNpc | Whisper")
    int32 Port = 8000;

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc | Whisper")
	void StartRecording();

	UFUNCTION(BlueprintCallable, Category = "LocalAiNpc | Whisper")
	FString StopRecording();

    UFUNCTION(BlueprintCallable, Category = "LocalAiNpc | Whisper")
    void TranscribeAudio(const FString& AudioPath);

    UPROPERTY(BlueprintAssignable, Category = "LocalAiNpc | Whisper")
    FOnWhisperTranscriptionComplete OnTranscriptionComplete;

private:
    FString RecordedAudioFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WhisperAudio"));

	Audio::FAudioCapture AudioCapture;
    int32 DeviceSampleRate;
    int32 DeviceChannels;

    TArray<float> CapturedAudioData;
	FCriticalSection AudioDataLock;
    
    void SaveWavFile(const TArray<float>& InAudioData, FString OutputPath);

    TArray<uint8> CreateMultiPartRequest(FString FilePath);
    FString CurrentBoundary;

	FString SanitizeString(const FString& String);

	void BeginPlay() override;
    void BeginDestroy() override;
};