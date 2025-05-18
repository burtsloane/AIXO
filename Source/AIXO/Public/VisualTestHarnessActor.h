#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SubmarineState.h"
#include "SSubDiagram.h"
#include "CommandDistributor.h"
#include "VisualizationManager.h"
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
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHarnessAndLlamaReady, ULlamaComponent*, LlamaComponent);

UCLASS()
class AIXO_API AVisualTestHarnessActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVisualTestHarnessActor();

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
	TUniquePtr<VisualizationManager> VizManager; // Manages visual elements
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
    FVector2D VisTextureSize = FVector2D(1024, 1024); // Default size

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

	// Initialization functions
	void CreateWidgetAndGetReferences();
	void InitializeCommandHandlers();
	void InitializeTestSystems();
	void InitializeVisualization();
	// Reverted: Remove JSON init function declaration
	// void InitializeGridFromJSON(); 

	// UI Update and Logging
	void UpdateStateDisplay();
	void AddLogMessage(const FString& Message);

	// UI Event Handlers
	UFUNCTION()
	void OnSendCommandClicked();

	UFUNCTION()
	void OnCommandTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION(BlueprintCallable)
	void HandleMouseTap(const FVector2D& WidgetPosition, int32 TouchType, int32 PointerIndex);


#ifdef never
	void HandleVisualizationTap(const FVector2D& ScreenPosition, TouchEvent::EType TouchType, int32 PointerIndex);
	// Input Action Handlers
	void OnVisInteractTriggered(const FInputActionValue& Value);
#endif // never

	// Helper for grid setup (kept for reference, but should be removed after JSON transition)
	void MakeConnectingSegment(const FString& Name, ICH_PowerJunction *JCT, int32 JCT_side, int32 JCT_offset, ICH_PowerJunction *SS, int32 SS_side, int32 SS_offset);

public:
	// Command processing passthrough
	UFUNCTION(BlueprintCallable, Category = "Commands")
	void ProcessCommandString(const FString& Command);

    UPROPERTY(BlueprintAssignable, Category = "AIXO")
    FOnHarnessAndLlamaReady OnHarnessAndLlamaReady;
    
	// State query passthrough
	UFUNCTION(BlueprintCallable, Category = "Commands")
	TArray<FString> GetAvailableCommandsList();

	UFUNCTION(BlueprintCallable, Category = "Commands")
	TArray<FString> QueryEntireStateList();

	UFUNCTION(BlueprintCallable, Category = "Commands")
	TArray<FString> GetSystemNotifications();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AIXO Components", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<ULlamaComponent> LlamaAIXOComponent; // Use TObjectPtr for modern UE

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config")
    FString PathToModel; // Renamed from pathToModel

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Llama|Config", meta = (MultiLine = true))
    FString SystemPromptText; // Renamed from prompt

private:
	// Hold loaded junctions/segments alive when JSON is imported
	TArray<TUniquePtr<ICH_PowerJunction>> PersistentJunctions;
	TArray<TUniquePtr<PWR_PowerSegment>>  PersistentSegments;

public:
	UPROPERTY(EditDefaultsOnly, Category="UI")
	UFont* TinyFont = nullptr;          // assign in the editor

	UPROPERTY(EditDefaultsOnly, Category="UI")
	UFont* SmallFont = nullptr;          // assign in the editor

	friend class SSubDiagram;
	friend class ULlamaComponent;

	// Input handlers
	void OnZoomInTriggered(const FInputActionValue& Value);
	void OnZoomOutTriggered(const FInputActionValue& Value);
	void OnPanTriggered(const FInputActionValue& Value);
	void OnPanStarted(const FInputActionValue& Value);
	void OnPanCompleted(const FInputActionValue& Value);

	// UI event handlers for zoom buttons
	UFUNCTION()
	void OnZoomInButtonClicked();

	UFUNCTION()
	void OnZoomOutButtonClicked();

	// Helper functions
	void ApplyZoom(float Delta);
	void ApplyPan(const FVector2D& Delta);
	bool IsPointInEmptySpace(const FVector2D& Point) const;

	// Touch gesture tracking
	bool bIsPanning = false;
	FVector2D LastPanPosition;
	float InitialPinchDistance = 0.0f;
	bool bIsPinching = false;
}; 
