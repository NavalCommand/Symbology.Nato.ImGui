#include <NavalCommand/Symbology/Nato/Api/Amplifiers.h>
#include <NavalCommand/Symbology/Nato/Api/Context.h>
#include <NavalCommand/Symbology/Nato/Api/RenderContext.h>
#include <NavalCommand/Symbology/Nato/Api/Sidc.h>
#include <NavalCommand/Symbology/Nato/Api/Symbol.h>
#include <NavalCommand/Symbology/Nato/ImGui/Renderer.h>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <imgui.h>
#include <stdexcept>
#include <vector>

namespace NavalCommand::Symbology::Nato::ImGui
{
	namespace
	{
		class TestProjection final : public Api::IMapProjection
		{
		public:
			Api::ScreenPoint Project(Api::GeographicCoordinate coordinate) const noexcept override
			{
				return {320.0 + coordinate.LongitudeDegrees, 240.0 - coordinate.LatitudeDegrees};
			}

			Api::ScreenVector ProjectLocalOffsetMeters(Api::GeographicCoordinate, double eastMeters, double northMeters) const noexcept override
			{
				return {eastMeters * 0.05, -northMeters * 0.05};
			}
		};

		class ImGuiFrame final
		{
		public:
			ImGuiFrame()
			{
				::ImGui::CreateContext();
				ImGuiIO& io = ::ImGui::GetIO();
				io.DisplaySize = {800.0f, 600.0f};
				io.DeltaTime = 1.0f / 60.0f;
				unsigned char* fontPixels{};
				int fontWidth{};
				int fontHeight{};
				io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
				::ImGui::NewFrame();
				::ImGui::Begin("renderer test");
			}

			~ImGuiFrame()
			{
				::ImGui::End();
				::ImGui::EndFrame();
				::ImGui::DestroyContext();
			}

			ImGuiFrame(const ImGuiFrame&) = delete;
			ImGuiFrame& operator=(const ImGuiFrame&) = delete;
		};

		Api::Symbol MakeSymbol(Api::SymbolSet symbolSet,
							   Api::StandardIdentity identity = Api::StandardIdentity::Friendly,
							   std::uint8_t entity = 0,
							   std::uint8_t entityType = 0,
							   std::uint8_t entitySubtype = 0,
							   std::uint8_t modifier1 = 0)
		{
			Api::SidcFields fields;
			fields.ContextValue = Api::Context::Simulation;
			fields.Identity = identity;
			fields.SymbolSetValue = symbolSet;
			fields.Entity = entity;
			fields.EntityType = entityType;
			fields.EntitySubtype = entitySubtype;
			fields.Modifier1 = modifier1;
			auto identification = Api::Sidc::Create(fields);
			if (!identification)
			{
				throw std::runtime_error(identification.error().Message);
			}
			return {*identification, {}, std::nullopt};
		}

		Api::RenderContext MakeContext(const Api::IMapProjection* projection = nullptr)
		{
			return {{400.0, 300.0}, {400.0, 300.0}, {{100.0, 80.0}, {700.0, 520.0}}, projection, 1.0, 0.0, 60.0};
		}

		bool HasVertexColorFrom(const ImDrawList& drawing, int firstVertex, ImU32 color)
		{
			for (int vertexIndex = firstVertex; vertexIndex < drawing.VtxBuffer.Size; ++vertexIndex)
			{
				if (drawing.VtxBuffer[vertexIndex].col == color)
				{
					return true;
				}
			}
			return false;
		}
	}

	TEST(RendererTest, UnknownOpenDomainFramesHaveRoomForMissionSectors)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto context = MakeContext();
		context.ShowLabels = false;
		const auto surface = renderer.Measure(MakeSymbol(Api::SymbolSet::SeaSurface, Api::StandardIdentity::Unknown), context);
		const auto air = renderer.Measure(MakeSymbol(Api::SymbolSet::Air, Api::StandardIdentity::Unknown), context);
		const auto subsurface = renderer.Measure(MakeSymbol(Api::SymbolSet::SeaSubsurface, Api::StandardIdentity::Unknown), context);
		EXPECT_GT(air.SymbolBounds.Maximum.Y - air.SymbolBounds.Minimum.Y, surface.SymbolBounds.Maximum.Y - surface.SymbolBounds.Minimum.Y);
		EXPECT_NEAR(air.SymbolBounds.Minimum.X, subsurface.SymbolBounds.Minimum.X, 0.001);
		EXPECT_NEAR(air.SymbolBounds.Maximum.X, subsurface.SymbolBounds.Maximum.X, 0.001);
		EXPECT_NEAR(air.SymbolBounds.Minimum.Y - context.VisualAnchor.Y, context.VisualAnchor.Y - subsurface.SymbolBounds.Maximum.Y, 0.001);
		EXPECT_NEAR(air.SymbolBounds.Maximum.Y - context.VisualAnchor.Y, context.VisualAnchor.Y - subsurface.SymbolBounds.Minimum.Y, 0.001);
	}

	TEST(RendererTest, GuidedMissileCruiserCommonModifierIsSupported)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto fields = MakeSymbol(Api::SymbolSet::SeaSurface, Api::StandardIdentity::Friendly, 12, 2, 2).Identification.Fields();
		fields.Modifier1 = 33;
		fields.ThirdTen[0] = '1';
		const auto identification = Api::Sidc::Create(fields);
		ASSERT_TRUE(identification);
		EXPECT_TRUE(renderer.Render({*identification, {}, std::nullopt}, MakeContext()).WasDrawn);
		fields.ThirdTen[0] = '0';
		const auto invalidModifier = Api::Sidc::Create(fields);
		ASSERT_TRUE(invalidModifier);
		EXPECT_FALSE(renderer.Render({*invalidModifier, {}, std::nullopt}, MakeContext()).WasDrawn);
	}

	TEST(RendererTest, UnknownSubsurfaceFillFringeMirrorsAirWithoutProtrudingAtJoins)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto context = MakeContext();
		context.ShowLabels = false;
		auto& drawing = *::ImGui::GetWindowDrawList();
		const auto fillColor = IM_COL32(255, 255, 128, 255);
		const auto fillVertices = [&](Api::SymbolSet set)
		{
			const int start = drawing.VtxBuffer.Size;
			renderer.Render(MakeSymbol(set, Api::StandardIdentity::Unknown), context);
			std::vector<ImVec2> points;
			for (int index = start; index < drawing.VtxBuffer.Size; ++index)
			{
				if (drawing.VtxBuffer[index].col == fillColor)
				{
					points.push_back(drawing.VtxBuffer[index].pos);
				}
			}
			return points;
		};
		const auto air = fillVertices(Api::SymbolSet::Air);
		const auto subsurface = fillVertices(Api::SymbolSet::SeaSubsurface);
		ASSERT_FALSE(air.empty());
		ASSERT_EQ(air.size(), subsurface.size());
		for (const auto point : air)
		{
			const auto reflectedY = 2.0 * context.VisualAnchor.Y - point.y;
			bool matched = false;
			for (const auto other : subsurface)
			{
				matched = matched || (std::abs(point.x - other.x) < 0.001 && std::abs(reflectedY - other.y) < 0.001);
			}
			EXPECT_TRUE(matched) << point.x << ", " << reflectedY;
		}
	}

	TEST(RendererTest, UsesStandardAffiliationColoursAndEntityGeometry)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto* drawing = ::ImGui::GetWindowDrawList();
		const int initialVertexCount = drawing->VtxBuffer.Size;

		const auto friendly = renderer.Render(MakeSymbol(Api::SymbolSet::Air, Api::StandardIdentity::Friendly, 11, 1), MakeContext());
		const int friendlyVertexCount = drawing->VtxBuffer.Size;
		const auto hostile = renderer.Render(MakeSymbol(Api::SymbolSet::Air, Api::StandardIdentity::Hostile, 11, 1), MakeContext());

		EXPECT_TRUE(friendly.WasDrawn);
		EXPECT_TRUE(hostile.WasDrawn);
		EXPECT_GT(friendlyVertexCount, initialVertexCount);
		EXPECT_GT(drawing->VtxBuffer.Size, friendlyVertexCount);
		EXPECT_NE(friendly.Bounds.Minimum.X, friendly.Bounds.Maximum.X);
	}

	TEST(RendererTest, UsesRoundedFriendlyFrameForSeaSurfaceSymbols)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto* drawing = ::ImGui::GetWindowDrawList();
		const int initialVertexCount = drawing->VtxBuffer.Size;

		const auto seaSurface = renderer.Render(MakeSymbol(Api::SymbolSet::SeaSurface, Api::StandardIdentity::Friendly), MakeContext());
		const int seaSurfaceVertexCount = drawing->VtxBuffer.Size - initialVertexCount;

		const int neutralStart = drawing->VtxBuffer.Size;
		const auto neutralSeaSurface = renderer.Render(MakeSymbol(Api::SymbolSet::SeaSurface, Api::StandardIdentity::Neutral), MakeContext());
		const int neutralSeaSurfaceVertexCount = drawing->VtxBuffer.Size - neutralStart;

		ASSERT_TRUE(seaSurface.WasDrawn);
		ASSERT_TRUE(neutralSeaSurface.WasDrawn);
		EXPECT_GT(seaSurfaceVertexCount, neutralSeaSurfaceVertexCount + 30);
	}

	TEST(RendererTest, AppliesScaleAndReturnsBoundsThatIncludeText)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto symbol = MakeSymbol(Api::SymbolSet::SeaSurface, Api::StandardIdentity::Neutral);
		symbol.ExternalAmplifiers.UniqueDesignation = "CVN-72";
		auto smallContext = MakeContext();
		smallContext.SymbolScale = 0.75;
		auto largeContext = MakeContext();
		largeContext.SymbolScale = 1.5;

		const auto small = renderer.Render(symbol, smallContext);
		const auto large = renderer.Render(symbol, largeContext);

		ASSERT_TRUE(small.WasDrawn);
		ASSERT_TRUE(large.WasDrawn);
		const double smallWidth = small.Bounds.Maximum.X - small.Bounds.Minimum.X;
		const double largeWidth = large.Bounds.Maximum.X - large.Bounds.Minimum.X;
		EXPECT_GT(largeWidth, smallWidth);
		EXPECT_GT(small.Bounds.Maximum.X, 400.0);
	}

	TEST(RendererTest, SpeedLeaderUsesCourseAndProjectionAndOmitsUnknownMovement)
	{
		ImGuiFrame frame;
		Renderer renderer;
		TestProjection projection;
		auto symbol = MakeSymbol(Api::SymbolSet::SeaSurface, Api::StandardIdentity::Friendly);
		symbol.ExternalAmplifiers.Position = Api::PositionAmplifier{{{48.0}, {5.0}}, std::nullopt};
		symbol.ExternalAmplifiers.SpeedMetersPerSecond = 10.0;
		symbol.ExternalAmplifiers.CourseOverGroundRadians = 0.0;
		auto context = MakeContext(&projection);
		const int initialVertexCount = ::ImGui::GetWindowDrawList()->VtxBuffer.Size;

		const auto moving = renderer.Render(symbol, context);
		const int movingVertexCount = ::ImGui::GetWindowDrawList()->VtxBuffer.Size - initialVertexCount;

		symbol.ExternalAmplifiers.CourseOverGroundRadians.reset();
		const auto unknownCourse = renderer.Render(symbol, context);
		const int unknownVertexCount = ::ImGui::GetWindowDrawList()->VtxBuffer.Size - initialVertexCount - movingVertexCount;

		ASSERT_TRUE(moving.WasDrawn);
		ASSERT_TRUE(unknownCourse.WasDrawn);
		EXPECT_LT(moving.GeographicBounds.Minimum.Y, unknownCourse.GeographicBounds.Minimum.Y);
		EXPECT_LT(unknownVertexCount, movingVertexCount);
	}

	TEST(RendererTest, RendersProjectedUncertaintyAndRejectsUnimplementedStandardEntity)
	{
		ImGuiFrame frame;
		Renderer renderer;
		TestProjection projection;
		auto uncertain = MakeSymbol(Api::SymbolSet::SeaSubsurface, Api::StandardIdentity::Unknown, 11, 1);
		uncertain.ExternalAmplifiers.Position = Api::PositionAmplifier{{{48.0}, {5.0}}, Api::GeographicUncertaintyEllipse{200.0, 100.0, 0.5}};
		const auto uncertainResult = renderer.Render(uncertain, MakeContext(&projection));

		auto unsupported = MakeSymbol(Api::SymbolSet::SeaSubsurface, Api::StandardIdentity::Unknown, 11, 2);
		const auto unsupportedResult = renderer.Render(unsupported, MakeContext(&projection));

		ASSERT_TRUE(uncertainResult.WasDrawn);
		EXPECT_GT(uncertainResult.Bounds.Maximum.X - uncertainResult.Bounds.Minimum.X, 38.0);
		EXPECT_FALSE(unsupportedResult.WasDrawn);
	}

	TEST(RendererTest, HeadingAloneIsShownWithoutCreatingMovementGraphics)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto withoutHeading = MakeSymbol(Api::SymbolSet::SeaSurface);
		auto withHeading = withoutHeading;
		withHeading.ExternalAmplifiers.HeadingRadians = 0.75;

		const auto baseline = renderer.Render(withoutHeading, MakeContext());
		const int baselineVertexCount = ::ImGui::GetWindowDrawList()->VtxBuffer.Size;
		const auto headingOnly = renderer.Render(withHeading, MakeContext());
		const int headingVertexCount = ::ImGui::GetWindowDrawList()->VtxBuffer.Size - baselineVertexCount;

		ASSERT_TRUE(baseline.WasDrawn);
		ASSERT_TRUE(headingOnly.WasDrawn);
		EXPECT_GT(headingVertexCount, 0);
		EXPECT_EQ(headingOnly.GeographicBounds.Maximum.X, baseline.GeographicBounds.Maximum.X);
	}

	TEST(RendererTest, DrawsTheExactSupportedEntityAndModifierAssignments)
	{
		ImGuiFrame frame;
		Renderer renderer;
		struct EntityCase
		{
			Api::SymbolSet Set;
			std::uint8_t Entity;
			std::uint8_t EntityType;
			std::uint8_t EntitySubtype;
			std::uint8_t Modifier1;
		};
		const std::array entityCases{EntityCase{Api::SymbolSet::Air, 11, 1, 0, 0},
									 EntityCase{Api::SymbolSet::Air, 11, 2, 0, 0},
									 EntityCase{Api::SymbolSet::Air, 11, 1, 0, 4},
									 EntityCase{Api::SymbolSet::Air, 11, 1, 0, 12},
									 EntityCase{Api::SymbolSet::Air, 11, 1, 0, 16},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 2, 2, 0},
									 EntityCase{Api::SymbolSet::AirMissile, 11, 0, 0, 6},
									 EntityCase{Api::SymbolSet::AirMissile, 11, 0, 0, 7},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 1, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 2, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 2, 3, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 2, 4, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 2, 5, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 3, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 12, 5, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 13, 1, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSurface, 14, 1, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSubsurface, 11, 1, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSubsurface, 11, 1, 0, 8},
									 EntityCase{Api::SymbolSet::SeaSubsurface, 11, 1, 0, 9},
									 EntityCase{Api::SymbolSet::SeaSubsurface, 11, 1, 0, 10},
									 EntityCase{Api::SymbolSet::SeaSubsurface, 13, 1, 0, 0},
									 EntityCase{Api::SymbolSet::SeaSubsurface, 13, 3, 0, 0},
									 EntityCase{Api::SymbolSet::MineWarfare, 11, 0, 0, 0},
									 EntityCase{Api::SymbolSet::LandEquipment, 11, 5, 0, 0},
									 EntityCase{Api::SymbolSet::LandEquipment, 11, 11, 0, 0},
									 EntityCase{Api::SymbolSet::LandInstallation, 11, 0, 0, 0}};
		auto* drawing = ::ImGui::GetWindowDrawList();
		for (const EntityCase& entityCase : entityCases)
		{
			const int previousVertexCount = drawing->VtxBuffer.Size;
			const auto symbol =
				MakeSymbol(entityCase.Set, Api::StandardIdentity::Friendly, entityCase.Entity, entityCase.EntityType, entityCase.EntitySubtype, entityCase.Modifier1);
			const auto result = renderer.Render(symbol, MakeContext());
			ASSERT_TRUE(result.WasDrawn) << "set=" << static_cast<int>(entityCase.Set) << " entity=" << static_cast<int>(entityCase.Entity)
										 << " type=" << static_cast<int>(entityCase.EntityType) << " subtype=" << static_cast<int>(entityCase.EntitySubtype)
										 << " modifier=" << static_cast<int>(entityCase.Modifier1);
			EXPECT_GT(drawing->VtxBuffer.Size, previousVertexCount);
		}

		const auto wrongLandVehicleAssignment = MakeSymbol(Api::SymbolSet::LandEquipment, Api::StandardIdentity::Friendly, 12);
		EXPECT_FALSE(renderer.Render(wrongLandVehicleAssignment, MakeContext()).WasDrawn);
	}

	TEST(RendererTest, UsesAffiliationAndOperationalConditionColours)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto* drawing = ::ImGui::GetWindowDrawList();
		const auto expectsAffiliationColor = [&](Api::StandardIdentity identity, ImU32 expectedColor)
		{
			const int firstVertex = drawing->VtxBuffer.Size;
			const auto result = renderer.Render(MakeSymbol(Api::SymbolSet::SeaSurface, identity), MakeContext());
			ASSERT_TRUE(result.WasDrawn);
			EXPECT_TRUE(HasVertexColorFrom(*drawing, firstVertex, expectedColor));
		};
		expectsAffiliationColor(Api::StandardIdentity::Unknown, IM_COL32(255, 255, 128, 255));
		expectsAffiliationColor(Api::StandardIdentity::Friendly, IM_COL32(128, 224, 255, 255));
		expectsAffiliationColor(Api::StandardIdentity::Neutral, IM_COL32(170, 255, 170, 255));
		expectsAffiliationColor(Api::StandardIdentity::Hostile, IM_COL32(255, 128, 128, 255));

		Api::SidcFields destroyedFields;
		destroyedFields.SymbolSetValue = Api::SymbolSet::SeaSurface;
		destroyedFields.Status = Api::SymbolStatus::PresentDestroyed;
		auto destroyed = MakeSymbol(Api::SymbolSet::SeaSurface);
		const auto destroyedSidc = Api::Sidc::Create(destroyedFields);
		ASSERT_TRUE(destroyedSidc);
		destroyed.Identification = *destroyedSidc;
		destroyed.IsDestroyed = true;
		const int firstDestroyedVertex = drawing->VtxBuffer.Size;
		const auto destroyedResult = renderer.Render(destroyed, MakeContext());
		ASSERT_TRUE(destroyedResult.WasDrawn);
		EXPECT_TRUE(HasVertexColorFrom(*drawing, firstDestroyedVertex, IM_COL32(255, 0, 0, 255)));

		const auto expectsConditionColor = [&](Api::SymbolStatus status, ImU32 expectedColor)
		{
			Api::SidcFields fields;
			fields.Identity = Api::StandardIdentity::Friendly;
			fields.SymbolSetValue = Api::SymbolSet::SeaSurface;
			fields.Status = status;
			const auto identification = Api::Sidc::Create(fields);
			ASSERT_TRUE(identification);
			const Api::Symbol symbol{*identification, {}, std::nullopt};
			const int firstVertex = drawing->VtxBuffer.Size;
			const auto result = renderer.Render(symbol, MakeContext());
			ASSERT_TRUE(result.WasDrawn);
			EXPECT_TRUE(HasVertexColorFrom(*drawing, firstVertex, expectedColor));
		};
		expectsConditionColor(Api::SymbolStatus::PresentFullyCapable, IM_COL32(0, 255, 0, 255));
		expectsConditionColor(Api::SymbolStatus::PresentDamaged, IM_COL32(255, 255, 0, 255));
		expectsConditionColor(Api::SymbolStatus::PresentFullToCapacity, IM_COL32(0, 180, 240, 255));
	}

	TEST(RendererTest, AppliesAltitudeAndDepthOnlyToApplicableSymbolSets)
	{
		ImGuiFrame frame;
		Renderer renderer;
		const auto air = MakeSymbol(Api::SymbolSet::Air);
		const auto airBaseline = renderer.Render(air, MakeContext());
		auto airWithAltitude = air;
		airWithAltitude.ExternalAmplifiers.VerticalPosition = Api::VerticalPositionAmplifier{9000.0, Api::VerticalReference::Altitude};
		const auto altitudeResult = renderer.Render(airWithAltitude, MakeContext());
		ASSERT_TRUE(airBaseline.WasDrawn);
		ASSERT_TRUE(altitudeResult.WasDrawn);
		EXPECT_NE(altitudeResult.Bounds.Maximum.X, airBaseline.Bounds.Maximum.X);

		const auto seaSurface = MakeSymbol(Api::SymbolSet::SeaSurface);
		const auto seaSurfaceBaseline = renderer.Render(seaSurface, MakeContext());
		auto surfaceWithDepth = seaSurface;
		surfaceWithDepth.ExternalAmplifiers.VerticalPosition = Api::VerticalPositionAmplifier{20.0, Api::VerticalReference::Depth};
		const auto ignoredDepth = renderer.Render(surfaceWithDepth, MakeContext());
		ASSERT_TRUE(seaSurfaceBaseline.WasDrawn);
		ASSERT_TRUE(ignoredDepth.WasDrawn);
		EXPECT_LT(ignoredDepth.LayoutBounds.Maximum.Y, seaSurfaceBaseline.LayoutBounds.Maximum.Y);

		auto submarine = MakeSymbol(Api::SymbolSet::SeaSubsurface, Api::StandardIdentity::Friendly, 11, 1);
		submarine.ExternalAmplifiers.VerticalPosition = Api::VerticalPositionAmplifier{200.0, Api::VerticalReference::Depth};
		const auto depthResult = renderer.Render(submarine, MakeContext());
		ASSERT_TRUE(depthResult.WasDrawn);
		EXPECT_GT(depthResult.LayoutBounds.Maximum.Y, ignoredDepth.LayoutBounds.Maximum.Y);
	}
	TEST(RendererMeasurementTest, SharesBoundsWithoutEmittingCommandsAcrossStylesAndFonts)
	{
		ImGuiFrame frame;
		Renderer concrete;
		Api::IRenderer& renderer = concrete;
		auto symbol = MakeSymbol(Api::SymbolSet::Air, Api::StandardIdentity::Friendly, 11, 1, 0, 4);
		symbol.ExternalAmplifiers.UniqueDesignation = "Long aircraft name\nSecond line";
		symbol.ExternalAmplifiers.NameOrType = "F-14A (Tomcat)";
		for (auto preset : {Api::StylePreset::FilledDark, Api::StylePreset::UnfilledDark, Api::StylePreset::FilledLight})
		{
			for (double scale : {0.5, 1.0, 2.3, 4.0})
			{
				for (bool visible : {false, true})
				{
					for (bool detailed : {false, true})
					{
						auto context = MakeContext();
						context.Style = Api::SymbolStyle::FromPreset(preset);
						context.SymbolScale = scale;
						context.ShowLabels = visible;
						context.DetailedLabels = detailed;
						auto& drawing = *::ImGui::GetWindowDrawList();
						const int vertexCount = drawing.VtxBuffer.Size;
						const int indexCount = drawing.IdxBuffer.Size;
						const int commandCount = drawing.CmdBuffer.Size;
						const auto measured = renderer.Measure(symbol, context);
						ASSERT_TRUE(measured.Supported);
						EXPECT_EQ(drawing.VtxBuffer.Size, vertexCount);
						EXPECT_EQ(drawing.IdxBuffer.Size, indexCount);
						EXPECT_EQ(drawing.CmdBuffer.Size, commandCount);
						const auto rendered = renderer.Render(symbol, context);
						EXPECT_EQ(measured.LayoutBounds.Maximum.X, rendered.LayoutBounds.Maximum.X);
						EXPECT_EQ(measured.LayoutBounds.Maximum.Y, rendered.LayoutBounds.Maximum.Y);
						EXPECT_EQ(measured.SymbolBounds.Minimum.X, rendered.SymbolBounds.Minimum.X);
						EXPECT_EQ(measured.Bounds.Minimum.Y, rendered.Bounds.Minimum.Y);
					}
				}
			}
		}
	}

	TEST(RendererMeasurementTest, VisibleRichAndMultilineLabelsExpandOnlyPresentationBounds)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto symbol = MakeSymbol(Api::SymbolSet::SeaSurface);
		symbol.ExternalAmplifiers.UniqueDesignation = "First line\nSecond line\nThird line";
		symbol.ExternalAmplifiers.NameOrType = "FFG";
		auto context = MakeContext();
		context.ShowLabels = false;
		const auto hidden = renderer.Measure(symbol, context);
		context.ShowLabels = true;
		context.DetailedLabels = false;
		const auto minimal = renderer.Measure(symbol, context);
		context.DetailedLabels = true;
		const auto rich = renderer.Measure(symbol, context);
		EXPECT_GT(minimal.LayoutBounds.Maximum.X, hidden.LayoutBounds.Maximum.X);
		EXPECT_GT(rich.LayoutBounds.Maximum.Y, minimal.LayoutBounds.Maximum.Y + 40.0);
		EXPECT_EQ(rich.SymbolBounds.Maximum.Y, hidden.SymbolBounds.Maximum.Y);
		EXPECT_EQ(rich.GeographicBounds.Maximum.X, hidden.GeographicBounds.Maximum.X);
	}

	TEST(RendererMeasurementTest, DisplacementNeverMovesKinematicsOrUncertainty)
	{
		ImGuiFrame frame;
		Renderer renderer;
		TestProjection projection;
		auto symbol = MakeSymbol(Api::SymbolSet::SeaSurface);
		symbol.ExternalAmplifiers.Position = Api::PositionAmplifier{{{48.0}, {5.0}}, Api::GeographicUncertaintyEllipse{200.0, 100.0, 0.5}};
		symbol.ExternalAmplifiers.CourseOverGroundRadians = 0.0;
		symbol.ExternalAmplifiers.SpeedMetersPerSecond = 100.0;
		auto context = MakeContext(&projection);
		context.ShowLabels = false;
		const auto original = renderer.Measure(symbol, context);
		context.VisualAnchor = {620.0, 410.0};
		const auto displaced = renderer.Measure(symbol, context);
		EXPECT_EQ(displaced.GeographicBounds.Minimum.X, original.GeographicBounds.Minimum.X);
		EXPECT_EQ(displaced.GeographicBounds.Minimum.Y, original.GeographicBounds.Minimum.Y);
		EXPECT_EQ(displaced.GeographicBounds.Maximum.X, original.GeographicBounds.Maximum.X);
		EXPECT_EQ(displaced.GeographicBounds.Maximum.Y, original.GeographicBounds.Maximum.Y);
		EXPECT_NEAR(displaced.SymbolBounds.Minimum.X - original.SymbolBounds.Minimum.X, 220.0, 0.001);
		EXPECT_NEAR(displaced.SymbolBounds.Minimum.Y - original.SymbolBounds.Minimum.Y, 110.0, 0.001);
		context.Layer = Api::RenderLayer::Geographic;
		const int firstVertex = ::ImGui::GetWindowDrawList()->VtxBuffer.Size;
		renderer.Render(symbol, context);
		const auto& drawing = *::ImGui::GetWindowDrawList();
		bool touchesActualAnchor = false;
		for (int index = firstVertex; index < drawing.VtxBuffer.Size; ++index)
		{
			const auto position = drawing.VtxBuffer[index].pos;
			EXPECT_LT(position.x, 450.0f);
			touchesActualAnchor |= std::abs(position.x - 400.0f) < 3.0f && std::abs(position.y - 300.0f) < 3.0f;
		}
		EXPECT_TRUE(touchesActualAnchor);
	}

	TEST(RendererStylingTest, OpenFramesAndUncertainIdentitiesRetainFillAndContrast)
	{
		ImGuiFrame frame;
		Renderer renderer;
		for (auto set : {Api::SymbolSet::Air, Api::SymbolSet::SeaSubsurface, Api::SymbolSet::SeaSurface})
		{
			for (auto identity : {Api::StandardIdentity::Friendly,
								  Api::StandardIdentity::Hostile,
								  Api::StandardIdentity::Unknown,
								  Api::StandardIdentity::Neutral,
								  Api::StandardIdentity::AssumedFriendly,
								  Api::StandardIdentity::Suspect,
								  Api::StandardIdentity::Pending})
			{
				auto context = MakeContext();
				context.ShowLabels = false;
				auto symbol = set == Api::SymbolSet::Air			 ? MakeSymbol(set, identity, 11, 1)
							  : set == Api::SymbolSet::SeaSubsurface ? MakeSymbol(set, identity, 11, 1)
																	 : MakeSymbol(set, identity, 12, 2, 4);
				const int firstVertex = ::ImGui::GetWindowDrawList()->VtxBuffer.Size;
				const auto result = renderer.Render(symbol, context);
				ASSERT_TRUE(result.WasDrawn);
				const auto fill = context.Style.Palette.Resolve(identity);
				EXPECT_TRUE(HasVertexColorFrom(*::ImGui::GetWindowDrawList(), firstVertex, IM_COL32(fill.Red, fill.Green, fill.Blue, 255)));
				EXPECT_TRUE(HasVertexColorFrom(*::ImGui::GetWindowDrawList(), firstVertex, IM_COL32(0, 0, 0, 255)));
			}
		}
	}

	TEST(RendererStylingTest, UnfilledHasNoSolidFillAndHonorsCustomApiPalette)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto context = MakeContext();
		context.ShowLabels = false;
		auto symbol = MakeSymbol(Api::SymbolSet::SeaSurface);
		context.Style.Palette.Friendly = {12, 130, 240};
		const int filledStart = ::ImGui::GetWindowDrawList()->IdxBuffer.Size;
		renderer.Render(symbol, context);
		const int filledCount = ::ImGui::GetWindowDrawList()->IdxBuffer.Size - filledStart;
		context.Style.Preset = Api::StylePreset::UnfilledDark;
		const int unfilledStart = ::ImGui::GetWindowDrawList()->IdxBuffer.Size;
		const int firstVertex = ::ImGui::GetWindowDrawList()->VtxBuffer.Size;
		renderer.Render(symbol, context);
		EXPECT_LT(::ImGui::GetWindowDrawList()->IdxBuffer.Size - unfilledStart, filledCount);
		EXPECT_TRUE(HasVertexColorFrom(*::ImGui::GetWindowDrawList(), firstVertex, IM_COL32(12, 130, 240, 255)));
		EXPECT_FALSE(HasVertexColorFrom(*::ImGui::GetWindowDrawList(), firstVertex, IM_COL32(0, 0, 0, 255)));
	}

	TEST(RendererMeasurementTest, HeadquartersDirectionArrowStartsAtGeographicAnchor)
	{
		ImGuiFrame frame;
		Renderer renderer;
		Api::SidcFields fields;
		fields.Identity = Api::StandardIdentity::Friendly;
		fields.SymbolSetValue = Api::SymbolSet::LandUnit;
		fields.Headquarters = Api::HeadquartersTaskForceDummy::Headquarters;
		Api::Symbol symbol{*Api::Sidc::Create(fields), {}, std::nullopt};
		symbol.ExternalAmplifiers.CourseOverGroundRadians = 0.0;
		auto context = MakeContext();
		context.ShowLabels = false;
		const auto baseline = renderer.Measure(symbol, context);
		context.VisualAnchor = {600, 400};
		const auto displaced = renderer.Measure(symbol, context);
		EXPECT_EQ(displaced.GeographicBounds.Minimum.X, baseline.GeographicBounds.Minimum.X);
		EXPECT_EQ(displaced.GeographicBounds.Maximum.Y, baseline.GeographicBounds.Maximum.Y);
		EXPECT_TRUE(displaced.GeographicBounds.Contains(context.GeographicAnchor));
		EXPECT_FALSE(displaced.GeographicBounds.Contains(context.VisualAnchor));
	}

	TEST(RendererStylingTest, PlannedAndAssumedFramesPreserveTheirPatterns)
	{
		ImGuiFrame frame;
		Renderer renderer;
		auto context = MakeContext();
		context.ShowLabels = false;
		context.Style.HaloWidth = 0;
		const auto drawCount = [&](Api::StandardIdentity identity, Api::SymbolStatus status)
		{
			Api::SidcFields fields;
			fields.Identity = identity;
			fields.SymbolSetValue = Api::SymbolSet::SeaSurface;
			fields.Status = status;
			const int first = ::ImGui::GetWindowDrawList()->VtxBuffer.Size;
			renderer.Render({*Api::Sidc::Create(fields), {}, std::nullopt}, context);
			return ::ImGui::GetWindowDrawList()->VtxBuffer.Size - first;
		};
		const int solid = drawCount(Api::StandardIdentity::Friendly, Api::SymbolStatus::Present);
		const int planned = drawCount(Api::StandardIdentity::Friendly, Api::SymbolStatus::PlannedAnticipatedSuspect);
		EXPECT_NE(solid, planned);
		const int start = ::ImGui::GetWindowDrawList()->VtxBuffer.Size;
		drawCount(Api::StandardIdentity::AssumedFriendly, Api::SymbolStatus::Present);
		EXPECT_TRUE(HasVertexColorFrom(*::ImGui::GetWindowDrawList(), start, IM_COL32(0, 0, 0, 255)));
		EXPECT_TRUE(HasVertexColorFrom(*::ImGui::GetWindowDrawList(), start, IM_COL32(255, 255, 255, 255)));
	}

}
