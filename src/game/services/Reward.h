#pragma once
// game/services/Reward — RewardService: singurul loc prin care sistemele dau recompense.
//
// Un sistem decide CAND si CE; Grant decide CUM:
//   - ce incape in inventar ajunge direct (itemele nu ajung niciodata pe jos);
//   - ce nu incape, sau yang-ul care ar depasi limita, intra in cutia de recompense in asteptare,
//     salvata prin PlayerSystemData, si se livreaza la urmatoarea intrare in joc sau cu /reward;
//   - daca recompensa nu poate fi pastrata (datele jucatorului inca nu au sosit, sau cutia e plina),
//     NU se da nimic: rezultatul e Rejected, iar sistemul poate incerca din nou mai tarziu.
//
// Recompensele din config se citesc cu core::reward::ReadReward(reader, game::reward::ItemProtoValidator()).

#include <cstdint>
#include <memory>
#include <string_view>

#include "core/log/Log.h"
#include "core/registry/Registry.h"
#include "core/reward/Reward.h"

class CHARACTER;

namespace game::reward
{
	enum class Status
	{
		Delivered, // totul a ajuns in inventar
		Queued,    // o parte (sau tot) asteapta in cutie
		Rejected   // nu s-a dat nimic (vezi reason)
	};

	struct GrantResult
	{
		Status status = Status::Rejected;
		core::reward::Reward delivered;
		core::reward::Reward queued;
		const char* reason = "";
	};

	GrantResult Grant(CHARACTER* ch, const core::reward::Reward& reward, std::string_view source, const core::log::Ctx& ctx);

	// Livreaza din cutie ce incape acum. Intoarce cate intrari mai asteapta (-1 = datele nu sunt disponibile).
	int DeliverPending(CHARACTER* ch, const core::log::Ctx& ctx);

	// Verifica in item_proto ca vnum-ul exista si poate fi dat ca recompensa.
	core::reward::ItemValidator ItemProtoValidator();

	const char* ToString(Status status);

	std::unique_ptr<core::registry::ISystem> CreateSystem();
}
