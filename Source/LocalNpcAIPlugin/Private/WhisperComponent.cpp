#include "WhisperComponent.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "AudioResampler.h"

UWhisperComponent::UWhisperComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UWhisperComponent::BeginPlay()
{
    Super::BeginPlay();

    if (RecordedAudioFolder.IsEmpty())
    {
        RecordedAudioFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WhisperAudio"));
    }
    if (!IFileManager::Get().DirectoryExists(*RecordedAudioFolder))
    {
        IFileManager::Get().MakeDirectory(*RecordedAudioFolder, true);
    }

    Audio::FCaptureDeviceInfo DeviceInfo;
    if (!AudioCapture.GetCaptureDeviceInfo(DeviceInfo))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Failed to get capture device info."));
        return;
    }
    DeviceSampleRate = DeviceInfo.PreferredSampleRate;
    DeviceChannels = DeviceInfo.InputChannels;

    Audio::FAudioCaptureDeviceParams CaptureParams;
    Audio::FOnAudioCaptureFunction CaptureCallback = [this](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverflow)
        {
            const float* AudioBuffer = static_cast<const float*>(InAudio);
            const int32 NumSamples = NumFrames * NumChannels;

            TArray<float> MonoBuffer;
            MonoBuffer.SetNumUninitialized(NumFrames);
            for (int32 i = 0; i < NumFrames; i++)
            {
                float Sum = 0.f;
                for (int32 c = 0; c < NumChannels; c++)
                {
                    Sum += AudioBuffer[i * NumChannels + c];
                }
                MonoBuffer[i] = Sum / NumChannels;
            }

            FScopeLock Lock(&AudioDataLock);

            if (IsSpeechFrame(MonoBuffer.GetData(), MonoBuffer.Num(), SampleRate))
            {
                CapturedAudioData.Append(MonoBuffer.GetData(), MonoBuffer.Num());
                SilenceSamplesCount = 0;
            }
            else if (CapturedAudioData.Num() > 0)
            {
                SilenceSamplesCount += MonoBuffer.Num();
                CapturedAudioData.Append(MonoBuffer.GetData(), MonoBuffer.Num());

                if (SilenceSamplesCount >= SecondsOfSilenceBeforeSend * SampleRate)
                {
                    if (CapturedAudioData.Num() >= MinSpeechDuration * SampleRate)
                    {
                        TArray<float> AudioToSave;
                        AudioToSave = CapturedAudioData;
                        CapturedAudioData.Empty();
                        SilenceSamplesCount = 0;

                        FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Short);
                        FString AudioPath = FPaths::Combine(RecordedAudioFolder, FString::Printf(TEXT("whisper-%s.wav"), *Guid));

                        SaveWavFile(AudioToSave, AudioPath);
						TranscribeAudio(AudioPath);
                    }
                    else
                    {
                        CapturedAudioData.Empty();
                        SilenceSamplesCount = 0;
                    }
                }
            }
        };

    if (!AudioCapture.OpenAudioCaptureStream(CaptureParams, CaptureCallback, 1024))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Failed to open audio capture stream."));
        return;
    }

	UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] Audio capture stream opened successfully on device %s"), *DeviceInfo.DeviceName);

    
    if (VadMode == EVadMode::WebRTC)
    {
        VadInstance = fvad_new();
        if (VadInstance)
        {
            fvad_set_mode(VadInstance, WebRtcVadAggressiveness);

            if (fvad_set_sample_rate(VadInstance, WebRtcVadSampleRate) < 0)
            {
                UE_LOG(LogTemp, Error, TEXT("[WebRTC VAD] Failed to set sample rate!"));
            }

            UE_LOG(LogTemp, Log, TEXT("[WebRTC VAD] Initialized with Aggressiveness=%d"), WebRtcVadAggressiveness);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[WebRTC VAD] Failed to create VAD instance!"));
        }
    }

    if (VadMode != EVadMode::Disabled)
    {
        StartRecording();
    }
}

void UWhisperComponent::StartRecording()
{
    if(!AudioCapture.IsStreamOpen())
    {
		UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Whisper] Stream is not open. Cannot start recording."));
        return;
	}

    if (AudioCapture.IsCapturing())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Whisper] Already recording."));
        return;
	}

    {
        FScopeLock Lock(&AudioDataLock);
        CapturedAudioData.Empty();
    }

    if (!AudioCapture.StartStream())
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Failed to start audio capture stream."));
        return;
	}

    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] Audio capture stream started."));
}

FString UWhisperComponent::StopRecording()
{
    if (VadMode != EVadMode::Disabled)
    {
		UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Whisper] VAD is enabled. Recording will stop automatically when speech is detected."));
		return TEXT("");
    }

	if (!AudioCapture.IsStreamOpen() || !AudioCapture.IsCapturing())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Whisper] Not currently recording."));
        return TEXT("");
    }

    if(!AudioCapture.StopStream())
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Failed to stop audio capture stream."));
        return TEXT("");
	}

    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] Recording stopped. Saving WAV file..."));

    TArray<float> AudioToSave;
    {
        FScopeLock Lock(&AudioDataLock);
        AudioToSave = CapturedAudioData;
        CapturedAudioData.Empty();
    }

    FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Short);
    FString AudioPath = FPaths::Combine(RecordedAudioFolder, FString::Printf(TEXT("whisper-%s.wav"), *Guid));
    
	SaveWavFile(AudioToSave, AudioPath);

	return AudioPath;
}

void UWhisperComponent::SaveWavFile(const TArray<float>& InAudioData, FString OutputPath) const
{
    if (InAudioData.Num() == 0)
    {
		UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Whisper] No audio data provided to save as WAV."));
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

    if (FFileHelper::SaveArrayToFile(WavData, *OutputPath))
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] WAV file saved to: %s"), *OutputPath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Failed to save WAV file to: %s"), *OutputPath);
    }
}

void UWhisperComponent::TranscribeAudio(const FString& AudioPath)
{
    if (!FPaths::FileExists(AudioPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Audio file not found: %s"), *AudioPath);

        OnTranscriptionComplete.Broadcast(TEXT(""));

        return;
    }

    FString Url = FString::Printf(TEXT("http://localhost:%d/inference"), Port);
	TArray<uint8> Content = CreateMultiPartRequest(AudioPath);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb("POST");
	Request->SetHeader("Content-Type", "multipart/form-data; boundary=" + CurrentBoundary);
    Request->SetContent(Content);

    double StartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

    Request->OnProcessRequestComplete().BindLambda([this, StartTimeBenchmark](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            double EndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
            double DurationBenchmark = EndTimeBenchmark - StartTimeBenchmark;

            if (!bWasSuccessful || !Response.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] Request failed."));

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnTranscriptionComplete.Broadcast(TEXT(""));
                    });

                return;
            }

            int32 Code = Response->GetResponseCode();
            if (Code != 200)
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Whisper] HTTP %d: %s"), Code, *Response->GetContentAsString());

                AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                        OnTranscriptionComplete.Broadcast(TEXT(""));
                    });

                return;
            }

            FString ResultText = Response->GetContentAsString();
			int32 LengthBenchmark = ResultText.Len();
			UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] Transcription completed in %.2f ms, %d characters received."), DurationBenchmark, LengthBenchmark);
            UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] Result: %s"), *ResultText);

			FString SanitizedResult = SanitizeString(ResultText);

            AsyncTask(ENamedThreads::GameThread, [this, SanitizedResult]()
                {
                    OnTranscriptionComplete.Broadcast(SanitizedResult);
                });
        });

    Request->ProcessRequest();
	UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] Transcription request for file %s sent to %s"), *AudioPath, *Url);
}

TArray<uint8> UWhisperComponent::CreateMultiPartRequest(FString FilePath)
{
    TArray<uint8> Payload;
    FString Boundary = "----UEBoundary" + FGuid::NewGuid().ToString().Replace(TEXT("-"), TEXT(""));
    CurrentBoundary = Boundary;

    auto AppendLine = [&Payload](const FString& Line)
        {
            FString WithNewline = Line + TEXT("\r\n");
            FTCHARToUTF8 Convert(*WithNewline);
            Payload.Append((uint8*)Convert.Get(), Convert.Length());
        };

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"file\"");
    AppendLine("");
    AppendLine(FilePath);

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"temperature\"");
    AppendLine("");
    AppendLine("0.0");

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"temperature_inc\"");
    AppendLine("");
    AppendLine("0.2");

    AppendLine("--" + Boundary);
    AppendLine("Content-Disposition: form-data; name=\"response_format\"");
    AppendLine("");
    AppendLine("text");

    AppendLine("--" + Boundary + "--");

    return Payload;
}

FString UWhisperComponent::SanitizeString(const FString& String)
{
    FString Result = String;

    auto RegexReplace = [](const FString& InputStr, const FString& PatternStr) -> FString
        {
            FRegexPattern Pattern(PatternStr);
            FString Output = InputStr;

            FRegexMatcher Matcher(Pattern, Output);
            while (Matcher.FindNext())
            {
                int32 Start = Matcher.GetMatchBeginning();
                int32 Length = Matcher.GetMatchEnding() - Start;
                Output.RemoveAt(Start, Length);

                Matcher = FRegexMatcher(Pattern, Output);
            }

            return Output;
        };

    Result = RegexReplace(Result, TEXT("\\[[^\\]]*\\]"));

    Result = RegexReplace(Result, TEXT("\\*[^\\*]*\\*"));

    Result = Result.TrimStartAndEnd();

    return Result;
}

bool UWhisperComponent::IsSpeechFrame(const float* Samples, int32 NumSamples, int32 SampleRate)
{
    switch (VadMode)
    {
        case EVadMode::Disabled:
            return true;

        case EVadMode::EnergyBased:
        {
            double SumSquares = 0.0;
            for (int32 i = 0; i < NumSamples; i++)
            {
                SumSquares += Samples[i] * Samples[i];
            }
            double Rms = FMath::Sqrt(SumSquares / FMath::Max(1, NumSamples));

            UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | VAD] RMS=%.5f, Threshold=%.5f"), Rms, EnergyThreshold);

            return (Rms >= EnergyThreshold);
        }

        case EVadMode::WebRTC:
        {
            if (VadInstance == nullptr)
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | VAD] WebRTC instance not initialized!"));
                return false;
            }

            const int32 FrameDurationMs = 20;
            const int32 FrameSize = WebRtcVadSampleRate * FrameDurationMs / 1000;

            if (SampleRate != WebRtcVadSampleRate)
            {
                Audio::VectorOps::FAlignedFloatBuffer InputBuffer(const_cast<float*>(Samples), NumSamples);

                Audio::FResamplingParameters Params = {
                    Audio::EResamplingMethod::BestSinc,
                    1,
                    SampleRate,
                    WebRtcVadSampleRate,
                    InputBuffer
                };

                int32 OutBufferSize = Audio::GetOutputBufferSize(Params);
                ResampledBuffer.Reset();
                ResampledBuffer.AddZeroed(OutBufferSize);

                Audio::FResamplerResults Results;
                Results.OutBuffer = &ResampledBuffer;
                Audio::Resample(Params, Results);
            }
            else
            {
                ResampledBuffer.Reset();
                ResampledBuffer.Append(Samples, NumSamples);
            }

            for (float F : ResampledBuffer)
            {
                float Clamped = FMath::Clamp(F, -1.f, 1.f);
                VadInputBuffer.Add(static_cast<int16>(Clamped * 32767.f));
            }

            if (VadInputBuffer.Num() < FrameSize)
            {
                return false;
            }

            TArray<int16> Frame;
            Frame.Append(VadInputBuffer.GetData(), FrameSize);

            VadInputBuffer.RemoveAt(0, FrameSize, EAllowShrinking::No);

            int Result = fvad_process(VadInstance, Frame.GetData(), FrameSize);

            UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | VAD] WebRTC Result = %d"), Result);

            if (Result == -1)
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | VAD] WebRTC VAD process failed!"));
                return false;
			}

            return (Result == 1);
        }
    }

    return false;
}

void UWhisperComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    if (AudioCapture.IsStreamOpen())
    {
        AudioCapture.AbortStream();
        AudioCapture.CloseStream();
    }

    if (!RecordedAudioFolder.IsEmpty() && IFileManager::Get().DirectoryExists(*RecordedAudioFolder))
    {
        TArray<FString> FilesToDelete, TxtFiles;
        IFileManager::Get().FindFiles(FilesToDelete, *RecordedAudioFolder, TEXT("*.wav"));
        IFileManager::Get().FindFiles(TxtFiles, *RecordedAudioFolder, TEXT("*.txt"));
        FilesToDelete.Append(TxtFiles);

        for (const FString& File : FilesToDelete)
        {
            FString FullPath = FPaths::Combine(RecordedAudioFolder, File);
            if (!IFileManager::Get().Delete(*FullPath, false, true))
            {
                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Whisper] Failed to delete file: %s"), *FullPath);
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Whisper] Cleaned up files in %s"), *RecordedAudioFolder);
    }

    if (VadInstance)
    {
        fvad_free(VadInstance);
        VadInstance = nullptr;
    }
}