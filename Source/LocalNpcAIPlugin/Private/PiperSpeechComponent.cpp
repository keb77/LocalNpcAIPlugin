#include "PiperSpeechComponent.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

UPiperSpeechComponent::UPiperSpeechComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPiperSpeechComponent::SpeakText(const FString& Text)
{
    if (bProcessing)
    {
        TextQueue.Enqueue(Text);
        return;
    }

    bProcessing = true;
    StartPiperProcess(Text);
}

void UPiperSpeechComponent::StartPiperProcess(const FString& Text)
{
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalNpcAIPlugin"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Plugin LocalNpcAIPlugin not found!"));
        bProcessing = false;
        return;
    }
    FString PluginDir = Plugin->GetBaseDir();
    FString ExePath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("piper"), TEXT("run_piper.exe"));
    FString ModelPath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("models"), ModelName);

    if (!FPaths::FileExists(ExePath))
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Exe file %s does not exist."), *ExePath);
        bProcessing = false;
        return;
    }
    if (!FPaths::FileExists(ModelPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Model file %s does not exist."), *ModelPath);
        bProcessing = false;
        return;
    }
    
    if (!FPaths::DirectoryExists(FPaths::GetPath(OutputAudioPath)))
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(OutputAudioPath));
	}

    FString Args = FString::Printf(TEXT("\"%s\" \"%s\" \"%s\""), *Text, *ModelPath, *OutputAudioPath);
	UE_LOG(LogTemp, Log, TEXT("[PiperTTS] Starting run_piper.exe with args: %s"), *Args);

    PiperStartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
    PiperLengthBenchmark = Text.Len();

    PiperProcHandle = FPlatformProcess::CreateProc(
        *ExePath,
        *Args,
        true, true, true,
        nullptr, 0,
        nullptr, nullptr
    );

    if (!PiperProcHandle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Failed to start piper!"));
        bProcessing = false;
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("[PiperTTS] piper started successfully."));

    GetWorld()->GetTimerManager().SetTimer(PiperCheckTimer, this, &UPiperSpeechComponent::CheckPiperProcess, 0.1f, true);
}

void UPiperSpeechComponent::CheckPiperProcess()
{
    if (!FPlatformProcess::IsProcRunning(PiperProcHandle))
    {
        GetWorld()->GetTimerManager().ClearTimer(PiperCheckTimer);
        FPlatformProcess::CloseProc(PiperProcHandle);

		USoundWave* SoundWave = LoadSoundWaveFromWav();
		OnPiperSoundReady.Broadcast(SoundWave);

		UE_LOG(LogTemp, Log, TEXT("[PiperTTS] Piper process completed. SoundWave ready."));

        PiperEndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
		UE_LOG(LogTemp, Log, TEXT("[PiperTTS] SoundWave generated for %d characters in %.2f ms."), PiperLengthBenchmark, PiperEndTimeBenchmark - PiperStartTimeBenchmark);

        bProcessing = false;
        FString NextText;
        if (TextQueue.Dequeue(NextText))
        {
            SpeakText(NextText);
        }
    }
}

USoundWave* UPiperSpeechComponent::LoadSoundWaveFromWav()
{
    TArray<uint8> FileData;

    if (!FFileHelper::LoadFileToArray(FileData, *OutputAudioPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Failed to load file: %s"), *OutputAudioPath);
        return nullptr;
    }

    FWaveModInfo WaveInfo;
    if (!WaveInfo.ReadWaveInfo(FileData.GetData(), FileData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Failed to parse WAV: %s"), *OutputAudioPath);
        return nullptr;
    }

    const uint16* Channels = WaveInfo.pChannels;
    const uint32* SampleRate = WaveInfo.pSamplesPerSec;
    const uint16* BitsPerSample = WaveInfo.pBitsPerSample;
    const uint32 ByteRate = (*SampleRate) * (*Channels) * (*BitsPerSample) / 8;
    const uint8* PcmData = WaveInfo.SampleDataStart;
    const int32 PcmDataSize = WaveInfo.SampleDataSize;

    if (!PcmData || PcmDataSize <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] No valid PCM data in WAV."));
        return nullptr;
    }

    USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>();
    SoundWave->SetSampleRate(*SampleRate);
    SoundWave->NumChannels = *Channels;
    SoundWave->SoundGroup = SOUNDGROUP_Default;
    SoundWave->bLooping = false;
	SoundWave->Duration = static_cast<float>(PcmDataSize) / ByteRate;
    SoundWave->QueueAudio(PcmData, PcmDataSize);

    return SoundWave;
}

void UPiperSpeechComponent::PlaySound(USoundWave* Sound)
{
    if (!Sound) 
    {
		UE_LOG(LogTemp, Warning, TEXT("[PiperTTS] SoundWave is null. Skipping..."));
		return;
    }

    SoundQueue.Add(Sound);

    if (!bIsPlayingSound)
    {
        PlayNextInQueue();
    }
}

void UPiperSpeechComponent::PlayNextInQueue()
{
    if (SoundQueue.Num() == 0)
    {
        bIsPlayingSound = false;
        return;
    }

    bIsPlayingSound = true;
    USoundWave* NextSound = SoundQueue[0];
    SoundQueue.RemoveAt(0);

	const float Duration = NextSound->GetDuration();

    if (!AudioComponent)
    {
        AudioComponent = NewObject<UAudioComponent>(this);
        AudioComponent->bAutoActivate = false;
        AudioComponent->bIsUISound = false;
        AudioComponent->RegisterComponent();
    }

    AudioComponent->SetSound(NextSound);
    AudioComponent->Play(0.0f);

    GetWorld()->GetTimerManager().ClearTimer(AudioFinishTimer);

    if (Duration > 0.0f)
    {
        UE_LOG(LogTemp, Log, TEXT("[PiperTTS] Playing sound for %.2f seconds."), Duration);
		GetWorld()->GetTimerManager().SetTimer(AudioFinishTimer, this, &UPiperSpeechComponent::PlayNextInQueue, Duration + 0.1f, false);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[PiperTTS] Sound duration is zero or negative. Skipping audio finished callback."));
        PlayNextInQueue();
	}
}

void UPiperSpeechComponent::BeginDestroy()
{
    if (PiperProcHandle.IsValid() && FPlatformProcess::IsProcRunning(PiperProcHandle))
    {
        FPlatformProcess::TerminateProc(PiperProcHandle);
    }
    FPlatformProcess::CloseProc(PiperProcHandle);

    Super::BeginDestroy();
}