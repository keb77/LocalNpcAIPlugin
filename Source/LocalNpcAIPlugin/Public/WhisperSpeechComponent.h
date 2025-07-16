#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
#include "WhisperSpeechComponent.generated.h"

// TODO:
// - try whisper-stream for faster(?) transcription
// - check if VAD possible

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWhisperTranscriptComplete, const FString&, Transcription);

UCLASS(ClassGroup = (WhisperCPP), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UWhisperSpeechComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UWhisperSpeechComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WhisperCPP")
    FString ModelName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WhisperCPP")
	int32 Threads = 4;

    UFUNCTION(BlueprintCallable, Category = "WhisperCPP")
	void StartRecording();

	UFUNCTION(BlueprintCallable, Category = "WhisperCPP")
	void StopRecordingAndTranscribe();

    UPROPERTY(BlueprintAssignable, Category = "WhisperCPP")
    FOnWhisperTranscriptComplete OnTranscriptionComplete;

private:
    FString RecordedAudioPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WhisperAudio"), TEXT("whisper-audio.wav"));

    FProcHandle WhisperProcHandle;
    FTimerHandle WhisperCheckTimer;

	Audio::FAudioCapture AudioCapture;
    int32 DeviceSampleRate;
    int32 DeviceChannels;
    TArray<float> CapturedAudioData;
	FCriticalSection AudioDataLock;

    TQueue<TArray<float>> AudioDataQueue;
    bool bIsProcessing = false;
    
    void SaveWavFile(const TArray<float>& InAudioData);
    void TranscribeAudio();
    void CheckWhisperProcess();
    void TryStartNextTranscription();

	void BeginPlay() override;
    void BeginDestroy() override;

    double WhisperStartTimeBenchmark = 0.0;
    double WhisperEndTimeBenchmark = 0.0;
    int32 WhisperLegthBenchmark = 0;
};