#include <NavalCommand/Symbology/Nato/Api/Amplifiers.h>
#include <NavalCommand/Symbology/Nato/Api/Context.h>
#include <NavalCommand/Symbology/Nato/Api/HeadquartersTaskForceDummy.h>
#include <NavalCommand/Symbology/Nato/Api/ScreenGeometry.h>
#include <NavalCommand/Symbology/Nato/Api/StandardIdentity.h>
#include <NavalCommand/Symbology/Nato/Api/SymbolSet.h>
#include <NavalCommand/Symbology/Nato/Api/SymbolStatus.h>
#include <NavalCommand/Symbology/Nato/ImGui/Renderer.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <imgui.h>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NavalCommand::Symbology::Nato::ImGui
{
	namespace
	{
		constexpr float BASE_FRAME_HALF_WIDTH = 19.0f;
		constexpr float BASE_FRAME_HALF_HEIGHT = 16.0f;
		constexpr float BASE_LINE_THICKNESS = 1.7f;
		constexpr float STATUS_BAR_WIDTH = 34.0f;
		constexpr float STATUS_BAR_HEIGHT = 6.0f;
		constexpr float UNCERTAINTY_LINE_THICKNESS = 1.25f;
		constexpr int UNCERTAINTY_SEGMENTS = 48;
		constexpr ImU32 BLACK = IM_COL32(0, 0, 0, 255);
		constexpr ImU32 WHITE = IM_COL32(255, 255, 255, 255);
		constexpr ImU32 NEUTRAL_GREEN = IM_COL32(0, 255, 0, 255);
		constexpr ImU32 DESTROYED_RED = IM_COL32(255, 0, 0, 255);
		constexpr ImU32 FULL_TO_CAPACITY_BLUE = IM_COL32(0, 180, 240, 255);

		enum class FrameDimension
		{
			Unknown,
			AirOrSpace,
			Land,
			Equipment,
			SeaSurface,
			SeaSubsurface
		};

		enum class IdentityGroup
		{
			Unknown,
			Friend,
			Neutral,
			Hostile
		};

		// Measurement traverses the same paths and font metrics with no ImDrawList.
		class DrawingSink
		{
			ImDrawList* m_Drawing{};

		public:
			bool Enabled{true};
			float ThicknessFactor{1.0f};
			float HaloWidth{};
			std::optional<ImU32> OverrideColor;

			explicit DrawingSink(ImDrawList* drawing) : m_Drawing{drawing}
			{
			}

			ImU32 Ink(ImU32 color) const
			{
				return OverrideColor.value_or(color);
			}
			float Stroke(float thickness) const
			{
				return thickness * ThicknessFactor + HaloWidth * 2.0f;
			}

			void AddLine(ImVec2 start, ImVec2 end, ImU32 color, float thickness)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->AddLine(start, end, Ink(color), Stroke(thickness));
				}
			}

			void AddPolyline(const ImVec2* points, int count, ImU32 color, ImDrawFlags flags, float thickness)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->AddPolyline(points, count, Ink(color), flags, Stroke(thickness));
				}
			}

			void AddConcavePolyFilled(const ImVec2* points, int count, ImU32 color)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->AddConcavePolyFilled(points, count, Ink(color));
				}
			}

			void AddCircle(ImVec2 center, float radius, ImU32 color, int segments, float thickness)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->AddCircle(center, radius, Ink(color), segments, Stroke(thickness));
				}
			}

			void AddRectFilled(ImVec2 minimum, ImVec2 maximum, ImU32 color)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->AddRectFilled(minimum, maximum, Ink(color));
				}
			}

			void AddRect(ImVec2 minimum, ImVec2 maximum, ImU32 color, float rounding, ImDrawFlags flags, float thickness)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->AddRect(minimum, maximum, Ink(color), rounding, flags, Stroke(thickness));
				}
			}

			void PathClear()
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->PathClear();
				}
			}

			void PathLineTo(ImVec2 point)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->PathLineTo(point);
				}
			}

			void PathBezierCubicCurveTo(ImVec2 first, ImVec2 second, ImVec2 end)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->PathBezierCubicCurveTo(first, second, end);
				}
			}

			void PathFillConcave(ImU32 color)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->PathFillConcave(Ink(color));
				}
			}

			void PathStroke(ImU32 color, ImDrawFlags flags = ImDrawFlags_None, float thickness = 1.0f)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->PathStroke(Ink(color), flags, Stroke(thickness));
				}
			}

			void PushClipRect(ImVec2 minimum, ImVec2 maximum, bool intersect)
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->PushClipRect(minimum, maximum, intersect);
				}
			}

			void PopClipRect()
			{
				if (m_Drawing && Enabled)
				{
					m_Drawing->PopClipRect();
				}
			}

			void AddText(ImFont* font, float size, ImVec2 position, ImU32 color, const char* text, const char* end = nullptr)
			{
				if (!m_Drawing || !Enabled)
				{
					return;
				}
				if (HaloWidth > 0.0f)
				{
					for (const ImVec2 offset : {ImVec2{-HaloWidth, 0}, ImVec2{HaloWidth, 0}, ImVec2{0, -HaloWidth}, ImVec2{0, HaloWidth}})
					{
						m_Drawing->AddText(font, size, {position.x + offset.x, position.y + offset.y}, Ink(color), text, end);
					}
				}
				m_Drawing->AddText(font, size, position, Ink(color), text, end);
			}
		};

		ImU32 ToImGui(Api::Color color)
		{
			return IM_COL32(color.Red, color.Green, color.Blue, color.Alpha);
		}

		struct BoundsBuilder
		{
			float MinimumX{std::numeric_limits<float>::max()};
			float MinimumY{std::numeric_limits<float>::max()};
			float MaximumX{std::numeric_limits<float>::lowest()};
			float MaximumY{std::numeric_limits<float>::lowest()};
			bool HasPoints{};

			void Add(ImVec2 point, float padding = 0.0f)
			{
				MinimumX = std::min(MinimumX, point.x - padding);
				MinimumY = std::min(MinimumY, point.y - padding);
				MaximumX = std::max(MaximumX, point.x + padding);
				MaximumY = std::max(MaximumY, point.y + padding);
				HasPoints = true;
			}

			void AddRectangle(ImVec2 minimum, ImVec2 maximum)
			{
				Add(minimum);
				Add(maximum);
			}

			Api::ScreenRectangle ToScreenRectangle() const
			{
				return {{MinimumX, MinimumY}, {MaximumX, MaximumY}};
			}
		};

		ImVec2 Add(ImVec2 left, ImVec2 right)
		{
			return {left.x + right.x, left.y + right.y};
		}

		ImVec2 Scale(ImVec2 vector, float factor)
		{
			return {vector.x * factor, vector.y * factor};
		}

		float Distance(ImVec2 first, ImVec2 second)
		{
			const float horizontal = second.x - first.x;
			const float vertical = second.y - first.y;
			return std::sqrt(horizontal * horizontal + vertical * vertical);
		}

		std::optional<FrameDimension> GetFrameDimension(Api::SymbolSet symbolSet)
		{
			switch (symbolSet)
			{
			case Api::SymbolSet::Unknown:
				return FrameDimension::Unknown;
			case Api::SymbolSet::Air:
			case Api::SymbolSet::AirMissile:
			case Api::SymbolSet::Space:
			case Api::SymbolSet::SpaceMissile:
				return FrameDimension::AirOrSpace;
			case Api::SymbolSet::LandUnit:
			case Api::SymbolSet::LandCivilianUnit:
			case Api::SymbolSet::LandInstallation:
			case Api::SymbolSet::DismountedIndividual:
				return FrameDimension::Land;
			case Api::SymbolSet::LandEquipment:
				return FrameDimension::Equipment;
			case Api::SymbolSet::SeaSurface:
				return FrameDimension::SeaSurface;
			case Api::SymbolSet::SeaSubsurface:
			case Api::SymbolSet::MineWarfare:
				return FrameDimension::SeaSubsurface;
			default:
				return std::nullopt;
			}
		}

		IdentityGroup GetIdentityGroup(Api::StandardIdentity identity)
		{
			switch (identity)
			{
			case Api::StandardIdentity::Pending:
			case Api::StandardIdentity::Unknown:
				return IdentityGroup::Unknown;
			case Api::StandardIdentity::AssumedFriendly:
			case Api::StandardIdentity::Friendly:
				return IdentityGroup::Friend;
			case Api::StandardIdentity::Neutral:
				return IdentityGroup::Neutral;
			case Api::StandardIdentity::Suspect:
			case Api::StandardIdentity::Hostile:
				return IdentityGroup::Hostile;
			}
			return IdentityGroup::Unknown;
		}

		bool IsUncertainIdentity(Api::StandardIdentity identity)
		{
			return identity == Api::StandardIdentity::Pending || identity == Api::StandardIdentity::AssumedFriendly || identity == Api::StandardIdentity::Suspect;
		}

		bool IsHeadquarters(Api::HeadquartersTaskForceDummy indicator)
		{
			return indicator == Api::HeadquartersTaskForceDummy::Headquarters || indicator == Api::HeadquartersTaskForceDummy::FeintOrDummyHeadquarters ||
				   indicator == Api::HeadquartersTaskForceDummy::TaskForceHeadquarters || indicator == Api::HeadquartersTaskForceDummy::FeintOrDummyTaskForceHeadquarters;
		}

		bool IsTaskForce(Api::HeadquartersTaskForceDummy indicator)
		{
			return indicator == Api::HeadquartersTaskForceDummy::TaskForce || indicator == Api::HeadquartersTaskForceDummy::FeintOrDummyTaskForce ||
				   indicator == Api::HeadquartersTaskForceDummy::TaskForceHeadquarters || indicator == Api::HeadquartersTaskForceDummy::FeintOrDummyTaskForceHeadquarters;
		}

		bool IsFeintOrDummy(Api::HeadquartersTaskForceDummy indicator)
		{
			return indicator == Api::HeadquartersTaskForceDummy::FeintOrDummy || indicator == Api::HeadquartersTaskForceDummy::FeintOrDummyHeadquarters ||
				   indicator == Api::HeadquartersTaskForceDummy::FeintOrDummyTaskForce || indicator == Api::HeadquartersTaskForceDummy::FeintOrDummyTaskForceHeadquarters;
		}

		std::vector<ImVec2> SampleCubic(ImVec2 start, ImVec2 firstControl, ImVec2 secondControl, ImVec2 end, int segments)
		{
			std::vector<ImVec2> points;
			points.reserve(static_cast<std::size_t>(segments));
			for (int segment = 1; segment <= segments; ++segment)
			{
				const float parameter = static_cast<float>(segment) / static_cast<float>(segments);
				const float inverse = 1.0f - parameter;
				const float startWeight = inverse * inverse * inverse;
				const float firstWeight = 3.0f * inverse * inverse * parameter;
				const float secondWeight = 3.0f * inverse * parameter * parameter;
				const float endWeight = parameter * parameter * parameter;
				points.push_back({start.x * startWeight + firstControl.x * firstWeight + secondControl.x * secondWeight + end.x * endWeight,
								  start.y * startWeight + firstControl.y * firstWeight + secondControl.y * secondWeight + end.y * endWeight});
			}
			return points;
		}

		void AppendCubic(std::vector<ImVec2>& points, ImVec2 start, ImVec2 firstControl, ImVec2 secondControl, ImVec2 end, int segments)
		{
			auto sampled = SampleCubic(start, firstControl, secondControl, end, segments);
			points.insert(points.end(), sampled.begin(), sampled.end());
		}

		std::vector<ImVec2> FramePoints(FrameDimension dimension, IdentityGroup group, ImVec2 center, float scale)
		{
			const float halfWidth = BASE_FRAME_HALF_WIDTH * scale;
			const float halfHeight = dimension == FrameDimension::Land && group == IdentityGroup::Friend ? BASE_FRAME_HALF_HEIGHT * scale : halfWidth;
			const auto point = [&](float horizontal, float vertical)
			{
				return ImVec2{center.x + horizontal * halfWidth, center.y + vertical * halfHeight};
			};

			if ((dimension == FrameDimension::Equipment || dimension == FrameDimension::SeaSurface) && group == IdentityGroup::Friend)
			{
				std::vector<ImVec2> points;
				points.reserve(33);
				const float radius = std::min(halfWidth, halfHeight) * 0.86f;
				for (int segment = 0; segment < 32; ++segment)
				{
					const float angle = static_cast<float>(segment) * (2.0f * std::numbers::pi_v<float> / 32.0f);
					points.push_back({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
				}
				return points;
			}

			if (group == IdentityGroup::Unknown)
			{
				if (dimension == FrameDimension::AirOrSpace || dimension == FrameDimension::SeaSubsurface)
				{
					// Open domain frames have elongated side lobes for the icon and
					// mission sectors. Subsurface is the vertical mirror of air.
					const auto domainPoint = [&](float horizontal, float vertical)
					{
						return point(horizontal, dimension == FrameDimension::SeaSubsurface ? -vertical : vertical);
					};
					const ImVec2 leftBottom = domainPoint(-0.45f, 0.9f);
					const ImVec2 leftShoulder = domainPoint(-0.45f, -0.6f);
					const ImVec2 rightShoulder = domainPoint(0.45f, -0.6f);
					const ImVec2 rightBottom = domainPoint(0.45f, 0.9f);
					std::vector<ImVec2> points{leftBottom};
					AppendCubic(points, leftBottom, domainPoint(-1.25f, 0.9f), domainPoint(-1.25f, -0.6f), leftShoulder, 16);
					AppendCubic(points, leftShoulder, domainPoint(-0.45f, -1.4f), domainPoint(0.45f, -1.4f), rightShoulder, 16);
					AppendCubic(points, rightShoulder, domainPoint(1.25f, -0.6f), domainPoint(1.25f, 0.9f), rightBottom, 16);
					return points;
				}
				// Closed surface/ground frames have four equal lobes. The closing
				// endpoint must not be repeated: ImGui closes the stroke itself.
				std::vector<ImVec2> points;
				const ImVec2 topLeft = point(-0.4f, -0.4f);
				const ImVec2 topRight = point(0.4f, -0.4f);
				const ImVec2 bottomRight = point(0.4f, 0.4f);
				const ImVec2 bottomLeft = point(-0.4f, 0.4f);
				points.push_back(bottomLeft);
				AppendCubic(points, bottomLeft, point(-1.2f, 0.4f), point(-1.2f, -0.4f), topLeft, 8);
				AppendCubic(points, topLeft, point(-0.4f, -1.2f), point(0.4f, -1.2f), topRight, 8);
				AppendCubic(points, topRight, point(1.2f, -0.4f), point(1.2f, 0.4f), bottomRight, 8);
				AppendCubic(points, bottomRight, point(0.4f, 1.2f), point(-0.4f, 1.2f), bottomLeft, 8);
				points.pop_back();
				return points;
			}

			if (dimension == FrameDimension::AirOrSpace)
			{
				if (group == IdentityGroup::Friend)
				{
					std::vector<ImVec2> points;
					points.reserve(19);
					const ImVec2 leftBottom = point(-1.0f, 0.9f);
					const ImVec2 top = point(0.0f, -1.0f);
					const ImVec2 rightBottom = point(1.0f, 0.9f);
					points.push_back(leftBottom);
					AppendCubic(points, leftBottom, point(-1.0f, -0.52f), point(-0.32f, -1.0f), top, 9);
					AppendCubic(points, top, point(0.32f, -1.0f), point(1.0f, -0.52f), rightBottom, 9);
					return points;
				}
				if (group == IdentityGroup::Neutral)
				{
					return {point(-1.0f, 0.9f), point(-1.0f, -1.0f), point(1.0f, -1.0f), point(1.0f, 0.9f)};
				}
				return {point(-1.0f, 0.0f), point(0.0f, -1.0f), point(1.0f, 0.0f)};
			}

			if (dimension == FrameDimension::SeaSubsurface)
			{
				if (group == IdentityGroup::Friend)
				{
					std::vector<ImVec2> points;
					points.reserve(19);
					const ImVec2 leftTop = point(-1.0f, -0.9f);
					const ImVec2 bottom = point(0.0f, 1.0f);
					const ImVec2 rightTop = point(1.0f, -0.9f);
					points.push_back(leftTop);
					AppendCubic(points, leftTop, point(-1.0f, 0.5f), point(-0.32f, 1.0f), bottom, 9);
					AppendCubic(points, bottom, point(0.32f, 1.0f), point(1.0f, 0.5f), rightTop, 9);
					return points;
				}
				if (group == IdentityGroup::Neutral)
				{
					return {point(-1.0f, -0.9f), point(-1.0f, 1.0f), point(1.0f, 1.0f), point(1.0f, -0.9f)};
				}
				return {point(-1.0f, 0.0f), point(0.0f, 1.0f), point(1.0f, 0.0f)};
			}

			if (group == IdentityGroup::Friend && (dimension == FrameDimension::Land || dimension == FrameDimension::Equipment))
			{
				const float widthScale = dimension == FrameDimension::Equipment ? 0.82f : 1.0f;
				return {point(-widthScale, -0.9f), point(widthScale, -0.9f), point(widthScale, 0.9f), point(-widthScale, 0.9f)};
			}
			if (group == IdentityGroup::Neutral)
			{
				return {point(-0.9f, -0.9f), point(0.9f, -0.9f), point(0.9f, 0.9f), point(-0.9f, 0.9f)};
			}
			if (group == IdentityGroup::Hostile)
			{
				return {point(0.0f, -1.0f), point(1.0f, 0.0f), point(0.0f, 1.0f), point(-1.0f, 0.0f)};
			}
			return {point(-1.0f, -0.9f), point(1.0f, -0.9f), point(1.0f, 0.9f), point(-1.0f, 0.9f)};
		}

		void DrawPatternedPath(DrawingSink& drawing,
							   const std::vector<ImVec2>& points,
							   ImU32 color,
							   float thickness,
							   bool closed,
							   float dashLength,
							   float gapLength,
							   bool uncertainIdentity,
							   BoundsBuilder& bounds)
		{
			if (points.size() < 2)
			{
				return;
			}

			if (dashLength <= 0.0f || gapLength < 0.0f)
			{
				drawing.AddPolyline(points.data(), static_cast<int>(points.size()), color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
				for (const auto point : points)
				{
					bounds.Add(point, thickness);
				}
				return;
			}

			const std::size_t segmentCount = closed ? points.size() : points.size() - 1;
			float patternOffset = 0.0f;
			const float patternLength = dashLength + gapLength;
			for (std::size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
			{
				const ImVec2 segmentStart = points[segmentIndex];
				const ImVec2 segmentEnd = points[(segmentIndex + 1) % points.size()];
				const float segmentLength = Distance(segmentStart, segmentEnd);
				if (segmentLength <= 0.0f)
				{
					continue;
				}

				float segmentOffset = 0.0f;
				while (segmentOffset < segmentLength)
				{
					const float cycleOffset = std::fmod(patternOffset + segmentOffset, patternLength);
					const bool onDash = cycleOffset < dashLength;
					const float runLength = onDash ? dashLength - cycleOffset : patternLength - cycleOffset;
					const float nextOffset = std::min(segmentLength, segmentOffset + std::max(runLength, 0.1f));
					if (onDash)
					{
						const float startFraction = segmentOffset / segmentLength;
						const float endFraction = nextOffset / segmentLength;
						const ImVec2 startPoint{segmentStart.x + (segmentEnd.x - segmentStart.x) * startFraction, segmentStart.y + (segmentEnd.y - segmentStart.y) * startFraction};
						const ImVec2 endPoint{segmentStart.x + (segmentEnd.x - segmentStart.x) * endFraction, segmentStart.y + (segmentEnd.y - segmentStart.y) * endFraction};
						const ImU32 dashColor = uncertainIdentity && (static_cast<int>((patternOffset + segmentOffset) / patternLength) % 2 == 0) ? BLACK
												: uncertainIdentity																				  ? WHITE
																																				  : color;
						drawing.AddLine(startPoint, endPoint, dashColor, thickness);
						bounds.Add(startPoint, thickness);
						bounds.Add(endPoint, thickness);
					}
					segmentOffset = nextOffset;
				}
				patternOffset += segmentLength;
			}
		}

		std::string_view AircraftRoleLabel(std::uint8_t modifier)
		{
			switch (modifier)
			{
			case 1:
				return "A";
			case 2:
				return "ASW";
			case 3:
				return "C";
			case 4:
				return "F";
			case 5:
				return "K";
			case 12:
				return "AEW";
			case 16:
				return "J";
			case 17:
				return "P";
			case 18:
				return "R";
			default:
				return {};
			}
		}

		bool HasGuidedMissileRole(const Api::SidcFields& fields)
		{
			return fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.ThirdTen[0] == '1' && fields.Modifier1 == 33;
		}

		bool IsSupportedEntity(const Api::SidcFields& fields)
		{
			if (fields.ThirdTen[1] != '0' || (fields.ThirdTen[0] != '0' && !HasGuidedMissileRole(fields)))
			{
				return false;
			}
			if (fields.Modifier2 != 0 && !(fields.SymbolSetValue == Api::SymbolSet::AirMissile && fields.Modifier1 == 1 && fields.Modifier2 == 1))
			{
				return false;
			}
			switch (fields.SymbolSetValue)
			{
			case Api::SymbolSet::Unknown:
			case Api::SymbolSet::Space:
			case Api::SymbolSet::LandUnit:
			case Api::SymbolSet::LandCivilianUnit:
				return fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0;
			case Api::SymbolSet::MineWarfare:
				return (fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   (fields.Entity == 11 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0);
			case Api::SymbolSet::LandInstallation:
				return (fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   (fields.Entity == 11 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0);
			case Api::SymbolSet::Air:
				return (fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   (fields.Entity == 11 && (fields.EntityType == 1 || fields.EntityType == 2) && fields.EntitySubtype == 0 &&
						(fields.Modifier1 == 0 || !AircraftRoleLabel(fields.Modifier1).empty()));
			case Api::SymbolSet::AirMissile:
				return fields.Entity == 11 && fields.EntityType == 0 && fields.EntitySubtype == 0 &&
					   (fields.Modifier1 == 0 || fields.Modifier1 == 1 || fields.Modifier1 == 6 || fields.Modifier1 == 7);
			case Api::SymbolSet::SpaceMissile:
				return fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0;
			case Api::SymbolSet::LandEquipment:
				return (fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   (fields.Entity == 11 && (fields.EntityType == 5 || fields.EntityType == 11) && fields.EntitySubtype == 0 && fields.Modifier1 == 0);
			case Api::SymbolSet::SeaSurface:
				return (fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   (fields.Entity == 12 && fields.EntityType == 1 && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   (fields.Entity == 12 && fields.EntityType == 2 &&
						(fields.EntitySubtype == 0 || fields.EntitySubtype == 2 || fields.EntitySubtype == 3 || fields.EntitySubtype == 4 || fields.EntitySubtype == 5) &&
						(fields.Modifier1 == 0 || (fields.EntitySubtype == 2 && HasGuidedMissileRole(fields)))) ||
					   (fields.Entity == 12 && (fields.EntityType == 3 || fields.EntityType == 5) && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   ((fields.Entity == 13 || fields.Entity == 14) && fields.EntityType == 1 && fields.EntitySubtype == 0 && fields.Modifier1 == 0);
			case Api::SymbolSet::SeaSubsurface:
				return (fields.Entity == 0 && fields.EntityType == 0 && fields.EntitySubtype == 0 && fields.Modifier1 == 0) ||
					   (fields.Entity == 11 && fields.EntityType == 1 && fields.EntitySubtype == 0 &&
						(fields.Modifier1 == 0 || fields.Modifier1 == 8 || fields.Modifier1 == 9 || fields.Modifier1 == 10)) ||
					   (fields.Entity == 13 && (fields.EntityType == 1 || fields.EntityType == 3) && fields.EntitySubtype == 0 && fields.Modifier1 == 0);
			default:
				return false;
			}
		}

		ImVec2 StandardPoint(ImVec2 center, float scale, float x, float y)
		{
			return {center.x + (x - 100.0f) * BASE_FRAME_HALF_HEIGHT * scale / 100.0f, center.y + (y - 100.0f) * BASE_FRAME_HALF_HEIGHT * scale / 100.0f};
		}

		void DrawStandardPath(DrawingSink& drawing, std::initializer_list<ImVec2> points, ImU32 color, float thickness, bool closed, bool filled, BoundsBuilder& bounds)
		{
			if (points.size() < 2)
			{
				return;
			}
			if (filled && closed && points.size() >= 3)
			{
				drawing.AddConcavePolyFilled(points.begin(), static_cast<int>(points.size()), color);
			}
			drawing.AddPolyline(points.begin(), static_cast<int>(points.size()), color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
			for (const ImVec2 point : points)
			{
				bounds.Add(point, thickness);
			}
		}

		void DrawCenteredEntityText(DrawingSink& drawing, std::string_view text, ImVec2 center, float scale, ImU32 color, BoundsBuilder& bounds)
		{
			const float fontSize = std::max(14.0f, ::ImGui::GetFontSize()) * scale;
			const ImVec2 textSize = ::ImGui::GetFont()->CalcTextSizeA(fontSize, std::numeric_limits<float>::max(), 0.0f, text.data(), text.data() + text.size());
			// Center the visible glyph ink, rather than its advance widths and side bearings.
			ImFontBaked* font = ::ImGui::GetFont()->GetFontBaked(fontSize);
			float advance = 0.0f;
			float inkLeft = std::numeric_limits<float>::max();
			float inkRight = std::numeric_limits<float>::lowest();
			for (const unsigned char character : text)
			{
				if (const ImFontGlyph* glyph = font->FindGlyph(character))
				{
					inkLeft = std::min(inkLeft, advance + glyph->X0);
					inkRight = std::max(inkRight, advance + glyph->X1);
					advance += glyph->AdvanceX;
				}
			}
			const float inkCenter = inkLeft <= inkRight ? (inkLeft + inkRight) * 0.5f : textSize.x * 0.5f;
			const ImVec2 position{center.x - inkCenter, center.y - textSize.y * 0.5f};
			drawing.AddText(nullptr, fontSize, position, color, text.data(), text.data() + text.size());
			bounds.AddRectangle(position, {position.x + textSize.x, position.y + textSize.y});
		}

		void DrawEntityIcon(DrawingSink& drawing, const Api::SidcFields& fields, ImVec2 center, float scale, ImU32 color, BoundsBuilder& bounds)
		{
			const float thickness = BASE_LINE_THICKNESS * scale;
			const bool aircraft = fields.SymbolSetValue == Api::SymbolSet::Air && fields.Entity == 11;
			const ImVec2 iconCenter{center.x, center.y + (aircraft && fields.Modifier1 != 0 ? 4.5f * scale : 0.0f)};
			const auto point = [&](float x, float y)
			{
				const bool capitalSurfaceIcon =
					fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.Entity == 12 && (fields.EntityType == 1 || (fields.EntityType == 2 && fields.EntitySubtype == 0));
				const bool submarineIcon = fields.SymbolSetValue == Api::SymbolSet::SeaSubsurface && fields.Entity == 11;
				const float geometryScale = aircraft ? 2.0f : capitalSurfaceIcon ? 2.8f : submarineIcon ? 1.8f : 1.25f;
				return StandardPoint(iconCenter, scale * geometryScale, x, y);
			};

			if (fields.SymbolSetValue == Api::SymbolSet::Air && fields.Entity == 11 && fields.EntityType == 1)
			{
				const auto buildPath = [&]()
				{
					drawing.PathClear();
					drawing.PathLineTo(point(100, 100));
					drawing.PathLineTo(point(130, 88));
					drawing.PathBezierCubicCurveTo(point(145, 88), point(145, 112), point(130, 112));
					drawing.PathLineTo(point(100, 100));
					drawing.PathLineTo(point(70, 112));
					drawing.PathBezierCubicCurveTo(point(55, 112), point(55, 88), point(70, 88));
				};
				buildPath();
				drawing.PathFillConcave(color);
				buildPath();
				drawing.PathStroke(color, ImDrawFlags_Closed, thickness);
				if (const auto label = AircraftRoleLabel(fields.Modifier1); !label.empty())
				{
					DrawCenteredEntityText(drawing, label, {center.x, center.y - 7.0f * scale}, scale * 0.8f, color, bounds);
				}
				bounds.Add(point(55, 88), thickness);
				bounds.Add(point(145, 112), thickness);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::Air && fields.Entity == 11 && fields.EntityType == 2)
			{
				// Triangulate the two lobes independently: the combined outline
				// touches itself at the rotor hub and is not a simple polygon.
				DrawStandardPath(drawing, {point(60, 85), point(100, 100), point(60, 115)}, color, thickness, true, true, bounds);
				DrawStandardPath(drawing, {point(140, 85), point(140, 115), point(100, 100)}, color, thickness, true, true, bounds);
				if (const auto label = AircraftRoleLabel(fields.Modifier1); !label.empty())
				{
					DrawCenteredEntityText(drawing, label, {center.x, center.y - 7.0f * scale}, scale * 0.8f, color, bounds);
				}
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::AirMissile && fields.Entity == 11)
			{
				DrawStandardPath(drawing,
								 {point(87, 135),
								  point(87, 124),
								  point(93, 119),
								  point(93, 65),
								  point(100, 55),
								  point(107, 65),
								  point(107, 119),
								  point(113, 124),
								  point(113, 135),
								  point(100, 125)},
								 color,
								 thickness,
								 true,
								 true,
								 bounds);
				if (fields.Modifier1 == 6 || fields.Modifier1 == 7)
				{
					DrawCenteredEntityText(drawing, fields.Modifier1 == 6 ? "B" : "C", point(68, 100), scale, color, bounds);
				}
				if (fields.Modifier1 == 1)
				{
					DrawCenteredEntityText(drawing, "A", point(65, 75), scale, color, bounds);
				}
				if (fields.Modifier2 == 1)
				{
					DrawCenteredEntityText(drawing, "A", point(65, 125), scale, color, bounds);
				}
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.Entity == 12 && fields.EntityType == 1)
			{
				DrawStandardPath(
					drawing, {point(80, 95), point(100, 120), point(120, 95), point(94, 95), point(94, 85), point(84, 85), point(84, 95)}, color, thickness, true, true, bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.Entity == 12 && fields.EntityType == 2)
			{
				if (fields.EntitySubtype == 0)
				{
					DrawStandardPath(drawing,
									 {point(100, 120),
									  point(75, 103),
									  point(90, 105),
									  point(90, 95),
									  point(95, 95),
									  point(95, 90),
									  point(80, 90),
									  point(80, 85),
									  point(95, 85),
									  point(95, 80),
									  point(105, 80),
									  point(105, 85),
									  point(120, 85),
									  point(120, 90),
									  point(105, 90),
									  point(105, 95),
									  point(110, 95),
									  point(110, 105),
									  point(125, 103)},
									 color,
									 thickness,
									 true,
									 true,
									 bounds);
				}
				else
				{
					const std::string_view label = fields.EntitySubtype == 2   ? (HasGuidedMissileRole(fields) ? "CG" : "CC")
												   : fields.EntitySubtype == 3 ? "DD"
												   : fields.EntitySubtype == 4 ? "FF"
																			   : "FS";
					DrawCenteredEntityText(drawing, label, center, scale, color, bounds);
				}
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.Entity == 12 && fields.EntityType == 3)
			{
				DrawStandardPath(drawing,
								 {point(100, 120), point(80, 100), point(90, 100), point(90, 80), point(110, 80), point(110, 100), point(120, 100)},
								 color,
								 thickness,
								 true,
								 true,
								 bounds);
				DrawStandardPath(drawing, {point(100, 120), point(120, 120)}, color, thickness, false, false, bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.Entity == 12 && fields.EntityType == 5)
			{
				DrawStandardPath(drawing,
								 {point(80, 100), point(100, 120), point(120, 100), point(110, 100), point(110, 80), point(90, 80), point(90, 100)},
								 color,
								 thickness,
								 true,
								 true,
								 bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.Entity == 13 && fields.EntityType == 1)
			{
				DrawCenteredEntityText(drawing, "AA", center, scale, color, bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSurface && fields.Entity == 14 && fields.EntityType == 1)
			{
				DrawStandardPath(drawing,
								 {point(75, 100), point(75, 65), point(125, 65), point(125, 100), point(145, 100), point(130, 135), point(70, 135), point(55, 100)},
								 color,
								 thickness,
								 true,
								 false,
								 bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSubsurface && fields.Entity == 11 && fields.EntityType == 1)
			{
				DrawStandardPath(drawing, {point(75, 85), point(125, 85), point(140, 100), point(125, 115), point(75, 115), point(60, 100)}, color, thickness, true, true, bounds);
				if (fields.Modifier1 == 8 || fields.Modifier1 == 9 || fields.Modifier1 == 10)
				{
					const std::string_view mission = fields.Modifier1 == 8 ? "A" : fields.Modifier1 == 9 ? "B" : "G";
					const float textScale = scale * 0.8f;
					const float textHeight =
						::ImGui::GetFont()
							->CalcTextSizeA(
								std::max(14.0f, ::ImGui::GetFontSize()) * textScale, std::numeric_limits<float>::max(), 0.0f, mission.data(), mission.data() + mission.size())
							.y;
					const ImVec2 modifierCenter{center.x, point(100, 85).y - 2.0f * scale - textHeight * 0.5f};
					DrawCenteredEntityText(drawing, mission, modifierCenter, textScale, color, bounds);
				}
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSubsurface && fields.Entity == 13 && fields.EntityType == 1)
			{
				DrawStandardPath(
					drawing,
					{point(50, 110), point(45, 100), point(50, 90), point(130, 90), point(135, 100), point(150, 88), point(150, 112), point(135, 100), point(130, 110)},
					color,
					thickness,
					true,
					true,
					bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::SeaSubsurface && fields.Entity == 13 && fields.EntityType == 3)
			{
				DrawStandardPath(drawing, {point(105, 120), point(90, 105), point(105, 90)}, color, thickness, true, true, bounds);
				DrawStandardPath(drawing, {point(85, 120), point(70, 105), point(85, 90)}, color, thickness, true, true, bounds);
				DrawStandardPath(drawing, {point(125, 90), point(110, 105), point(125, 120)}, color, thickness, true, true, bounds);
				DrawStandardPath(drawing, {point(70, 75), point(70, 70), point(125, 70), point(125, 75)}, color, thickness, true, false, bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::MineWarfare && fields.Entity == 11)
			{
				const float mineRadius = 5.0f * scale;
				drawing.AddCircle(center, mineRadius, color, 32, thickness);
				bounds.Add(center, mineRadius + thickness);
				DrawStandardPath(drawing, {point(92, 70), point(92, 55), point(108, 55), point(108, 70)}, color, thickness, false, false, bounds);
				DrawStandardPath(drawing, {point(84, 73), point(74, 62), point(63, 73), point(74, 84)}, color, thickness, false, false, bounds);
				DrawStandardPath(drawing, {point(116, 73), point(126, 62), point(137, 73), point(126, 84)}, color, thickness, false, false, bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::LandEquipment && fields.Entity == 11 && fields.EntityType == 5)
			{
				drawing.PathClear();
				drawing.PathLineTo(point(85, 140));
				drawing.PathLineTo(point(115, 140));
				drawing.PathBezierCubicCurveTo(point(115, 120), point(85, 120), point(85, 140));
				drawing.PathStroke(color, ImDrawFlags_Closed, thickness);
				bounds.Add(point(85, 120), thickness);
				bounds.Add(point(115, 140), thickness);
				DrawStandardPath(drawing, {point(100, 60), point(100, 125)}, color, thickness, false, false, bounds);
				DrawStandardPath(drawing, {point(85, 95), point(85, 135)}, color, thickness, false, false, bounds);
				DrawStandardPath(drawing, {point(115, 95), point(115, 135)}, color, thickness, false, false, bounds);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::LandEquipment && fields.Entity == 11 && fields.EntityType == 11)
			{
				drawing.PathClear();
				drawing.PathLineTo(point(85, 140));
				drawing.PathLineTo(point(115, 140));
				drawing.PathBezierCubicCurveTo(point(115, 120), point(85, 120), point(85, 140));
				drawing.PathStroke(color, ImDrawFlags_Closed, thickness);
				bounds.Add(point(85, 120), thickness);
				bounds.Add(point(115, 140), thickness);
				DrawStandardPath(drawing, {point(100, 125), point(100, 60)}, color, thickness, false, false, bounds);
				drawing.PathClear();
				drawing.PathLineTo(point(85, 140));
				drawing.PathLineTo(point(85, 75));
				drawing.PathBezierCubicCurveTo(point(85, 55), point(115, 55), point(115, 75));
				drawing.PathLineTo(point(115, 140));
				drawing.PathStroke(color, ImDrawFlags_None, thickness);
				bounds.Add(point(85, 55), thickness);
				bounds.Add(point(115, 140), thickness);
				return;
			}
			if (fields.SymbolSetValue == Api::SymbolSet::LandInstallation && fields.Entity == 11)
			{
				DrawCenteredEntityText(drawing, "MIL", center, scale, color, bounds);
			}
		}

		bool IsSpeedLeaderApplicable(Api::SymbolSet symbolSet)
		{
			return symbolSet == Api::SymbolSet::Air || symbolSet == Api::SymbolSet::AirMissile || symbolSet == Api::SymbolSet::Space || symbolSet == Api::SymbolSet::SeaSurface ||
				   symbolSet == Api::SymbolSet::SeaSubsurface;
		}

		bool IsDirectionArrowApplicable(Api::SymbolSet symbolSet)
		{
			return symbolSet == Api::SymbolSet::LandUnit || symbolSet == Api::SymbolSet::LandEquipment || symbolSet == Api::SymbolSet::LandInstallation ||
				   symbolSet == Api::SymbolSet::ControlMeasure || symbolSet == Api::SymbolSet::DismountedIndividual;
		}

		bool IsSupportedStatus(Api::SymbolStatus status)
		{
			return status == Api::SymbolStatus::Present || status == Api::SymbolStatus::PlannedAnticipatedSuspect || status == Api::SymbolStatus::PresentFullyCapable ||
				   status == Api::SymbolStatus::PresentDamaged || status == Api::SymbolStatus::PresentDestroyed || status == Api::SymbolStatus::PresentFullToCapacity;
		}

		ImU32 OperationalConditionColor(Api::SymbolStatus status)
		{
			switch (status)
			{
			case Api::SymbolStatus::PresentFullyCapable:
				return NEUTRAL_GREEN;
			case Api::SymbolStatus::PresentDamaged:
				return IM_COL32(255, 255, 0, 255);
			case Api::SymbolStatus::PresentDestroyed:
				return DESTROYED_RED;
			case Api::SymbolStatus::PresentFullToCapacity:
				return FULL_TO_CAPACITY_BLUE;
			default:
				return 0;
			}
		}

		void DrawArrowHead(DrawingSink& drawing, ImVec2 tip, ImVec2 direction, ImU32 color, float scale, BoundsBuilder& bounds)
		{
			const float directionLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
			if (directionLength <= 0.0f)
			{
				return;
			}
			const ImVec2 unitDirection{direction.x / directionLength, direction.y / directionLength};
			const ImVec2 perpendicular{-unitDirection.y, unitDirection.x};
			const float arrowLength = 5.0f * scale;
			const float arrowWidth = 2.7f * scale;
			const ImVec2 base = Add(tip, Scale(unitDirection, -arrowLength));
			const ImVec2 left = Add(base, Scale(perpendicular, arrowWidth));
			const ImVec2 right = Add(base, Scale(perpendicular, -arrowWidth));
			drawing.AddLine(tip, left, color, BASE_LINE_THICKNESS * scale);
			drawing.AddLine(tip, right, color, BASE_LINE_THICKNESS * scale);
			bounds.Add(tip, arrowLength);
			bounds.Add(left, arrowLength);
			bounds.Add(right, arrowLength);
		}

		void DrawTextAmplifier(DrawingSink& drawing, std::string_view text, ImVec2 position, float fontSize, ImU32 color, BoundsBuilder& bounds)
		{
			if (text.empty())
			{
				return;
			}

			const auto dimensions = ::ImGui::GetFont()->CalcTextSizeA(fontSize, std::numeric_limits<float>::max(), 0.0f, text.data(), text.data() + text.size());
			drawing.AddText(nullptr, fontSize, position, color, text.data(), text.data() + text.size());
			bounds.AddRectangle(position, {position.x + dimensions.x, position.y + dimensions.y});
		}

		std::string FormatNumber(double value)
		{
			std::array<char, 48> buffer{};
			const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed, 1);
			if (error != std::errc{})
			{
				return {};
			}
			std::string result{buffer.data(), end};
			if (result.ends_with(".0"))
			{
				result.resize(result.size() - 2);
			}
			return result;
		}

		std::optional<std::string> HeadingLabel(const Api::Amplifiers& amplifiers)
		{
			if (!amplifiers.HeadingRadians || !std::isfinite(*amplifiers.HeadingRadians))
			{
				return "HDG unknown";
			}
			constexpr double RADIANS_TO_DEGREES = 180.0 / std::numbers::pi;
			const double degrees = std::fmod(*amplifiers.HeadingRadians * RADIANS_TO_DEGREES + 360.0, 360.0);
			return "HDG " + FormatNumber(degrees) + " deg";
		}

		std::optional<std::string> SpeedLabel(const Api::Amplifiers& amplifiers)
		{
			if (!amplifiers.SpeedMetersPerSecond || !std::isfinite(*amplifiers.SpeedMetersPerSecond) || *amplifiers.SpeedMetersPerSecond < 0.0)
			{
				return "SPD unknown";
			}
			return "SPD " + FormatNumber(*amplifiers.SpeedMetersPerSecond) + " m/s";
		}

		std::optional<std::string> VerticalPositionLabel(const Api::SidcFields& fields, const Api::Amplifiers& amplifiers)
		{
			if (!amplifiers.VerticalPosition || !std::isfinite(amplifiers.VerticalPosition->ValueMeters))
			{
				return "ELEV unknown";
			}
			std::string label;
			switch (amplifiers.VerticalPosition->Reference)
			{
			case Api::VerticalReference::Altitude:
				if (fields.SymbolSetValue != Api::SymbolSet::Air && fields.SymbolSetValue != Api::SymbolSet::AirMissile && fields.SymbolSetValue != Api::SymbolSet::Space &&
					fields.SymbolSetValue != Api::SymbolSet::SpaceMissile)
				{
					return std::nullopt;
				}
				label = "ALT ";
				break;
			case Api::VerticalReference::Depth:
				if (fields.SymbolSetValue != Api::SymbolSet::SeaSubsurface)
				{
					return std::nullopt;
				}
				label = "DEPTH ";
				break;
			case Api::VerticalReference::Elevation:
				label = "ELEV ";
				break;
			}
			return label + FormatNumber(amplifiers.VerticalPosition->ValueMeters) + " m";
		}

		void DrawUncertaintyEllipse(DrawingSink& drawing,
									const Api::GeographicLocation& location,
									const Api::GeographicUncertaintyEllipse& ellipse,
									const Api::IMapProjection& projection,
									ImU32 color,
									BoundsBuilder& bounds)
		{
			if (!location.LatitudeDegrees || !location.LongitudeDegrees || !std::isfinite(ellipse.SemiMajorAxisMeters) || !std::isfinite(ellipse.SemiMinorAxisMeters) ||
				ellipse.SemiMajorAxisMeters < 0.0 || ellipse.SemiMinorAxisMeters < 0.0)
			{
				return;
			}

			const Api::GeographicCoordinate origin{*location.LatitudeDegrees, *location.LongitudeDegrees};
			const Api::ScreenPoint projectedOrigin = projection.Project(origin);
			const ImVec2 anchor{static_cast<float>(projectedOrigin.X), static_cast<float>(projectedOrigin.Y)};
			drawing.PathClear();
			for (int segment = 0; segment <= UNCERTAINTY_SEGMENTS; ++segment)
			{
				const double ellipseParameter = static_cast<double>(segment) * 2.0 * std::numbers::pi / UNCERTAINTY_SEGMENTS;
				const double majorComponent = ellipse.SemiMajorAxisMeters * std::cos(ellipseParameter);
				const double minorComponent = ellipse.SemiMinorAxisMeters * std::sin(ellipseParameter);
				const double eastMeters = majorComponent * std::sin(ellipse.OrientationRadians) + minorComponent * std::cos(ellipse.OrientationRadians);
				const double northMeters = majorComponent * std::cos(ellipse.OrientationRadians) - minorComponent * std::sin(ellipse.OrientationRadians);
				const Api::ScreenVector screenOffset = projection.ProjectLocalOffsetMeters(origin, eastMeters, northMeters);
				const ImVec2 projectedPoint{anchor.x + static_cast<float>(screenOffset.X), anchor.y + static_cast<float>(screenOffset.Y)};
				drawing.PathLineTo(projectedPoint);
				bounds.Add(projectedPoint, UNCERTAINTY_LINE_THICKNESS * drawing.ThicknessFactor);
			}
			drawing.PathStroke(color, ImDrawFlags_Closed, UNCERTAINTY_LINE_THICKNESS);
		}

		void DrawMovement(DrawingSink& drawing,
						  const Api::SidcFields& fields,
						  const Api::Amplifiers& amplifiers,
						  const Api::RenderContext& context,
						  ImVec2 frameCenter,
						  float scale,
						  ImU32 color,
						  BoundsBuilder& bounds)
		{
			if (!amplifiers.CourseOverGroundRadians || !std::isfinite(*amplifiers.CourseOverGroundRadians))
			{
				return;
			}

			const double courseRadians = *amplifiers.CourseOverGroundRadians;
			if (IsSpeedLeaderApplicable(fields.SymbolSetValue) && amplifiers.SpeedMetersPerSecond && context.Projection && amplifiers.Position &&
				amplifiers.Position->Location.LatitudeDegrees && amplifiers.Position->Location.LongitudeDegrees && std::isfinite(*amplifiers.SpeedMetersPerSecond) &&
				*amplifiers.SpeedMetersPerSecond >= 0.0 && std::isfinite(context.SpeedLeaderTimeHorizonSeconds) && context.SpeedLeaderTimeHorizonSeconds > 0.0)
			{
				const double distanceMeters = *amplifiers.SpeedMetersPerSecond * context.SpeedLeaderTimeHorizonSeconds;
				const double eastMeters = std::sin(courseRadians) * distanceMeters;
				const double northMeters = std::cos(courseRadians) * distanceMeters;
				const Api::ScreenVector screenOffset = context.Projection->ProjectLocalOffsetMeters(
					{*amplifiers.Position->Location.LatitudeDegrees, *amplifiers.Position->Location.LongitudeDegrees}, eastMeters, northMeters);
				const ImVec2 leaderEnd{frameCenter.x + static_cast<float>(screenOffset.X), frameCenter.y + static_cast<float>(screenOffset.Y)};
				drawing.AddLine(frameCenter, leaderEnd, color, BASE_LINE_THICKNESS * scale);
				bounds.Add(frameCenter, BASE_LINE_THICKNESS * scale);
				bounds.Add(leaderEnd, BASE_LINE_THICKNESS * scale);
				return;
			}

			if (!IsDirectionArrowApplicable(fields.SymbolSetValue))
			{
				return;
			}

			const double screenBearing = courseRadians - context.MapOrientationRadians;
			ImVec2 screenDirection{static_cast<float>(std::sin(screenBearing)), static_cast<float>(-std::cos(screenBearing))};
			if (context.Projection && amplifiers.Position && amplifiers.Position->Location.LatitudeDegrees && amplifiers.Position->Location.LongitudeDegrees)
			{
				const Api::ScreenVector projectedDirection = context.Projection->ProjectLocalOffsetMeters(
					{*amplifiers.Position->Location.LatitudeDegrees, *amplifiers.Position->Location.LongitudeDegrees}, std::sin(courseRadians), std::cos(courseRadians));
				const float projectedLength = std::sqrt(static_cast<float>(projectedDirection.X * projectedDirection.X + projectedDirection.Y * projectedDirection.Y));
				if (projectedLength > 0.0f)
				{
					screenDirection = {static_cast<float>(projectedDirection.X) / projectedLength, static_cast<float>(projectedDirection.Y) / projectedLength};
				}
			}
			const ImVec2 arrowEnd = Add(frameCenter, Scale(screenDirection, 30.0f * scale));
			drawing.AddLine(frameCenter, arrowEnd, color, BASE_LINE_THICKNESS * scale);
			DrawArrowHead(drawing, arrowEnd, screenDirection, color, scale, bounds);
			bounds.Add(frameCenter, BASE_LINE_THICKNESS * scale);
			bounds.Add(arrowEnd, BASE_LINE_THICKNESS * scale);
		}

		bool IsInsideViewport(const Api::ScreenRectangle& bounds, const Api::ScreenRectangle& viewport)
		{
			return bounds.Maximum.X >= viewport.Minimum.X && bounds.Minimum.X <= viewport.Maximum.X && bounds.Maximum.Y >= viewport.Minimum.Y &&
				   bounds.Minimum.Y <= viewport.Maximum.Y;
		}
	}

	static Api::MeasureResult Evaluate(const Api::Symbol& symbol, const Api::RenderContext& context, DrawingSink& drawing)
	{
		if (::ImGui::GetCurrentContext() == nullptr || !std::isfinite(context.SymbolScale) || context.SymbolScale <= 0.0 ||
			!IsSupportedStatus(symbol.Identification.Fields().Status))
		{
			return {};
		}

		const auto& fields = symbol.Identification.Fields();
		const auto frameDimension = GetFrameDimension(fields.SymbolSetValue);
		if (!frameDimension || !IsSupportedEntity(fields))
		{
			return {};
		}

		const float scale = static_cast<float>(std::clamp(context.SymbolScale, 0.5, 4.0));
		const float halfWidth = BASE_FRAME_HALF_WIDTH * scale;
		const float halfHeight = BASE_FRAME_HALF_HEIGHT * scale;
		ImVec2 frameCenter{static_cast<float>(context.VisualAnchor.X), static_cast<float>(context.VisualAnchor.Y)};
		if (IsHeadquarters(fields.Headquarters))
		{
			frameCenter.y -= halfHeight * 2.0f;
		}

		drawing.Enabled = true;
		drawing.PushClipRect({static_cast<float>(context.Viewport.Minimum.X), static_cast<float>(context.Viewport.Minimum.Y)},
							 {static_cast<float>(context.Viewport.Maximum.X), static_cast<float>(context.Viewport.Maximum.Y)},
							 true);

		BoundsBuilder bounds;
		drawing.Enabled = context.Layer != Api::RenderLayer::Geographic;
		drawing.ThicknessFactor = static_cast<float>(std::clamp(context.Style.LineThicknessFactor, 0.5, 3.0));
		const ImU32 affiliationColor = ToImGui(context.Style.Palette.Resolve(fields.Identity));
		const bool filled = context.Style.Preset != Api::StylePreset::UnfilledDark;
		const ImU32 frameColor = filled ? ToImGui(context.Style.FrameForeground) : affiliationColor;
		const ImU32 textColor = ToImGui(context.Style.TextForeground);
		const float lineThickness = BASE_LINE_THICKNESS * scale;
		const auto framePoints = FramePoints(*frameDimension, GetIdentityGroup(fields.Identity), frameCenter, scale);
		const bool frameIsClosed = fields.SymbolSetValue != Api::SymbolSet::Air && fields.SymbolSetValue != Api::SymbolSet::AirMissile &&
								   fields.SymbolSetValue != Api::SymbolSet::Space && fields.SymbolSetValue != Api::SymbolSet::SpaceMissile &&
								   fields.SymbolSetValue != Api::SymbolSet::SeaSubsurface && fields.SymbolSetValue != Api::SymbolSet::MineWarfare;
		const bool uncertainIdentity = IsUncertainIdentity(fields.Identity);
		const float dashLength = uncertainIdentity ? 2.0f * scale : fields.Status == Api::SymbolStatus::PlannedAnticipatedSuspect ? 5.0f * scale : 0.0f;
		const float gapLength = uncertainIdentity ? 2.4f * scale : fields.Status == Api::SymbolStatus::PlannedAnticipatedSuspect ? 3.6f * scale : 0.0f;
		if (filled)
		{
			// Fill the area bounded by the existing contour; never stroke the
			// implicit closing edge of an open air or subsurface frame.
			drawing.AddConcavePolyFilled(framePoints.data(), static_cast<int>(framePoints.size()), affiliationColor);
		}
		if (filled && context.Style.HaloWidth > 0.0)
		{
			drawing.HaloWidth = static_cast<float>(std::clamp(context.Style.HaloWidth, 0.0, 2.0)) * scale;
			DrawPatternedPath(drawing, framePoints, ToImGui(context.Style.Halo), lineThickness, frameIsClosed, dashLength, gapLength, false, bounds);
			drawing.HaloWidth = 0.0f;
		}
		DrawPatternedPath(drawing, framePoints, frameColor, lineThickness, frameIsClosed, dashLength, gapLength, uncertainIdentity, bounds);

		const ImU32 iconColor = filled ? ToImGui(context.Style.IconForeground) : affiliationColor;
		// A narrow affiliation halo keeps icon strokes beyond an open contour
		// visible without adding a closing frame or a rectangular background.
		const float haloWidth = static_cast<float>(std::clamp(context.Style.HaloWidth, 0.0, 2.0)) * scale;
		if (filled && haloWidth > 0.0f)
		{
			drawing.OverrideColor = affiliationColor;
			drawing.HaloWidth = haloWidth;
			DrawEntityIcon(drawing, fields, frameCenter, scale, iconColor, bounds);
			drawing.OverrideColor.reset();
			drawing.HaloWidth = 0.0f;
		}
		DrawEntityIcon(drawing, fields, frameCenter, scale, iconColor, bounds);

		// Conservative ink padding includes antialiasing, halo and thicker styles.
		const float inkPadding = haloWidth + BASE_LINE_THICKNESS * scale * (drawing.ThicknessFactor - 0.5f);
		bounds.Add({bounds.MinimumX, bounds.MinimumY}, inkPadding);
		bounds.Add({bounds.MaximumX, bounds.MaximumY}, inkPadding);
		const Api::ScreenRectangle symbolBounds = bounds.ToScreenRectangle();

		if (IsTaskForce(fields.Headquarters))
		{
			const ImVec2 bracketLeft{frameCenter.x - halfWidth * 0.56f, frameCenter.y - halfHeight - 4.0f * scale};
			const ImVec2 bracketRight{frameCenter.x + halfWidth * 0.56f, bracketLeft.y};
			drawing.AddLine(bracketLeft, bracketRight, affiliationColor, lineThickness);
			drawing.AddLine(bracketLeft, {bracketLeft.x, bracketLeft.y + 4.0f * scale}, affiliationColor, lineThickness);
			drawing.AddLine(bracketRight, {bracketRight.x, bracketRight.y + 4.0f * scale}, affiliationColor, lineThickness);
			bounds.Add(bracketLeft, lineThickness);
			bounds.Add(bracketRight, lineThickness);
		}

		if (IsHeadquarters(fields.Headquarters))
		{
			const ImVec2 staffStart{frameCenter.x - halfWidth, frameCenter.y + halfHeight};
			const ImVec2 staffEnd{staffStart.x, static_cast<float>(context.VisualAnchor.Y)};
			drawing.AddLine(staffStart, staffEnd, affiliationColor, lineThickness);
			bounds.Add(staffStart, lineThickness);
			bounds.Add(staffEnd, lineThickness);
		}

		if (IsFeintOrDummy(fields.Headquarters))
		{
			const ImVec2 slashStart{frameCenter.x - halfWidth * 0.72f, frameCenter.y - halfHeight * 0.74f};
			const ImVec2 slashEnd{frameCenter.x + halfWidth * 0.72f, frameCenter.y + halfHeight * 0.74f};
			drawing.AddLine(slashStart, slashEnd, affiliationColor, lineThickness);
			bounds.Add(slashStart, lineThickness);
			bounds.Add(slashEnd, lineThickness);
		}

		if (fields.ContextValue == Api::Context::Exercise || fields.ContextValue == Api::Context::Simulation)
		{
			const char* contextMarker = fields.ContextValue == Api::Context::Exercise ? "E" : "S";
			const float fontSize = ::ImGui::GetFontSize() * scale;
			const ImVec2 contextPosition{static_cast<float>(symbolBounds.Maximum.X) + 3.0f * scale, static_cast<float>(symbolBounds.Minimum.Y) - fontSize - 3.0f * scale};
			DrawTextAmplifier(drawing, contextMarker, contextPosition, fontSize, textColor, bounds);
		}

		if (const ImU32 statusColor = OperationalConditionColor(fields.Status); statusColor != 0)
		{
			const float barWidth = STATUS_BAR_WIDTH * scale;
			const float barHeight = STATUS_BAR_HEIGHT * scale;
			const ImVec2 barMinimum{frameCenter.x - barWidth * 0.5f, frameCenter.y + halfHeight + 2.0f * scale};
			const ImVec2 barMaximum{barMinimum.x + barWidth, barMinimum.y + barHeight};
			drawing.AddRectFilled(barMinimum, barMaximum, statusColor);
			drawing.AddRect(barMinimum, barMaximum, affiliationColor, 0.0f, ImDrawFlags_None, lineThickness * 0.75f);
			bounds.Add(barMinimum, lineThickness);
			bounds.Add(barMaximum, lineThickness);
		}

		const float fontSize = ::ImGui::GetFontSize() * scale;
		const float labelStartX = frameCenter.x + halfWidth + 7.0f * scale;
		float labelY = frameCenter.y - halfHeight * 0.78f;
		const auto appendLabel = [&](std::string_view text)
		{
			DrawTextAmplifier(drawing, text, {labelStartX, labelY}, fontSize, textColor, bounds);
			const auto dimensions = ::ImGui::GetFont()->CalcTextSizeA(fontSize, std::numeric_limits<float>::max(), 0.0f, text.data(), text.data() + text.size());
			labelY += dimensions.y + 2.0f * scale;
		};
		if (context.ShowLabels)
		{
			if (context.DetailedLabels && symbol.ExternalAmplifiers.UniqueDesignation)
			{
				appendLabel(*symbol.ExternalAmplifiers.UniqueDesignation);
			}
			if (symbol.ExternalAmplifiers.NameOrType)
			{
				appendLabel(*symbol.ExternalAmplifiers.NameOrType);
			}
			if (context.DetailedLabels && symbol.ExternalAmplifiers.AdditionalInformation)
			{
				appendLabel(*symbol.ExternalAmplifiers.AdditionalInformation);
			}
			for (const auto& label : {HeadingLabel(symbol.ExternalAmplifiers), SpeedLabel(symbol.ExternalAmplifiers), VerticalPositionLabel(fields, symbol.ExternalAmplifiers)})
			{
				if (context.DetailedLabels && label)
				{
					appendLabel(*label);
				}
			}
		}

		const auto layoutBounds = bounds.ToScreenRectangle();
		drawing.Enabled = context.Layer != Api::RenderLayer::Presentation;
		BoundsBuilder geographicBounds;
		const ImVec2 geographicCenter{static_cast<float>(context.GeographicAnchor.X), static_cast<float>(context.GeographicAnchor.Y)};
		DrawMovement(drawing, fields, symbol.ExternalAmplifiers, context, geographicCenter, scale, affiliationColor, geographicBounds);

		if (symbol.ExternalAmplifiers.Position && symbol.ExternalAmplifiers.Position->Uncertainty && context.Projection)
		{
			DrawUncertaintyEllipse(drawing,
								   symbol.ExternalAmplifiers.Position->Location,
								   *symbol.ExternalAmplifiers.Position->Uncertainty,
								   *context.Projection,
								   IM_COL32(255, 165, 0, 255),
								   geographicBounds);
		}

		const auto geographicRectangle =
			geographicBounds.HasPoints ? geographicBounds.ToScreenRectangle() : Api::ScreenRectangle{context.GeographicAnchor, context.GeographicAnchor};
		if (geographicBounds.HasPoints)
		{
			bounds.AddRectangle({geographicBounds.MinimumX, geographicBounds.MinimumY}, {geographicBounds.MaximumX, geographicBounds.MaximumY});
		}
		drawing.Enabled = true;
		drawing.PopClipRect();

		if (!bounds.HasPoints)
		{
			return {};
		}
		const Api::ScreenRectangle screenBounds = bounds.ToScreenRectangle();
		return {screenBounds, symbolBounds, layoutBounds, geographicRectangle, true};
	}
	Api::MeasureResult Renderer::Measure(const Api::Symbol& symbol, const Api::RenderContext& context)
	{
		DrawingSink measurement{nullptr};
		return Evaluate(symbol, context, measurement);
	}

	Api::RenderResult Renderer::Render(const Api::Symbol& symbol, const Api::RenderContext& context)
	{
		if (::ImGui::GetCurrentContext() == nullptr)
		{
			return {};
		}
		DrawingSink drawing{::ImGui::GetWindowDrawList()};
		const auto geometry = Evaluate(symbol, context, drawing);
		return {
			geometry.Bounds, geometry.Supported && IsInsideViewport(geometry.Bounds, context.Viewport), geometry.SymbolBounds, geometry.LayoutBounds, geometry.GeographicBounds};
	}

}
