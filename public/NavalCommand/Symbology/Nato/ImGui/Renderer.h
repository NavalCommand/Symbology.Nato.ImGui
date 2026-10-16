#pragma once

#include <NavalCommand/Symbology/Nato/Api/IRenderer.h>

namespace NavalCommand::Symbology::Nato::ImGui
{
	// Renders the NavalCommand symbol sets used by APP-06(E)(1). Unsupported
	// standard entities return WasDrawn=false and are not silently replaced.
	class Renderer final : public Api::IRenderer
	{
	public:
		Api::MeasureResult Measure(const Api::Symbol& symbol, const Api::RenderContext& context) override;
		Api::RenderResult Render(const Api::Symbol& symbol, const Api::RenderContext& context) override;
	};
}
