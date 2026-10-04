#include "core/net/Router.h"

#include <exception>

namespace core::net
{
	namespace
	{
		uint16_t ReadU16(const uint8_t* p)
		{
			return static_cast<uint16_t>(p[0] | (p[1] << 8));
		}

		void WriteU16(std::vector<uint8_t>& out, uint16_t value)
		{
			out.push_back(static_cast<uint8_t>(value & 0xFF));
			out.push_back(static_cast<uint8_t>(value >> 8));
		}
	}

	const char* ToString(Result result)
	{
		switch (result)
		{
			case Result::Ok:             return "ok";
			case Result::TooShort:       return "too_short";
			case Result::UnknownMessage: return "unknown_message";
			case Result::TooLarge:       return "too_large";
			case Result::HandlerError:   return "handler_error";
		}
		return "unknown";
	}

	bool Router::Register(uint16_t system, uint16_t type, std::string owner, Handler handler, size_t maxBody)
	{
		const log::Ctx ctx{};
		if (system == 0 || type == 0 || !handler)
		{
			Log().Error(ctx, "register refuzat pentru {}: system si type trebuie sa fie > 0", owner);
			return false;
		}
		const auto key = std::make_pair(system, type);
		if (auto it = m_routes.find(key); it != m_routes.end())
		{
			Log().Error(ctx, "register refuzat pentru {}: {}:{} e deja al lui {}", owner, system, type, it->second.owner);
			return false;
		}
		if (maxBody > kMaxBodySize)
			maxBody = kMaxBodySize;

		m_routes.emplace(key, Route{ std::move(owner), std::move(handler), maxBody });
		return true;
	}

	void Router::UnregisterOwner(std::string_view owner)
	{
		for (auto it = m_routes.begin(); it != m_routes.end();)
		{
			if (it->second.owner == owner)
				it = m_routes.erase(it);
			else
				++it;
		}
	}

	size_t Router::OwnerCount(std::string_view owner) const
	{
		size_t n = 0;
		for (const auto& [key, route] : m_routes)
			if (route.owner == owner)
				++n;
		return n;
	}

	bool Router::Has(uint16_t system, uint16_t type) const
	{
		return m_routes.count(std::make_pair(system, type)) != 0;
	}

	Result Router::Dispatch(uint32_t pid, Body payload)
	{
		const log::Ctx ctx{ log::NewTrace(), pid };

		if (payload.size() < kPrefixSize)
		{
			Log().Warn(ctx, "mesaj prea scurt ({} octeti)", payload.size());
			return Result::TooShort;
		}

		const uint16_t system = ReadU16(payload.data());
		const uint16_t type = ReadU16(payload.data() + 2);
		const Body body = payload.subspan(kPrefixSize);

		auto it = m_routes.find(std::make_pair(system, type));
		if (it == m_routes.end())
		{
			Log().Warn(ctx, "mesaj necunoscut {}:{} ({} octeti)", system, type, body.size());
			return Result::UnknownMessage;
		}

		const Route& route = it->second;
		if (body.size() > route.maxBody)
		{
			Log().Warn(ctx, "mesaj {}:{} pentru {} prea mare: {} > {}", system, type, route.owner, body.size(), route.maxBody);
			return Result::TooLarge;
		}

		Log().Trace(ctx, "recv {}:{} -> {} ({} octeti)", system, type, route.owner, body.size());

		// Copii locale: handler-ul poate dezinregistra rute (chiar si pe a lui) cat ruleaza.
		const std::string owner = route.owner;
		const Handler handler = route.handler;
		try
		{
			handler(pid, body, ctx);
		}
		catch (const std::exception& e)
		{
			Log().Error(ctx, "{} a aruncat o exceptie la {}:{}: {}", owner, system, type, e.what());
			return Result::HandlerError;
		}
		catch (...)
		{
			Log().Error(ctx, "{} a aruncat o exceptie necunoscuta la {}:{}", owner, system, type);
			return Result::HandlerError;
		}
		return Result::Ok;
	}

	std::vector<uint8_t> EncodePayload(uint16_t system, uint16_t type, Body body)
	{
		std::vector<uint8_t> out;
		out.reserve(kPrefixSize + body.size());
		WriteU16(out, system);
		WriteU16(out, type);
		out.insert(out.end(), body.begin(), body.end());
		return out;
	}

	Router& Global()
	{
		static Router router;
		return router;
	}
}
