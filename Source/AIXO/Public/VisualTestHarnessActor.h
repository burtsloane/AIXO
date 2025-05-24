#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubmarineState.h"
#include "SSubDiagram.h"
#include "CommandDistributor.h"
#include "UnrealRenderingContext.h"
#include "Components/Image.h" // For UImage
#include "Components/NativeWidgetHost.h"
#include "ICH_PowerJunction.h"
#include "PWR_PowerSegment.h"
#include "VisualTestHarnessActor.generated.h"

// Forward Declarations
class UUserWidget;
class UEditableTextBox;
class UButton;
class UScrollBox;
class UTextBlock;
class UTextureRenderTarget2D;
class UTexture2D;
class UInputMappingContext;
class UInputAction;
class ULlamaComponent;
class VisualizationManager;
struct FInputActionValue;

// Touch tracking struct
struct FTouchInfo
{
	int32 PointerId;
	FVector2D InitialPosition;  // Initial touch position in world space
	FVector2D CurrentPosition;  // Current touch position in world space
	FVector2D InitialPanOffset; // ViewOffset when touch started
	float InitialScale;         // ViewScale when touch started
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHarnessAndLlamaReady, ULlamaComponent*, LlamaComponent);

UCLASS()
class AIXO_API AVisualTestHarnessActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVisualTestHarnessActor();
	virtual ~AVisualTestHarnessActor() override;  // Add virtual destructor

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	TSharedPtr<SSubDiagram> DiagramSlate;            // Slate widget instance
	TObjectPtr<UNativeWidgetHost> SubDiagramHost;    // BP host (UWidget)

	// Core systems
    CommandDistributor CmdDistributor; // Manages command handlers
	VisualizationManager* VizManager; // Changed from TUniquePtr to raw pointer
    TUniquePtr<UnrealRenderingContext> RenderContext; // Handles drawing

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Submarine")
    TObjectPtr<ASubmarineState> SubmarineState; 

protected:
	// UMG Widget references
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSubclassOf<UUserWidget> TestHarnessWidgetClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UUserWidget> TestHarnessWidgetInstance;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UEditableTextBox> CommandInputBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UButton> SendCommandButton;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UScrollBox> LogScrollBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UTextBlock> LogOutputText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UScrollBox> StateScrollBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UTextBlock> StateOutputText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UImage> VisualizationImage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    FVector2D VisTextureSize = FVector2D(1024, 1024); // Default size BUT THIS DOESN"T SEEM TO MATTER ANY MORE???

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
    FBox2D VisExtent = FBox2D(FVector2D(100, 100), FVector2D(524, 524));

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visualization")
    TObjectPtr<UTextureRenderTarget2D> VisRenderTarget;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visualization")
    TObjectPtr<UTexture2D> SolidColorTexture;

    // Map to store marker names and their values for JSON generation/loading
    TMap<FString, float> GridMarkerDefinitions;

    // Arrays/Map to hold the grid loaded from JSON
    TMap<FString, ICH_PowerJunction*> JunctionMap;

	// Input Handling
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> InputMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> VisInteractAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> ZoomInAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> ZoomOutAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> PanAction;

	// Zoom and pan state
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visualization")
	FVector2D ViewOffset = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visualization")
	float ViewScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	float MinZoom = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	float MaxZoom = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization")
	float ZoomStep = 0.2f;

	// UI elements for zoom controls
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UButton> ZoomInButton;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UButton> ZoomOutButton;

	// Initialization functions - moved to cpp
	void CreateWidgetAndGetReferences();
	void InitializeCommandHandlers();
	void InitializeTestSystems();
	void InitializeVisualization();

	// UI Update and Logging - moved to cpp
	void UpdateStateDisplay();
	void AddLogMessage(const FString& Message);

	// UI Event Handlers - moved to cpp
	UFUNCTION()
	void OnSendCommandClicked();

	UFUNCTION()
	void OnCommandTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION(BlueprintCallable)
	bool HandleMouseTap(const FVector2D& WidgetPosition, int32 TouchType, int32 PointerIndex);

	// Helper for grid setup - moved to cpp
	void MakeConnectingSegment(const FString& Name, ICH_PowerJunction *JCT, int32 JCT_side, int32 JCT_offset, ICH_PowerJunction *SS, int32 SS_side, int32 SS_offset);

public:
    UPROPERTY(BlueprintAssignable, Category = "AIXO")
    FOnHarnessAndLlamaReady OnHarnessAndLlamaReady;
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AIXO Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<ULlamaComponent> LlamaAIXOComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config")
    FString PathToModel;

private:
	// Hold loaded junctions/segments alive when JSON is imported
	TArray<TUniquePtr<ICH_PowerJunction>> PersistentJunctions;
	TArray<TUniquePtr<PWR_PowerSegment>>  PersistentSegments;

	FString SystemsContextBlockRecent;
	FString LowFreqContextBlockRecent;
    bool bPendingStaticWorldInfoUpdate = false;
    FString PendingStaticWorldInfoText;
    bool bPendingLowFrequencyStateUpdate = false;
    FString PendingLowFrequencyStateText;

public:
	UPROPERTY(EditDefaultsOnly, Category="UI")
	UFont* TinyFont = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="UI")
	UFont* SmallFont = nullptr;

	// Input handlers - moved to cpp
	void OnZoomInTriggered(const FInputActionValue& Value);
	void OnZoomOutTriggered(const FInputActionValue& Value);
	void OnPanTriggered(const FInputActionValue& Value);
	void OnPanStarted(const FInputActionValue& Value);
	void OnPanCompleted(const FInputActionValue& Value);

	// UI event handlers for zoom buttons - moved to cpp
	UFUNCTION()
	void OnZoomInButtonClicked();

	UFUNCTION()
	void OnZoomOutButtonClicked();

	// Touch gesture tracking
	bool bIsPanning = false;
	bool bIsPinching = false;
	bool bIsOnJunction = false;
	TMap<int32, FTouchInfo> ActiveTouches;  // Map of pointer ID to touch info
	float InitialPinchDistance = 0.0f;

	// Helper functions - moved to cpp
	void ApplyZoom(float Delta);
	void ApplyPan(const FVector2D& Delta, float Scale);
	bool IsPointInEmptySpace(const FVector2D& Point) const;

public:
	UFUNCTION()
	void ProcessToolCall(const FString &ToolCallJsonRaw);
	void HandleToolCall_GetSystemInfo(const FString& QueryString);
	void HandleToolCall_CommandSubmarineSystem(const FString& QueryString);
	void HandleToolCall_QuerySubmarineSystem(const FString& QueryString);
	FString MakeCommandHandlerString(ICommandHandler *ich);
	FString MakeSystemsBlock();
	FString MakeStatusBlock();
	FString MakeHFSString();
	void SendToolResponseToLlama(const FString& ToolName, const FString& JsonResponseContent);

// calls from blueprints
	// Command processing passthrough - moved to cpp
	UFUNCTION(BlueprintCallable, Category = "Commands")
	void ProcessCommandString(const FString& Command);

	// State query passthrough - moved to cpp
	UFUNCTION(BlueprintCallable, Category = "Commands")
	TArray<FString> GetAvailableCommandsList();

	UFUNCTION(BlueprintCallable, Category = "Commands")
	TArray<FString> QueryEntireStateList();

	UFUNCTION(BlueprintCallable, Category = "Commands")
	TArray<FString> GetSystemNotifications();

	UFUNCTION(BlueprintCallable, Category = "Commands")
	FContextVisPayload AugmentContextVisPayload(FContextVisPayload p);

public:
	friend class VisualizationManager;
	friend class SSubDiagram;
	friend class ULlamaComponent;
}; 
