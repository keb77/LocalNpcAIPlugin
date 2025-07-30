#include "KokoroComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

UKokoroComponent::UKokoroComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UKokoroComponent::BeginPlay()
{
    Super::BeginPlay();
    if (OutputAudioFolder.IsEmpty())
    {
        OutputAudioFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KokoroAudio"));
    }
    if (!IFileManager::Get().DirectoryExists(*OutputAudioFolder))
    {
        IFileManager::Get().MakeDirectory(*OutputAudioFolder, true);
    }
}

void UKokoroComponent::CreateSoundWave(const FString& Text)
{
    if (Text.IsEmpty())
    {
		UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Kokoro] Text is empty. Skipping..."));

        AsyncTask(ENamedThreads::GameThread, [this]()
            {
				OnSoundReady.Broadcast({ nullptr, 0.0f });
            });

        return;
    }

    if (Voice.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Kokoro] Voice is not set. Please set a voice before generating audio."));

        AsyncTask(ENamedThreads::GameThread, [this]()
            {
                OnSoundReady.Broadcast({ nullptr, 0.0f });
            });

        return;
	}

    FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Short);
    FString AudioPath = FPaths::Combine(OutputAudioFolder, FString::Printf(TEXT("kokoro-%s.wav"), *Guid));

    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/audio/speech"), Port);
    FString Content = CreateJsonRequest(Text);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb("POST");
    Request->SetHeader("Content-Type", "application/json");
    Request->SetContentAsString(Content);

    double StartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
    int32 LengthBenchmark = Text.Len();

    Request->OnProcessRequestComplete().BindLambda([this, StartTimeBenchmark, LengthBenchmark, AudioPath](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            double EndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
            double DurationBenchmark = EndTimeBenchmark - StartTimeBenchmark;

            if (!bWasSuccessful || !Response.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Request failed."));

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnSoundReady.Broadcast({ nullptr, 0.0f });
                    });

                return;
            }

            int32 Code = Response->GetResponseCode();
            if (Code != 200)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] HTTP %d: %s"), Code, *Response->GetContentAsString());

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnSoundReady.Broadcast({ nullptr, 0.0f });
                    });

                return;
            }

            const TArray<uint8>& AudioData = Response->GetContent();
            if (FFileHelper::SaveArrayToFile(AudioData, *AudioPath))
            {
                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Kokoro] Audio for %d characters generated in %.2f ms. Result saved to %s."), LengthBenchmark, DurationBenchmark, *AudioPath);

				FSoundWaveWithDuration Sound = LoadSoundWaveFromWav(AudioPath);
                AsyncTask(ENamedThreads::GameThread, [this, Sound]()
                    {
                        OnSoundReady.Broadcast(Sound);
                    });
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Kokoro] Failed to save audio to: %s"), *AudioPath);

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnSoundReady.Broadcast({ nullptr, 0.0f });
                    });
            }
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Kokoro] Request sent to %s. Response will be saved to %s"), *Url, *AudioPath);
}

FString UKokoroComponent::CreateJsonRequest(FString Input)
{
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField("input", Input);
    RootObject->SetStringField("voice", Voice);
    RootObject->SetStringField("response_format", TEXT("wav"));
	RootObject->SetNumberField("speed", Speed);
	RootObject->SetNumberField("volume_multiplier", Volume);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    return OutputString;
}

FSoundWaveWithDuration UKokoroComponent::LoadSoundWaveFromWav(FString AudioPath)
{
    TArray<uint8> FileData;

    if (!FFileHelper::LoadFileToArray(FileData, *AudioPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Kokoro] Failed to load file: %s"), *AudioPath);
		return { nullptr, 0.0f };
    }

    FWaveModInfo WaveInfo;
    if (!WaveInfo.ReadWaveInfo(FileData.GetData(), FileData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Kokoro] Failed to parse WAV: %s"), *AudioPath);
		return { nullptr, 0.0f };
    }

    const uint16* Channels = WaveInfo.pChannels;
    const uint32* SampleRate = WaveInfo.pSamplesPerSec;
    const uint16* BitsPerSample = WaveInfo.pBitsPerSample;
    const uint32 ByteRate = (*SampleRate) * (*Channels) * (*BitsPerSample) / 8;
    const uint8* PcmData = WaveInfo.SampleDataStart;
    const int32 PcmDataSize = WaveInfo.SampleDataSize;

    if (!PcmData || PcmDataSize <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Kokoro] No valid PCM data in WAV."));
		return { nullptr, 0.0f };
    }

    USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>();
    SoundWave->SetSampleRate(*SampleRate);
    SoundWave->NumChannels = *Channels;
    SoundWave->SoundGroup = SOUNDGROUP_Default;
    SoundWave->bLooping = false;
	SoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
    SoundWave->QueueAudio(PcmData, PcmDataSize);

	float Duration = static_cast<float>(PcmDataSize) / ByteRate;

	return { SoundWave, Duration };
}

void UKokoroComponent::PlaySoundWave(FSoundWaveWithDuration Sound)
{
    if (!Sound.SoundWave)
    {
		UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Kokoro] SoundWave is null. Skipping..."));
		return;
    }

    FScopeLock Lock(&SoundQueueLock);
    SoundQueue.Enqueue(Sound);

    if (!bIsPlayingSound)
    {
        PlayNextInQueue();
    }
}

void UKokoroComponent::PlayNextInQueue()
{
	FSoundWaveWithDuration NextSound;
	FScopeLock Lock(&SoundQueueLock);
    if (bIsPlayingSound || SoundQueue.IsEmpty() || !SoundQueue.Dequeue(NextSound))
    {
        return;
	}

    bIsPlayingSound = true;

    FVector Location = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
    USoundAttenuation* CustomAttenuation = NewObject<USoundAttenuation>();
    CustomAttenuation->Attenuation.bAttenuate = true;
	CustomAttenuation->Attenuation.bAttenuateWithLPF = true;
    CustomAttenuation->Attenuation.bSpatialize = true;
	CustomAttenuation->Attenuation.AbsorptionMethod = EAirAbsorptionMethod::Linear;
    CustomAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
    CustomAttenuation->Attenuation.FalloffDistance = 1000.f;
    UGameplayStatics::PlaySoundAtLocation(this, NextSound.SoundWave, Location, 1.0f, 1.0f, 0.0f, CustomAttenuation);

    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Kokoro] Playing sound for %.2f seconds."), NextSound.Duration);
	GetWorld()->GetTimerManager().SetTimer(AudioFinishTimer, this, &UKokoroComponent::AudioFinishedHandler, NextSound.Duration, false);
}

void UKokoroComponent::AudioFinishedHandler() 
{
    bIsPlayingSound = false;
    GetWorld()->GetTimerManager().ClearTimer(AudioFinishTimer);

	PlayNextInQueue();
}

void UKokoroComponent::BeginDestroy()
{
    if (!OutputAudioFolder.IsEmpty() && IFileManager::Get().DirectoryExists(*OutputAudioFolder))
    {
        TArray<FString> FilesToDelete;
        IFileManager::Get().FindFiles(FilesToDelete, *OutputAudioFolder, TEXT("*.wav"));

        for (const FString& File : FilesToDelete)
        {
            FString FullPath = FPaths::Combine(OutputAudioFolder, File);
            if (!IFileManager::Get().Delete(*FullPath, false, true))
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Kokoro] Failed to delete file: %s"), *FullPath);
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Kokoro] Cleaned up files in %s"), *OutputAudioFolder);
    }

    Super::BeginDestroy();
}