#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LlamaServerManager.generated.h"

// TODO
// - add -t (--threads), -ngl (--gpu-layers), -c (--ctx-size) and other options, if needed
// - broadcast llama server ready event
// - add support for remote server (llama.cpp running on another machine)

USTRUCT(BlueprintType)
struct FLLamaServerProcessInfo
{
    GENERATED_BODY()

    int32 Port;
    FProcHandle ProcessHandle;
};

UCLASS(ClassGroup = (LlamaCPP))
class LOCALNPCAIPLUGIN_API ULlamaServerManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    bool StartServer(const FString& ModelName, int32 Port = 8080);

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    bool IsServerRunning(const FString& ModelName) const;

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    int32 GetServerPort(const FString& ModelName) const;

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    void ShutdownServer(const FString& ModelName);

    UFUNCTION(BlueprintCallable, Category = "LlamaCPP")
    void ShutdownAll();

private:
    TMap<FString, FLLamaServerProcessInfo> ModelServers;
};
