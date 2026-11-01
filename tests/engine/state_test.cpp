#include "core/pointer_engine.hpp"
#include "state_baseline.hpp"
#include <gtest/gtest.h>

TEST(MotionState, MatchesFrozenContinuousStateAcrossConfigurationAndDpiChanges)
{
	pa::PointerEngine engine;
	for (std::size_t i = 0; i < pa::test::stateBaseline.size(); ++i)
	{
		const auto& s = pa::test::stateBaseline[i];
		engine.configure({static_cast<std::uint8_t>(s.speed), s.enhanced});
		if (s.reset)
			engine.reset();
		EXPECT_EQ(engine.apply({s.x, s.y}, s.dpi), (pa::Motion{s.outputX, s.outputY})) << "step " << i;
	}
}
