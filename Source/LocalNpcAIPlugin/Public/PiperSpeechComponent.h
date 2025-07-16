#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PiperSpeechComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPiperSoundReady, USoundWave*, SoundWave);

UCLASS(ClassGroup = (PiperTTS), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UPiperSpeechComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPiperSpeechComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PiperTTS")
    FString ModelName;

    UFUNCTION(BlueprintCallable, Category = "PiperTTS")
	void SpeakText(const FString& Text);

    UPROPERTY(BlueprintAssignable, Category = "PiperTTS")
    FOnPiperSoundReady OnPiperSoundReady;

    UFUNCTION(BlueprintCallable, Category = "PiperTTS")
    void PlaySound(USoundWave* Sound);

private:
    TQueue<FString> TextQueue;
    bool bProcessing = false;
	void StartPiperProcess(const FString& Text);

    FProcHandle PiperProcHandle;
	FTimerHandle PiperCheckTimer;
    void CheckPiperProcess();
    USoundWave* LoadSoundWaveFromWav();

    FString OutputAudioPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PiperAudio"), TEXT("piper-audio.wav"));

    UAudioComponent* AudioComponent = nullptr;

    TArray<USoundWave*> SoundQueue;
    bool bIsPlayingSound = false;
    UFUNCTION()
	void PlayNextInQueue();
    FTimerHandle AudioFinishTimer;

	void BeginDestroy() override;

    double PiperStartTimeBenchmark = 0.0;
    double PiperEndTimeBenchmark = 0.0;
	int32 PiperLengthBenchmark = 0;
};