#pragma once
// daily_reward — logica pura: ce zi din ciclu urmeaza si daca jucatorul poate revendica azi.
//
// Nu depinde de joc (fara CHARACTER, fara retea), ca sa poata fi testata singura.
// Zilele sunt core::time::DayKey (zile de joc, cu resetul din game_time).
//
// Reguli:
//   - o revendicare pe zi de joc;
//   - ziua urmatoare dupa o revendicare continua seria (streak + 1);
//   - o zi ratata: seria reincepe de la 1 (reset_on_miss = true) sau continua (false);
//   - ziua din ciclu = ((streak - 1) % cycle) + 1, deci dupa ultima zi ciclul reincepe;
//   - daca ziua salvata e "in viitor" (ceasul serverului a dat inapoi), NU se poate revendica.

#include <cstdint>

namespace game::systems::daily_reward
{
	constexpr int64_t kNeverClaimed = -1;

	struct State
	{
		int64_t lastClaimDay = kNeverClaimed;
		uint32_t streak = 0;      // zile consecutive revendicate (seria curenta)
		uint32_t totalClaims = 0; // toate revendicarile, pentru statistici
	};

	struct Rules
	{
		uint32_t cycleLength = 7;
		bool resetOnMiss = true;
	};

	enum class Reason
	{
		CanClaim,
		AlreadyClaimedToday,
		ClockWentBack // ziua salvata e dupa ziua curenta
	};

	struct View
	{
		Reason reason = Reason::CanClaim;
		bool canClaim = false;
		uint32_t dayIndex = 1;     // 1..cycle: ziua care se revendica azi (sau care s-a revendicat azi)
		uint32_t nextStreak = 1;   // seria dupa revendicarea de azi
		bool streakBroken = false; // a ratat cel putin o zi de la ultima revendicare
	};

	inline uint32_t DayIndex(uint32_t streak, uint32_t cycleLength)
	{
		if (cycleLength == 0 || streak == 0)
			return 1;
		return ((streak - 1) % cycleLength) + 1;
	}

	inline View Evaluate(const State& state, int64_t today, const Rules& rules)
	{
		View view;

		if (state.lastClaimDay == kNeverClaimed)
		{
			view.canClaim = true;
			view.nextStreak = 1;
		}
		else if (state.lastClaimDay > today)
		{
			view.reason = Reason::ClockWentBack;
			view.canClaim = false;
			view.nextStreak = state.streak;
		}
		else if (state.lastClaimDay == today)
		{
			view.reason = Reason::AlreadyClaimedToday;
			view.canClaim = false;
			view.nextStreak = state.streak;
		}
		else if (state.lastClaimDay == today - 1)
		{
			view.canClaim = true;
			view.nextStreak = state.streak + 1;
		}
		else
		{
			view.canClaim = true;
			view.streakBroken = true;
			view.nextStreak = rules.resetOnMiss ? 1 : state.streak + 1;
		}

		view.dayIndex = DayIndex(view.nextStreak, rules.cycleLength);
		return view;
	}

	// Starea dupa revendicarea de azi. Se apeleaza doar daca Evaluate(...).canClaim e true.
	inline State AfterClaim(const State& state, int64_t today, const Rules& rules)
	{
		const View view = Evaluate(state, today, rules);
		State next = state;
		next.lastClaimDay = today;
		next.streak = view.nextStreak;
		next.totalClaims = state.totalClaims + 1;
		return next;
	}

	inline const char* ToString(Reason reason)
	{
		switch (reason)
		{
			case Reason::CanClaim:            return "can_claim";
			case Reason::AlreadyClaimedToday: return "already_claimed_today";
			case Reason::ClockWentBack:       return "clock_went_back";
		}
		return "unknown";
	}
}
