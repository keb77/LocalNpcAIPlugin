#include "PiperSpeechComponent.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"

UPiperSpeechComponent::UPiperSpeechComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UPiperSpeechComponent::SpeakText(const FString& Text)
{
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalNpcAIPlugin"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Plugin LocalNpcAIPlugin not found!"));
        return;
    }
    FString PluginDir = Plugin->GetBaseDir();
    FString ExePath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("piper"), TEXT("run_piper.exe"));
    FString ModelPath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("models"), ModelName);

    if (!FPaths::FileExists(ExePath))
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Exe file %s does not exist."), *ExePath);
        return;
    }
    if (!FPaths::FileExists(ModelPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] Model file %s does not exist."), *ModelPath);
        return;
    }
    
    if (!FPaths::DirectoryExists(FPaths::GetPath(OutputAudioPath)))
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(OutputAudioPath));
	}

    FString Args = FString::Printf(TEXT("\"%s\" \"%s\" \"%s\""), *Text, *ModelPath, *OutputAudioPath);
	UE_LOG(LogTemp, Log, TEXT("[PiperTTS] Starting run_piper.exe with args: %s"), *Args);

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
		OnPiperFileReady.Broadcast(SoundWave);

		UE_LOG(LogTemp, Log, TEXT("[PiperTTS] Piper process completed. SoundWave ready."));

        //IFileManager::Get().Delete(*OutputAudioPath);
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

    const uint8* PcmData = WaveInfo.SampleDataStart;
    int32 PcmDataSize = WaveInfo.SampleDataSize;

    if (!PcmData || PcmDataSize <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[PiperTTS] No valid PCM data in WAV."));
        return nullptr;
    }

    USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>();
    SoundWave->SetSampleRate(*SampleRate);
    SoundWave->NumChannels = *Channels;
    SoundWave->SoundGroup = SOUNDGROUP_Default;
    SoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
    SoundWave->bLooping = false;

    SoundWave->QueueAudio(PcmData, PcmDataSize);

    return SoundWave;
}

void UPiperSpeechComponent::BeginDestroy()
{
    FPlatformProcess::TerminateProc(PiperProcHandle);
    FPlatformProcess::CloseProc(PiperProcHandle);

    Super::BeginDestroy();
}