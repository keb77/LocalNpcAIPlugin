#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PiperSpeechComponent.generated.h"

// TODO:
// - add input (from llm) stream support (send llm response in chunks) 
// - check if other command options are needed/useful
// - try output streaming to stdout for faster(?) response

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPiperFileReady, USoundWave*, SoundWave);

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
    FOnPiperFileReady OnPiperFileReady;

    UFUNCTION(BlueprintCallable, Category = "PiperTTS")
    USoundWave* LoadSoundWaveFromWav();

private:
    FProcHandle PiperProcHandle;
	FTimerHandle PiperCheckTimer;
    void CheckPiperProcess();

    FString OutputAudioPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PiperAudio"), TEXT("piper-audio.wav"));

	void BeginDestroy() override;
};