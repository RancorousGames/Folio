#pragma once

#include <RmlUi/Core/CompiledFilterShader.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Filter.h>
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
	}

	/// Drops the instancers. RmlUi holds raw pointers, so this must happen after shutdown.
	inline void ReleaseFolioFilters()
	{
		Detail::OwnedFilterInstancers().clear();
	}
}
