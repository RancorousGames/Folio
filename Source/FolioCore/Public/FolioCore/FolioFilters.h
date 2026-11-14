#pragma once

#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderManager.h>

#include <cstdlib>
#include <memory>
#include <vector>

namespace Folio
{
	//
	// Defined here rather than compiled into FolioCore, for the same reason the font effect
	// instancers are: RmlUi is a static library linked into both modules, each with its own
	// globals, and an instancer's constructor calls RegisterProperty, which resolves its parsers
	// through the StyleSheetSpecification that only one Rml::Initialise populated. Constructing
	// one in the other module reads state that was never built.
	//
	namespace Detail
	{
		/// `backdrop-filter: outside-blur(6dp)` - blurs everything the element is not.
		///
		/// The modal case. RmlUi's own `blur` in a backdrop-filter blurs what is behind the
		/// element and nothing else, which is right for a frosted panel and useless for a scrim:
		/// a dialog wants the screen behind it softened and itself left sharp.
		///
		/// The renderer does the inverting, because only it knows the viewport. This carries the
		/// radius and a name it can recognise.
		class FOutsideBlur final : public Rml::Filter
		{
		public:
			bool Initialise(const Rml::NumericValue InSigma)
			{
				SigmaValue = InSigma;
				return Rml::Any(InSigma.unit & Rml::Unit::LENGTH);
			}

			Rml::CompiledFilter CompileFilter(Rml::Element* Element) const override
			{
				const float Radius = Element->ResolveLength(SigmaValue);
				return Element->GetRenderManager()->CompileFilter("outside-blur",
					Rml::Dictionary{ { "sigma", Rml::Variant(Radius) } });
			}

			// Deliberately no ExtendInkOverflow. RmlUi's blur widens the scissor so the filter can
			// read past the element's edge; here the scissor is the hole, and widening it would
			// leave a ring of unblurred screen around the element.

		private:
			Rml::NumericValue SigmaValue;
		};

		class FOutsideBlurInstancer final : public Rml::FilterInstancer
		{
		public:
			FOutsideBlurInstancer()
			{
				SigmaId = RegisterProperty("sigma", "0px").AddParser("length").GetId();
				RegisterShorthand("filter", "sigma", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String& /*Name*/,
				const Rml::PropertyDictionary& Properties) override
			{
				const Rml::Property* Sigma = Properties.GetProperty(SigmaId);
				if (Sigma == nullptr)
				{
					return nullptr;
				}

				auto Filter = Rml::MakeShared<FOutsideBlur>();
				return Filter->Initialise(Sigma->GetNumericValue()) ? Filter : nullptr;
			}

		private:
			Rml::PropertyId SigmaId = {};
		};

		/// `filter: gpu-filter("rui-glow profile=fancy intensity=4.2 reach=120")` - a game-registered
		/// GPU filter (see Folio's FolioCustomFilters.h). The string is the filter's name and its own
		/// arguments; `reach=<dp>` is read here too, because only RmlUi can widen the ink overflow
		/// that gives a glow layer pixels to land on outside the element.
		class FGpuFilter final : public Rml::Filter
		{
		public:
			bool Initialise(const Rml::String& InValue)
			{
				Value = InValue;
				const size_t At = Value.find("reach=");
				if (At != Rml::String::npos)
				{
					ReachDp = std::strtof(Value.c_str() + At + 6, nullptr);
				}
				return !Value.empty();
			}

			Rml::CompiledFilter CompileFilter(Rml::Element* Element) const override
			{
				const float Dp = DpRatio(Element);
				const Rml::Vector2f Position = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
				const Rml::Vector2f Size = Element->GetBox().GetSize(Rml::BoxArea::Border);
				// Effective opacity, for filters whose output does not come from the element's own pixels
				// (a radial haze). RmlUi recompiles filters when the element's own opacity changes.
				float Opacity = 1.0f;
				for (Rml::Element* At = Element; At; At = At->GetParentNode())
				{
					Opacity *= At->GetComputedValues().opacity();
				}
				return Element->GetRenderManager()->CompileFilter("gpu-filter", Rml::Dictionary{
					{ "value", Rml::Variant(Value) },
					{ "opacity", Rml::Variant(Opacity) },
					{ "dp", Rml::Variant(Dp) },
					{ "reach", Rml::Variant(ReachDp * Dp) },
					{ "x", Rml::Variant(Position.x) },
					{ "y", Rml::Variant(Position.y) },
					{ "w", Rml::Variant(Size.x) },
					{ "h", Rml::Variant(Size.y) } });
			}

			void ExtendInkOverflow(Rml::Element* Element, Rml::Rectanglef& Overflow) const override
			{
				Overflow = Overflow.Extend(ReachDp * DpRatio(Element));
			}

		private:
			static float DpRatio(Rml::Element* Element)
			{
				const Rml::Context* Context = Element ? Element->GetContext() : nullptr;
				return Context ? Context->GetDensityIndependentPixelRatio() : 1.0f;
			}

			Rml::String Value;
			float ReachDp = 0.0f;
		};

		class FGpuFilterInstancer final : public Rml::FilterInstancer
		{
		public:
			FGpuFilterInstancer()
			{
				ValueId = RegisterProperty("value", "").AddParser("string").GetId();
				RegisterShorthand("filter", "value", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String& /*Name*/,
				const Rml::PropertyDictionary& Properties) override
			{
				const Rml::Property* Value = Properties.GetProperty(ValueId);
				if (Value == nullptr)
				{
					return nullptr;
				}

				auto Filter = Rml::MakeShared<FGpuFilter>();
				return Filter->Initialise(Value->Get<Rml::String>()) ? Filter : nullptr;
			}

		private:
			Rml::PropertyId ValueId = {};
		};

		/// `filter: drop-shadow(...)`, taking its colour first as RmlUi writes it or last as CSS does.
		///
		/// RmlUi's own instancer reads only `color offset-x offset-y [sigma]`, and the CSS order every
		/// browser-made mockup uses, `offset-x offset-y [blur] color`, is a syntax error that drops the whole
		/// declaration. This one parses either and builds the same filter: the renderer is handed RmlUi's own
		/// "drop-shadow" parameters, so both renderers draw it as before.
		class FDropShadow final : public Rml::Filter
		{
		public:
			bool Initialise(const Rml::Colourb InColour, const Rml::NumericValue InX, const Rml::NumericValue InY,
				const Rml::NumericValue InSigma)
			{
				Colour = InColour;
				X = InX;
				Y = InY;
				Sigma = InSigma;
				return Rml::Any(X.unit & Rml::Unit::LENGTH) && Rml::Any(Y.unit & Rml::Unit::LENGTH)
					&& Rml::Any(Sigma.unit & Rml::Unit::LENGTH);
			}

			Rml::CompiledFilter CompileFilter(Rml::Element* Element) const override
			{
				const Rml::Vector2f Offset(Element->ResolveLength(X), Element->ResolveLength(Y));
				return Element->GetRenderManager()->CompileFilter("drop-shadow", Rml::Dictionary{
					{ "color", Rml::Variant(Colour) },
					{ "offset", Rml::Variant(Offset) },
					{ "sigma", Rml::Variant(Element->ResolveLength(Sigma)) } });
			}

			// Room for the moved silhouette and the blur's tail, as RmlUi's own does.
			void ExtendInkOverflow(Rml::Element* Element, Rml::Rectanglef& Overflow) const override
			{
				const Rml::Vector2f Offset(Element->ResolveLength(X), Element->ResolveLength(Y));
				const Rml::Vector2f Blur(3.0f * Element->ResolveLength(Sigma));
				Overflow = Overflow.Extend(Rml::Math::Max(-Offset, Rml::Vector2f(0.0f)) + Blur,
					Rml::Math::Max(Offset, Rml::Vector2f(0.0f)) + Blur);
			}

		private:
			Rml::Colourb Colour;
			Rml::NumericValue X, Y, Sigma;
		};

		class FDropShadowInstancer final : public Rml::FilterInstancer
		{
		public:
			FDropShadowInstancer()
			{
				// A fall-through shorthand tries each value against the properties in turn, so a leading colour
				// lands in ColourId and a trailing one, after the lengths, in TrailingColourId.
				ColourId = RegisterProperty("color", "transparent").AddParser("color").GetId();
				XId = RegisterProperty("offset-x", "0px").AddParser("length").GetId();
				YId = RegisterProperty("offset-y", "0px").AddParser("length").GetId();
				SigmaId = RegisterProperty("sigma", "0px").AddParser("length").GetId();
				TrailingColourId = RegisterProperty("color-after", "transparent").AddParser("color").GetId();
				RegisterShorthand("filter", "color, offset-x, offset-y, sigma, color-after", Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String& /*Name*/,
				const Rml::PropertyDictionary& Properties) override
			{
				const Rml::Property* Colour = Properties.GetProperty(ColourId);
				const Rml::Property* Trailing = Properties.GetProperty(TrailingColourId);
				const Rml::Property* X = Properties.GetProperty(XId);
				const Rml::Property* Y = Properties.GetProperty(YId);
				const Rml::Property* Sigma = Properties.GetProperty(SigmaId);
				if (!X || !Y || !Sigma)
				{
					return nullptr;
				}

				// Both colours default to transparent, so whichever was written is the one with any alpha.
				Rml::Colourb Chosen = Colour ? Colour->Get<Rml::Colourb>() : Rml::Colourb(0, 0, 0, 0);
				if (Trailing && Trailing->Get<Rml::Colourb>().alpha > 0)
				{
					Chosen = Trailing->Get<Rml::Colourb>();
				}

				auto Filter = Rml::MakeShared<FDropShadow>();
				return Filter->Initialise(Chosen, X->GetNumericValue(), Y->GetNumericValue(), Sigma->GetNumericValue())
					? Filter : nullptr;
			}

		private:
			Rml::PropertyId ColourId = {}, TrailingColourId = {}, XId = {}, YId = {}, SigmaId = {};
		};

		inline std::vector<std::unique_ptr<Rml::FilterInstancer>>& OwnedFilterInstancers()
		{
			static std::vector<std::unique_ptr<Rml::FilterInstancer>> Owned;
			return Owned;
		}
	}

	/// Adds Folio's own filters to the factory.
	///
	/// Call from the module that called Rml::Initialise, after it.
	inline void RegisterFolioFilters()
	{
		auto& Owned = Detail::OwnedFilterInstancers();
		if (!Owned.empty())
		{
			return;
		}

		auto Instancer = std::make_unique<Detail::FOutsideBlurInstancer>();
		Rml::Factory::RegisterFilterInstancer("outside-blur", Instancer.get());
		Owned.push_back(std::move(Instancer));

		auto Gpu = std::make_unique<Detail::FGpuFilterInstancer>();
		Rml::Factory::RegisterFilterInstancer("gpu-filter", Gpu.get());
		Owned.push_back(std::move(Gpu));

		// Replaces RmlUi's own, which reads only the colour-first order.
		auto Shadow = std::make_unique<Detail::FDropShadowInstancer>();
		Rml::Factory::RegisterFilterInstancer("drop-shadow", Shadow.get());
		Owned.push_back(std::move(Shadow));
	}

	/// Drops the instancers. RmlUi holds raw pointers, so this must happen after shutdown.
	inline void ReleaseFolioFilters()
	{
		Detail::OwnedFilterInstancers().clear();
	}
}
