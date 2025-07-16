#include "WhisperSpeechComponent.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

UWhisperSpeechComponent::UWhisperSpeechComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UWhisperSpeechComponent::BeginPlay()
{
    Super::BeginPlay();

    Audio::FCaptureDeviceInfo DeviceInfo;
    if (!AudioCapture.GetCaptureDeviceInfo(DeviceInfo))
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Failed to get capture device info."));
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] Using capture device: %s"), *DeviceInfo.DeviceName);
    DeviceSampleRate = DeviceInfo.PreferredSampleRate;
    DeviceChannels = DeviceInfo.InputChannels;

    Audio::FAudioCaptureDeviceParams CaptureParams;
    Audio::FOnAudioCaptureFunction CaptureCallback = [this](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverflow)
        {
            const float* AudioBuffer = static_cast<const float*>(InAudio);
            const int32 NumSamples = NumFrames * NumChannels;

            FScopeLock Lock(&AudioDataLock);
            CapturedAudioData.Append(AudioBuffer, NumSamples);
        };
    if (!AudioCapture.OpenAudioCaptureStream(CaptureParams, CaptureCallback, 1024))
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Failed to open audio capture stream."));
        return;
    }
	UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] Audio capture stream opened successfully."));
}

void UWhisperSpeechComponent::StartRecording()
{
    if(!AudioCapture.IsStreamOpen())
    {
		UE_LOG(LogTemp, Warning, TEXT("[WhisperCPP] Stream is not open. Cannot start recording."));
        return;
	}

    if (AudioCapture.IsCapturing())
    {
        UE_LOG(LogTemp, Warning, TEXT("[WhisperCPP] Already recording."));
        return;
	}

    CapturedAudioData.Empty();

    if (!AudioCapture.StartStream())
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Failed to start audio capture stream."));
        return;
	}
    UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] Audio capture stream started."));
}

void UWhisperSpeechComponent::StopRecordingAndTranscribe()
{
	if (!AudioCapture.IsStreamOpen() || !AudioCapture.IsCapturing())
    {
        UE_LOG(LogTemp, Warning, TEXT("[WhisperCPP] Not currently recording."));
        return;
    }

    if(!AudioCapture.StopStream())
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Failed to stop audio capture stream."));
        return;
	}

    UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] Recording stopped. Queuing audio data."));

    {
        FScopeLock Lock(&AudioDataLock);
        AudioDataQueue.Enqueue(CapturedAudioData);
        CapturedAudioData.Empty();
    }

    TryStartNextTranscription();
}

void UWhisperSpeechComponent::TryStartNextTranscription()
{
    if (bIsProcessing || AudioDataQueue.IsEmpty())
    {
        return;
    }

    TArray<float> AudioToProcess;
    if (!AudioDataQueue.Dequeue(AudioToProcess))
    {
        return;
    }

    bIsProcessing = true;

    SaveWavFile(AudioToProcess);
    TranscribeAudio();
}

void UWhisperSpeechComponent::SaveWavFile(const TArray<float>& InAudioData)
{
    FScopeLock Lock(&AudioDataLock);

    if (InAudioData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WhisperCPP] No audio data captured."));
        return;
    }

    int32 SampleRate = DeviceSampleRate;
    int32 NumChannels = DeviceChannels;
    int32 BitsPerSample = 16;

    TArray<uint8> WavData;
    int32 NumSamples = InAudioData.Num();
    int32 DataSize = NumSamples * sizeof(int16);
    int32 FileSize = 44 + DataSize - 8;

    WavData.Append((uint8*)"RIFF", 4);
    WavData.Append((uint8*)&FileSize, 4);
    WavData.Append((uint8*)"WAVE", 4);

    WavData.Append((uint8*)"fmt ", 4);
    uint32 Subchunk1Size = 16;
    uint16 AudioFormat = 1;
    WavData.Append((uint8*)&Subchunk1Size, 4);
    WavData.Append((uint8*)&AudioFormat, 2);
    WavData.Append((uint8*)&NumChannels, 2);
    WavData.Append((uint8*)&SampleRate, 4);

    uint32 ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
    uint16 BlockAlign = NumChannels * BitsPerSample / 8;
    WavData.Append((uint8*)&ByteRate, 4);
    WavData.Append((uint8*)&BlockAlign, 2);
    WavData.Append((uint8*)&BitsPerSample, 2);

    WavData.Append((uint8*)"data", 4);
    WavData.Append((uint8*)&DataSize, 4);

    for (int32 i = 0; i < InAudioData.Num(); i += NumChannels)
    {
        float MonoSample = 0.0f;
        for (int32 c = 0; c < NumChannels; ++c)
            MonoSample += InAudioData[i + c];
        MonoSample /= NumChannels;

        int16 IntSample = static_cast<int16>(FMath::Clamp(MonoSample, -0.999f, 0.999f) * 32767.0f);
        WavData.Append((uint8*)&IntSample, sizeof(int16));
    }

    if (FFileHelper::SaveArrayToFile(WavData, *RecordedAudioPath))
    {
        UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] WAV file saved to: %s"), *RecordedAudioPath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Failed to save WAV file to: %s"), *RecordedAudioPath);
    }
}

void UWhisperSpeechComponent::TranscribeAudio()
{
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalNpcAIPlugin"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Plugin LocalNpcAIPlugin not found!"));
        return;
    }
    FString PluginDir = Plugin->GetBaseDir();
    FString ExePath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("whisper.cpp"), TEXT("whisper-cli.exe"));
    FString ModelPath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("models"), ModelName);

    if (!FPaths::FileExists(ExePath))
    {
		UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Executable file %s does not exist."), *ExePath);
        return;
    }
    if (!FPaths::FileExists(ModelPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Model file %s does not exist."), *ModelPath);
        return;
	}
    if (!FPaths::FileExists(RecordedAudioPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Recorded audio file %s does not exist."), *RecordedAudioPath);
        return;
	}
    if (Threads <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WhisperCPP] Invalid thread count %d, using default value 4."), Threads);
        Threads = 4;
    }

	FString Params = FString::Printf(TEXT("-m \"%s\" -f \"%s\" -otxt -t %d"), *ModelPath, *RecordedAudioPath, Threads);
    UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] Starting whisper-cli with params: %s"), *Params);

    WhisperStartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

    WhisperProcHandle = FPlatformProcess::CreateProc(
        *ExePath,
        *Params,
        true, false, false,
        nullptr, 0,
        nullptr, nullptr
    );

    if (!WhisperProcHandle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Failed to start whisper-cli!"));
        return;
    }
	UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] whisper-cli started successfully."));

    GetWorld()->GetTimerManager().SetTimer(WhisperCheckTimer, this, &UWhisperSpeechComponent::CheckWhisperProcess, 0.1f, true);    
}

void UWhisperSpeechComponent::CheckWhisperProcess()
{
    if (!FPlatformProcess::IsProcRunning(WhisperProcHandle))
    {
        GetWorld()->GetTimerManager().ClearTimer(WhisperCheckTimer);
        FPlatformProcess::CloseProc(WhisperProcHandle);

		FString OutputFilePath = RecordedAudioPath + TEXT(".txt");
        FString TranscribedText;
        if (FFileHelper::LoadFileToString(TranscribedText, *OutputFilePath))
        {
            UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] Transcription completed: %s"), *TranscribedText);
            OnTranscriptionComplete.Broadcast(TranscribedText);

			WhisperEndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
            WhisperLegthBenchmark = TranscribedText.Len();
			UE_LOG(LogTemp, Log, TEXT("[WhisperCPP] Transcription for %d characters completed in %.2f ms"), WhisperLegthBenchmark, WhisperEndTimeBenchmark - WhisperStartTimeBenchmark);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[WhisperCPP] Failed to read transcription output at %s"), *OutputFilePath);
            OnTranscriptionComplete.Broadcast(TEXT(""));
        }

        bIsProcessing = false;
        TryStartNextTranscription();
    }
}

void UWhisperSpeechComponent::BeginDestroy()
{
    if (AudioCapture.IsStreamOpen())
    {
        AudioCapture.AbortStream();
    }
    FPlatformProcess::TerminateProc(WhisperProcHandle);
    FPlatformProcess::CloseProc(WhisperProcHandle);

    Super::BeginDestroy();
}