#include <NavalCommand/Symbology/Nato/Api/Amplifiers.h>
#include <NavalCommand/Symbology/Nato/Api/Context.h>
#include <NavalCommand/Symbology/Nato/Api/RenderContext.h>
#include <NavalCommand/Symbology/Nato/Api/Sidc.h>
#include <NavalCommand/Symbology/Nato/Api/Symbol.h>
#include <NavalCommand/Symbology/Nato/ImGui/Renderer.h>
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

			Api::ScreenVector ProjectLocalOffsetMeters(
				Api::GeographicCoordinate,
				double eastMeters,
				double northMeters) const noexcept override
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

		Api::Symbol MakeSymbol(
			Api::SymbolSet symbolSet,
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
			return {{400.0, 300.0}, {{100.0, 80.0}, {700.0, 520.0}}, projection, 1.0, 0.0, 60.0};
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
		EXPECT_LT(moving.Bounds.Minimum.Y, unknownCourse.Bounds.Minimum.Y);
		EXPECT_LT(unknownVertexCount, movingVertexCount);
	}

	TEST(RendererTest, RendersProjectedUncertaintyAndRejectsUnimplementedStandardEntity)
	{
		ImGuiFrame frame;
		Renderer renderer;
		TestProjection projection;
		auto uncertain = MakeSymbol(Api::SymbolSet::SeaSubsurface, Api::StandardIdentity::Unknown, 11, 1);
		uncertain.ExternalAmplifiers.Position = Api::PositionAmplifier{
			{{48.0}, {5.0}}, Api::GeographicUncertaintyEllipse{200.0, 100.0, 0.5}};
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
		EXPECT_GT(headingOnly.Bounds.Maximum.X, baseline.Bounds.Maximum.X);
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
		const std::array entityCases{
			EntityCase{Api::SymbolSet::Air, 11, 1, 0, 0},
			EntityCase{Api::SymbolSet::Air, 11, 2, 0, 0},
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
			const auto symbol = MakeSymbol(
				entityCase.Set,
				Api::StandardIdentity::Friendly,
				entityCase.Entity,
				entityCase.EntityType,
				entityCase.EntitySubtype,
				entityCase.Modifier1);
			const auto result = renderer.Render(symbol, MakeContext());
			ASSERT_TRUE(result.WasDrawn)
				<< "set=" << static_cast<int>(entityCase.Set)
				<< " entity=" << static_cast<int>(entityCase.Entity)
				<< " type=" << static_cast<int>(entityCase.EntityType)
				<< " subtype=" << static_cast<int>(entityCase.EntitySubtype)
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
		expectsAffiliationColor(Api::StandardIdentity::Unknown, IM_COL32(255, 255, 0, 255));
		expectsAffiliationColor(Api::StandardIdentity::Friendly, IM_COL32(0, 255, 255, 255));
		expectsAffiliationColor(Api::StandardIdentity::Neutral, IM_COL32(0, 255, 0, 255));
		expectsAffiliationColor(Api::StandardIdentity::Hostile, IM_COL32(255, 0, 0, 255));

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
		EXPECT_GT(altitudeResult.Bounds.Maximum.X, airBaseline.Bounds.Maximum.X);

		const auto seaSurface = MakeSymbol(Api::SymbolSet::SeaSurface);
		const auto seaSurfaceBaseline = renderer.Render(seaSurface, MakeContext());
		auto surfaceWithDepth = seaSurface;
		surfaceWithDepth.ExternalAmplifiers.VerticalPosition = Api::VerticalPositionAmplifier{20.0, Api::VerticalReference::Depth};
		const auto ignoredDepth = renderer.Render(surfaceWithDepth, MakeContext());
		ASSERT_TRUE(seaSurfaceBaseline.WasDrawn);
		ASSERT_TRUE(ignoredDepth.WasDrawn);
		EXPECT_EQ(ignoredDepth.Bounds.Maximum.X, seaSurfaceBaseline.Bounds.Maximum.X);

		auto submarine = MakeSymbol(Api::SymbolSet::SeaSubsurface, Api::StandardIdentity::Friendly, 11, 1);
		submarine.ExternalAmplifiers.VerticalPosition = Api::VerticalPositionAmplifier{200.0, Api::VerticalReference::Depth};
		const auto depthResult = renderer.Render(submarine, MakeContext());
		ASSERT_TRUE(depthResult.WasDrawn);
		EXPECT_GT(depthResult.Bounds.Maximum.X, seaSurfaceBaseline.Bounds.Maximum.X);
	}
}
