#pragma once
// daily_reward — recompensa zilnica.
//
// Ciclu de N zile (din daily_reward.json), o revendicare pe zi de joc (resetul din game_time).
// Starea jucatorului (ultima zi, seria) e salvata prin PlayerSystemData, deci e sigura la warp;
// recompensa trece prin RewardService, deci nu se pierde cu inventarul plin.
//
// Ordinea la revendicare: verificari -> RewardService::Grant -> abia apoi starea avanseaza si se
// salveaza imediat. Daca Grant refuza, starea ramane neschimbata (jucatorul nu pierde ziua).

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/registry/Registry.h"
#include "core/reward/Reward.h"

#include "DailyRewardLogic.h"

class CHARACTER;

namespace game::systems::daily_reward
{
	struct DailyRewardConfig
	{
		std::vector<core::reward::Reward> days; // days[0] = ziua 1
		int64_t minLevel = 10;
		bool resetOnMiss = true;
		bool notifyOnLogin = true;
		bool openWindowOnLogin = true;
	};

	class DailyReward : public core::registry::System<DailyRewardConfig>
	{
	public:
		std::string_view Name() const override { return "daily_reward"; }
		void Describe(std::vector<std::string>& lines) const override;

		// Folosite si de comanda GM /daily_test (aceleasi drumuri ca fereastra din client).
		void SendState(CHARACTER* ch, bool autoOpen);
		void HandleClaim(CHARACTER* ch);
		bool LoadState(uint32_t pid, State& out) const;
		bool SaveState(uint32_t pid, const State& state);
		Rules CurrentRules() const;

	protected:
		void Read(core::config::Reader& root, DailyRewardConfig& out) override;
		void OnStart() override;
		void OnStop() override;

	private:
		std::unordered_map<uint32_t, int64_t> m_lastClaimAttemptMs; // pauza minima intre doua cereri
		uint64_t m_claims = 0, m_refused = 0, m_rejectedByReward = 0, m_queued = 0;
	};

	// Instanta pornita (nullptr daca sistemul e oprit), pentru comanda GM.
	DailyReward* Instance();
}
