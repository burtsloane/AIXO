#include "RemoteLLMProvider.h"
#include "Json.h"
#include "JsonObjectConverter.h"

URemoteLLMProvider::URemoteLLMProvider()
{
    // Initialize HTTP module if needed
    FHttpModule::Get();
}

void URemoteLLMProvider::Initialize(const FString& ModelPath)
{
    APIEndpoint = ModelPath; // For llama.cpp server, this is the full URL like "http://localhost:8080/v1/chat/completions"
    
    // Validate endpoint
    if (!APIEndpoint.StartsWith(TEXT("http")))
    {
        APIEndpoint = TEXT("http://") + APIEndpoint;
    }
    
    // Test connection
    TSharedPtr<IHttpRequest> TestRequest = FHttpModule::Get().CreateRequest();
    TestRequest->SetURL(APIEndpoint);
    TestRequest->SetVerb(TEXT("GET"));
    TestRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    
    TestRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
    {
        if (!bSuccess || !Response.IsValid())
        {
            FString ErrorMsg = FString::Printf(TEXT("Failed to connect to LLM server at %s"), *APIEndpoint);
            OnError.Broadcast(ErrorMsg);
            return;
        }
        
        // Check if server is running llama.cpp
        if (Response->GetResponseCode() == 404)
        {
            // 404 is actually okay - it means the server is running but the test endpoint doesn't exist
            // This is expected for llama.cpp server
            OnReady.Broadcast(TEXT("Connected to llama.cpp server"));
        }
        else if (Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300)
        {
            OnReady.Broadcast(TEXT("Connected to LLM server"));
        }
        else
        {
            FString ErrorMsg = FString::Printf(TEXT("Server returned unexpected status code: %d"), Response->GetResponseCode());
            OnError.Broadcast(ErrorMsg);
        }
    });
    
    TestRequest->ProcessRequest();
}

void URemoteLLMProvider::ProcessInput(const FString& Input, const FString& HighFreqContext)
{
    if (bIsWaitingForResponse.load())
    {
        OnError.Broadcast(TEXT("Already processing a request"));
        return;
    }
    
    // Prepare OpenAI-compatible request
    TSharedPtr<FJsonObject> RequestObj = MakeShared<FJsonObject>();
    
    // Add messages array
    TArray<TSharedPtr<FJsonValue>> Messages;
    
    // System message from context
    if (ContextManager)
    {
        FString SystemPrompt = ContextManager->GetBlockContent(ELLMContextBlockType::SystemPrompt);
        if (!SystemPrompt.IsEmpty())
        {
            TSharedPtr<FJsonObject> SystemMsg = MakeShared<FJsonObject>();
            SystemMsg->SetStringField(TEXT("role"), TEXT("system"));
            SystemMsg->SetStringField(TEXT("content"), SystemPrompt);
            Messages.Add(MakeShared<FJsonValueObject>(SystemMsg));
        }
    }
    
    // Add user message
    TSharedPtr<FJsonObject> UserMsg = MakeShared<FJsonObject>();
    UserMsg->SetStringField(TEXT("role"), TEXT("user"));
    UserMsg->SetStringField(TEXT("content"), Input);
    Messages.Add(MakeShared<FJsonValueObject>(UserMsg));
    
    RequestObj->SetArrayField(TEXT("messages"), Messages);
    RequestObj->SetBoolField(TEXT("stream"), true);
    RequestObj->SetNumberField(TEXT("temperature"), 0.7f);
    RequestObj->SetNumberField(TEXT("max_tokens"), 2048);
    
    // Convert to string
    FString RequestBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(RequestObj.ToSharedRef(), Writer);
    
    // Create and configure request
    CurrentRequest = FHttpModule::Get().CreateRequest();
    CurrentRequest->SetURL(APIEndpoint);
    CurrentRequest->SetVerb(TEXT("POST"));
    CurrentRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    if (!APIKey.IsEmpty())
    {
        CurrentRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *APIKey));
    }
    CurrentRequest->SetContentAsString(RequestBody);
    
    // Set up response handling
    CurrentRequest->OnRequestProgress64().BindLambda([this](FHttpRequestPtr Request, int64 BytesSent, int64 BytesReceived)
    {
        if (BytesReceived > 0)
        {
            ResponseProgress.store(static_cast<float>(BytesReceived) / Request->GetContentLength());
        }
    });
    
    CurrentRequest->OnProcessRequestComplete().BindUObject(this, &URemoteLLMProvider::OnResponseReceived);
    
    // Start request with retry logic
    StartNewRequest();
}

// Helper function for retry logic
void URemoteLLMProvider::DoRetryRequest(int32& RetryCount, const int32 MaxRetries, const float RetryDelay)
{
    if (RetryCount >= MaxRetries)
    {
        OnError.Broadcast(TEXT("Max retries exceeded"));
        return;
    }
    
    if (RetryCount > 0)
    {
        // Wait before retry
        FPlatformProcess::Sleep(RetryDelay * RetryCount); // Exponential backoff
    }
    
    bIsWaitingForResponse.store(true);
    ResponseProgress.store(0.0f);
    ResponseBuffer.Empty();
    
    if (!CurrentRequest->ProcessRequest())
    {
        // Immediate failure (e.g., invalid URL)
        FString ErrorMsg = FString::Printf(TEXT("Failed to start request (attempt %d/%d)"), RetryCount + 1, MaxRetries);
        OnError.Broadcast(ErrorMsg);
        bIsWaitingForResponse.store(false);
        RetryCount++;
        DoRetryRequest(RetryCount, MaxRetries, RetryDelay); // Retry
    }
}

void URemoteLLMProvider::StartNewRequest()
{
    const int32 MaxRetries = 3;
    const float RetryDelay = 1.0f; // seconds
    int32 RetryCount = 0;
    
    DoRetryRequest(RetryCount, MaxRetries, RetryDelay);
}

void URemoteLLMProvider::OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
    if (!bSuccess || !Response.IsValid())
    {
        FString ErrorMsg = TEXT("Request failed or invalid response");
        OnError.Broadcast(ErrorMsg);
        bIsWaitingForResponse.store(false);
        return;
    }
    
    // Handle streaming response
    if (Response->GetContentType().Contains(TEXT("text/event-stream")))
    {
        FString Content = Response->GetContentAsString();
        TArray<FString> Lines;
        Content.ParseIntoArray(Lines, TEXT("\n"), true);
        
        for (const FString& Line : Lines)
        {
            if (Line.StartsWith(TEXT("data: ")))
            {
                FString Data = Line.RightChop(6); // Remove "data: "
                if (Data == TEXT("[DONE]"))
                {
                    // End of stream
                    bIsWaitingForResponse.store(false);
                    ResponseProgress.store(1.0f);
                    UpdateVisualizationState();
                    return;
                }
                
                // Parse JSON response
                TSharedPtr<FJsonObject> JsonObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
                if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
                {
                    // Extract token from OpenAI format
                    TArray<TSharedPtr<FJsonValue>> Choices = JsonObject->GetArrayField(TEXT("choices"));
                    if (Choices.Num() > 0)
                    {
                        TSharedPtr<FJsonObject> Choice = Choices[0]->AsObject();
                        TSharedPtr<FJsonObject> Delta = Choice->GetObjectField(TEXT("delta"));
                        if (Delta->HasField(TEXT("content")))
                        {
                            FString Token = Delta->GetStringField(TEXT("content"));
                            OnTokenGenerated.Broadcast(Token);
                            ResponseBuffer += Token;
                        }
                    }
                }
            }
        }
    }
    else
    {
        // Non-streaming response (error case)
        FString ErrorMsg = FString::Printf(TEXT("Unexpected response type: %s"), *Response->GetContentType());
        OnError.Broadcast(ErrorMsg);
    }
    
    bIsWaitingForResponse.store(false);
    UpdateVisualizationState();
}

void URemoteLLMProvider::Shutdown()
{
    if (CurrentRequest.IsValid())
    {
        CurrentRequest->CancelRequest();
    }
    bIsWaitingForResponse.store(false);
    ResponseProgress.store(0.0f);
    ResponseBuffer.Empty();
}

FContextVisPayload URemoteLLMProvider::GetContextVisualization() const
{
    FContextVisPayload Payload;
    
    // For remote provider, we show a simpler visualization
    Payload.TotalTokenCapacity = 4096; // Example value
    Payload.KvCacheDecodedTokenCount = ResponseBuffer.Len(); // Approximate token count
    
    // Add a single block for the current response
    if (!ResponseBuffer.IsEmpty())
    {
        float Progress = ResponseProgress.load();
        Payload.Blocks.Add(FContextVisBlock(
            EContextVisBlockType::ConversationTurnAssistant,
            0.0f,
            Progress,
            FLinearColor::Blue,
            FText::FromString(FString::Printf(TEXT("Response (%.1f%%)"), Progress * 100.0f))
        ));
        
        // Add free space
        if (Progress < 1.0f)
        {
            Payload.Blocks.Add(FContextVisBlock(
                EContextVisBlockType::FreeSpace,
                Progress,
                1.0f - Progress,
                FLinearColor(0.1f, 0.1f, 0.1f, 0.5f),
                FText::FromString(TEXT("Waiting for response..."))
            ));
        }
    }
    
    return Payload;
}

void URemoteLLMProvider::UpdateVisualizationState()
{
    OnContextChanged.Broadcast(GetContextVisualization());
}

void URemoteLLMProvider::UpdateContextBlock(ELLMContextBlockType BlockType, const FString& NewContent)
{
    if (ContextManager)
    {
        ContextManager->UpdateBlock(BlockType, NewContent);
        UpdateVisualizationState();
    }
}

ULLMContextManager* URemoteLLMProvider::GetContextManager() const
{
    return ContextManager;
}

void URemoteLLMProvider::SetContextManager(ULLMContextManager* InContextManager)
{
    ContextManager = InContextManager;
}

void URemoteLLMProvider::BroadcastContextUpdate(const FContextVisPayload& Payload)
{
    OnContextChanged.Broadcast(Payload);
} 