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

ULlamaChatComponent::ULlamaChatComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void ULlamaChatComponent::SendChatRequest(FString Message)
{
    ULlamaServerManager* ServerManager = GetWorld()->GetGameInstance()->GetSubsystem<ULlamaServerManager>();
    if (!ServerManager || !ServerManager->IsServerRunning(ModelName))
    {
        UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Server for model %s is not running."), *ModelName);
        return;
    }
    int32 Port = ServerManager->GetServerPort(ModelName);
    FString Url = FString::Printf(TEXT("http://localhost:%d/v1/chat/completions"), Port);

    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalNpcAIPlugin"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Plugin LocalNpcAIPlugin not found!"));
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

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb("POST");
    Request->SetHeader("Content-Type", "application/json");
    Request->SetContentAsString(OutputString);

    Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            if (!IsValid(this)) return;

            if (bWasSuccessful && Response.IsValid())
            {
                FString JsonResponse = Response->GetContentAsString();
                TSharedPtr<FJsonObject> JsonObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResponse);
                if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Failed to parse JSON response"));
                    OnResponseReceived.Broadcast(TEXT("Request Failed"));
                    return;
                }
                const TArray<TSharedPtr<FJsonValue>>* Choices;
                if (!JsonObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
                {
                    UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] No choices found in response"));
                    OnResponseReceived.Broadcast(TEXT("Request Failed"));
                    return;
                }
                TSharedPtr<FJsonObject> ChoiceObj = (*Choices)[0]->AsObject();
                if (!ChoiceObj.IsValid())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Invalid choice object in response"));
                    OnResponseReceived.Broadcast(TEXT("Request Failed"));
                    return;
                }
                const TSharedPtr<FJsonObject>* MessageObj;
                if (!ChoiceObj->TryGetObjectField(TEXT("message"), MessageObj) || !MessageObj->IsValid())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] No message object found in choice"));
                    OnResponseReceived.Broadcast(TEXT("Request Failed"));
                    return;
                }
                FString ResponseContent;
                if (!(*MessageObj)->TryGetStringField(TEXT("content"), ResponseContent) || ResponseContent.IsEmpty())
                {
                    UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] No content field found in message"));
                    OnResponseReceived.Broadcast(TEXT("Request Failed"));
                    return;
                }

                UE_LOG(LogTemp, Log, TEXT("[LLamaCPP] Model Response: %s"), *ResponseContent);
                OnResponseReceived.Broadcast(ResponseContent);

                FChatMessage NewResponse;
                NewResponse.Role = "assistant";
                NewResponse.Content = ResponseContent;
                ChatHistory.Add(NewResponse);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[LLamaCPP] Llama request failed"));
                OnResponseReceived.Broadcast(TEXT("Request Failed"));
            }
        });

    Request->ProcessRequest();
}

void ULlamaChatComponent::ClearChatHistory()
{
    ChatHistory.Empty();
    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Chat history cleared"));
}