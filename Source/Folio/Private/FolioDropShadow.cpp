#include "FolioDropShadow.h"

#include "FolioBlurShader.h"

#include "GlobalRenderResources.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

IMPLEMENT_GLOBAL_SHADER(FFolioDropShadowPS, "/Folio/FolioDropShadow.usf", "MainPS", SF_Pixel);

namespace
{
	struct FDropShadowPayload final : Folio::FCustomFilterPayload
	{
		/** Premultiplied, linear light. */
		FLinearColor Colour = FLinearColor::Transparent;
		/** Document pixels. */
		FVector2f Offset = FVector2f::ZeroVector;
		float Sigma = 0.0f;
	};

	void AddShadowPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Source, FRDGTextureRef Shadow, FRDGTextureRef Destination,
		const FDropShadowPayload& Payload, const FVector2f OffsetUV, const float Mode, const FIntPoint Size)
	{
		FFolioDropShadowPS::FParameters* Pass = GraphBuilder.AllocParameters<FFolioDropShadowPS::FParameters>();
		Pass->SourceTexture = Source;
		// Bilinear, so a fractional offset moves the silhouette smoothly rather than snapping to pixels.
		Pass->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Pass->ShadowTexture = Shadow;
		Pass->ShadowSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Pass->ShadowColour = FVector4f(Payload.Colour.R, Payload.Colour.G, Payload.Colour.B, Payload.Colour.A);
		Pass->ShadowParameters = FVector4f(OffsetUV.X, OffsetUV.Y, Mode, 0.0f);
		Pass->InvSize = FVector2f(1.0f / Size.X, 1.0f / Size.Y);
		Pass->RenderTargets[0] = FRenderTargetBinding(Destination, ERenderTargetLoadAction::ENoAction);

		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FFolioDropShadowPS> PixelShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioDropShadow"),
			PixelShader, Pass, FIntRect(0, 0, Size.X, Size.Y));
	}

	/** Silhouette, blur, then the layer over it, written back into the layer. Render thread. */
	void AddDropShadowPasses(Folio::FCustomFilterPassArgs& Args)
	{
		const FDropShadowPayload* Payload = static_cast<const FDropShadowPayload*>(Args.Payload);
		const FIntPoint Size = Args.LayerSize;
		if (!Payload || Size.X <= 0 || Size.Y <= 0)
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Args.GraphBuilder;
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Size, Args.Layer->Desc.Format,
			FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource);
		FRDGTextureRef Shadow = GraphBuilder.CreateTexture(Desc, TEXT("FolioDropShadow"));
		FRDGTextureRef Scratch = GraphBuilder.CreateTexture(Desc, TEXT("FolioDropShadowScratch"));

		// The offset and the blur are in document pixels, and a supersampled layer has Scale of its own per one.
		const FVector2f OffsetUV(Payload->Offset.X * Args.Scale / Size.X, Payload->Offset.Y * Args.Scale / Size.Y);
		AddShadowPass(GraphBuilder, Args.Layer, Args.Layer, Shadow, *Payload, OffsetUV, 0.0f, Size);
		Folio::AddBlurPasses(GraphBuilder, Shadow, Scratch, Payload->Sigma * Args.Scale, Size, Args.Scale);

		// A pass cannot read and write one texture, so the result lands in the scratch and is copied back.
		AddShadowPass(GraphBuilder, Args.Layer, Shadow, Scratch, *Payload, OffsetUV, 1.0f, Size);
		AddCopyTexturePass(GraphBuilder, Scratch, Args.Layer);
	}
}

namespace Folio
{
	FCustomFilterCall MakeDropShadowCall(const FColor Colour, const FVector2f Offset, const float Sigma)
	{
		TSharedPtr<FDropShadowPayload, ESPMode::ThreadSafe> Payload = MakeShared<FDropShadowPayload, ESPMode::ThreadSafe>();
		// RmlUi hands the colour over straight and in sRGB; the layer's arithmetic is linear and premultiplied.
		const FLinearColor Linear = FLinearColor::FromSRGBColor(FColor(Colour.R, Colour.G, Colour.B, 255));
		const float Alpha = Colour.A / 255.0f;
		Payload->Colour = FLinearColor(Linear.R * Alpha, Linear.G * Alpha, Linear.B * Alpha, Alpha);
		Payload->Offset = Offset;
		Payload->Sigma = FMath::Max(0.0f, Sigma);

		FCustomFilterCall Call;
		Call.Name = TEXT("drop-shadow");
		// Room for the moved silhouette and the blur's tail, as RmlUi's own ink overflow reserves.
		Call.Reach = 3.0f * Payload->Sigma + FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y));
		Call.Print = HashCombineFast(GetTypeHash(Colour), HashCombineFast(GetTypeHash(Offset), GetTypeHash(Payload->Sigma)));
		Call.Payload = Payload;
		Call.AddPasses = &AddDropShadowPasses;
		return Call;
	}
}
