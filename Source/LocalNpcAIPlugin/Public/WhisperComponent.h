#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureCore.h"
extern "C" {
    #include "fvad.h"
}
#include "WhisperComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWhisperTranscriptionComplete, const FString&, Transcription);

UENUM(BlueprintType)
enum class EVadMode : uint8
{
    Disabled               UMETA(DisplayName = "Disabled"),
    EnergyBased              UMETA(DisplayName = "Energy-based"),
    WebRTC  UMETA(DisplayName = "WebRTC")
};

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

private:
    FString RecordedAudioFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WhisperAudio"));

	Audio::FAudioCapture AudioCapture;
    int32 DeviceSampleRate;
    int32 DeviceChannels;

    TArray<float> CapturedAudioData;
	FCriticalSection AudioDataLock;
    
    void SaveWavFile(const TArray<float>& InAudioData, FString OutputPath) const;

    TArray<uint8> CreateMultiPartRequest(FString FilePath);
    FString CurrentBoundary;

	FString SanitizeString(const FString& String);


    bool IsSpeechFrame(const float* Samples, int32 NumSamples, int32 SampleRate);
    Fvad* VadInstance = nullptr;
    TArray<int16> VadInputBuffer;
    Audio::VectorOps::FAlignedFloatBuffer ResampledBuffer;
	const int32 WebRtcVadSampleRate = 16000;
	int32 SilenceSamplesCount = 0;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};