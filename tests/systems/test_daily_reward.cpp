#include <doctest.h>

#include "game/systems/daily_reward/DailyRewardLogic.h"

using namespace game::systems::daily_reward;

namespace
{
	// Revendica in ziua `day` si intoarce noua stare (verifica si ca era permis).
	State ClaimOn(const State& s, int64_t day, const Rules& rules = {})
	{
		const View v = Evaluate(s, day, rules);
		CHECK(v.canClaim);
		return AfterClaim(s, day, rules);
	}
}

TEST_CASE("daily_reward: prima revendicare e ziua 1")
{
	const View v = Evaluate(State{}, 100, Rules{});
	CHECK(v.canClaim);
	CHECK(v.dayIndex == 1);
	CHECK_FALSE(v.streakBroken);

	const State s = AfterClaim(State{}, 100, Rules{});
	CHECK(s.lastClaimDay == 100);
	CHECK(s.streak == 1);
	CHECK(s.totalClaims == 1);
}

TEST_CASE("daily_reward: a doua revendicare in aceeasi zi e refuzata si arata ziua revendicata")
{
	const State s = ClaimOn(State{}, 100);
	const View v = Evaluate(s, 100, Rules{});
	CHECK_FALSE(v.canClaim);
	CHECK(v.reason == Reason::AlreadyClaimedToday);
	CHECK(v.dayIndex == 1);
}

TEST_CASE("daily_reward: zile consecutive continua seria, iar dupa ziua 7 ciclul reincepe")
{
	State s;
	for (int64_t day = 100; day < 107; ++day)
	{
		const View v = Evaluate(s, day, Rules{});
		CHECK(v.dayIndex == static_cast<uint32_t>(day - 99));
		s = ClaimOn(s, day);
	}
	CHECK(s.streak == 7);

	const View eighth = Evaluate(s, 107, Rules{});
	CHECK(eighth.canClaim);
	CHECK(eighth.dayIndex == 1);
	CHECK(eighth.nextStreak == 8);
}

TEST_CASE("daily_reward: o zi ratata reia de la ziua 1 (implicit)")
{
	State s = ClaimOn(State{}, 100);
	s = ClaimOn(s, 101);
	s = ClaimOn(s, 102); // ziua 3

	const View v = Evaluate(s, 104, Rules{}); // 103 ratata
	CHECK(v.canClaim);
	CHECK(v.streakBroken);
	CHECK(v.dayIndex == 1);
	CHECK(v.nextStreak == 1);
}

TEST_CASE("daily_reward: cu reset_on_miss = false, o zi ratata continua seria")
{
	Rules rules;
	rules.resetOnMiss = false;
	State s = ClaimOn(State{}, 100, rules);
	s = ClaimOn(s, 101, rules);

	const View v = Evaluate(s, 110, rules);
	CHECK(v.canClaim);
	CHECK(v.streakBroken);
	CHECK(v.dayIndex == 3);
}

TEST_CASE("daily_reward: daca ceasul serverului da inapoi, nu se poate revendica")
{
	const State s = ClaimOn(State{}, 100);
	const View v = Evaluate(s, 99, Rules{});
	CHECK_FALSE(v.canClaim);
	CHECK(v.reason == Reason::ClockWentBack);
}

TEST_CASE("daily_reward: ciclu de o zi da mereu ziua 1, iar ciclul configurat e respectat")
{
	Rules one;
	one.cycleLength = 1;
	State s = ClaimOn(State{}, 100, one);
	CHECK(Evaluate(s, 101, one).dayIndex == 1);

	Rules three;
	three.cycleLength = 3;
	State t;
	uint32_t seen[5] = {};
	for (int i = 0; i < 5; ++i)
	{
		seen[i] = Evaluate(t, 200 + i, three).dayIndex;
		t = ClaimOn(t, 200 + i, three);
	}
	CHECK(seen[0] == 1);
	CHECK(seen[2] == 3);
	CHECK(seen[3] == 1);
	CHECK(seen[4] == 2);
}
