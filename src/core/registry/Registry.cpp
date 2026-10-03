#include "core/registry/Registry.h"

namespace core::registry
{
	namespace
	{
		log::Channel& RegistryLog() { return log::Get("REGISTRY"); }

	}

	std::string ChannelName(std::string_view name)
	{
		std::string upper(name);
		for (auto& c : upper)
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
		return upper;
	}

	const char* ToString(State state)
	{
		switch (state)
		{
			case State::NotLoaded: return "not_loaded";
			case State::Disabled:  return "disabled";
			case State::Running:   return "running";
			case State::Failed:    return "failed";
		}
		return "unknown";
	}

	bool IsValidName(std::string_view name)
	{
		if (name.empty() || name.size() > 32)
			return false;
		if (name[0] < 'a' || name[0] > 'z')
			return false;
		for (char c : name)
		{
			const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			if (!ok)
				return false;
		}
		return true;
	}

	Registry::Registry(std::function<std::string(std::string_view)> pathFor)
		: m_pathFor(std::move(pathFor))
	{
	}

	bool Registry::Register(std::unique_ptr<ISystem> system)
	{
		const log::Ctx ctx{ log::NewTrace(), 0 };
		if (!system)
		{
			RegistryLog().Error(ctx, "register refuzat: sistem nul");
			return false;
		}

		const std::string name(system->Name());
		if (!IsValidName(name))
		{
			RegistryLog().Error(ctx, "register refuzat: nume invalid \"{}\" (a-z, 0-9, _)", name);
			return false;
		}
		if (FindEntry(name))
		{
			RegistryLog().Error(ctx, "register refuzat: \"{}\" e deja inregistrat", name);
			return false;
		}

		Entry entry;
		entry.status.name = name;
		entry.system = std::move(system);
		m_entries.push_back(std::move(entry));
		RegistryLog().Debug(ctx, "register {}", name);
		return true;
	}

	Registry::Entry* Registry::FindEntry(std::string_view name)
	{
		for (auto& entry : m_entries)
			if (entry.status.name == name)
				return &entry;
		return nullptr;
	}

	const Status* Registry::Find(std::string_view name) const
	{
		for (const auto& entry : m_entries)
			if (entry.status.name == name)
				return &entry.status;
		return nullptr;
	}

	std::vector<Status> Registry::List() const
	{
		std::vector<Status> out;
		out.reserve(m_entries.size());
		for (const auto& entry : m_entries)
			out.push_back(entry.status);
		return out;
	}

	bool Registry::LoadConfig(Entry& entry, config::Common& common, std::string& error)
	{
		config::Report report;
		const std::string path = m_pathFor(entry.status.name);
		auto json = config::LoadFile(path, report);

		if (json)
		{
			config::Reader root(*json, report);
			common = config::ReadCommon(root);
			entry.system->Stage(root);
		}

		if (!report.Ok())
		{
			entry.system->Discard();
			error = path + ":\n" + report.Summary();
			return false;
		}

		entry.system->Commit();
		error.clear();
		return true;
	}

	void Registry::StartEntry(Entry& entry, const log::Ctx& ctx)
	{
		entry.system->Start();
		entry.status.state = State::Running;
		RegistryLog().Info(ctx, "start {}", entry.status.name);
	}

	void Registry::StopEntry(Entry& entry, const log::Ctx& ctx)
	{
		entry.system->Stop();
		entry.status.state = State::Disabled;
		RegistryLog().Info(ctx, "stop {}", entry.status.name);
	}

	void Registry::StartAll()
	{
		const log::Ctx ctx{ log::NewTrace(), 0 };
		for (auto& entry : m_entries)
		{
			if (entry.status.state == State::Running)
				continue;

			config::Common common;
			std::string error;
			if (!LoadConfig(entry, common, error))
			{
				entry.status.state = State::Failed;
				entry.status.lastError = error;
				RegistryLog().Error(ctx, "{} NU porneste, config invalid:\n{}", entry.status.name, error);
				continue;
			}

			entry.status.lastError.clear();
			entry.status.logLevel = common.logLevel;
			log::Get(ChannelName(entry.status.name)).SetLevel(common.logLevel);

			if (!common.enabled)
			{
				entry.status.state = State::Disabled;
				RegistryLog().Info(ctx, "{} dezactivat din config", entry.status.name);
				continue;
			}

			StartEntry(entry, ctx);
		}
	}

	void Registry::StopAll()
	{
		const log::Ctx ctx{ log::NewTrace(), 0 };
		for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
		{
			if (it->status.state == State::Running)
				StopEntry(*it, ctx);
		}
	}

	bool Registry::Reload(std::string_view name, std::string* errorOut)
	{
		const log::Ctx ctx{ log::NewTrace(), 0 };
		Entry* entry = FindEntry(name);
		if (!entry)
		{
			if (errorOut)
				*errorOut = "sistem necunoscut: " + std::string(name);
			return false;
		}

		config::Common common;
		std::string error;
		if (!LoadConfig(*entry, common, error))
		{
			// Config-ul activ ramane cel vechi; starea nu se schimba.
			entry->status.lastError = error;
			RegistryLog().Error(ctx, "reload {} refuzat, raman pe config-ul vechi:\n{}", entry->status.name, error);
			if (errorOut)
				*errorOut = error;
			return false;
		}

		entry->status.lastError.clear();
		entry->status.logLevel = common.logLevel;
		log::Get(ChannelName(entry->status.name)).SetLevel(common.logLevel);

		const bool running = entry->status.state == State::Running;
		if (running && !common.enabled)
			StopEntry(*entry, ctx);
		else if (!running && common.enabled)
			StartEntry(*entry, ctx);
		else if (running)
			entry->system->ConfigReloaded();
		else
			entry->status.state = State::Disabled;

		RegistryLog().Info(ctx, "reload {} -> {}", entry->status.name, ToString(entry->status.state));
		if (errorOut)
			errorOut->clear();
		return true;
	}

	Registry& Global()
	{
		static Registry registry;
		return registry;
	}
}
