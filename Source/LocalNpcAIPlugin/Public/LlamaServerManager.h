#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LlamaServerManager.generated.h"

// TODO
// - add llama server ready event

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
	bool StartServer(const FString& ModelName, int32 Port = 8080, int32 Threads = -1, int32 GpuLayers = 0, int32 ContextSize = 0);

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
