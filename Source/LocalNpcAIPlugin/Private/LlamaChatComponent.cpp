#include "LlamaChatComponent.h"
#include "LlamaServerManager.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

ULlamaChatComponent::ULlamaChatComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

    OnStreamTokenReceived.AddDynamic(this, &ULlamaChatComponent::HandleStreamChunk);
}

void ULlamaChatComponent::SendChatRequest(FString Message)
{
	FString SanitizedMessage = SanitizeString(Message);

    if (SanitizedMessage.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] Empty message received, ignoring."));
        return;
	}

    if (!bProcessing)
    {
        bProcessing = true;
        StartLlamaProcess(SanitizedMessage);
    }
    else 
    { 
		UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] Already processing a request, ignoring new message: %s"), *Message);
    }
}

void ULlamaChatComponent::StartLlamaProcess(const FString& Message)
{
    ULlamaServerManager* ServerManager = GetWorld()->GetGameInstance()->GetSubsystem<ULlamaServerManager>();
    if (!ServerManager || !ServerManager->IsServerRunning(ModelName))
    {
        UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Server for model %s is not running."), *ModelName);

		bProcessing = false;
        return;
    }
    int32 Port = ServerManager->GetServerPort(ModelName);
    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/chat/completions"), Port);

    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalNpcAIPlugin"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Plugin LocalNpcAIPlugin not found!"));

        bProcessing = false;
        return;
    }
    FString PluginDir = Plugin->GetBaseDir();
    FString ModelPath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("models"), ModelName);

    FChatMessage NewMessage;
    NewMessage.Role = "user";
    NewMessage.Content = Message;
    ChatHistory.Add(NewMessage);

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField("model", ModelPath);
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

    LlamaStartTimeBenchmark = FPlatformTime::Seconds() * 1000.0;

    if (!bStream)
    {
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb("POST");
        Request->SetHeader("Content-Type", "application/json");
        Request->SetContentAsString(OutputString);

        Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
            {
                if (!IsValid(this))
                {
                    bProcessing = false;
                    return;
                }

                if (bWasSuccessful && Response.IsValid())
                {
                    FString JsonResponse = Response->GetContentAsString();
                    TSharedPtr<FJsonObject> JsonObject;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResponse);
                    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Failed to parse JSON response"));
                        UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Response: %s"), *JsonResponse);
                        OnResponseReceived.Broadcast(TEXT("I'm sorry, I cannot answer that right now."));

                        bProcessing = false;
                        return;
                    }
                    const TArray<TSharedPtr<FJsonValue>>* Choices;
                    if (!JsonObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] No choices found in response"));
                        UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Response: %s"), *JsonResponse);
                        OnResponseReceived.Broadcast(TEXT("I'm sorry, I cannot answer that right now."));

                        bProcessing = false;
                        return;
                    }
                    TSharedPtr<FJsonObject> ChoiceObj = (*Choices)[0]->AsObject();
                    if (!ChoiceObj.IsValid())
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Invalid choice object in response"));
                        UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Response: %s"), *JsonResponse);
                        OnResponseReceived.Broadcast(TEXT("I'm sorry, I cannot answer that right now."));

                        bProcessing = false;
                        return;
                    }
                    const TSharedPtr<FJsonObject>* MessageObj;
                    if (!ChoiceObj->TryGetObjectField(TEXT("message"), MessageObj) || !MessageObj->IsValid())
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] No message object found in choice"));
                        UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Response: %s"), *JsonResponse);
                        OnResponseReceived.Broadcast(TEXT("I'm sorry, I cannot answer that right now."));

                        bProcessing = false;
                        return;
                    }
                    FString ResponseContent;
                    if (!(*MessageObj)->TryGetStringField(TEXT("content"), ResponseContent) || ResponseContent.IsEmpty())
                    {
                        UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] No content field found in message"));
                        UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Response: %s"), *JsonResponse);
                        OnResponseReceived.Broadcast(TEXT("I'm sorry, I cannot answer that right now."));

                        bProcessing = false;
                        return;
                    }

                    FString SanitizedResponse = SanitizeString(ResponseContent);

                    UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Model Response: %s"), *SanitizedResponse);
                    OnResponseReceived.Broadcast(SanitizedResponse);

                    LlamaLengthBenchmark = ResponseContent.Len();
                    LlamaEndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
                    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Response of %d characters received in %.2f ms"), LlamaLengthBenchmark, LlamaEndTimeBenchmark - LlamaStartTimeBenchmark);

                    FChatMessage NewResponse;
                    NewResponse.Role = "assistant";
                    NewResponse.Content = SanitizedResponse;
                    ChatHistory.Add(NewResponse);
                }
                else
                {
                    FString ErrorReason = Response.IsValid() ? Response->GetContentAsString() : TEXT("No response");
                    int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : -1;

                    UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Llama request failed. StatusCode: %d, Error: %s"), StatusCode, *ErrorReason);
                    OnResponseReceived.Broadcast(TEXT("I'm sorry, I cannot answer that right now."));
                }

                bProcessing = false;
            });

        Request->ProcessRequest();
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
        Port, OutputString.Len());

        FString FullRequest = RequestHeaders + OutputString;

        Async(EAsyncExecution::Thread, [this, FullRequest = MoveTemp(FullRequest), Port]()
            {
                ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
                TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
                bool bIsValid;
                Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
                Addr->SetPort(Port);

                if (!bIsValid)
                {
                    UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Invalid IP address"));
                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
							bProcessing = false;
                        });
                    return;
                }

                FSocket* Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("LlamaStreamSocket"), false);
                if (!Socket || !Socket->Connect(*Addr))
                {
                    UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Failed to connect to Llama server on port %d"), Port);
                    AsyncTask(ENamedThreads::GameThread, [this]()
                        {
                            bProcessing = false;
                        });
                    return;
                }

                int32 BytesSent = 0;
                auto ConvertedRequest = StringCast<UTF8CHAR>(*FullRequest);
                Socket->Send((const uint8*)ConvertedRequest.Get(), ConvertedRequest.Length(), BytesSent);

                UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Sent streaming request, waiting for response..."));

                constexpr int32 BufferSize = 8192;
                uint8 Buffer[BufferSize];
                FString StreamedData;
                bool bDone = false;

                FString FullResponse;

                const double TimeoutSeconds = 30.0;
                double StartTime = FPlatformTime::Seconds();

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
                                        bDone = true;
                                        OnStreamTokenReceived.Broadcast("", bDone);
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
                                                    FullResponse.Append(PartialText);
                                                    AsyncTask(ENamedThreads::GameThread, [this, PartialText, bDone]()
                                                        {
                                                            OnStreamTokenReceived.Broadcast(PartialText, bDone);
                                                            UE_LOG(LogTemp, Verbose, TEXT("[LlamaCPP] Streamed token: %s"), *PartialText);
														});
                                                }
                                                else
                                                {
                                                    UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] No content field in delta"));
                                                }
                                            }
                                            else
                                            {
                                                UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] No delta field in choice"));
                                            }
                                        }
                                        else
                                        {
                                            UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] No choices found in streamed data"));
                                        }
                                    }
                                    else
                                    {
                                        UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Failed to parse JSON from streamed data: %s"), *Payload);
                                    }
                                }
                            }
                        }
                        else
                        {
                            UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Failed to read data from socket"));
                            bDone = true;
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Verbose, TEXT("[LlamaCPP] No data ready to read yet..."));
                    }
                }
                if (!bDone)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] Streaming timed out after %.2f seconds"), TimeoutSeconds);
                }

                Socket->Close();
                ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
                UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Streamed response complete. Full response: %s"), *FullResponse);
                UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Socket closed after streaming"));

                LlamaLengthBenchmark = FullResponse.Len();
                LlamaEndTimeBenchmark = FPlatformTime::Seconds() * 1000.0;
				UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Response of %d characters received in %.2f ms"), LlamaLengthBenchmark, LlamaEndTimeBenchmark - LlamaStartTimeBenchmark);

                bProcessing = false;
            });
    }
}

void ULlamaChatComponent::HandleStreamChunk(const FString& Token, bool bDone)
{
    FScopeLock Lock(&ChunkMutex);

    AccumulatedChunk += Token;

    if (bDone)
    {
		FString SanitizedChunk = SanitizeString(AccumulatedChunk);
        if (SanitizedChunk.Len() > 1)
        {
            OnChunkReceived.Broadcast(SanitizedChunk);
            UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Chunk: %s"), *SanitizedChunk);
        }
		AccumulatedChunk.Empty();
		return;
	}

    TSet<FString> AbbreviationWhitelist = {
    TEXT("Mr."), TEXT("Mrs."), TEXT("Ms."), TEXT("Dr."), TEXT("Jr."), TEXT("Sr."), TEXT("Prof."), TEXT("St."), TEXT("U."), TEXT("S.")
    };

    int32 LastDelimiterIndex = -1;

    for (int32 i = 0; i < AccumulatedChunk.Len(); ++i)
    {
		TCHAR CurrentChar = AccumulatedChunk[i];
        if (CurrentChar == '.' || CurrentChar == '!' || CurrentChar == '?' || CurrentChar == ',' || CurrentChar == ';' || CurrentChar == '\n' || CurrentChar == '\r')
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
        OnChunkReceived.Broadcast(Chunk);
        UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Chunk: %s"), *Chunk);
    }
}

FString ULlamaChatComponent::SanitizeString(const FString& String)
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
    Result = Result.TrimStartAndEnd();

    return Result;
}


void ULlamaChatComponent::ClearChatHistory()
{
    ChatHistory.Empty();
    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Chat history cleared"));
}