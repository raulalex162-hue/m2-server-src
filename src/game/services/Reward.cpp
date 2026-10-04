// Fisierul sta in src/game/services, deci headerele jocului se includ cu "../".
#include "../stdafx.h"
#include "Reward.h"

#include <chrono>
#include <string>
#include <vector>

#include "../char.h"
#include "../char_manager.h"
#include "../item_manager.h"
#include "common/length.h"

#include "core/events/Bus.h"
#include "core/events/GameEvents.h"
#include "PlayerData.h"

#include "ProtoBegin.h"
#include "server/reward_state.pb.h"
#include "server/storage_ids.pb.h"
#include "ProtoEnd.h"

namespace game::reward
{
	namespace
	{
		namespace pb = m2::server::reward;
		using core::reward::Item;
		using core::reward::Mailbox;
		using core::reward::Pending;
		using core::reward::Reward;

		constexpr uint32_t kMailboxStorage = m2::server::STORAGE_ID_REWARD_MAILBOX;

		uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
		}

		core::log::Channel& Log() { return core::log::Get("REWARD"); }

		std::string Describe(const Reward& r)
		{
			std::string out;
			for (const Item& item : r.items)
				out += (out.empty() ? "" : ", ") + std::to_string(item.vnum) + "x" + std::to_string(item.count);
			if (r.gold > 0)
				out += (out.empty() ? "" : ", ") + std::to_string(r.gold) + " yang";
			return out.empty() ? "-" : out;
		}

		// Inventarul real al jucatorului, vazut de core::reward.
		class CharacterReceiver : public core::reward::IReceiver
		{
		public:
			explicit CharacterReceiver(CHARACTER* ch) : m_ch(ch) {}

			bool CanReceiveItem(const Item& item) override
			{
				TItemTable* table = ITEM_MANAGER::instance().GetTable(item.vnum);
				if (!table || table->bType == ITEM_DS)
					return false;
				// Cerem mereu o celula libera, chiar daca itemul s-ar putea aduna intr-un teanc existent:
				// asa AutoGiveItem nu ajunge niciodata pe ramura care pune itemul pe jos.
				return m_ch->GetEmptyInventory(table->bSize) != -1;
			}

			void GiveItem(const Item& item) override
			{
				m_ch->AutoGiveItem(item.vnum, static_cast<BYTE>(item.count));
			}

			uint64_t GoldCapacity() override
			{
				// PointChange refuza daca totalul ajunge la GOLD_MAX, deci maximul permis e GOLD_MAX - 1.
				const int64_t room = static_cast<int64_t>(GOLD_MAX) - 1 - static_cast<int64_t>(m_ch->GetGold());
				return room > 0 ? static_cast<uint64_t>(room) : 0;
			}

			void GiveGold(uint64_t gold) override
			{
				m_ch->PointChange(POINT_GOLD, static_cast<int>(gold), true);
			}

		private:
			CHARACTER* m_ch;
		};

		Mailbox FromProto(const pb::Mailbox& state, size_t limit)
		{
			Mailbox box(limit);
			for (const auto& p : state.pending())
			{
				Pending pending;
				pending.source = p.source();
				pending.reward.gold = p.gold();
				pending.createdMs = p.created_ms();
				for (const auto& i : p.items())
					pending.reward.items.push_back(Item{ i.vnum(), i.count() });
				box.MutableEntries().push_back(std::move(pending));
			}
			return box;
		}

		pb::Mailbox ToProto(const Mailbox& box)
		{
			pb::Mailbox state;
			for (const Pending& pending : box.Entries())
			{
				auto* p = state.add_pending();
				p->set_source(pending.source);
				p->set_gold(pending.reward.gold);
				p->set_created_ms(pending.createdMs);
				for (const Item& item : pending.reward.items)
				{
					auto* i = p->add_items();
					i->set_vnum(item.vnum);
					i->set_count(item.count);
				}
			}
			return state;
		}

		struct RewardConfig
		{
			int64_t mailboxLimit = 50;
			bool deliverOnLogin = true;
		};

		class RewardSystem;
		RewardSystem* g_instance = nullptr;

		class RewardSystem : public core::registry::System<RewardConfig>
		{
		public:
			std::string_view Name() const override { return "reward"; }

			size_t MailboxLimit() const { return static_cast<size_t>(Config().mailboxLimit); }

			void Describe(std::vector<std::string>& lines) const override
			{
				lines.push_back("recompense: " + std::to_string(m_grants) + " (direct " + std::to_string(m_delivered)
					+ ", in cutie " + std::to_string(m_queued) + ", refuzate " + std::to_string(m_rejected) + ")");
				lines.push_back("livrari din cutie: " + std::to_string(m_mailboxDeliveries));
				lines.push_back("mailbox_limit=" + std::to_string(Config().mailboxLimit)
					+ " deliver_on_login=" + std::string(Config().deliverOnLogin ? "true" : "false"));
			}

			void CountGrant(Status status)
			{
				++m_grants;
				if (status == Status::Delivered) ++m_delivered;
				else if (status == Status::Queued) ++m_queued;
				else ++m_rejected;
			}
			void CountMailboxDelivery() { ++m_mailboxDeliveries; }

		protected:
			void Read(core::config::Reader& root, RewardConfig& out) override
			{
				out.mailboxLimit = root.OptionalInt("mailbox_limit", 50, 1, 200);
				out.deliverOnLogin = root.OptionalBool("deliver_on_login", true);
				root.RejectUnknown({ "enabled", "log_level", "mailbox_limit", "deliver_on_login" });
			}

			void OnStart() override
			{
				g_instance = this;
				core::events::Global().Subscribe<core::events::SystemDataReady>("reward",
					[this](const core::events::SystemDataReady& e, const core::log::Ctx& ctx) {
						if (!Config().deliverOnLogin)
							return;
						CHARACTER* ch = CHARACTER_MANAGER::instance().FindByPID(e.pid);
						if (!ch)
							return;
						const int waiting = DeliverPending(ch, ctx);
						if (waiting > 0)
							ch->ChatPacket(CHAT_TYPE_INFO, "[reward] %d recompense asteapta loc in inventar. Fa loc si scrie /reward.", waiting);
					});
				Log().Info(LifecycleCtx(), "pornit: mailbox_limit={} deliver_on_login={}", Config().mailboxLimit, Config().deliverOnLogin);
			}

			void OnStop() override
			{
				g_instance = nullptr;
				Log().Info(LifecycleCtx(), "oprit: recompense={} in cutie={} refuzate={}", m_grants, m_queued, m_rejected);
			}

		private:
			uint64_t m_grants = 0, m_delivered = 0, m_queued = 0, m_rejected = 0, m_mailboxDeliveries = 0;
		};

		GrantResult Reject(const char* reason, const core::log::Ctx& ctx, std::string_view source, const Reward& reward)
		{
			GrantResult result;
			result.status = Status::Rejected;
			result.reason = reason;
			Log().Error(ctx, "REFUZAT source={} {}: {}", source, Describe(reward), reason);
			if (g_instance)
				g_instance->CountGrant(Status::Rejected);
			return result;
		}
	}

	const char* ToString(Status status)
	{
		switch (status)
		{
			case Status::Delivered: return "delivered";
			case Status::Queued:    return "queued";
			case Status::Rejected:  return "rejected";
		}
		return "unknown";
	}

	GrantResult Grant(CHARACTER* ch, const Reward& reward, std::string_view source, const core::log::Ctx& ctx)
	{
		if (!ch || !ch->IsPC())
			return Reject("jucator invalid", ctx, source, reward);
		if (!g_instance)
			return Reject("sistemul reward e oprit", ctx, source, reward);
		if (reward.Empty())
			return Reject("recompensa goala", ctx, source, reward);

		const uint32_t pid = ch->GetPlayerID();

		// Cutia trebuie sa poata primi restul INAINTE sa dam ceva; altfel am putea pierde o parte.
		pb::Mailbox state;
		if (!game::playerdata::Load(pid, kMailboxStorage, state))
			return Reject("datele jucatorului nu sunt inca disponibile", ctx, source, reward);
		Mailbox box = FromProto(state, g_instance->MailboxLimit());
		if (box.Full())
			return Reject("cutia de recompense e plina", ctx, source, reward);

		CharacterReceiver receiver(ch);
		core::reward::Delivery delivery = core::reward::Deliver(receiver, reward);

		GrantResult result;
		result.delivered = delivery.delivered;
		result.status = Status::Delivered;

		if (!delivery.remaining.Empty())
		{
			box.Add(Pending{ std::string(source), delivery.remaining, NowMs() });
			// Salvare imediata: o recompensa in asteptare nu trebuie sa depinda de salvarea periodica.
			game::playerdata::Store(pid, kMailboxStorage, ToProto(box), true);
			result.queued = delivery.remaining;
			result.status = Status::Queued;
			ch->ChatPacket(CHAT_TYPE_INFO, "[reward] O parte din recompensa nu a incaput si te asteapta. Fa loc si scrie /reward.");
		}

		g_instance->CountGrant(result.status);
		Log().Info(ctx, "source={} {} -> inventar: {} | cutie: {}", source, ToString(result.status),
			Describe(result.delivered), Describe(result.queued));
		return result;
	}

	int DeliverPending(CHARACTER* ch, const core::log::Ctx& ctx)
	{
		if (!ch || !g_instance)
			return -1;
		const uint32_t pid = ch->GetPlayerID();

		pb::Mailbox state;
		if (!game::playerdata::Load(pid, kMailboxStorage, state))
			return -1;
		if (state.pending_size() == 0)
			return 0;

		Mailbox box = FromProto(state, g_instance->MailboxLimit());
		CharacterReceiver receiver(ch);
		const Reward delivered = box.DeliverAll(receiver);

		if (!delivered.Empty())
		{
			game::playerdata::Store(pid, kMailboxStorage, ToProto(box), true);
			g_instance->CountMailboxDelivery();
			Log().Info(ctx, "din cutie -> inventar: {} | mai asteapta: {} intrari", Describe(delivered), box.Size());
		}
		return static_cast<int>(box.Size());
	}

	core::reward::ItemValidator ItemProtoValidator()
	{
		return [](uint32_t vnum, std::string& why) {
			TItemTable* table = ITEM_MANAGER::instance().GetTable(vnum);
			if (!table)
			{
				why = "nu exista in item_proto";
				return false;
			}
			if (table->bType == ITEM_DS)
			{
				why = "pietrele de dragon (ITEM_DS) nu sunt inca suportate ca recompensa";
				return false;
			}
			return true;
		};
	}

	std::unique_ptr<core::registry::ISystem> CreateSystem()
	{
		return std::make_unique<RewardSystem>();
	}
}
