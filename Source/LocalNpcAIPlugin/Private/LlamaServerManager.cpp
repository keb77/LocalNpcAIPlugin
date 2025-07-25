#include "LlamaServerManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

void ULlamaServerManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Server Manager Initialized"));
}

void ULlamaServerManager::Deinitialize()
{
    ShutdownAll();

    Super::Deinitialize();
    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Server Manager Deinitialized"));
}

void ULlamaServerManager::StartServer(const FString& ModelName, int32 Port, int32 Threads, int32 GpuLayers, int32 ContextSize)
{
    if (Port <= 0 || Port > 65535)
    {
		UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Invalid port number %d for model %s. Using default port 8080."), Port, *ModelName);
		Port = 8080;
    }
	if (Threads < 1 && Threads != -1)
    {
		UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Invalid number of threads %d for model %s. Using default value of -1."), Threads, *ModelName);
		Threads = -1;
	}
    if(GpuLayers < 0)
    {
		UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Invalid number of GPU layers %d for model %s. Using default value of 0."), GpuLayers, *ModelName);
		GpuLayers = 0;
	}
	if (ContextSize < 512 && ContextSize != 0)
    {
		UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Invalid context size %d for model %s. Using default value of 0."), ContextSize, *ModelName);
		ContextSize = 0;
	}

    if (IsServerRunning(ModelName))
    {
        UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] Server for model %s is already running."), *ModelName);
    }
    for (const auto& Pair : ModelServers)
    {
        if (Pair.Value.Port == Port)
        {
            UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Port %d is already in use by model %s."), Port, *Pair.Key);
        }
    }

    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LocalNpcAIPlugin"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Plugin LocalNpcAIPlugin not found!"));
    }
    FString PluginDir = Plugin->GetBaseDir();

    FString ExePath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("llama.cpp"), TEXT("llama-server.exe"));
    FString ModelPath = FPaths::Combine(PluginDir, TEXT("ThirdParty"), TEXT("models"), ModelName);

    if (!FPaths::FileExists(ExePath))
    {
        UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Executable file %s does not exist."), *ExePath);
    }
    if (!FPaths::FileExists(ModelPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Model file %s does not exist."), *ModelPath);
    }

    FString Params = FString::Printf(TEXT("-m \"%s\" --port %d -t %d -ngl %d -c %d"), *ModelPath, Port, Threads, GpuLayers, ContextSize);
    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Starting llama-server with params: %s"), *Params);

    FProcHandle ServerHandle = FPlatformProcess::CreateProc(
        *ExePath,
        *Params,
        true, false, false,
        nullptr, 0,
        nullptr, nullptr
    );

    if (!ServerHandle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Failed to start llama-server!"));
    }

    FLLamaServerProcessInfo Info;
    Info.Port = Port;
    Info.ProcessHandle = ServerHandle;
    ModelServers.Add(ModelName, Info);

    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Started llama-server for model %s on port %d"), *ModelName, Port);
}

void ULlamaServerManager::AddServer(const FString& ModelName, int32 Port)
{
    if (IsServerRunning(ModelName))
    {
        UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] Server for model %s is already running."), *ModelName);
    }
    for (const auto& Pair : ModelServers)
    {
        if (Pair.Value.Port == Port)
        {
            UE_LOG(LogTemp, Error, TEXT("[LlamaCPP] Port %d is already in use by model %s."), Port, *Pair.Key);
        }
    }

    FLLamaServerProcessInfo Info;
    Info.Port = Port;
	Info.ProcessHandle = FProcHandle();
    ModelServers.Add(ModelName, Info);

    UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Added llama-server for model %s on port %d"), *ModelName, Port);
}

bool ULlamaServerManager::IsServerRunning(const FString& ModelName) const
{
    return ModelServers.Contains(ModelName);
}

int32 ULlamaServerManager::GetServerPort(const FString& ModelName) const
{
    const FLLamaServerProcessInfo* Info = ModelServers.Find(ModelName);
    return Info ? Info->Port : -1;
}

void ULlamaServerManager::ShutdownServer(const FString& ModelName)
{
    if (!ModelServers.Contains(ModelName))
    {
        UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] No server running for model %s."), *ModelName);
        return;
    }

    FLLamaServerProcessInfo& Info = ModelServers[ModelName];
    if (FPlatformProcess::IsProcRunning(Info.ProcessHandle))
    {
        FPlatformProcess::TerminateProc(Info.ProcessHandle, true);
        FPlatformProcess::CloseProc(Info.ProcessHandle);
        UE_LOG(LogTemp, Log, TEXT("[LlamaCPP] Shutdown llama-server for model %s on port %d"), *ModelName, Info.Port);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LlamaCPP] Server for model %s is not running."), *ModelName);
    }

    ModelServers.Remove(ModelName);
}

void ULlamaServerManager::ShutdownAll()
{
    for (const auto& Pair : ModelServers)
    {
        const FString& ModelName = Pair.Key;
        ShutdownServer(ModelName);
    }
}