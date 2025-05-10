#pragma once
#include "CoreMinimal.h"

/**
 * Shared vertex layout for the submarine diagram.
 * Both UnrealRenderingContext and the Slate widget reference this type.
 */
struct FDiagramVertex
{
    FVector2f Pos;                      // widget-local position
    FVector2f UV  {0.f, 0.f};           // primary UV (icons / white brush)
    FColor    Col {255, 255, 255, 255}; // per-vertex sRGB color
    uint8 	  Page = 255;       		// 255 = generic‑white; 0‑n = font pages
};
