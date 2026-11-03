#include "FolioCustomFilterApply.h"

#include "FolioModule.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Misc/App.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
	/** Game thread only: registration happens at module startup, lookups during paint. */
	TMap<FName, Folio::FCustomFilter>& Registry()
	{
		static TMap<FName, Folio::FCustomFilter> Filters;
		return Filters;
	}

	/** GFrameCounter when an animated filter last ran. */
	uint64 LastAnimatedFrame = 0;
}

namespace Folio
{
	void RegisterCustomFilter(const FName Name, FCustomFilter Filter)
	{
		check(IsInGameThread());
		Registry().Add(Name, MoveTemp(Filter));
	}

	void UnregisterCustomFilter(const FName Name)
	{
		check(IsInGameThread());
		Registry().Remove(Name);
	}

	bool CompileCustomFilter(const FString& Value, const FCustomFilterCompileArgs& Context, const float Reach,
		FCustomFilterCall& OutCall)
	{
		FString Trimmed = Value.TrimStartAndEnd();
		FString NameText = Trimmed;
		FString Arguments;
		Trimmed.Split(TEXT(" "), &NameText, &Arguments);

		const FName Name(*NameText);
		const FCustomFilter* Filter = Registry().Find(Name);
		if (!Filter || !Filter->AddPasses)
		{
			static TSet<FName> Reported;
			if (!Reported.Contains(Name))
			{
				Reported.Add(Name);
				UE_LOG(LogFolio, Warning, TEXT("gpu-filter '%s' is not registered and renders as nothing"),
					*NameText);
			}
			return false;
		}

		FCustomFilterCompileArgs Args = Context;
		Args.Arguments = Arguments;

		OutCall.Name = Name;
		OutCall.Reach = Reach;
		OutCall.bAnimated = Filter->bAnimated;
		OutCall.AddPasses = Filter->AddPasses;
		OutCall.Payload = Filter->Compile ? Filter->Compile(Args) : nullptr;

		OutCall.Print = GetTypeHash(Trimmed);
		OutCall.Print = HashCombineFast(OutCall.Print, GetTypeHash(Args.DpRatio));
		OutCall.Print = HashCombineFast(OutCall.Print, GetTypeHash(Args.ElementPosition));
		OutCall.Print = HashCombineFast(OutCall.Print, GetTypeHash(Args.ElementSize));
		OutCall.Print = HashCombineFast(OutCall.Print, GetTypeHash(Args.Opacity));
		return true;
	}

	bool CustomFiltersAnimating()
	{
		return LastAnimatedFrame != 0 && GFrameCounter - LastAnimatedFrame <= 2;
	}

	void ApplyCustomFilters(UTextureRenderTarget2D* Target, const TArrayView<const FCustomFilterCall> Calls,
		const float Scale, const FIntPoint Origin)
	{
		if (!Target || Calls.IsEmpty())
		{
			return;
		}

		for (const FCustomFilterCall& Call : Calls)
		{
			if (Call.bAnimated)
			{
				LastAnimatedFrame = GFrameCounter;
			}
		}

		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			return;
		}

		const FIntPoint Size(Target->SizeX, Target->SizeY);
		const double Time = FApp::GetCurrentTime() - GStartTime;
		TArray<FCustomFilterCall> Ordered(Calls.GetData(), Calls.Num());

		ENQUEUE_RENDER_COMMAND(FolioCustomFilter)(
			[Resource, Size, Scale, Origin, Time, Ordered = MoveTemp(Ordered)](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Folio_CustomFilter);

				FRDGBuilder GraphBuilder(RHICmdList);
				{
					RDG_EVENT_SCOPE(GraphBuilder, "FolioCustomFilters");

					FRDGTextureRef Layer = RegisterExternalTexture(GraphBuilder,
						Resource->GetRenderTargetTexture(), TEXT("FolioLayer"));

					for (const FCustomFilterCall& Call : Ordered)
					{
						FCustomFilterPassArgs Args{ GraphBuilder, Layer, Size };
						Args.Scale = Scale;
						Args.LayerOrigin = FVector2f(Origin);
						Args.Time = Time;
						Args.Payload = Call.Payload.Get();
						Call.AddPasses(Args);
					}
				}
				GraphBuilder.Execute();
			});
	}
}
