#pragma once

#include "CoreMinimal.h"
#include "RenderGraphDefinitions.h"
#include "Templates/Function.h"

class FRDGBuilder;

/**
 * Game-registered GPU filters: RCSS `filter: gpu-filter("<name> <arguments...> reach=<dp>")`.
 *
 * Folio's built-in filters (blur, colour, mask) run as RDG passes over an element's resolved layer.
 * This is the same seam opened to the game: a module registers a name, Folio hands it the layer
 * render target, and the module adds whatever passes it likes - including global shaders it already
 * owns elsewhere (Wartribes runs RancorousUI's production glow shader through it).
 *
 *   Folio::FCustomFilter Glow;
 *   Glow.Compile   = [](const Folio::FCustomFilterCompileArgs& Args) { ... parse on the game thread ... };
 *   Glow.AddPasses = [](Folio::FCustomFilterPassArgs& Args) { ... RDG passes on the render thread ... };
 *   Folio::RegisterCustomFilter(TEXT("rui-glow"), MoveTemp(Glow));
 *
 * `reach=<dp>` in the string widens the element's ink overflow, so a glow has layer pixels to land on.
 * Not available in the standalone preview / Studio, which renders the element without the filter.
 */
namespace Folio
{
	/** Whatever a filter parsed from its arguments, carried to the render thread. Immutable once compiled. */
	struct FCustomFilterPayload
	{
		virtual ~FCustomFilterPayload() = default;
	};

	/** Game thread, when RmlUi compiles the element's filter. Lengths are already in document pixels. */
	struct FCustomFilterCompileArgs
	{
		/** Everything after the filter's name, unparsed. */
		FString Arguments;
		/** Document pixels per dp, so arguments authored in dp can be resolved. */
		float DpRatio = 1.0f;
		/** The element's border box in document pixels. */
		FVector2f ElementPosition = FVector2f::ZeroVector;
		FVector2f ElementSize = FVector2f::ZeroVector;
		/** The element's effective opacity (own x ancestors). Its pixels already carry it; this is for
		 *  output that does not come from them. A change recompiles the filter. */
		float Opacity = 1.0f;
	};

	/** Render thread, once per layer resolve. The pass edits Layer in place (read a copy to sample it). */
	struct FCustomFilterPassArgs
	{
		FRDGBuilder& GraphBuilder;
		FRDGTextureRef Layer;
		FIntPoint LayerSize;
		/** Layer pixels per document pixel (supersampling). */
		float Scale = 1.0f;
		/** Where the layer's top-left sits in document pixels. */
		FVector2f LayerOrigin = FVector2f::ZeroVector;
		/** Seconds, for animated filters. */
		double Time = 0.0;
		const FCustomFilterPayload* Payload = nullptr;
	};

	struct FCustomFilter
	{
		TFunction<TSharedPtr<FCustomFilterPayload, ESPMode::ThreadSafe>(const FCustomFilterCompileArgs&)> Compile;
		TFunction<void(FCustomFilterPassArgs&)> AddPasses;
		/** Animated filters are never served from Folio's layer cache; the document must keep painting. */
		bool bAnimated = false;
	};

	FOLIO_API void RegisterCustomFilter(FName Name, FCustomFilter Filter);
	FOLIO_API void UnregisterCustomFilter(FName Name);
}
