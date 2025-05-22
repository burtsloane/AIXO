#include "VisualTestHarnessActor.h"
#include "VisualizationManager.h"
#include "GameFramework/Actor.h" // Explicitly include Actor base class here
// ... other includes ...
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "Input/Events.h" // For FPointerEvent
#include "GameFramework/PlayerController.h" // Needed for input
#include "Components/InputComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Texture2D.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"

// Include necessary VE headers if not forward declared
#include "UnrealRenderingContext.h"
#include "VE_ToggleButton.h"
#include "VE_CommandButton.h"
#include "VE_Slider.h"

// include the rest of the submarine
#include "PID_HoverDepth.h"
#include "PID_PlaneDepthRate.h"
#include "PID_PlanePitch.h"
#include "PID_RudderHeading.h"
#include "PID_TrimPitch.h"
#include "SS_AIP.h"
#include "SS_AirCompressor.h"
#include "SS_Airlock.h"
#include "SS_Battery.h"
#include "SS_BowPlanes.h"
#include "SS_ControlRoom.h"
#include "SS_Countermeasures.h"
#include "SS_XTBTPump.h"
#include "SS_Electrolysis.h"
#include "SS_Elevator.h"
//#include "SS_FMBTVent.h"
#include "SS_FTBTPump.h"
#include "SS_MainMotor.h"
//#include "SS_RMBTVent.h"
#include "SS_MBT.h"
#include "SS_RTBTPump.h"
#include "SS_Rudder.h"
#include "SS_SolarPanels.h"
#include "SS_Sonar.h"
#include "SS_TBT.h"
#include "SS_TorpedoLoader.h"
#include "SS_TorpedoTube.h"

#include "PWR_PowerPropagation.h"
#include "PWRJ_MultiConnJunction.h"

// Add includes for the newly separated classes
#include "SS_Degaussing.h"
#include "SS_CO2Scrubber.h"
#include "SS_O2Generator.h"
#include "SS_Dehumidifier.h"
#include "SS_ROVCharging.h"
#include "SS_Hatch.h"
#include "SS_GPS.h"
#include "SS_Camera.h"
#include "SS_Antenna.h"
#include "SS_Radar.h"
#include "SS_TowedSonarArray.h"

#include "UPowerGridLoader.h" // Need this again for the generation call
#include "HAL/PlatformFileManager.h" // Required for directory creation

#include "LlamaComponent.h"

const bool bGenerateJson = true;   // ←‑‑ set to false to load, true to generate JSON

AVisualTestHarnessActor::AVisualTestHarnessActor()
{
	PrimaryActorTick.bCanEverTick = true;

    InputComponent = CreateDefaultSubobject<UEnhancedInputComponent>(TEXT("EnhancedInputComponent"));

    // Create the SubmarineState UObject
    if (!SubmarineState) {
        SubmarineState = CreateDefaultSubobject<ASubmarineState>(TEXT("SubmarineStateInstance"));
    }

    // Find the Engine's White Square Texture asset
    static ConstructorHelpers::FObjectFinder<UTexture2D> WhiteTexFinder(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    if (WhiteTexFinder.Succeeded())
    {
        SolidColorTexture = WhiteTexFinder.Object;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find /Engine/EngineResources/WhiteSquareTexture!"));
    }
    
    LlamaAIXOComponent = CreateDefaultSubobject<ULlamaComponent>(TEXT("LlamaAIXOComponentInstance"));
    LlamaAIXOComponent->PathToModel = PathToModel;
}

AVisualTestHarnessActor::~AVisualTestHarnessActor()
{
    delete VizManager;  // Clean up the raw pointer
}

void AVisualTestHarnessActor::BeginPlay()
{
	Super::BeginPlay();

    // Get the player controller
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get PlayerController in VisualTestHarnessActor::BeginPlay."));
        return;
    }

    // Set up UI input mode
    FInputModeGameAndUI InputModeData;
    InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputModeData.SetHideCursorDuringCapture(false);
    PC->SetInputMode(InputModeData);
    PC->bShowMouseCursor = true;

    // Set up enhanced input
    if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent))
    {
        if (ZoomInAction)
        {
            EnhancedInputComponent->BindAction(ZoomInAction, ETriggerEvent::Triggered, this, &AVisualTestHarnessActor::OnZoomInTriggered);
        }
        if (ZoomOutAction)
        {
            EnhancedInputComponent->BindAction(ZoomOutAction, ETriggerEvent::Triggered, this, &AVisualTestHarnessActor::OnZoomOutTriggered);
        }
        if (PanAction)
        {
            EnhancedInputComponent->BindAction(PanAction, ETriggerEvent::Started, this, &AVisualTestHarnessActor::OnPanStarted);
            EnhancedInputComponent->BindAction(PanAction, ETriggerEvent::Triggered, this, &AVisualTestHarnessActor::OnPanTriggered);
            EnhancedInputComponent->BindAction(PanAction, ETriggerEvent::Completed, this, &AVisualTestHarnessActor::OnPanCompleted);
        }
    }

    CreateWidgetAndGetReferences();

	if (bGenerateJson) {			// true: generate JSON, false: read JSON
		// --- JSON GENERATION STEP --- 
		InitializeCommandHandlers();
		{
			FString OutputJsonPath = FPaths::ProjectContentDir() + TEXT("Data/PowerGridDefinition.json");
			FString DataDir = FPaths::GetPath(OutputJsonPath);
			if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*DataDir))
			{
				FPlatformFileManager::Get().GetPlatformFile().CreateDirectory(*DataDir);
			}

			// Pass the populated GridMarkerDefinitions map
			if (UPowerGridLoader::GenerateJsonFromGrid(CmdDistributor, OutputJsonPath, GridMarkerDefinitions))
			{
				UE_LOG(LogTemp, Warning, TEXT("****** Successfully generated PowerGridDefinition.json ******"));
				AddLogMessage(TEXT("GENERATED PowerGridDefinition.json"));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("****** FAILED to generate PowerGridDefinition.json ******"));
				 AddLogMessage(TEXT("ERROR generating PowerGridDefinition.json"));
			}
		}
		// --- END JSON GENERATION STEP ---
    } else {
		// --- TEMPORARY JSON PARSER --- 
		FString InputJsonPath = FPaths::ProjectContentDir() + TEXT("Data/PowerGridDefinition.json");

		TArray<TUniquePtr<ICH_PowerJunction>>  TempJunctions;
		TArray<TUniquePtr<PWR_PowerSegment>>   TempSegments;
		TMap<FString, ICH_PowerJunction*>      TempJunctionMap;
		TMap<FString, float>	 			   TempMarkerDefinitions;

		if (UPowerGridLoader::LoadPowerGridFromJson(
				InputJsonPath,
				SubmarineState.Get(),
				TempJunctions,
				TempSegments,
				TempJunctionMap,
				TempMarkerDefinitions))
		{
			UE_LOG(LogTemp, Warning, TEXT("****** Successfully loaded PowerGridDefinition.json ******"));
			AddLogMessage(TEXT("LOADED PowerGridDefinition.json"));

			// Keep them alive for the rest of the run
			for (TUniquePtr<ICH_PowerJunction>& JunPtr : TempJunctions)
			{
				CmdDistributor.RegisterHandler(JunPtr.Get());
				PersistentJunctions.Add(MoveTemp(JunPtr));
			}
			for (TUniquePtr<PWR_PowerSegment>& SegPtr : TempSegments)
			{
				CmdDistributor.RegisterSegment(SegPtr.Get());
				PersistentSegments.Add(MoveTemp(SegPtr));
			}
			GridMarkerDefinitions = TempMarkerDefinitions;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("****** FAILED to load PowerGridDefinition.json ******"));
			AddLogMessage(TEXT("ERROR loading PowerGridDefinition.json"));
		}
		// --- END JSON PARSER --- 
    }

    InitializeTestSystems();
    InitializeVisualization();
	if (VizManager) VizManager->ClearSelections();
	
	for (ICommandHandler* Handler : CmdDistributor.GetCommandHandlers())
	{
		if (Handler)
		{
			ICH_PowerJunction* Junction = Handler->GetAsPowerJunction();		// downcast without RTTI
			if (Junction)
			{
				if (VisExtent.Min.X > Junction->X) VisExtent.Min.X = Junction->X;
				if (VisExtent.Max.X < Junction->X+Junction->W) VisExtent.Max.X = Junction->X+Junction->W;
				if (VisExtent.Min.Y > Junction->Y) VisExtent.Min.Y = Junction->Y;
				if (VisExtent.Max.Y < Junction->Y+Junction->W) VisExtent.Max.Y = Junction->Y+Junction->W;
			}
		}
	}
	
    if (LlamaAIXOComponent)
    {
        // Call your custom initialization function on the component
        LlamaAIXOComponent->ActivateLlamaComponent(this); // Pass 'this' (the harness actor)
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("AVisualTestHarnessActor: LlamaAIXOComponent is null in BeginPlay! Check constructor or Blueprint setup."));
    }

	if (LlamaAIXOComponent /*&& LlamaAIXOComponent->IsLlamaCoreReady()*/) // Assuming IsLlamaCoreReady flag
	{
		OnHarnessAndLlamaReady.Broadcast(LlamaAIXOComponent);
	}
}

void AVisualTestHarnessActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

    UpdateStateDisplay();

    if (RenderContext && VizManager)
    {
		PWR_PowerPropagation::PropagatePower(VizManager->Segments, VizManager->Junctions);

        if (RenderContext->BeginDrawing()) 
        {
        	// fill with white
			FGeometry ImageGeometry = VisualizationImage->GetCachedGeometry();
			FBox2D r;
			r.Min.X = 0;
			r.Min.Y = 0;
			r.Max.X = VisTextureSize.X;
			r.Max.Y = VisTextureSize.Y;
			RenderContext->DrawRectangle(r, FLinearColor::White, true);

            // Apply view transform
            RenderContext->PushTransform(FTransform2D(ViewScale, ViewOffset));

			r.Min.X = VisExtent.Min.X;
			r.Min.Y = VisExtent.Min.Y;
			r.Max.X = VisExtent.Max.X;
			r.Max.Y = VisExtent.Max.Y;
			RenderContext->DrawRectangle(r, FLinearColor(0.9f, 0.9f, 0.9f), false);

	// show the guide marks
	for (const auto& Entry : GridMarkerDefinitions)
	{
		FString Key = Entry.Key;
		float Value = Entry.Value;

		// Access key and value
		if (Key[0] == 'Y') {
			r.Min.X = VisExtent.Min.X;
			r.Min.Y = Value;
			r.Max.X = VisExtent.Max.X;
			r.Max.Y = Value+1;
			RenderContext->DrawRectangle(r, FLinearColor(0.8f, 0.8f, 0.8f), false);
		} else if (Key[0] == 'X') {
			r.Min.X = Value;
			r.Min.Y = VisExtent.Min.Y;
			r.Max.X = Value+1;
			r.Max.Y = VisExtent.Max.Y;
			RenderContext->DrawRectangle(r, FLinearColor(0.8f, 0.8f, 0.8f), false);
		}
	}

	// show all CH rectangles
//	for (ICommandHandler* Handler : CmdDistributor.GetCommandHandlers())
//	{
//		if (Handler)
//		{
//			ICH_PowerJunction* Junction = Handler->GetAsPowerJunction();		// downcast without RTTI
//			if (Junction)
//			{
//				Junction->RenderHighlights(*RenderContext);
//			}
//		}
//	}

            VizManager->Render(*RenderContext);

            RenderContext->PopTransform();

            RenderContext->EndDrawing(); 

			if (DiagramSlate.IsValid())
				DiagramSlate->InvalidateFast();
		}
        else
        {
            // Add Log: Error if BeginDrawing fails
            UE_LOG(LogTemp, Error, TEXT("Tick: RenderContext->BeginDrawing() failed!"));
        }
    }
    else 
    {
        // Add Log: Indicate why rendering might not be happening
        if (!RenderContext) UE_LOG(LogTemp, Warning, TEXT("Tick: RenderContext is null."));
        if (!VizManager) UE_LOG(LogTemp, Warning, TEXT("Tick: VizManager is null."));
    }

    CmdDistributor.TickAll(DeltaTime);
}

void AVisualTestHarnessActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    delete VizManager;  // Clean up
    VizManager = nullptr;
    RenderContext.Reset();

    Super::EndPlay(EndPlayReason);
}

void AVisualTestHarnessActor::InitializeTestSystems()
{
    AddLogMessage(TEXT("Test Systems Initialized."));
}

void AVisualTestHarnessActor::InitializeVisualization()
{
    // Create the visualization manager
    delete VizManager;  // Clean up any existing instance
    VizManager = new VisualizationManager();  // Create new instance
    UE_LOG(LogTemp, Log, TEXT("InitializeVisualization: VizManager created."));
    
    VisRenderTarget = NewObject<UTextureRenderTarget2D>(this);
    if (!VisRenderTarget)
    {
        UE_LOG(LogTemp, Error, TEXT("InitializeVisualization: Failed to create VisRenderTarget!"));
        return;
    }
    // Add Log: Confirm render target format/size
    UE_LOG(LogTemp, Log, TEXT("InitializeVisualization: VisRenderTarget created (Size: %d x %d)."), static_cast<int32>(VisTextureSize.X), static_cast<int32>(VisTextureSize.Y));
    VisRenderTarget->InitCustomFormat(VisTextureSize.X, VisTextureSize.Y, PF_B8G8R8A8, true); 
    VisRenderTarget->UpdateResourceImmediate(true);

    if (VisualizationImage)
    {
        // Add Log: Confirm assigning render target to image
        UE_LOG(LogTemp, Log, TEXT("InitializeVisualization: Assigning VisRenderTarget to VisualizationImage."));
        VisualizationImage->SetBrushResourceObject(VisRenderTarget.Get());
    }
    else 
    {
        UE_LOG(LogTemp, Warning, TEXT("InitializeVisualization: VisualizationImage is null!"));
    }

    // Add Log: Check SolidColorTexture validity
    if (!SolidColorTexture)
    {
        UE_LOG(LogTemp, Error, TEXT("InitializeVisualization: SolidColorTexture is NOT valid!"));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("InitializeVisualization: SolidColorTexture is valid."));
    }

    RenderContext = MakeUnique<UnrealRenderingContext>(GetWorld(), VisRenderTarget.Get(), SolidColorTexture.Get());
    UE_LOG(LogTemp, Log, TEXT("InitializeVisualization: RenderContext created."));

	if (SubDiagramHost && RenderContext)
	{
		DiagramSlate = SNew(SSubDiagram)
					   .OwnerActor(this)
					   .Verts(RenderContext->GetPresentedVertices())
					   .Indices(RenderContext->GetPresentedIndices());

		SubDiagramHost->SetContent(DiagramSlate.ToSharedRef());

		UE_LOG(LogTemp, Log, TEXT("DiagramSlate created: verts=%d"),
			   RenderContext->GetPresentedVertices()->Num());
	}

    if (VizManager) {
        // Add command handlers
        for (ICommandHandler* Handler : CmdDistributor.GetCommandHandlers())
        {
            if (Handler)
            {
                ICH_PowerJunction* Junction = Handler->GetAsPowerJunction();		// downcast without RTTI
                if (Junction)
                {
                    VizManager->AddJunction(Junction);
                    Junction->InitializeVisualElements(); // Ensure visuals are initialized
                    Junction->CalculateExtentsVisualElements();
                }
            }
        }
    	for (PWR_PowerSegment *Segment : CmdDistributor.GetSegments())
    	{
    		if (Segment)
    		{
    			VizManager->AddSegment(Segment);
    		}
    	}
    }
    else 
    {
         UE_LOG(LogTemp, Error, TEXT("InitializeVisualization: VizManager is unexpectedly null after creation!"));
    }
    
    // Add Log: Indicate visualization initialization finished
    UE_LOG(LogTemp, Log, TEXT("InitializeVisualization: Finished adding junctions to VizManager."));
    AddLogMessage(TEXT("Visualization Initialized."));
}

void AVisualTestHarnessActor::CreateWidgetAndGetReferences()
{
    if (!TestHarnessWidgetClass) 
    {
        UE_LOG(LogTemp, Error, TEXT("CreateWidgetAndGetReferences: TestHarnessWidgetClass not set!"));
        return;
    }

    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
    if (!PlayerController) 
    {
        UE_LOG(LogTemp, Error, TEXT("CreateWidgetAndGetReferences: Failed to get PlayerController!"));
        return;
    }

    TestHarnessWidgetInstance = CreateWidget<UUserWidget>(PlayerController, TestHarnessWidgetClass);
    if (!TestHarnessWidgetInstance)
    { 
        UE_LOG(LogTemp, Error, TEXT("CreateWidgetAndGetReferences: Failed to create TestHarnessWidgetInstance!"));
        return;
    }
    // Add Log: Confirm widget instance creation
    UE_LOG(LogTemp, Log, TEXT("CreateWidgetAndGetReferences: TestHarnessWidgetInstance created successfully."));

    CommandInputBox = Cast<UEditableTextBox>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("CommandInputBox")));
    SendCommandButton = Cast<UButton>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("SendCommandButton")));
    LogScrollBox = Cast<UScrollBox>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("LogScrollBox")));
    LogOutputText = Cast<UTextBlock>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("LogOutputText")));
    StateScrollBox = Cast<UScrollBox>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("StateScrollBox")));
    StateOutputText = Cast<UTextBlock>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("StateOutputText")));
    
    // Add Log: Check widget retrieval before casting
    UWidget* FoundWidget = TestHarnessWidgetInstance->GetWidgetFromName(TEXT("VisualizationImage"));
    if (!FoundWidget)
    {
        UE_LOG(LogTemp, Error, TEXT("CreateWidgetAndGetReferences: GetWidgetFromName failed to find 'VisualizationImage'!"));
    }
    else
    {
        VisualizationImage = Cast<UImage>(FoundWidget);
        if (!VisualizationImage)
        {
            UE_LOG(LogTemp, Error, TEXT("CreateWidgetAndGetReferences: Found 'VisualizationImage' but failed to Cast to UImage!"));
        }
        else
        {
			UE_LOG(LogTemp, Log, TEXT("CreateWidgetAndGetReferences: Successfully found and cast 'VisualizationImage'."));
        }
    }

    if (SendCommandButton) {
        SendCommandButton->OnClicked.AddDynamic(this, &AVisualTestHarnessActor::OnSendCommandClicked);
    }
    if (CommandInputBox) {
        CommandInputBox->OnTextCommitted.AddDynamic(this, &AVisualTestHarnessActor::OnCommandTextCommitted);
    }

    TestHarnessWidgetInstance->AddToViewport();

    SubDiagramHost = Cast<UNativeWidgetHost>(
        TestHarnessWidgetInstance->GetWidgetFromName(TEXT("SubDiagramHost")));

    if (!SubDiagramHost)
        UE_LOG(LogTemp, Error, TEXT("SubDiagramHost not found"));

    // Get references to zoom buttons
    if (TestHarnessWidgetInstance)
    {
        ZoomInButton = Cast<UButton>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("ZoomInButton")));
        ZoomOutButton = Cast<UButton>(TestHarnessWidgetInstance->GetWidgetFromName(TEXT("ZoomOutButton")));

        if (ZoomInButton)
        {
            ZoomInButton->OnClicked.AddDynamic(this, &AVisualTestHarnessActor::OnZoomInButtonClicked);
        }
        if (ZoomOutButton)
        {
            ZoomOutButton->OnClicked.AddDynamic(this, &AVisualTestHarnessActor::OnZoomOutButtonClicked);
        }
    }
}

void AVisualTestHarnessActor::UpdateStateDisplay()
{
    if (StateOutputText && SubmarineState)
    {
        // Assuming SubmarineState has a suitable DumpState method
        // StateOutputText->SetText(FText::FromString(SubmarineState->DumpState())); 
        StateOutputText->SetText(FText::FromString(TEXT("Submarine State Display Placeholder")));
    }
}

void AVisualTestHarnessActor::AddLogMessage(const FString& Message)
{
    if (LogOutputText)
    {
        FString ExistingLog = LogOutputText->GetText().ToString();
        ExistingLog.Append(TEXT("\n"));
        ExistingLog.Append(Message);
        LogOutputText->SetText(FText::FromString(ExistingLog));
        if (LogScrollBox) {
            LogScrollBox->ScrollToEnd();
        }
    }
    UE_LOG(LogTemp, Log, TEXT("TestHarness: %s"), *Message);
}

bool AVisualTestHarnessActor::HandleMouseTap(const FVector2D& WidgetPosition, int32 InTouchType, int32 PointerIndex)
{
    TouchEvent::EType TouchType = (TouchEvent::EType)InTouchType;
    // Ignore mouse move events when no button is pressed (hover events)
    if (TouchType == TouchEvent::EType::Move && !bIsPanning && !bIsPinching && !bIsOnJunction)
    {
        return false;
    }

//    UE_LOG(LogTemp, Log, TEXT("HandleMouseTap: Type=%d, Index=%d, Pos=(%.1f, %.1f)"), TouchType, PointerIndex, WidgetPosition.X, WidgetPosition.Y);
//    ICH_PowerJunction *pj = VizManager->Junctions[0];
//    UE_LOG(LogTemp, Log, TEXT("              : Junctions[0]=%s (%.1f, %.1f, %.1f, %.1f)"), *pj->GetSystemName(), pj->X, pj->Y, pj->W, pj->H);

    // Get the widget's geometry
    FGeometry ImageGeometry = VisualizationImage->GetCachedGeometry();
    
    // Get the widget's paint geometry (actual painted position in viewport)
    FVector2D PaintPosition = ImageGeometry.GetAbsolutePosition();
    FVector2D PaintSize = ImageGeometry.GetLocalSize();
    
//    UE_LOG(LogTemp, Log, TEXT("  Paint position: (%.1f, %.1f)"), PaintPosition.X, PaintPosition.Y);
//    UE_LOG(LogTemp, Log, TEXT("  Paint size: (%.1f, %.1f)"), PaintSize.X, PaintSize.Y);
    
    // Convert to diagram space (0 to VisTextureSize)
    FVector2D DiagramPosition;
//     = FVector2D(
//        (WidgetPosition.X / PaintSize.X) * VisTextureSize.X,
//        (WidgetPosition.Y / PaintSize.Y) * VisTextureSize.Y
//    );
	DiagramPosition = WidgetPosition;

    // Then convert from diagram space to world space
    FTransform2D ViewTransform(ViewScale, ViewOffset);
    FTransform2D InverseViewTransform = ViewTransform.Inverse();
    FVector2D WorldPosition = InverseViewTransform.TransformPoint(DiagramPosition);
    
//    {
//		FVector2D LocalPosition = ImageGeometry.AbsoluteToLocal(WidgetPosition);
//		FVector2D LWorldPosition = ImageGeometry.LocalToAbsolute(WidgetPosition);
////		UE_LOG(LogTemp, Warning, TEXT("              : %f, %f type: %d index: %d"), Evt.Position.X, Evt.Position.Y, TouchType, PointerIndex);
//		UE_LOG(LogTemp, Warning, TEXT("              : Widget-relative position: %f, %f"), WidgetPosition.X, WidgetPosition.Y);
//		UE_LOG(LogTemp, Warning, TEXT("              : Local position: %f, %f"), LocalPosition.X, LocalPosition.Y);
//		UE_LOG(LogTemp, Warning, TEXT("              : Local position: %f, %f"), LWorldPosition.X, LWorldPosition.Y);
//		UE_LOG(LogTemp, Warning, TEXT("              : VisTextureSize: %f, %f"), VisTextureSize.X, VisTextureSize.Y);
//		UE_LOG(LogTemp, Warning, TEXT("              : ImageGeometry.GetLocalSize(): %f, %f"), ImageGeometry.GetLocalSize().X, ImageGeometry.GetLocalSize().Y);
//		UE_LOG(LogTemp, Warning, TEXT("              : ImageGeometry.GetAbsoluteSize(): %f, %f"), ImageGeometry.GetAbsoluteSize().X, ImageGeometry.GetAbsoluteSize().Y);
//    }
    
//    UE_LOG(LogTemp, Log, TEXT("  Diagram space: (%.1f, %.1f)"), DiagramPosition.X, DiagramPosition.Y);
//    UE_LOG(LogTemp, Log, TEXT("  World space: (%.1f, %.1f), ViewOffset=(%.1f, %.1f), ViewScale=%.2f"),
//           WorldPosition.X, WorldPosition.Y, ViewOffset.X, ViewOffset.Y, ViewScale);

    // Handle touch events for pinch-to-zoom
    if (TouchType == TouchEvent::EType::Down)
    {
        // First check if we're over a junction
        bool bOverJunction = false;
        for (const auto& Junction : VizManager->Junctions)
        {
            if (Junction && Junction->IsPointNear(WorldPosition))
            {
                bOverJunction = true;
                break;
            }
        }

        if (bOverJunction)
        {
            // Junction gets the grab
            TouchEvent Evt;
            Evt.Type = TouchEvent::EType::Down;
            Evt.Position = WorldPosition;
            Evt.TouchID = PointerIndex;
//UE_LOG(LogTemp, Log, TEXT("  Started touch at (%.1f, %.1f)"), DiagramPosition.X, DiagramPosition.Y);
			VizManager->HandleTouchEvent(Evt, &CmdDistributor);
			bIsOnJunction = true;
			FTouchInfo& TouchInfo = ActiveTouches.Add(PointerIndex);
			TouchInfo.PointerId = PointerIndex;
			TouchInfo.InitialPosition = DiagramPosition;
			TouchInfo.CurrentPosition = DiagramPosition;
			TouchInfo.InitialPanOffset = ViewOffset * ViewScale;
			TouchInfo.InitialScale = ViewScale;
            return true;  // Junction grabbed the event
        }
        else if (ActiveTouches.Num() == 0)
        {
            // Not over a junction, start pan if in empty space
//            bool bInEmptySpace = IsPointInEmptySpace(DiagramPosition);
//            UE_LOG(LogTemp, Log, TEXT("  Touch began: InEmptySpace=%d"), bInEmptySpace);
//            if (bInEmptySpace)
            {
                bIsPanning = true;
                // Store initial touch info
                FTouchInfo& TouchInfo = ActiveTouches.Add(PointerIndex);
                TouchInfo.PointerId = PointerIndex;
                TouchInfo.InitialPosition = DiagramPosition;
                TouchInfo.CurrentPosition = DiagramPosition;
                TouchInfo.InitialPanOffset = ViewOffset * ViewScale;
                TouchInfo.InitialScale = ViewScale;
//                UE_LOG(LogTemp, Log, TEXT("  Started panning from (%.1f, %.1f)"), DiagramPosition.X, DiagramPosition.Y);
                return true;  // Pan mode grabbed the event
            }
        }
        else if (ActiveTouches.Num() == 1 && bIsPanning)
        {
            // Second finger - start pinch
            FTouchInfo& TouchInfo = ActiveTouches.Add(PointerIndex);
            TouchInfo.PointerId = PointerIndex;
            TouchInfo.InitialPosition = DiagramPosition;
            TouchInfo.CurrentPosition = DiagramPosition;
            TouchInfo.InitialPanOffset = ViewOffset * ViewScale;
            TouchInfo.InitialScale = ViewScale;
            
            // Get the first touch's current position
            const FTouchInfo& FirstTouch = ActiveTouches[ActiveTouches.begin().Key()];
            InitialPinchDistance = FVector2D::Distance(FirstTouch.CurrentPosition, DiagramPosition);
            bIsPinching = true;
			if (bIsOnJunction) {
				TouchEvent Evt;
				Evt.Type = TouchEvent::EType::Cancel;
				Evt.Position = WorldPosition;
				Evt.TouchID = PointerIndex;
				VizManager->HandleTouchEvent(Evt, &CmdDistributor);
				bIsOnJunction = false;
			}
            UE_LOG(LogTemp, Log, TEXT("  Started pinching, initial distance=%.1f"), InitialPinchDistance);
            return true;  // Pinch mode grabbed the event
        }
    }
    else if (TouchType == TouchEvent::EType::Move)
    {
        if (FTouchInfo* TouchInfo = ActiveTouches.Find(PointerIndex))
        {
            TouchInfo->CurrentPosition = DiagramPosition;
//if (!bIsOnJunction) UE_LOG(LogTemp, Log, TEXT("  MISSING FLAG Moving touch at (%.1f, %.1f)"), DiagramPosition.X, DiagramPosition.Y);
            
            if (bIsOnJunction) {
				TouchEvent Evt;
				Evt.Type = TouchEvent::EType::Move;
				Evt.Position = WorldPosition;
				Evt.TouchID = PointerIndex;
//UE_LOG(LogTemp, Log, TEXT("  Moving touch at (%.1f, %.1f)"), DiagramPosition.X, DiagramPosition.Y);
				VizManager->HandleTouchEvent(Evt, &CmdDistributor);
				return true;
            }
            else if (bIsPanning && PointerIndex == ActiveTouches.begin().Key())
            {
                // First finger - continue pan
                // Calculate delta from initial position
                FVector2D Delta = DiagramPosition - TouchInfo->InitialPosition;
                // Scale the delta by the initial scale to maintain consistent pan speed
                ApplyPan(TouchInfo->InitialPanOffset + Delta*TouchInfo->InitialScale, TouchInfo->InitialScale);
                return true;  // Pan mode has the grab
            }
            else if (bIsPinching && ActiveTouches.Num() == 2)
            {
                // Second finger - update pinch
                const FTouchInfo& FirstTouch = ActiveTouches[ActiveTouches.begin().Key()];
                float CurrentDistance = FVector2D::Distance(FirstTouch.CurrentPosition, DiagramPosition);
                float PinchDelta = (CurrentDistance - InitialPinchDistance) * 0.01f; // Scale factor
                UE_LOG(LogTemp, Log, TEXT("  Pinching: Current=%.1f, Delta=%.2f"), CurrentDistance, PinchDelta);
                ApplyZoom(PinchDelta);
                InitialPinchDistance = CurrentDistance;
                return true;  // Pinch mode has the grab
            } 
        }
//else {
//UE_LOG(LogTemp, Log, TEXT("  NO POINTER %d Moving touch at (%.1f, %.1f)"), PointerIndex, DiagramPosition.X, DiagramPosition.Y);
//for (TPair<int32, FTouchInfo> p : ActiveTouches) {
////	TMap<int32, FTouchInfo> ActiveTouches;  // Map of pointer ID to touch info
//UE_LOG(LogTemp, Log, TEXT("             %d Active touch"), get<1>(p).PointerId);
//}
//}
    }
    else if (TouchType == TouchEvent::EType::Up)
    {
        ActiveTouches.Remove(PointerIndex);
        if (bIsOnJunction) {
            TouchEvent Evt;
            Evt.Type = TouchEvent::EType::Up;
            Evt.Position = WorldPosition;
            Evt.TouchID = PointerIndex;
//UE_LOG(LogTemp, Log, TEXT("  End touch at (%.1f, %.1f)"), DiagramPosition.X, DiagramPosition.Y);
			VizManager->HandleTouchEvent(Evt, &CmdDistributor);
			bIsOnJunction = false;
            bIsPanning = false;
            bIsPinching = false;
            return true;  // Release
		}
		else if (ActiveTouches.Num() == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("  Touch ended: WasPanning=%d"), bIsPanning);
            bIsPanning = false;
            bIsPinching = false;
            return true;  // Release pan/pinch grab
        }
        else if (ActiveTouches.Num() == 1)
        {
            UE_LOG(LogTemp, Log, TEXT("  Touch ended: WasPinching=%d"), bIsPinching);
            bIsPinching = false;
            return true;  // Release pinch grab
        } 
    }
    else if (TouchType == TouchEvent::EType::Cancel)
    {
        ActiveTouches.Remove(PointerIndex);
        if (bIsOnJunction) {
            TouchEvent Evt;
            Evt.Type = TouchEvent::EType::Cancel;
            Evt.Position = WorldPosition;
            Evt.TouchID = PointerIndex;
//UE_LOG(LogTemp, Log, TEXT("  Cancel touch at (%.1f, %.1f)"), DiagramPosition.X, DiagramPosition.Y);
			VizManager->HandleTouchEvent(Evt, &CmdDistributor);
			bIsOnJunction = false;
            bIsPanning = false;
            bIsPinching = false;
            return true;  // Release
		}
		else if (ActiveTouches.Num() == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("  Touch canceled: WasPanning=%d"), bIsPanning);
            bIsPanning = false;
            bIsPinching = false;
            return true;  // Release pan/pinch grab
        }
        else if (ActiveTouches.Num() == 1)
        {
            UE_LOG(LogTemp, Log, TEXT("  Touch canceled: WasPinching=%d"), bIsPinching);
            bIsPinching = false;
            return true;  // Release pinch grab
        }
    }

    return false;
}

void AVisualTestHarnessActor::OnZoomInTriggered(const FInputActionValue& Value)
{
    ApplyZoom(ZoomStep);
}

void AVisualTestHarnessActor::OnZoomOutTriggered(const FInputActionValue& Value)
{
    ApplyZoom(-ZoomStep);
}

void AVisualTestHarnessActor::OnPanStarted(const FInputActionValue& Value)
{
	UE_LOG(LogTemp, Log, TEXT("  OnPanStarted"));
    if (Value.GetValueType() == EInputActionValueType::Axis2D)
    {
        FVector2D Position = Value.Get<FVector2D>();
        FGeometry ImageGeometry = VisualizationImage->GetCachedGeometry();
        FVector2D LocalPosition = ImageGeometry.AbsoluteToLocal(Position);
        
        // Convert to diagram space
        FVector2D DiagramPosition = LocalPosition;
//        FVector2D(
//            VisTextureSize.X * LocalPosition.X / ImageGeometry.GetLocalSize().X,
//            VisTextureSize.Y * LocalPosition.Y / ImageGeometry.GetLocalSize().Y
//        );
        
        // Then apply inverse of view transform to get world position
        FTransform2D ViewTransform(ViewScale, ViewOffset);
        FTransform2D InverseViewTransform = ViewTransform.Inverse();
        FVector2D WorldPosition = InverseViewTransform.TransformPoint(DiagramPosition);
        
        if (IsPointInEmptySpace(WorldPosition))
        {
            bIsPanning = true;
            // Store initial touch info for mouse pan
            FTouchInfo& TouchInfo = ActiveTouches.Add(0); // Use 0 as pointer ID for mouse
            TouchInfo.PointerId = 0;
            TouchInfo.InitialPosition = WorldPosition;
            TouchInfo.CurrentPosition = WorldPosition;
            TouchInfo.InitialPanOffset = ViewOffset * ViewScale;
            TouchInfo.InitialScale = ViewScale;
        }
    }
}

void AVisualTestHarnessActor::OnPanTriggered(const FInputActionValue& Value)
{
	UE_LOG(LogTemp, Log, TEXT("  OnPanTriggered"));
    if (bIsPanning && Value.GetValueType() == EInputActionValueType::Axis2D)
    {
        FVector2D CurrentPosition = Value.Get<FVector2D>();
        FGeometry ImageGeometry = VisualizationImage->GetCachedGeometry();
        FVector2D LocalPosition = ImageGeometry.AbsoluteToLocal(CurrentPosition);
        
        // Convert to diagram space
        FVector2D DiagramPosition = LocalPosition;
//        FVector2D(
//            VisTextureSize.X * LocalPosition.X / ImageGeometry.GetLocalSize().X,
//            VisTextureSize.Y * LocalPosition.Y / ImageGeometry.GetLocalSize().Y
//        );
        
        // Then apply inverse of view transform to get world position
        FTransform2D ViewTransform(ViewScale, ViewOffset);
        FTransform2D InverseViewTransform = ViewTransform.Inverse();
        FVector2D WorldPosition = InverseViewTransform.TransformPoint(DiagramPosition);
        
        if (FTouchInfo* TouchInfo = ActiveTouches.Find(0)) // Find mouse touch info
        {
            TouchInfo->CurrentPosition = WorldPosition;
            // Calculate delta from initial position
            FVector2D Delta = WorldPosition - TouchInfo->InitialPosition;
            // Scale the delta by the initial scale to maintain consistent pan speed
            ApplyPan(TouchInfo->InitialPanOffset + Delta*TouchInfo->InitialScale, TouchInfo->InitialScale);
        }
    }
}

void AVisualTestHarnessActor::OnPanCompleted(const FInputActionValue& Value)
{
	UE_LOG(LogTemp, Log, TEXT("  OnPanCompleted"));
    ActiveTouches.Remove(0); // Remove mouse touch info
    bIsPanning = false;
}

void AVisualTestHarnessActor::OnZoomInButtonClicked()
{
    ApplyZoom(ZoomStep);
}

void AVisualTestHarnessActor::OnZoomOutButtonClicked()
{
    ApplyZoom(-ZoomStep);
}

void AVisualTestHarnessActor::ApplyZoom(float Delta)
{
    UE_LOG(LogTemp, Log, TEXT("ApplyZoom: Delta=%.2f, CurrentScale=%.2f"), Delta, ViewScale);
    float NewScale = FMath::Clamp(ViewScale + Delta, MinZoom, MaxZoom);
    ViewScale = NewScale;
    UE_LOG(LogTemp, Log, TEXT("  New scale=%.2f"), ViewScale);
}

void AVisualTestHarnessActor::ApplyPan(const FVector2D& NewPan, float Scale)
{
//    UE_LOG(LogTemp, Log, TEXT("ApplyPan: NewPan=(%.1f, %.1f), CurrentOffset=(%.1f, %.1f), Scale=%.2f"),
//           NewPan.X, NewPan.Y, ViewOffset.X, ViewOffset.Y, Scale);
    
    // Scale the pan delta by the initial scale to maintain consistent pan speed
    ViewOffset = NewPan / Scale;
    
//    UE_LOG(LogTemp, Log, TEXT("  New offset=(%.1f, %.1f)"), ViewOffset.X, ViewOffset.Y);
}

bool AVisualTestHarnessActor::IsPointInEmptySpace(const FVector2D& WorldPoint) const
{
    if (!VizManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("IsPointInEmptySpace: VizManager is null"));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("IsPointInEmptySpace: Checking point (%.1f, %.1f)"), WorldPoint.X, WorldPoint.Y);

    // WorldPoint is already in world space, no need to transform
    for (const auto& Junction : VizManager->Junctions)
    {
        if (Junction && Junction->IsPointNear(WorldPoint))
        {
            UE_LOG(LogTemp, Log, TEXT("  Point is near junction %s"), *Junction->GetSystemName());
            return false;
        }
    }

    for (const auto& Segment : VizManager->Segments)
    {
        if (Segment && Segment->IsPointNear(WorldPoint))
        {
            UE_LOG(LogTemp, Log, TEXT("  Point is near segment %s"), *Segment->GetName());
            return false;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("  Point is in empty space"));
    return true;
}

// --- UI Event Handlers ---
void AVisualTestHarnessActor::OnSendCommandClicked()
{
    if (CommandInputBox)
    {
        FString Command = CommandInputBox->GetText().ToString();
        if (!Command.IsEmpty())
        {
            AddLogMessage(FString::Printf(TEXT("CMD> %s"), *Command));
            ECommandResult Result = CmdDistributor.ProcessCommand(Command); // Use Get() for TUniquePtr
            AddLogMessage(FString::Printf(TEXT("Result: %d"), static_cast<int32>(Result))); 
            CommandInputBox->SetText(FText::GetEmpty()); 
        }
    }
}

void AVisualTestHarnessActor::OnCommandTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
    if (CommitMethod == ETextCommit::OnEnter)
    {
        OnSendCommandClicked(); 
    }
}

void AVisualTestHarnessActor::MakeConnectingSegment(const FString& Name, ICH_PowerJunction *JCT, int32 JCT_side, int32 JCT_offset, ICH_PowerJunction *SS, int32 SS_side, int32 SS_offset) {
	PWR_PowerSegment *segment = new PWR_PowerSegment(Name);
	CmdDistributor.RegisterSegment(segment);
	segment->SetJunctionA(SS->GetNumPorts(), SS);		SS->AddPort(segment, SS_side, SS_offset);
	segment->SetJunctionB(JCT->GetNumPorts(), JCT);		JCT->AddPort(segment, JCT_side, JCT_offset);
}

void AVisualTestHarnessActor::InitializeCommandHandlers()
{
	int32 XC = 200;

	SS_Sonar *sonar = new SS_Sonar("SONAR", SubmarineState.Get(), XC+191, 4, 64, 64);
	sonar->MarkerX = TEXT("XC");
	CmdDistributor.RegisterHandler(sonar);
	SS_BowPlanes *lbp = new SS_BowPlanes("LBP", SubmarineState.Get(), XC+150-75-16-3-13+1 + 40, 60-18, 80, 32);
	lbp->MarkerX = TEXT("XC");
	CmdDistributor.RegisterHandler(lbp);
	SS_BowPlanes *rbp = new SS_BowPlanes("RBP", SubmarineState.Get(), XC+150+75+16+3+13-1 + 30-1, 60-18, 80, 32);
	rbp->MarkerX = TEXT("XC");
	CmdDistributor.RegisterHandler(rbp);
//	SS_FMBTVent *fv = new SS_FMBTVent("FMBTVENT", SubmarineState.Get(), XC+150-75-16-3-13+1, 10+90-8, 80, 32);
//	fv->MarkerX = TEXT("XC");
//	CmdDistributor.RegisterHandler(fv);

	PWRJ_MultiConnJunction *mfj = new PWRJ_MultiConnJunction("FJ", SubmarineState.Get(), XC+150+25, 110, 100, 40);
	mfj->MarkerX = TEXT("XC");
	CmdDistributor.RegisterHandler(mfj);
	MakeConnectingSegment("sonar_feed", mfj, 0, 50, sonar, 2, 2+64/2);
	MakeConnectingSegment("rbp_feed", mfj, 0, 100-20, rbp, 1, 8+8);
	MakeConnectingSegment("lbp_feed", mfj, 0, 20, lbp, 3, 8+8);
//	MakeConnectingSegment("fmbt_feed", mfj, 1, 10, fv, 3, 8+8);
	
	int32 Y1 = 168 + 10;
	SS_TorpedoTube *tt = new SS_TorpedoTube("TORPEDOROOM", SubmarineState.Get(), XC+150, Y1-2, 150, 24);
	tt->MarkerX = TEXT("XC");
	tt->MarkerY = TEXT("Y1");
	CmdDistributor.RegisterHandler(tt);
//	SS_FTBTPump *ftp = new SS_FTBTPump("FTBTPUMP", SubmarineState.Get(), XC+150, Y1+30, 150, 24);
//	ftp->MarkerX = TEXT("XC");
//	CmdDistributor.RegisterHandler(ftp);
		
	PWRJ_MultiConnJunction *mfjL = new PWRJ_MultiConnJunction("L1", SubmarineState.Get(), XC+55+30, Y1, 20, 51);
	mfjL->MarkerX = TEXT("XC");
	mfjL->MarkerY = TEXT("Y1");
	CmdDistributor.RegisterHandler(mfjL);
	MakeConnectingSegment("lf_feed", mfjL, 0, 10, mfj, 1, 30);
	MakeConnectingSegment("ttl_feed", mfjL, 3, 10, tt, 1, 8+4);
//	MakeConnectingSegment("ftpl_feed", mfjL, 3, 42, ftp, 1, 8+4);

	PWRJ_MultiConnJunction *mfjR = new PWRJ_MultiConnJunction("R1", SubmarineState.Get(), XC+375-30, Y1, 20, 51);
	mfjR->MarkerX = TEXT("XC");
	mfjR->MarkerY = TEXT("Y1");
	CmdDistributor.RegisterHandler(mfjR);
	MakeConnectingSegment("rf_feed", mfjR, 0, 10, mfj, 3, 30);
	MakeConnectingSegment("ttr_feed", mfjR, 1, 10, tt, 3, 8+4);
//	MakeConnectingSegment("ftpr_feed", mfjR, 1, 42, ftp, 3, 8+4);

	int32 Y2 = Y1+79 + 15 + 10;
	SS_Battery *b1 = new SS_Battery1("BATTERY1", SubmarineState.Get(), XC+150, Y2-4, 150, 32);
	b1->MarkerX = TEXT("XC");
	b1->MarkerY = TEXT("Y2");
	CmdDistributor.RegisterHandler(b1);
//	MakeConnectingSegment("ftbt_private", ftp, 2, 75, b1, 0, 75);

	PWRJ_MultiConnJunction *mfjL2 = new PWRJ_MultiConnJunction("L2", SubmarineState.Get(), XC+55+30, Y2, 20, 24);
	mfjL2->MarkerX = TEXT("XC");
	mfjL2->MarkerY = TEXT("Y2");
	CmdDistributor.RegisterHandler(mfjL2);
	MakeConnectingSegment("l2", mfjL2, 0, 10, mfjL, 2, 10);

	PWRJ_MultiConnJunction *mfjR2 = new PWRJ_MultiConnJunction("R2", SubmarineState.Get(), XC+375-30, Y2, 20, 24);
	mfjR2->MarkerX = TEXT("XC");
	mfjR2->MarkerY = TEXT("Y2");
	CmdDistributor.RegisterHandler(mfjR2);
	MakeConnectingSegment("r2", mfjR2, 0, 10, mfjR, 2, 10);

	int32 Y3 = Y1+30+128+2 + 10 + 10;
	SS_ControlRoom *cr = new SS_ControlRoom("CONTROLROOM", SubmarineState.Get(), XC+150, Y3-2, 150, 24);
	cr->MarkerX = TEXT("XC");
	cr->MarkerY = TEXT("Y3");
	CmdDistributor.RegisterHandler(cr);
	MakeConnectingSegment("cr_private", cr, 0, 75, b1, 2, 75);		// b1 and b2 must have L and R bus as pins 1 & 2
	MakeConnectingSegment("b1l_feed", mfjL2, 3, 12, b1, 1, 16);
	MakeConnectingSegment("b1r_feed", mfjR2, 1, 12, b1, 3, 16);
	SS_Airlock *al = new SS_Airlock("AIRLOCK", SubmarineState.Get(), XC+150, Y3+30, 150, 24);
	al->MarkerX = TEXT("XC");
	al->MarkerY = TEXT("Y3");
	CmdDistributor.RegisterHandler(al);
	SS_XTBTPump *xtbt = new SS_XTBTPump("XTBTPUMP", SubmarineState.Get(), XC+150, Y3+62, 150, 24);
	xtbt->MarkerX = TEXT("XC");
	xtbt->MarkerY = TEXT("Y3");
	CmdDistributor.RegisterHandler(xtbt);

	PWRJ_MultiConnJunction *mfjL3 = new PWRJ_MultiConnJunction("L3", SubmarineState.Get(), XC+55+30, Y3, 20, 82);
	mfjL3->MarkerX = TEXT("XC");
	mfjL3->MarkerY = TEXT("Y3");
	CmdDistributor.RegisterHandler(mfjL3);
	MakeConnectingSegment("l3", mfjL3, 0, 10, mfjL2, 2, 10);
	MakeConnectingSegment("l3_cr", mfjL3, 3, 10, cr, 1, 8+4);
	MakeConnectingSegment("l3_al", mfjL3, 3, 42, al, 1, 8+4);
	MakeConnectingSegment("l3_xtbt", mfjL3, 3, 74, xtbt, 1, 8+4);

	PWRJ_MultiConnJunction *mfjR3 = new PWRJ_MultiConnJunction("R3", SubmarineState.Get(), XC+375-30, Y3, 20, 116);
	mfjR3->MarkerX = TEXT("XC");
	mfjR3->MarkerY = TEXT("Y3");
	CmdDistributor.RegisterHandler(mfjR3);
	MakeConnectingSegment("r3", mfjR3, 0, 10, mfjR2, 2, 10);
	MakeConnectingSegment("r3_cr", mfjR3, 1, 10, cr, 3, 8+4);
	MakeConnectingSegment("r3_al", mfjR3, 1, 42, al, 3, 8+4);
	MakeConnectingSegment("r3_xtbt", mfjR3, 1, 74, xtbt, 3, 8+4);

//

	int32 Y4 = Y3+10+192 + 10 - 40;
	int32 Y5 = Y4+62 + 10 + 10;
	int32 Y6 = Y5+47 + 20 + 10;
	int32 Y7 = Y6+49 + 18 + 10;
	int32 Y8 = Y7+50 + 18 + 10;

// Engine Room

	int32 erdy = 80;
	int32 YE = Y3+6-erdy;	//Y1+70+134;
	PWRJ_MultiConnJunction *mfE = new PWRJ_MultiConnJunction("ER", SubmarineState.Get(), XC+30, YE+4+18, 20, 220);
	mfE->MarkerX = TEXT("XC");
	mfE->MarkerY = TEXT("Y3");
	CmdDistributor.RegisterHandler(mfE);
	MakeConnectingSegment("el", mfjL3, 1, 60, mfE, 3, 80-24-24+erdy);
	MakeConnectingSegment("er", mfjR3, 1, 85+20, mfE, 3, 105-24-4+erdy);

	int32 YE1 = 10+90-8;
	PWRJ_MultiConnJunction *mfEu3 = new PWRJ_MultiConnJunction("E3", SubmarineState.Get(), XC+30, YE1+6, 20, 20);
	mfEu3->MarkerX = TEXT("XC");
	mfEu3->MarkerY = TEXT("YE1");
	CmdDistributor.RegisterHandler(mfEu3);

	PWRJ_MultiConnJunction *mfEu2 = new PWRJ_MultiConnJunction("E2", SubmarineState.Get(), XC+30, YE1+6 + 76, 20, 20);
	mfEu2->MarkerX = TEXT("XC");
	mfEu2->MarkerY = TEXT("YE1");
	CmdDistributor.RegisterHandler(mfEu2);

	PWRJ_MultiConnJunction *mfEu1 = new PWRJ_MultiConnJunction("E1", SubmarineState.Get(), XC+30, YE1+6 + 152, 20, 20);
	mfEu1->MarkerX = TEXT("XC");
	mfEu1->MarkerY = TEXT("YE1");
	CmdDistributor.RegisterHandler(mfEu1);
	MakeConnectingSegment("e1", mfEu1, 2, 10, mfE, 0, 10);
	MakeConnectingSegment("e2", mfEu2, 2, 10, mfEu1, 0, 10);
	MakeConnectingSegment("e3", mfEu3, 2, 10, mfEu2, 0, 10);

	int32 YE3 = Y8+37-10;
	PWRJ_MultiConnJunction *mfEu4 = new PWRJ_MultiConnJunction("E4", SubmarineState.Get(), XC+30, YE3+6 - 152, 20, 20);
	mfEu4->MarkerX = TEXT("XC");
	mfEu4->MarkerY = TEXT("YE3");
	CmdDistributor.RegisterHandler(mfEu4);

	PWRJ_MultiConnJunction *mfEu5 = new PWRJ_MultiConnJunction("E5", SubmarineState.Get(), XC+30, YE3+6 - 76, 20, 20);
	mfEu5->MarkerX = TEXT("XC");
	mfEu5->MarkerY = TEXT("YE3");
	CmdDistributor.RegisterHandler(mfEu5);

	PWRJ_MultiConnJunction *mfEu6 = new PWRJ_MultiConnJunction("E6", SubmarineState.Get(), XC+30, YE3+6, 20, 20);
	mfEu6->MarkerX = TEXT("XC");
	mfEu6->MarkerY = TEXT("YE3");
	CmdDistributor.RegisterHandler(mfEu6);
	MakeConnectingSegment("e4", mfEu4, 0, 10, mfE, 2, 10);
	MakeConnectingSegment("e5", mfEu5, 0, 10, mfEu4, 2, 10);
	MakeConnectingSegment("e6", mfEu6, 0, 10, mfEu5, 2, 10);

	int32 XCE = 64;
	SS_CO2Scrubber *co2 = new SS_CO2Scrubber("CO2SCRUBBER", SubmarineState.Get(), XCE, YE+20, 120, 24);
	co2->MarkerX = TEXT("XCE");
	co2->MarkerY = TEXT("YE");
	CmdDistributor.RegisterHandler(co2);
	MakeConnectingSegment("er_elec", mfE, 1, 10, co2, 3, 8+4);
	SS_Degaussing *dgau = new SS_Degaussing("DEGAUSSER", SubmarineState.Get(), XCE, YE+20+40, 120, 24);
	dgau->MarkerX = TEXT("XCE");
	dgau->MarkerY = TEXT("YE");
	CmdDistributor.RegisterHandler(dgau);
	MakeConnectingSegment("er_dgau", mfE, 1, 10+40, dgau, 3, 8+4);
	SS_AirCompressor *ac = new SS_AirCompressor("AIRCOMPRESSOR", SubmarineState.Get(), XCE-30, YE+20+40*2, 120+30, 24);
	ac->MarkerX = TEXT("XCE");
	ac->MarkerY = TEXT("YE");
	CmdDistributor.RegisterHandler(ac);
	MakeConnectingSegment("er_ac", mfE, 1, 10+40*2, ac, 3, 8+4);
	SS_Dehumidifier *dh = new SS_Dehumidifier("DEHUMIDIFIER", SubmarineState.Get(), XCE, YE+20+40*3, 120, 24);
	dh->MarkerX = TEXT("XCE");
	dh->MarkerY = TEXT("YE");
	CmdDistributor.RegisterHandler(dh);
	MakeConnectingSegment("er_dh", mfE, 1, 10+40*3, dh, 3, 8+4);
	SS_O2Generator *o2 = new SS_O2Generator("O2GENERATOR", SubmarineState.Get(), XCE, YE+20+40*4, 120, 24);
	o2->MarkerX = TEXT("XCE");
	o2->MarkerY = TEXT("YE");
	CmdDistributor.RegisterHandler(o2);
	MakeConnectingSegment("er_o2", mfE, 1, 10+40*4, o2, 3, 8+4);
	SS_Electrolysis *elec = new SS_Electrolysis("ELECTROLYZER", SubmarineState.Get(), XCE, YE+20+40*5, 120, 24);
	elec->MarkerX = TEXT("XCE");
	elec->MarkerY = TEXT("YE");
	CmdDistributor.RegisterHandler(elec);
	MakeConnectingSegment("er_co2", mfE, 1, 10+40*5, elec, 3, 8+4);

//

//	int32 Y4 = Y3+10+192 + 10 - 40;
	SS_AIP *aip = new SS_AIP("FUELCELL", SubmarineState.Get(), XC+150, Y4, 150, 32);
	aip->MarkerX = TEXT("XC");
	aip->MarkerY = TEXT("Y4");
	CmdDistributor.RegisterHandler(aip);

	PWRJ_MultiConnJunction *mfjL4 = new PWRJ_MultiConnJunction("L4", SubmarineState.Get(), XC+55+30, Y4+4, 20, 24);
	mfjL4->MarkerX = TEXT("XC");
	mfjL4->MarkerY = TEXT("Y4");
	CmdDistributor.RegisterHandler(mfjL4);
	MakeConnectingSegment("l4", mfjL4, 0, 10, mfjL3, 2, 10);
	MakeConnectingSegment("l4_aip", mfjL4, 3, 12, aip, 1, 16);

	PWRJ_MultiConnJunction *mfjR4 = new PWRJ_MultiConnJunction("R4", SubmarineState.Get(), XC+375-30, Y4+4, 20, 24);
	mfjR4->MarkerX = TEXT("XC");
	mfjR4->MarkerY = TEXT("Y4");
	CmdDistributor.RegisterHandler(mfjR4);
	MakeConnectingSegment("r4", mfjR4, 0, 10, mfjR3, 2, 10);
	MakeConnectingSegment("r4_aip", mfjR4, 1, 12, aip, 3, 16);

//	int32 Y5 = Y4+62 + 10 + 10;
	SS_MainMotor *mm = new SS_MainMotor("MAINMOTOR", SubmarineState.Get(), XC+150, Y5-4, 150, 32);
	mm->MarkerX = TEXT("XC");
	mm->MarkerY = TEXT("Y5");
	CmdDistributor.RegisterHandler(mm);
	MakeConnectingSegment("mm_aip", mm, 0, 75, aip, 2, 75);

	PWRJ_MultiConnJunction *mfjL5 = new PWRJ_MultiConnJunction("L5", SubmarineState.Get(), XC+55+30, Y5, 20, 24);
	mfjL5->MarkerX = TEXT("XC");
	mfjL5->MarkerY = TEXT("Y5");
	CmdDistributor.RegisterHandler(mfjL5);
	MakeConnectingSegment("l5", mfjL5, 0, 10, mfjL4, 2, 10);
	MakeConnectingSegment("l5_mm", mfjL5, 3, 12, mm, 1, 8+8);

	PWRJ_MultiConnJunction *mfjR5 = new PWRJ_MultiConnJunction("R5", SubmarineState.Get(), XC+375-30, Y5, 20, 24);
	mfjR5->MarkerX = TEXT("XC");
	mfjR5->MarkerY = TEXT("Y5");
	CmdDistributor.RegisterHandler(mfjR5);
	MakeConnectingSegment("r5", mfjR5, 0, 10, mfjR4, 2, 10);
	MakeConnectingSegment("r5_mm", mfjR5, 1, 12, mm, 3, 8+8);

//	int32 Y6 = Y5+47 + 20 + 10;
	SS_Battery *b2 = new SS_Battery2("BATTERY2", SubmarineState.Get(), XC+150, Y6-4, 150, 32);
	b2->MarkerX = TEXT("XC");
	b2->MarkerY = TEXT("Y6");
	CmdDistributor.RegisterHandler(b2);
	MakeConnectingSegment("b2_mm", b2, 0, 75, mm, 2, 75);		// b1 and b2 must have L and R bus as pins 1 & 2

	PWRJ_MultiConnJunction *mfjL6 = new PWRJ_MultiConnJunction("L6", SubmarineState.Get(), XC+55+30, Y6, 20, 24);
	mfjL6->MarkerX = TEXT("XC");
	mfjL6->MarkerY = TEXT("Y6");
	CmdDistributor.RegisterHandler(mfjL6);
	MakeConnectingSegment("l6", mfjL6, 0, 10, mfjL5, 2, 10);
	MakeConnectingSegment("l6_b2", mfjL6, 3, 12, b2, 1, 16);

	PWRJ_MultiConnJunction *mfjR6 = new PWRJ_MultiConnJunction("R6", SubmarineState.Get(), XC+375-30, Y6, 20, 24);
	mfjR6->MarkerX = TEXT("XC");
	mfjR6->MarkerY = TEXT("Y6");
	CmdDistributor.RegisterHandler(mfjR6);
	MakeConnectingSegment("r6", mfjR6, 0, 10, mfjR5, 2, 10);
	MakeConnectingSegment("r6_b2", mfjR6, 1, 12, b2, 3, 16);

//	int32 Y7 = Y6+49 + 18 + 10;
//	SS_RTBTPump *rtp = new SS_RTBTPump("RTBTPUMP", SubmarineState.Get(), XC+150, Y7+2-4, 150, 24);
//	rtp->MarkerX = TEXT("XC");
//	rtp->MarkerY = TEXT("Y7");
//	CmdDistributor.RegisterHandler(rtp);
//	MakeConnectingSegment("b2_rtbt", b2, 2, 75, rtp, 0, 75);
	SS_ROVCharging *rov = new SS_ROVCharging("ROV", SubmarineState.Get(), XC+150, Y7+2+32-4, 150, 24);
	rov->MarkerX = TEXT("XC");
	rov->MarkerY = TEXT("Y7");
	CmdDistributor.RegisterHandler(rov);

	PWRJ_MultiConnJunction *mfjL7 = new PWRJ_MultiConnJunction("L7", SubmarineState.Get(), XC+55+30, Y7, 20, 52);
	mfjL7->MarkerX = TEXT("XC");
	mfjL7->MarkerY = TEXT("Y7");
	CmdDistributor.RegisterHandler(mfjL7);
	MakeConnectingSegment("l7", mfjL7, 0, 10, mfjL6, 2, 10);
//	MakeConnectingSegment("l7_rtbt", mfjL7, 3, 10, rtp, 1, 12);
	MakeConnectingSegment("l7_rov", mfjL7, 3, 42, rov, 1, 12);

	PWRJ_MultiConnJunction *mfjR7 = new PWRJ_MultiConnJunction("R7", SubmarineState.Get(), XC+375-30, Y7, 20, 52);
	mfjR7->MarkerX = TEXT("XC");
	mfjR7->MarkerY = TEXT("Y7");
	CmdDistributor.RegisterHandler(mfjR7);
	MakeConnectingSegment("r7", mfjR7, 0, 10, mfjR6, 2, 10);
//	MakeConnectingSegment("r7_rtbt", mfjR7, 1, 10, rtp, 3, 12);
	MakeConnectingSegment("r7_rov", mfjR7, 1, 42, rov, 3, 12);

//	int32 Y8 = Y7+50 + 18 + 10;
	PWRJ_MultiConnJunction *mrj = new PWRJ_MultiConnJunction("RJ", SubmarineState.Get(), XC+150+25, Y8+4, 100, 40);
	mrj->MarkerX = TEXT("XC");
	mrj->MarkerY = TEXT("Y8");
	CmdDistributor.RegisterHandler(mrj);
	MakeConnectingSegment("rr", mrj, 3, 10, mfjR7, 2, 10);
	MakeConnectingSegment("lr", mrj, 1, 10, mfjL7, 2, 10);

//	SS_RMBTVent *rmbtvent = new SS_RMBTVent("RMBTVENT", SubmarineState.Get(), XC+150-75-16-3-13+1, Y8+37-10, 80, 32);
//	rmbtvent->MarkerX = TEXT("XC");
//	rmbtvent->MarkerY = TEXT("Y8");
//	CmdDistributor.RegisterHandler(rmbtvent);
//	MakeConnectingSegment("rmbt_feed", mrj, 1, 30, rmbtvent, 3, 8+8);

	SS_Rudder *rudder = new SS_Rudder("RUDDER", SubmarineState.Get(), XC+150-75-16-3-13+1 + 40, Y8+77, 80, 32);
	rudder->MarkerX = TEXT("XC");
	rudder->MarkerY = TEXT("Y8");
	CmdDistributor.RegisterHandler(rudder);
	SS_Elevator *elev = new SS_Elevator("ELEVATOR", SubmarineState.Get(), XC+150+75+16+3+13-1 + 30-1, Y8+77, 80, 32);
	elev->MarkerX = TEXT("XC");
	elev->MarkerY = TEXT("Y8");
	CmdDistributor.RegisterHandler(elev);
	MakeConnectingSegment("rud_feed", rudder, 3, 8+8, mrj, 2, 20);
	MakeConnectingSegment("elev_feed", elev, 1, 8+8, mrj, 2, 100-20);

// spine

	int32 XCS = XC+375-30 + 40;
	int32 XCS2 = XCS+45 + 20;
	PWRJ_MultiConnJunction *s1 = new PWRJ_MultiConnJunction("S1", SubmarineState.Get(), XCS, 150+7, 20, 92+48);
	s1->MarkerX = TEXT("XCS");
	s1->MarkerY = TEXT("");
	CmdDistributor.RegisterHandler(s1);
	MakeConnectingSegment("s1_feed", s1, 0, 10, mfj, 3, 10);
	SS_GPS *gps = new SS_GPS("GPS", SubmarineState.Get(), XCS2, 150+8+1-4, 140, 24);
	gps->MarkerX = TEXT("XCS2");
	gps->MarkerY = TEXT("");
	CmdDistributor.RegisterHandler(gps);
	MakeConnectingSegment("gps_feed", gps, 1, 12, s1, 3, 10);
	SS_Camera *cam = new SS_Camera("CAMERA", SubmarineState.Get(), XCS2, 150+8+40+1-4, 140, 24);
	cam->MarkerX = TEXT("XCS2");
	cam->MarkerY = TEXT("");
	CmdDistributor.RegisterHandler(cam);
	MakeConnectingSegment("cam_feed", cam, 1, 12, s1, 3, 10+40);
	SS_Antenna *ant = new SS_Antenna("ANTENNA", SubmarineState.Get(), XCS2, 150+8+40*2+1-4, 140, 24);
	ant->MarkerX = TEXT("XCS2");
	ant->MarkerY = TEXT("");
	CmdDistributor.RegisterHandler(ant);
	MakeConnectingSegment("ant_feed", ant, 1, 12, s1, 3, 10+40*2);
	SS_Radar *radar = new SS_Radar("RADAR", SubmarineState.Get(), XCS2, 150+8+40*3+1-4, 140, 24);
	radar->MarkerX = TEXT("XCS2");
	radar->MarkerY = TEXT("");
	CmdDistributor.RegisterHandler(radar);
	MakeConnectingSegment("radar_feed", radar, 1, 12, s1, 3, 10+40*3);

	SS_Hatch *hatch = new SS_Hatch("HATCH", SubmarineState.Get(), XCS2, Y3-4+35, 140, 24);
	hatch->MarkerX = TEXT("XCS2");
	hatch->MarkerY = TEXT("Y3");
	CmdDistributor.RegisterHandler(hatch);

	int32 YCS = Y8 - 80 - 32;
	PWRJ_MultiConnJunction *s2 = new PWRJ_MultiConnJunction("S2", SubmarineState.Get(), XCS, YCS+1, 20, 68+32);
	s2->MarkerX = TEXT("XCS");
	s2->MarkerY = TEXT("YCS");
	CmdDistributor.RegisterHandler(s2);
	MakeConnectingSegment("s2_feed", s2, 0, 10, s1, 2, 10);
	MakeConnectingSegment("s2b_feed", s2, 2, 10, mrj, 3, 30);
	SS_Countermeasures *ctm = new SS_Countermeasures("COUNTERMEASURES", SubmarineState.Get(), XCS2, YCS+3-4, 140, 24);
	ctm->MarkerX = TEXT("XCS2");
	ctm->MarkerY = TEXT("YCS");
	CmdDistributor.RegisterHandler(ctm);
	MakeConnectingSegment("ctm_feed", ctm, 1, 12, s2, 3, 10);
	SS_SolarPanels *sol = new SS_SolarPanels("SOLARPANELS", SubmarineState.Get(), XCS2, YCS+40+3-4, 140, 24);
	sol->MarkerX = TEXT("XCS2");
	sol->MarkerY = TEXT("YCS");
	CmdDistributor.RegisterHandler(sol);
	MakeConnectingSegment("sol_feed", sol, 1, 12, s2, 3, 10+40);
	SS_TowedSonarArray *tsa = new SS_TowedSonarArray("TOWEDSONAR", SubmarineState.Get(), XCS2, YCS+40*2+3-4, 140, 24);
	tsa->MarkerX = TEXT("XCS2");
	tsa->MarkerY = TEXT("YCS");
	CmdDistributor.RegisterHandler(tsa);
	MakeConnectingSegment("tsa_feed", tsa, 1, 12, s2, 3, 10+40*2);

//

	SS_MBT *fmbt = new SS_MBT("FMBT", SubmarineState.Get(), XCE, YE1-14);
	fmbt->MarkerX = TEXT("XCE");
	fmbt->MarkerY = TEXT("YE1");
	CmdDistributor.RegisterHandler(fmbt);
	MakeConnectingSegment("fmbt_feed", mfEu3, 1, 10, fmbt, 3, fmbt->H/2);

	SS_TBT *ftbt = new SS_TBT("FTBT", SubmarineState.Get(), XCE, YE1 + 152-14);
	ftbt->MarkerX = TEXT("XCE");
	ftbt->MarkerY = TEXT("YE1");
	CmdDistributor.RegisterHandler(ftbt);
	MakeConnectingSegment("ftbt_feed", mfEu1, 1, 10, ftbt, 3, ftbt->H/2);

	SS_TBT *rtbt = new SS_TBT("RTBT", SubmarineState.Get(), XCE, YE3 - 152-14);
	rtbt->MarkerX = TEXT("XCE");
	rtbt->MarkerY = TEXT("YE3");
	CmdDistributor.RegisterHandler(rtbt);
	MakeConnectingSegment("ftbt_feed", mfEu4, 1, 10, rtbt, 3, rtbt->H/2);

	SS_MBT *rmbt = new SS_MBT("RMBT", SubmarineState.Get(), XCE, YE3-14);
	rmbt->MarkerX = TEXT("XCE");
	rmbt->MarkerY = TEXT("YE3");
	CmdDistributor.RegisterHandler(rmbt);
	MakeConnectingSegment("rmbt_feed", mfEu6, 1, 10, rmbt, 3, rmbt->H/2);

//

b1->SetPortEnabled(0, false);
b1->SetPortEnabled(1, false);
b1->SetPortEnabled(2, false);
b1->SetPortEnabled(3, false);
b2->SetPortEnabled(0, false);
b2->SetPortEnabled(1, false);
b2->SetPortEnabled(2, false);
b2->SetPortEnabled(3, false);
aip->SetPortEnabled(1, false);
aip->SetPortEnabled(0, true);
aip->SetPortEnabled(2, false);
//ftp->SetSelectedPort(1);
mm->SetSelectedPort(1);

elec->HandleCommand("ON", "SET", "true");
aip->HandleCommand("ON", "SET", "true");
sol->HandleCommand("ON", "SET", "true");
sol->SetSelectedPort(-1);

	// --- Populate Marker Map ---
	GridMarkerDefinitions.Empty(); // Ensure clean map
	GridMarkerDefinitions.Add(TEXT("XC"), (float)XC);
	GridMarkerDefinitions.Add(TEXT("XCS"), (float)XCS);
	GridMarkerDefinitions.Add(TEXT("XCS2"), (float)XCS2);
	GridMarkerDefinitions.Add(TEXT("XCE"), (float)XCE);
	GridMarkerDefinitions.Add(TEXT("YE"), (float)YE);
	GridMarkerDefinitions.Add(TEXT("Y1"), (float)Y1);
	GridMarkerDefinitions.Add(TEXT("Y2"), (float)Y2);
	GridMarkerDefinitions.Add(TEXT("Y3"), (float)Y3);
	GridMarkerDefinitions.Add(TEXT("Y4"), (float)Y4);
	GridMarkerDefinitions.Add(TEXT("Y5"), (float)Y5);
	GridMarkerDefinitions.Add(TEXT("Y6"), (float)Y6);
	GridMarkerDefinitions.Add(TEXT("Y7"), (float)Y7);
	GridMarkerDefinitions.Add(TEXT("Y8"), (float)Y8);
	GridMarkerDefinitions.Add(TEXT("YCS"), (float)YCS);
	GridMarkerDefinitions.Add(TEXT("ERDY"), (float)erdy);

    AddLogMessage(TEXT("Command Handlers Initialized."));
}

void AVisualTestHarnessActor::ProcessCommandString(const FString& Command)
{
    UE_LOG(LogTemp, Log, TEXT("Processing Command: %s"), *Command);
    CmdDistributor.ProcessCommandBlock(Command);
}

TArray<FString> AVisualTestHarnessActor::GetAvailableCommandsList()
{
    return CmdDistributor.GetAvailableCommands();
}

TArray<FString> AVisualTestHarnessActor::QueryEntireStateList()
{
    return CmdDistributor.GenerateCommandsFromEntireState();
}

TArray<FString> AVisualTestHarnessActor::GetSystemNotifications()
{
    return CmdDistributor.GetSystemNotifications();
}

