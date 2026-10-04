// Comenzile GM pentru sistemele noi (declarate si inregistrate in cmd.cpp):
//   /sysinfo                 lista sistemelor de pe acest core si starea lor
//   /sysinfo <sistem>        detalii: stare, nivel de log, abonamente, mesaje, ultima eroare
//   /sysreload <sistem>      reciteste conf/systems/<sistem>.json (config invalid -> ramane cel vechi)
//   /sysdebug <sistem> on|off  log debug pornit/oprit pana la urmatorul /sysreload
//
// ATENTIE: comenzile actioneaza doar pe core-ul pe care se afla GM-ul. Celelalte core-uri
// pastreaza config-ul lor pana primesc si ele /sysreload (sau pana la restart).

// Fisierul sta in src/game/services, deci headerele jocului se includ cu "../".
#include "../stdafx.h"

#include <sstream>
#include <string>
#include <vector>

#include "../char.h"
#include "../cmd.h"
#include "../config.h"

#include "core/events/Bus.h"
#include "core/log/Log.h"
#include "core/net/Router.h"
#include "core/registry/Registry.h"

namespace
{
	std::vector<std::string> SplitArgs(const char* argument)
	{
		std::vector<std::string> args;
		std::istringstream in(argument ? argument : "");
		std::string word;
		while (in >> word)
			args.push_back(word);
		return args;
	}

	// O linie in chat; liniile foarte lungi sunt taiate, ca sa incapa in pachetul de chat.
	void Reply(LPCHARACTER ch, const std::string& line)
	{
		constexpr size_t kMaxChatLine = 200;
		const std::string text = line.size() > kMaxChatLine ? line.substr(0, kMaxChatLine) + "..." : line;
		ch->ChatPacket(CHAT_TYPE_INFO, "%s", text.c_str());
	}

	// Un text pe mai multe linii (ex. raportul unui config invalid), maxim maxLines linii.
	void ReplyLines(LPCHARACTER ch, const std::string& text, size_t maxLines = 8)
	{
		std::istringstream in(text);
		std::string line;
		size_t count = 0;
		while (std::getline(in, line))
		{
			if (count++ == maxLines)
			{
				Reply(ch, "... (restul in systems.log)");
				return;
			}
			Reply(ch, line);
		}
	}

	const char* LevelName(core::log::Level level)
	{
		switch (level)
		{
			case core::log::Level::Trace: return "trace";
			case core::log::Level::Debug: return "debug";
			case core::log::Level::Info:  return "info";
			case core::log::Level::Warn:  return "warn";
			case core::log::Level::Error: return "error";
			default:                      return "off";
		}
	}

	core::log::Ctx CommandCtx(LPCHARACTER ch)
	{
		return core::log::Ctx{ core::log::NewTrace(), ch->GetPlayerID() };
	}
}

ACMD(do_sysinfo)
{
	auto& registry = core::registry::Global();
	const auto args = SplitArgs(argument);

	if (args.empty())
	{
		const auto list = registry.List();
		if (list.empty())
		{
			Reply(ch, "[sys] niciun sistem pe acest core");
			return;
		}
		Reply(ch, "[sys] sisteme pe acest core (" + std::to_string(list.size()) + "):");
		for (const auto& status : list)
			Reply(ch, "  " + status.name + ": " + core::registry::ToString(status.state) + ", log " + LevelName(status.logLevel)
				+ (status.lastError.empty() ? "" : ", ULTIMA INCARCARE A ESUAT"));
		return;
	}

	const std::string& name = args[0];
	const auto* status = registry.Find(name);
	if (!status)
	{
		Reply(ch, "[sys] sistem necunoscut: " + name);
		return;
	}

	Reply(ch, "[sys] " + name + ": " + core::registry::ToString(status->state) + ", log " + LevelName(status->logLevel));
	Reply(ch, "  abonamente la evenimente: " + std::to_string(core::events::Global().OwnerCount(name))
		+ ", mesaje de retea: " + std::to_string(core::net::Global().OwnerCount(name)));
	for (const auto& line : registry.Describe(name))
		Reply(ch, "  " + line);
	if (!status->lastError.empty())
	{
		Reply(ch, "  ultima eroare de config:");
		ReplyLines(ch, status->lastError);
	}
}

ACMD(do_sysreload)
{
	const auto args = SplitArgs(argument);
	if (args.size() != 1)
	{
		Reply(ch, "[sys] folosire: /sysreload <sistem>");
		return;
	}

	const std::string& name = args[0];
	std::string error;
	const bool ok = core::registry::Global().Reload(name, &error);
	core::log::Get("REGISTRY").Info(CommandCtx(ch), "GM {} /sysreload {} -> {}", ch->GetName(), name, ok ? "ok" : "refuzat");

	if (ok)
	{
		const auto* status = core::registry::Global().Find(name);
		Reply(ch, "[sys] " + name + " reincarcat: " + core::registry::ToString(status->state) + " (doar pe acest core)");
		return;
	}

	Reply(ch, "[sys] " + name + ": reload refuzat, sistemul ramane pe config-ul vechi:");
	ReplyLines(ch, error);
}

ACMD(do_sysdebug)
{
	const auto args = SplitArgs(argument);
	if (args.size() != 2 || (args[1] != "on" && args[1] != "off"))
	{
		Reply(ch, "[sys] folosire: /sysdebug <sistem> on|off");
		return;
	}

	const std::string& name = args[0];
	const auto level = args[1] == "on" ? core::log::Level::Debug : core::log::Level::Info;
	if (!core::registry::Global().SetLogLevel(name, level))
	{
		Reply(ch, "[sys] sistem necunoscut: " + name);
		return;
	}

	core::log::Get("REGISTRY").Info(CommandCtx(ch), "GM {} /sysdebug {} {}", ch->GetName(), name, args[1]);
	Reply(ch, "[sys] " + name + ": log " + LevelName(level) + " pana la urmatorul /sysreload (doar pe acest core)");
}
