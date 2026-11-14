#pragma once

#include "FolioCustomFilterApply.h"

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/** The two passes of filter: drop-shadow(), see FolioDropShadow.usf. */
class FFolioDropShadowPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioDropShadowPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioDropShadowPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowSampler)
		SHADER_PARAMETER(FVector4f, ShadowColour)
		SHADER_PARAMETER(FVector4f, ShadowParameters)
		SHADER_PARAMETER(FVector2f, InvSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

namespace Folio
{
	/**
	 * filter: drop-shadow() as the renderer runs it: a built-in call on the custom-filter seam, which already
	 * carries what the effect needs from the layer stack - a print for the layer cache, a margin for the
	 * shadow to land in, and RDG passes over the resolved layer.
	 *
	 * Colour is RmlUi's, straight sRGB. Offset and Sigma are in document pixels, as RmlUi resolves them.
	 */
	FCustomFilterCall MakeDropShadowCall(FColor Colour, FVector2f Offset, float Sigma);
}
