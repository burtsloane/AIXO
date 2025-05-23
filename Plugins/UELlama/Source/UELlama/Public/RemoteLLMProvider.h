#pragma once

#include "CoreMinimal.h"
#include "LLMProvider.h"
#include "Http.h"
#include "RemoteLLMProvider.generated.h"

UCLASS()
class UELLAMA_API URemoteLLMProvider : public UObject, public ILLMProvider
{
    GENERATED_BODY()

public:
    URemoteLLMProvider();

    // ILLMProvider interface
    virtual void Initialize(const FString& ModelPath) override;
    virtual void ProcessInput(const FString& Input, const FString& HighFreqContext) override;
    virtual void Shutdown() override;
    virtual void UpdateContextBlock(ELLMContextBlockType BlockType, const FString& NewContent) override;
    virtual ULLMContextManager* GetContextManager() const override;
    virtual void SetContextManager(ULLMContextManager* InContextManager) override;
    virtual FContextVisPayload GetContextVisualization() const override;
    virtual void BroadcastContextUpdate(const FContextVisPayload& Payload) override;
    virtual FOnTokenGenerated& GetOnTokenGenerated() override { return OnTokenGenerated; }
    virtual FOnContextChanged& GetOnContextChanged() override { return OnContextChanged; }
    
    // API configuration
    void SetAPIKey(const FString& NewAPIKey) { APIKey = NewAPIKey; }

    // Delegates
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnError, const FString&);
    FOnError OnError;

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnReady, const FString&);
    FOnReady OnReady;

private:
    // HTTP client
    TSharedPtr<IHttpRequest> CurrentRequest;
    TSharedPtr<IHttpResponse> CurrentResponse;
    
    // Response handling
    void OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
    void OnResponseChunkReceived(const FString& Chunk);
    
    // Context assembly
    FString AssembleFullContext() const;
    
    // State tracking
    std::atomic<bool> bIsWaitingForResponse{false};
    std::atomic<float> ResponseProgress{0.0f};
    FString CurrentOperation;
    
    // Configuration
    UPROPERTY()
    FString APIEndpoint;
    
    UPROPERTY()
    FString APIKey;
    
    // Response buffer
    FString ResponseBuffer;
    
    // Helper functions
    void StartNewRequest();
    void HandleResponseChunk(const FString& Chunk);
    void UpdateVisualizationState();
    void DoRetryRequest(int32& RetryCount, const int32 MaxRetries, const float RetryDelay);

    // Context manager
    UPROPERTY()
    ULLMContextManager* ContextManager;

    // Delegate instances
    FOnTokenGenerated OnTokenGenerated;
    FOnContextChanged OnContextChanged;
}; 