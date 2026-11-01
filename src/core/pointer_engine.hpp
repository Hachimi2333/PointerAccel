// SPDX-License-Identifier: BSD-3-Clause
// Derived from windows-pointer-linux 0bdd644d420737a7aea506e252fc5e12db34c599.
// See README.md (third-party license) and docs/algorithm-origin.md.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pa
{

struct Motion
{
	std::int32_t x = 0;
	std::int32_t y = 0;

	auto operator==(const Motion&) const -> bool = default;
};

struct PointerSettings
{
	std::uint8_t pointerSpeed = 10;
	bool enhancePointerPrecision = true;

	auto operator==(const PointerSettings&) const -> bool = default;
};

[[nodiscard]] auto parsePointerSpeed(std::string_view value) -> std::optional<std::uint8_t>;

class PointerEngine
{
public:
	explicit PointerEngine(PointerSettings settings = {});

	[[nodiscard]] auto apply(Motion raw, std::uint16_t displayDpi = 96) -> Motion;
	void configure(PointerSettings settings);
	void reset();

	[[nodiscard]] auto settings() const -> PointerSettings;

private:
	struct Ballistics
	{
		std::array<std::int64_t, 5> x{};
		std::array<std::int64_t, 4> slopes{};
		std::array<std::int64_t, 4> intercepts{};
	};

	void buildCurve(std::uint16_t displayDpi);

	[[nodiscard]] auto applyEnhanced(Motion raw, std::uint16_t displayDpi) -> Motion;
	[[nodiscard]] auto applyLinear(Motion raw) -> Motion;

	PointerSettings m_settings;
	Ballistics m_ballistics;
	std::uint16_t m_ballisticsDpi = 0;

	std::int64_t m_enhancedXRemainder = 0;
	std::int64_t m_enhancedYRemainder = 0;
	std::size_t m_previousSegment = 0;

	std::int64_t m_linearXRemainder = 0;
	std::int64_t m_linearYRemainder = 0;
};

[[nodiscard]] auto displayDpiFromScale(double scale) -> std::uint16_t;

} // namespace pa
