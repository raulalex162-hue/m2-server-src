#pragma once
// core/events/Bus.h — magistrala de evenimente.
//
// Codul de baza publica:
//   core::events::Global().Publish(core::events::MobKill{ ... }, ctx);
// Un sistem asculta (de obicei in OnStart):
//   core::events::Global().Subscribe<core::events::MobKill>("achievement",
//       [this](const core::events::MobKill& e, const core::log::Ctx& ctx) { ... });
// si renunta in OnStop (registry-ul o face oricum automat, dupa numele sistemului).
//
// Garantii:
//   - un abonat care arunca o exceptie e logat si NU ii opreste pe ceilalti si nici jocul;
//   - un abonat se poate dezabona (sau abona pe altii) chiar in timpul unei publicari;
//   - abonatii primesc evenimentele in ordinea in care s-au abonat.

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "core/log/Log.h"

namespace core::events
{
	using SubscriptionId = uint64_t;

	class Bus
	{
	public:
		template <typename Event>
		using Handler = std::function<void(const Event&, const log::Ctx&)>;

		template <typename Event>
		SubscriptionId Subscribe(std::string owner, Handler<Event> handler)
		{
			auto& list = m_handlers[std::type_index(typeid(Event))];
			const SubscriptionId id = ++m_nextId;
			list.push_back(Entry{
				id,
				std::move(owner),
				[h = std::move(handler)](const void* event, const log::Ctx& ctx) {
					h(*static_cast<const Event*>(event), ctx);
				},
				true });
			return id;
		}

		template <typename Event>
		void Publish(const Event& event, const log::Ctx& ctx)
		{
			auto it = m_handlers.find(std::type_index(typeid(Event)));
			const size_t count = (it == m_handlers.end()) ? 0 : it->second.size();
			Log().Trace(ctx, "emit {} subscribers={}", NameOf(event), count);
			if (count == 0)
				return;

			// O referinta la lista ramane valida chiar daca un abonat adauga alte tipuri de
			// evenimente (iteratorul din unordered_map nu ar ramane).
			std::vector<Entry>& list = it->second;

			++m_depth;
			// Abonatii adaugati in timpul publicarii primesc doar evenimentele urmatoare.
			for (size_t i = 0; i < count; ++i)
			{
				if (!list[i].active)
					continue;
				// Copii locale: lista se poate realoca daca abonatul se aboneaza din nou.
				const std::string owner = list[i].owner;
				const auto call = list[i].call;
				try
				{
					call(&event, ctx);
				}
				catch (const std::exception& e)
				{
					Log().Error(ctx, "{} a aruncat o exceptie la {}: {}", owner, NameOf(event), e.what());
				}
				catch (...)
				{
					Log().Error(ctx, "{} a aruncat o exceptie necunoscuta la {}", owner, NameOf(event));
				}
			}
			--m_depth;
			if (m_depth == 0)
				Compact();
		}

		void Unsubscribe(SubscriptionId id);
		// Toate abonamentele unui sistem (apelat automat de registry la oprire).
		void UnsubscribeOwner(std::string_view owner);

		// Cate abonamente active are un eveniment (pentru teste si /sysinfo).
		template <typename Event>
		size_t SubscriberCount() const
		{
			auto it = m_handlers.find(std::type_index(typeid(Event)));
			if (it == m_handlers.end())
				return 0;
			size_t n = 0;
			for (const auto& entry : it->second)
				if (entry.active)
					++n;
			return n;
		}

		// Cate abonamente active are un sistem, la toate evenimentele.
		size_t OwnerCount(std::string_view owner) const;

	private:
		struct Entry
		{
			SubscriptionId id;
			std::string owner;
			std::function<void(const void*, const log::Ctx&)> call;
			bool active;
		};

		static log::Channel& Log() { return log::Get("EVENTS"); }
		void Deactivate(const std::function<bool(const Entry&)>& match);
		void Compact();

		std::unordered_map<std::type_index, std::vector<Entry>> m_handlers;
		SubscriptionId m_nextId = 0;
		int m_depth = 0;
	};

	// Magistrala procesului (cea folosita de game).
	Bus& Global();
}
