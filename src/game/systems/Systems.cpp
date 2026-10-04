// Fisierul sta in src/game/systems, deci headerele jocului se includ cu "../".
#include "../stdafx.h"
#include "Systems.h"

#include <memory>

#include "../config.h"

#include "core/log/Log.h"
#include "core/registry/Registry.h"

#include "../services/GameTime.h"
#include "../services/PlayerData.h"
#include "../services/Reward.h"
#include "daily_reward/DailyReward.h"
#include "heartbeat/Heartbeat.h"

namespace game::systems
{
	namespace
	{
		bool g_started = false;

		void RegisterAll(core::registry::Registry& registry)
		{
			// Ordinea de pornire. Fiecare sistem nou se adauga aici, cu o linie.
			// Serviciile intai: pornesc inaintea sistemelor care le folosesc si se opresc dupa ele.
			// game_time: ceasul si ziua de joc. player_data: salveaza la oprire si ce au scris sistemele.
			registry.Register(game::gametime::CreateSystem());
			registry.Register(game::playerdata::CreateSystem());
			registry.Register(game::reward::CreateSystem());
			registry.Register(std::make_unique<daily_reward::DailyReward>());
			registry.Register(std::make_unique<heartbeat::Heartbeat>());
		}
	}

	void StartAll()
	{
		const core::log::Ctx ctx{ core::log::NewTrace(), 0 };
		if (g_bAuthServer)
		{
			core::log::Get("REGISTRY").Info(ctx, "core de auth: sistemele nu pornesc aici");
			return;
		}

		auto& registry = core::registry::Global();
		RegisterAll(registry);
		registry.StartAll();
		g_started = true;

		for (const auto& status : registry.List())
			core::log::Get("REGISTRY").Info(ctx, "{} -> {}", status.name, core::registry::ToString(status.state));
	}

	void StopAll()
	{
		if (!g_started)
			return;
		core::registry::Global().StopAll();
		g_started = false;
	}
}
