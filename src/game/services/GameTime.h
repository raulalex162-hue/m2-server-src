#pragma once
// game/services/GameTime — ceasul jocului: ziua de joc comuna si "bataia" planificatorului.
//
// Sistemul "game_time" (primul din lista de sisteme):
//   - acționeaza core::scheduler::Global() o data pe secunda, pe firul principal;
//   - tine ora de inceput a zilei de joc (day_reset din game_time.json), aceeasi pentru toate sistemele.
//
// Sistemele zilnice folosesc DayKeyNow() pentru "ce zi e azi" si nu isi definesc propriul reset.

#include <cstdint>
#include <memory>

#include "core/registry/Registry.h"
#include "core/time/Calendar.h"

namespace game::gametime
{
	// Minutul din zi la care incepe ziua de joc (0 = 00:00).
	int DayResetMinute();

	int64_t DayKeyNow();
	int64_t WeekKeyNow();
	core::time::Seconds NextResetNow();

	std::unique_ptr<core::registry::ISystem> CreateSystem();
}
