#pragma once

#include "CoreMinimal.h" // For FVector2D, FBox2D, FString, TArray - Adjust if not UE
// #include "ICommandHandler.h" // Not directly needed here anymore

// Forward Declarations
class RenderingContext;
class CommandDistributor;
class ICH_PowerJunction; // Forward declare the typical owner

// Structure to hold touch event data
struct TouchEvent
{
    enum class EType { Down=1, Move=2, Up=3, Cancel=4 };
    EType Type = EType::Cancel;
    FVector2D Position = FVector2D::ZeroVector; // Absolute screen/texture coordinates
    int32 TouchID = -1; // To track multi-touch if needed
};

/**
 * Interface for a visual component associated with an owner (typically ICH_PowerJunction),
 * rendered relative to the owner's position.
 */
class IVisualElement
{
protected:
    ICH_PowerJunction* Owner = nullptr; // Pointer to the owning junction (provides position and context)

public:
    IVisualElement(ICH_PowerJunction* InOwner) : Owner(InOwner) {}
    virtual ~IVisualElement() = default;

    /**
     * Renders the element using the provided context.
     * @param Context The rendering context for drawing primitives.
     * @param OwnerPosition The absolute world/screen position of the owner.
     */
    virtual void Render(RenderingContext& Context, const FVector2D& OwnerPosition) = 0;

    /**
     * Handles a touch event, potentially sending commands via the distributor.
     * Coordinates in the TouchEvent are typically absolute, but interpretation might be relative.
     * @param Event The touch event data.
     * @param Distributor The command distributor to send commands through.
     * @return True if the event was handled by this element, false otherwise.
     */
    virtual bool HandleTouch(const TouchEvent& Event, CommandDistributor* Distributor) = 0;

    /**
     * Returns the 2D bounding box *relative* to the owner's position, used for hit testing.
     */
    virtual FBox2D GetRelativeBounds() const = 0;

    /** Allows the element to refresh its visual state based on the owning junction's state. */
    virtual void UpdateState() = 0; // Gets state directly from Owner pointer
};

/**
 * Abstract base class/interface for a 2D rendering context.
 * Platform-specific implementations (e.g., for Slate/Canvas, Metal, etc.)
 * will derive from this and implement the drawing methods.
 */
class RenderingContext
{
public:
    virtual ~RenderingContext() = default;

    // --- Primitive Drawing Methods ---
    virtual void DrawLine(const FVector2D& Start, const FVector2D& End, const FLinearColor& Color, float Thickness) = 0;
    virtual void DrawCircle(const FVector2D& Center, float Radius, const FLinearColor& Color, bool bFill = false, int Segments = 16) = 0;
    virtual void DrawRectangle(const FBox2D& Rect, const FLinearColor& Color, bool bFill = false) = 0;
    virtual void DrawTriangle(const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, const FLinearColor& Color, bool bFill = true);
    virtual void DrawText(const FVector2D& Position, const FString& Text, const FLinearColor& Color /* Font parameters? */) = 0;
    virtual void DrawTinyText(const FVector2D& Position, const FString& Text, const FLinearColor& Color /* Font parameters? */) = 0;
    // virtual void DrawTexture(const FVector2D& Position, UTexture* Texture, const FVector2D& Size, float Rotation = 0.0f) = 0; // Example for UE
    // ... other drawing methods as needed ...

    // --- State Management (Optional) ---
    // virtual void PushTransform(const FTransform2D& Transform) = 0;
    // virtual void PopTransform() = 0;
    // virtual void SetClippingRect(const FBox2D& ClipRect) = 0;
}; 
