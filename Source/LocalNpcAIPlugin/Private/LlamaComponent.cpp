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

    if (RagMode != ERagMode::Disabled)
    {
        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Starting RAG process"));

        Async(EAsyncExecution::Thread, [this, Message]()
            {
                TArray<float> Embedding = EmbedText(Message);

                TArray<FString> RagDocuments = GetTopKDocuments(Embedding);

                for (const FString& Doc : RagDocuments)
                {
                    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Embedding selected document: %s"), *Doc);
				}

                if (RagMode == ERagMode::EmbeddingPlusReranker)
                {
                    RagDocuments = RerankDocuments(Message, RagDocuments);

                    for (const FString& Doc : RagDocuments)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Reranking selected document: %s"), *Doc);
                    }
                }

                AsyncTask(ENamedThreads::GameThread, [this, RagDocuments]()
                    {
                        FString NewSystemMessage = SystemMessage;

                        NewSystemMessage.Append(TEXT("\n\n"));
                        NewSystemMessage.Append(TEXT("Relevant context for the query: \n"));
                        for (const FString& Doc : RagDocuments)
                        {
                            NewSystemMessage.Append(Doc);
                            NewSystemMessage.Append(TEXT("\n"));
                        }

                        if (!bStream)
                        {
                            SendRequest(NewSystemMessage);
                        }
                        else
                        {
							SendRequestStreaming(NewSystemMessage);
                        }
                    });
            });
    }
    else
    {
        if (!bStream)
        {
            SendRequest(SystemMessage);
        }
        else
        {
            SendRequestStreaming(SystemMessage);
		}
	}
}

void ULlamaComponent::SendRequest(FString InSystemMessage)
{
    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/chat/completions"), Port);
    FString Content = CreateJsonRequest(InSystemMessage);

    double StartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

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
            UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Response: %s"), *ResponseContent);

            AsyncTask(ENamedThreads::GameThread, [this, SanitizedResponse]()
                {
                    OnResponseReceived.Broadcast(SanitizedResponse);
                });

            FChatMessage NewResponse;
            NewResponse.Role = "assistant";
            NewResponse.Content = SanitizedResponse;
            ChatHistory.Add(NewResponse);

            FRegexPattern ActionPattern(TEXT("\\[\\[action: (.+?)\\]\\]"));
            FRegexMatcher Matcher(ActionPattern, ResponseContent);
            while (Matcher.FindNext())
            {
                FString ActionCommand = Matcher.GetCaptureGroup(1);
                HandleNpcAction(ActionCommand);
            }
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Request sent to %s"), *Url);
}

void ULlamaComponent::SendRequestStreaming(FString InSystemMessage)
{
    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/chat/completions"), Port);
    FString Content = CreateJsonRequest(InSystemMessage);

    double StartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

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

            AsyncTask(ENamedThreads::GameThread, [this, FullResponse]()
                {
                    FString SanitizedResponse = SanitizeString(FullResponse);
                    OnResponseReceived.Broadcast(SanitizedResponse);
                    FChatMessage NewResponse;
                    NewResponse.Role = "assistant";
                    NewResponse.Content = SanitizedResponse;
                    ChatHistory.Add(NewResponse);

                    FRegexPattern ActionPattern(TEXT("\\[\\[action: (.+?)\\]\\]"));
                    FRegexMatcher Matcher(ActionPattern, FullResponse);
                    while (Matcher.FindNext())
                    {
                        FString ActionCommand = Matcher.GetCaptureGroup(1);
                        HandleNpcAction(ActionCommand);
                    }
                });
        });
}

FString ULlamaComponent::CreateJsonRequest(FString InSystemMessage)
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
        SystemObj->SetStringField("content", InSystemMessage);
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

    Result = RegexReplace(Result, TEXT("\\[\\[action: .*?\\]\\]"));

    Result = RegexReplace(Result, TEXT("\\[[^\\]]*\\]"));

    Result = RegexReplace(Result, TEXT("\\*[^\\*]*\\*"));

    Result = Result.TrimStartAndEnd();

    return Result;
}

void ULlamaComponent::ClearChatHistory()
{
    ChatHistory.Empty();
    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Chat history cleared"));
}

TArray<float> ULlamaComponent::EmbedText(const FString& Text)
{
    TArray<float> EmbeddingResult;

    TSharedPtr<FJsonObject> JsonRequest = MakeShared<FJsonObject>();
    JsonRequest->SetStringField("input", Text);

    FString RequestString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestString);
    FJsonSerializer::Serialize(JsonRequest.ToSharedRef(), Writer);

    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/embeddings"), EmbeddingPort);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestString);

    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

	double StartTime = FPlatformTime::Seconds() * 1000.0;

    Request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
        {
			double EndTime = FPlatformTime::Seconds() * 1000.0;
			double Duration = EndTime - StartTime;

            if (bConnected && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode()))
            {
                TSharedPtr<FJsonObject> JsonObject;
				FString Content = Res->GetContentAsString();
				TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Content);
                if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* Data;
                    if (JsonObject->TryGetArrayField(TEXT("data"), Data))
                    {
                        const TArray<TSharedPtr<FJsonValue>>* Embedding;
						if (Data->Num() > 0 && (*Data)[0]->AsObject()->TryGetArrayField(TEXT("embedding"), Embedding))
                        {
                            for (const TSharedPtr<FJsonValue>& Value : *Embedding)
                            {
                                EmbeddingResult.Add(Value->AsNumber());
                            }

							UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Embedding received in %.2f ms, %d dimensions."), Duration, EmbeddingResult.Num());
                        }
                        else 
                        {
							UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] Embedding field not found in response: %s"), *Content);
						}
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] Data field not found in response: %s"), *Content);
					}
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] Failed to parse JSON response: %s"), *Content);
				}
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] Embedding request failed: %s"), Res.IsValid() ? *Res->GetContentAsString() : TEXT("No response"));
            }

            CompletionEvent->Trigger();
        });

    Request->ProcessRequest();
	UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Embedding request sent to %s"), *Url);

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    return EmbeddingResult;
}

void ULlamaComponent::GenerateKnowledge()
{
    Knowledge.Empty();

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *KnowledgePath))
    {
        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] Failed to read document: %s"), *KnowledgePath);
        return;
    }

    TArray<FString> Sentences;
    FString AccumulatedSentence;
    for (int32 i = 0; i < FileContent.Len(); ++i)
    {
        const TCHAR c = FileContent[i];
        AccumulatedSentence.AppendChar(c);
        if ((c == '.' || c == '!' || c == '?') && (i + 1 >= FileContent.Len() || FileContent[i + 1] == ' ' 
            || FileContent[i + 1] == '\n' || FileContent[i + 1] == '\r' || FileContent[i + 1] == '\t'))
        {
            FString S = AccumulatedSentence.TrimStartAndEnd();
            if (!S.IsEmpty())
            {
                Sentences.Add(S);
            }
            AccumulatedSentence.Empty();
        }
    }
    if (!AccumulatedSentence.TrimStartAndEnd().IsEmpty())
    {
        Sentences.Add(AccumulatedSentence.TrimStartAndEnd());
    }

	double StartTime = FPlatformTime::Seconds() * 1000.0;

    int32 Step = FMath::Max(1, SentencesPerChunk - SentenceOverlap);
    for (int32 i = 0; i < Sentences.Num(); i += Step)
    {
        int32 EndIdx = FMath::Min(i + SentencesPerChunk, Sentences.Num());

        FString ChunkText;
        for (int32 j = i; j < EndIdx; j++)
        {
            if (!ChunkText.IsEmpty())
                ChunkText += TEXT(" ");
            ChunkText += Sentences[j];
        }

        TArray<float> Emb = EmbedText(ChunkText);

        FKnowledgeEntry Chunk;
        Chunk.Text = ChunkText;
        Chunk.Embedding = Emb;
        Knowledge.Add(Chunk);
    }

	double EndTime = FPlatformTime::Seconds() * 1000.0;
	UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Generated knowledge in %.2f ms for document of %d characters."), EndTime - StartTime, FileContent.Len());
}

float ULlamaComponent::ComputeCosineSimilarity(const TArray<float>& A, const TArray<float>& B)
{
    if (A.Num() == 0 || B.Num() == 0 || A.Num() != B.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | RAG] Invalid vectors for cosine similarity calculation"));
        return 0;
    }
    double DotProduct = 0.0;
    double NormA = 0.0;
    double NormB = 0.0;

    for (int32 i = 0; i < A.Num(); i++)
    {
        DotProduct += A[i] * B[i];
        NormA += A[i] * A[i];
        NormB += B[i] * B[i];
    }

    double Denom = FMath::Sqrt(NormA) * FMath::Sqrt(NormB);
    if (Denom <= KINDA_SMALL_NUMBER)
    {
        return 0.0f;
    }

    return static_cast<float>(DotProduct / Denom);
}

TArray<FString> ULlamaComponent::GetTopKDocuments(const TArray<float>& QueryEmbedding)
{
    TArray<FString> TopChunks;

    if (Knowledge.Num() == 0 || QueryEmbedding.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | RAG] No knowledge available or query embedding is empty."));
        return TopChunks;
    }

    struct FScoredChunk
    {
        float Score;
        FString Text;
    };

    TArray<FScoredChunk> ScoredChunks;
    ScoredChunks.Reserve(Knowledge.Num());

    for (const FKnowledgeEntry& Entry : Knowledge)
    {
        float Similarity = ComputeCosineSimilarity(QueryEmbedding, Entry.Embedding);
        ScoredChunks.Add({ Similarity, Entry.Text });
    }

    ScoredChunks.Sort([](const FScoredChunk& A, const FScoredChunk& B)
        {
            return A.Score > B.Score;
        });

    int32 Count = FMath::Min(EmbeddingTopK, ScoredChunks.Num());
    for (int32 i = 0; i < Count; i++)
    {
        TopChunks.Add(ScoredChunks[i].Text);
    }

    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Selected top-%d chunks out of %d knowledge entries."), Count, ScoredChunks.Num());

    return TopChunks;
}

TArray<FString> ULlamaComponent::RerankDocuments(const FString& Query, const TArray<FString>& Documents)
{
    TArray<FString> RerankedDocs;

    if (Documents.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | RAG] No documents provided for reranking."));
        return RerankedDocs;
    }

    TSharedPtr<FJsonObject> JsonRequest = MakeShared<FJsonObject>();
    JsonRequest->SetStringField("query", Query);
    JsonRequest->SetNumberField("top_n", RerankingTopN);

    TArray<TSharedPtr<FJsonValue>> JsonDocs;
    for (const FString& Doc : Documents)
    {
        JsonDocs.Add(MakeShared<FJsonValueString>(Doc));
    }
    JsonRequest->SetArrayField("documents", JsonDocs);

    FString RequestString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestString);
    FJsonSerializer::Serialize(JsonRequest.ToSharedRef(), Writer);

    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/rerank"), RerankerPort);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestString);

    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);

    double StartTime = FPlatformTime::Seconds() * 1000.0;

    Request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
        {
            double EndTime = FPlatformTime::Seconds() * 1000.0;
            double Duration = EndTime - StartTime;

            if (bConnected && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode()))
            {
                FString Content = Res->GetContentAsString();
                TSharedPtr<FJsonObject> JsonObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);

                if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* Results;
                    if (JsonObject->TryGetArrayField(TEXT("results"), Results))
                    {
                        struct FScoredDoc
                        {
                            float Score;
                            int32 Index;
                        };

                        TArray<FScoredDoc> ScoredDocs;
                        for (const TSharedPtr<FJsonValue>& Value : *Results)
                        {
                            TSharedPtr<FJsonObject> Obj = Value->AsObject();
                            if (Obj.IsValid())
                            {
                                int32 Idx = Obj->GetIntegerField(TEXT("index"));
                                float Score = Obj->GetNumberField(TEXT("relevance_score"));
                                ScoredDocs.Add({ Score, Idx });
                            }
                            else
                            {
                                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | RAG] Invalid document object in reranker response: %s"), *Content);
							}
                        }

                        ScoredDocs.Sort([](const FScoredDoc& A, const FScoredDoc& B)
                            {
                                return A.Score > B.Score;
                            });

                        int32 Count = FMath::Min(RerankingTopN, ScoredDocs.Num());
                        for (int32 i = 0; i < Count; i++)
                        {
                            int32 DocIdx = ScoredDocs[i].Index;
                            if (Documents.IsValidIndex(DocIdx))
                            {
                                RerankedDocs.Add(Documents[DocIdx]);
                            }
                            else
                            {
                                UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | RAG] Document index %d out of bounds for reranked documents."), DocIdx);
							}
                        }

                        UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Reranked %d documents in %.2f ms."), Count, Duration);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] No 'results' in reranker response: %s"), *Content);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] Failed to parse reranker response: %s"), *Content);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[LocalAINpc | RAG] Rerank request failed: %s"), Res.IsValid() ? *Res->GetContentAsString() : TEXT("No response"));
            }

            CompletionEvent->Trigger();
        });

    Request->ProcessRequest();
    UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | RAG] Rerank request sent to %s"), *Url);

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    if (RerankedDocs.Num() == 0)
    {
		UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | RAG] No documents returned from reranker. Returning embedding mode results."));
        for (int i = 0; i < FMath::Min(RerankingTopN, Documents.Num()); i++)
        {
            RerankedDocs.Add(Documents[i]);
		}
    }

    return RerankedDocs;
}

void ULlamaComponent::HandleNpcAction(const FString& ActionCommand)
{
    FString Action;
    FString Object;

    FNpcAction* FoundAction = nullptr;
    for (FNpcAction& A : KnownActions)
    {
        if (ActionCommand.StartsWith(A.Name, ESearchCase::IgnoreCase))
        {
            if (!FoundAction || A.Name.Len() > FoundAction->Name.Len())
            {
                FoundAction = &A;
            }
        }
    }

    if (FoundAction)
    {
        if (FoundAction->bHasTargetObject)
        {
            Object = ActionCommand.Mid(FoundAction->Name.Len()).TrimStartAndEnd();

            FNpcObject* FoundObject = KnownObjects.FindByPredicate(
                [&](const FNpcObject& O) { return O.Name.Equals(Object, ESearchCase::IgnoreCase); });

            if (FoundObject)
            {
                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Actions] Executing action \"%s\" on object \"%s\""), *FoundAction->Name, *FoundObject->Name);
                OnActionReceived.Broadcast(FoundAction->Name, FoundObject->ActorRef);
                return;
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Actions] Executing action \"%s\""), *FoundAction->Name);
            OnActionReceived.Broadcast(FoundAction->Name, nullptr);
            return;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | Actions] Unknown action command: %s"), *ActionCommand);
}


FString ULlamaComponent::BuildActionsSystemMessage()
{
    FString Message;
    Message += TEXT("You can do two things:\n");
    Message += TEXT("1. Speak normally as dialogue with the user.\n");
    Message += TEXT("2. Perform actions by inserting action tags directly into your dialogue, if suitable in the current context.\n\n");

    Message += TEXT("The action tag must always respect the following format:\n");
    Message += TEXT("[[action: <action name> <optional object name>]]\n");

    Message += TEXT("Examples:\n");
    Message += TEXT("- I'm so tired [[action: sit]]\n");
    Message += TEXT("- Follow me [[action: move to door]]\n\n");

    Message += TEXT("The only actions you can use in the action tags are:\n");
    for (const auto& Action : KnownActions)
    {
        Message += FString::Printf(TEXT("- %s: %s"), *Action.Name, *Action.Description);
        if (Action.bHasTargetObject)
        {
            Message += TEXT(" (requires an object)");
		}
		Message += TEXT("\n");
    }

    Message += TEXT("\nThe only objects you can use in the action tags are:\n");
    for (const auto& Object : KnownObjects)
    {
        Message += FString::Printf(TEXT("- %s: %s\n"), *Object.Name, *Object.Description);
    }
    
    return Message;
}


void ULlamaComponent::BeginPlay()
{
    Super::BeginPlay();

    if (RagMode != ERagMode::Disabled)
    {
		UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] RAG mode enabled, generating knowledge..."));
        
        Async(EAsyncExecution::Thread, [this]()
            {
                GenerateKnowledge();
                UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] Knowledge generation complete."));
            });
    }

    if (!KnownActions.IsEmpty())
    {
		SystemMessage += TEXT("\n\n") + BuildActionsSystemMessage();
		UE_LOG(LogTemp, Log, TEXT("[LocalAINpc | Llama] SystemMessage updated with known actions and objects."));
    }
}