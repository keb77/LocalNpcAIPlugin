#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "KokoroComponent.generated.h"

USTRUCT(BlueprintType)
struct FSoundWaveWithDuration
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    USoundWave* SoundWave;

    UPROPERTY(BlueprintReadWrite)
    float Duration;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKokoroSoundReady, FSoundWaveWithDuration, SoundWave);

UCLASS(ClassGroup = (NpcAI), meta = (BlueprintSpawnableComponent))
class LOCALNPCAIPLUGIN_API UKokoroComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UKokoroComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    int32 Port = 8880;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    FString Voice;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    double Speed = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LocalAINpc | Kokoro")
    double Volume = 1.0;

    UFUNCTION(BlueprintCallable, Category = "LocalAINpc | Kokoro")
	void CreateSoundWave(const FString& Text);

    UPROPERTY(BlueprintAssignable, Category = "LocalAINpc | Kokoro")
    FOnKokoroSoundReady OnSoundReady;

    UFUNCTION(BlueprintCallable, Category = "LocalAINpc | Kokoro")
    void PlaySoundWave(FSoundWaveWithDuration Sound);

private:
    FString OutputAudioFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KokoroAudio"));

    FString CreateJsonRequest(FString Input);

    FSoundWaveWithDuration LoadSoundWaveFromWav(FString AudioPath);

    TQueue<FSoundWaveWithDuration> SoundQueue;
    FCriticalSection SoundQueueLock;
    bool bIsPlayingSound = false;
    FTimerHandle AudioFinishTimer;
    UFUNCTION()
    void PlayNextInQueue();
	UFUNCTION()
	void AudioFinishedHandler();

	void BeginPlay() override;
	void BeginDestroy() override;
};