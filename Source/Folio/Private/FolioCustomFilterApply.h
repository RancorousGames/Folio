#pragma once

#include "FolioCustomFilters.h"

class UTextureRenderTarget2D;

namespace Folio
{
	/** One compiled `gpu-filter(...)` as the renderer holds it between CompileFilter and ReleaseFilter. */
	struct FCustomFilterCall
	{
		FName Name;
		/** Hashed into the layer print, so a changed argument re-renders rather than reusing a stale layer. */
		uint64 Print = 0;
		/** Extra layer margin in document pixels (the resolved `reach=`). */
		float Reach = 0.0f;
		bool bAnimated = false;
		TSharedPtr<FCustomFilterPayload, ESPMode::ThreadSafe> Payload;
		TFunction<void(FCustomFilterPassArgs&)> AddPasses;
	};

	/**
	 * Compiles a `gpu-filter` value. False when the name is not registered; the caller then renders
	 * the element unfiltered, like any other unimplemented filter.
	 */
	bool CompileCustomFilter(const FString& Value, const FCustomFilterCompileArgs& Context, float Reach,
		FCustomFilterCall& OutCall);

	/** Whether an animated gpu-filter was drawn in the last couple of frames; keeps documents from idling. */
	bool CustomFiltersAnimating();

	/** Enqueues the filters, in order, over a resolved layer target. Game thread. */
	void ApplyCustomFilters(UTextureRenderTarget2D* Target, TArrayView<const FCustomFilterCall> Calls,
		float Scale, FIntPoint Origin);
}
