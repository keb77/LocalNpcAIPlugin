#include "LlamaComponent.h"
#include "Misc/Paths.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

ULlamaComponent::ULlamaComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    OnStreamTokenReceived.AddDynamic(this, &ULlamaComponent::HandleStreamChunk);
}

void ULlamaComponent::SendChatMessage(FString Message)
{
    if (Message.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Llama] Empty message received, ignoring."));

        OnResponseReceived.Broadcast(TEXT(""));

        return;
	}

    FChatMessage NewMessage;
    NewMessage.Role = "user";
    NewMessage.Content = Message;
    ChatHistory.Add(NewMessage);

    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/chat/completions"), Port);
    FString Content = CreateJsonRequest();

    double StartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

    if (!bStream)
    {
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb("POST");
        Request->SetHeader("Content-Type", "application/json");
        Request->SetContentAsString(Content);

        Request->OnProcessRequestComplete().BindLambda([this, StartTimeBenchmark](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
            {
                double EndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
                double DurationBenchmark = EndTimeBenchmark - StartTimeBenchmark;

                if (!bWasSuccessful || !Response.IsValid())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] Request failed."));

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }

                int32 Code = Response->GetResponseCode();
                if (Code != 200)
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] HTTP %d: %s"), Code, *Response->GetContentAsString());

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }

                FString JsonResponse = Response->GetContentAsString();
                TSharedPtr<FJsonObject> JsonObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResponse);
                if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] Failed to parse JSON response"));
                    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response: %s"), *JsonResponse);

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }
                const TArray<TSharedPtr<FJsonValue>>* Choices;
                if (!JsonObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] No choices found in response"));
                    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response: %s"), *JsonResponse);

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }
                TSharedPtr<FJsonObject> ChoiceObj = (*Choices)[0]->AsObject();
                if (!ChoiceObj.IsValid())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] Invalid choice object in response"));
                    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response: %s"), *JsonResponse);

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }
                const TSharedPtr<FJsonObject>* MessageObj;
                if (!ChoiceObj->TryGetObjectField(TEXT("message"), MessageObj) || !MessageObj->IsValid())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] No message object found in choice"));
                    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response: %s"), *JsonResponse);

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }
                FString ResponseContent;
                if (!(*MessageObj)->TryGetStringField(TEXT("content"), ResponseContent) || ResponseContent.IsEmpty())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] No content field found in message"));
                    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response: %s"), *JsonResponse);

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }

                FString SanitizedResponse = SanitizeString(ResponseContent);
                int32 LengthBenchmark = ResponseContent.Len();
				UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response received in %.2f ms, %d characters."), DurationBenchmark, LengthBenchmark);
				UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response: %s"), *SanitizedResponse);

                AsyncTask(ENamedThreads::GameThread, [this, SanitizedResponse]()
                    {
                        OnResponseReceived.Broadcast(SanitizedResponse);
                    });

                FChatMessage NewResponse;
                NewResponse.Role = "assistant";
                NewResponse.Content = SanitizedResponse;
                ChatHistory.Add(NewResponse);
            });

        Request->ProcessRequest();
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Request sent to %s"), *Url);
    }
    else
    {
        FString RequestHeaders = FString::Printf(
        TEXT("POST /v1/chat/completions HTTP/1.1\r\n")
        TEXT("Host: localhost:%d\r\n")
        TEXT("Content-Type: application/json\r\n")
        TEXT("Accept: text/event-stream\r\n")
        TEXT("Content-Length: %d\r\n")
        TEXT("Connection: close\r\n\r\n"),
        Port, Content.Len());

        FString FullRequest = RequestHeaders + Content;

        Async(EAsyncExecution::Thread, [this, FullRequest = MoveTemp(FullRequest), StartTimeBenchmark]()
            {
                ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
                TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
                bool bIsValid;
                Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
                Addr->SetPort(Port);

                if (!bIsValid)
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] Invalid IP address"));

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }

                FSocket* Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("LlamaStreamSocket"), false);
                if (!Socket || !Socket->Connect(*Addr))
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] Failed to connect to Llama server on port %d"), Port);

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
                }

                int32 BytesSent = 0;
                auto ConvertedRequest = StringCast<UTF8CHAR>(*FullRequest);
                Socket->Send((const uint8*)ConvertedRequest.Get(), ConvertedRequest.Length(), BytesSent);

                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Sent streaming request, waiting for response..."));

                constexpr int32 BufferSize = 8192;
                uint8 Buffer[BufferSize];
                FString StreamedData;
                bool bDone = false;

                FString FullResponse;

                const double TimeoutSeconds = 60.0;
                double StartTime = FPlatformTime::Seconds();

                double TokenStartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
                double ChunkStartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

                while (!bDone && (FPlatformTime::Seconds() - StartTime) < TimeoutSeconds)
                {
                    if (Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(1.0f)))
                    {
                        int32 BytesRead = 0;
                        if (Socket->Recv(Buffer, BufferSize, BytesRead))
                        {
                            FString Chunk = FString(StringCast<TCHAR>((const UTF8CHAR*)Buffer, BytesRead).Get(), BytesRead);
                            StreamedData += Chunk;

                            TArray<FString> Lines;
                            Chunk.ParseIntoArrayLines(Lines);
                            for (const FString& Line : Lines)
                            {
                                if (Line.StartsWith("data: "))
                                {
                                    FString Payload = Line.Mid(6).TrimStartAndEnd();
                                    if (Payload == TEXT("[DONE]"))
                                    {
                                        int32 LengthBenchmark = FullResponse.Len();
                                        double EndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
                                        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response of %d characters received in %.2f ms"), LengthBenchmark, EndTimeBenchmark - StartTimeBenchmark);

                                        bDone = true;
                                        AsyncTask(ENamedThreads::GameThread, [this, bDone]()
                                            {
                                                OnStreamTokenReceived.Broadcast(TEXT(""), bDone);
                                            });
                                        break;
                                    }

                                    TSharedPtr<FJsonObject> JsonObject;
                                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
                                    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
                                    {
                                        const TArray<TSharedPtr<FJsonValue>>* Choices;
                                        if (JsonObject->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
                                        {
                                            const TSharedPtr<FJsonObject>* Delta;
                                            if ((*Choices)[0]->AsObject()->TryGetObjectField(TEXT("delta"), Delta))
                                            {
                                                FString PartialText;
                                                if ((*Delta)->TryGetStringField(TEXT("content"), PartialText))
                                                {
                                                    double TokenEndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
                                                    UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | Llama] Token received in %.2f ms"), TokenEndTimeBenchmark - TokenStartTimeBenchmark);
                                                    TokenStartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

                                                    FullResponse.Append(PartialText);
                                                    AsyncTask(ENamedThreads::GameThread, [this, PartialText, bDone]()
                                                        {
                                                            OnStreamTokenReceived.Broadcast(PartialText, bDone);
                                                            UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | Llama] Streamed token: %s"), *PartialText);
														});
                                                }
                                                else
                                                {
                                                    UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Llama] No content field in delta"));
													UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Payload: %s"), *Payload);
                                                }
                                            }
                                            else
                                            {
                                                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Llama] No delta field in choice"));
                                                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Payload: %s"), *Payload);
                                            }
                                        }
                                        else
                                        {
                                            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Llama] No choices found in streamed data"));
                                            UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Payload: %s"), *Payload);
                                        }
                                    }
                                    else
                                    {
                                        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Llama] Failed to parse JSON from streamed data: %s"), *Payload);
                                    }
                                }
                                else
                                {
                                    UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | Llama] Ignoring line: %s"), *Line);
                                }
                            }
                        }
                        else
                        {
                            UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | Llama] Failed to read data from socket"));
                            bDone = true;
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | Llama] No data ready to read yet..."));
                    }
                }
                if (!bDone)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Llama] Streaming timed out after %.2f seconds"), TimeoutSeconds);

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });
                }

                Socket->Close();
                ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Streamed response complete. Full response: %s"), *FullResponse);
                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Socket closed after streaming"));

                if (FullResponse.IsEmpty())
                {
                    UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Llama] No response received from server"));

                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            OnResponseReceived.Broadcast(TEXT(""));
                        });

                    return;
				}

				FString SanitizedResponse = SanitizeString(FullResponse);
                AsyncTask(ENamedThreads::GameThread, [this, SanitizedResponse]()
                    {
                        OnResponseReceived.Broadcast(SanitizedResponse);
                    });
            });
    }
}

FString ULlamaComponent::CreateJsonRequest()
{
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetNumberField("temperature", Temperature);
    RootObject->SetNumberField("top_p", TopP);
    RootObject->SetNumberField("max_tokens", MaxTokens);
    RootObject->SetNumberField("repeat_penalty", RepeatPenalty);
    RootObject->SetNumberField("seed", Seed);
    RootObject->SetBoolField("stream", bStream);

    TArray<TSharedPtr<FJsonValue>> JsonMessages;
    if (!SystemMessage.IsEmpty())
    {
        TSharedPtr<FJsonObject> SystemObj = MakeShared<FJsonObject>();
        SystemObj->SetStringField("role", "system");
        SystemObj->SetStringField("content", SystemMessage);
        JsonMessages.Add(MakeShared<FJsonValueObject>(SystemObj));
    }
    for (const FChatMessage& Msg : ChatHistory)
    {
        TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
        MsgObj->SetStringField("role", Msg.Role);
        MsgObj->SetStringField("content", Msg.Content);
        JsonMessages.Add(MakeShared<FJsonValueObject>(MsgObj));
    }
    RootObject->SetArrayField("messages", JsonMessages);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	return OutputString;
}

void ULlamaComponent::HandleStreamChunk(const FString& Token, bool bDone)
{
    FScopeLock Lock(&ChunkMutex);

    AccumulatedChunk += Token;

    if (bDone)
    {
		FString SanitizedChunk = SanitizeString(AccumulatedChunk);
        if (SanitizedChunk.Len() > 1)
        {
            OnChunkReceived.Broadcast(SanitizedChunk, bDone);
            UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Chunk: %s"), *SanitizedChunk);

			double ChunkEndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
			UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Chunk processed in %.2f ms"), ChunkEndTimeBenchmark - ChunkStartTimeBenchmark);
        }
        else
        {
			OnChunkReceived.Broadcast(TEXT(""), bDone);
        }

		AccumulatedChunk.Empty();
		return;
	}

    TSet<FString> AbbreviationWhitelist = {
    TEXT("Mr."), TEXT("Mrs."), TEXT("Ms."), TEXT("Dr."), TEXT("Jr.")
    };

    int32 LastDelimiterIndex = -1;

    for (int32 i = 0; i < AccumulatedChunk.Len(); ++i)
    {
		TCHAR CurrentChar = AccumulatedChunk[i];
        if (CurrentChar == '.' || CurrentChar == '!' || CurrentChar == '?' || CurrentChar == ';' || CurrentChar == '\n' || CurrentChar == '\r')
        {
            bool bIsAbbreviation = false;
            if (CurrentChar == '.')
            {
                int32 Lookbehind = 8;
                int32 Start = FMath::Max(0, i - Lookbehind + 1);
                FString Sub = AccumulatedChunk.Mid(Start, i - Start + 1);
                for (const FString& Abbr : AbbreviationWhitelist)
                {
                    if (Sub.EndsWith(Abbr))
                    {
                        bIsAbbreviation = true;
                        break;
                    }
                }
            }
            if (!bIsAbbreviation)
            {
                LastDelimiterIndex = i;
			}
        }
    }

    if (LastDelimiterIndex == -1)
    {
        return;
    }

    FString Chunk = AccumulatedChunk.Left(LastDelimiterIndex + 1);
    AccumulatedChunk = AccumulatedChunk.Mid(LastDelimiterIndex + 1);

    Chunk = SanitizeString(Chunk);

    if (Chunk.Len() > 1)
    {
        OnChunkReceived.Broadcast(Chunk, bDone);
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Chunk: %s"), *Chunk);

		double ChunkEndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
		UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Chunk processed in %.2f ms"), ChunkEndTimeBenchmark - ChunkStartTimeBenchmark);
        ChunkStartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
    }
    else
    {
		UE_LOG(LogTemp, Verbose, TEXT("[LocalAINpc | Llama] Empty chunk received, ignoring"));
    }
}

FString ULlamaComponent::SanitizeString(const FString& String)
{
    FString Result  = String;
    
    auto RegexReplace = [](const FString& InputStr, const FString& PatternStr) -> FString
        {
            FRegexPattern Pattern(PatternStr);
            FRegexMatcher Matcher(Pattern, InputStr);
            FString Output = InputStr;

            while (Matcher.FindNext())
            {
                int32 MatchBeginning = Matcher.GetMatchBeginning();
                int32 MatchEnding = Matcher.GetMatchEnding();
                Output.RemoveAt(MatchBeginning, MatchEnding - MatchBeginning);
                Matcher = FRegexMatcher(Pattern, Output);
            }

            return Output;
        };

    Result = RegexReplace(Result, TEXT("\\[[^\\]]*\\]"));
    Result = RegexReplace(Result, TEXT("\\*[^\\*]*\\*"));
    Result = RegexReplace(Result, TEXT("<[^>]*>"));
    Result = Result.TrimStartAndEnd();

    return Result;
}

void ULlamaComponent::ClearChatHistory()
{
    ChatHistory.Empty();
    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Chat history cleared"));
}