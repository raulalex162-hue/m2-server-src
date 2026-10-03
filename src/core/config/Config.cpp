#include "core/config/Config.h"

#include <fstream>
#include <sstream>

namespace core::config
{
	namespace
	{
		const char* TypeName(const Json& value)
		{
			switch (value.type())
			{
				case Json::value_t::null:            return "null";
				case Json::value_t::boolean:         return "bool";
				case Json::value_t::number_integer:
				case Json::value_t::number_unsigned: return "numar intreg";
				case Json::value_t::number_float:    return "numar zecimal";
				case Json::value_t::string:          return "text";
				case Json::value_t::array:           return "lista";
				case Json::value_t::object:          return "obiect";
				default:                             return "necunoscut";
			}
		}

		std::string Expected(const char* wanted, const Json& got)
		{
			return std::string("asteptat ") + wanted + ", gasit " + TypeName(got);
		}

		// Linia si coloana (de la 1) pentru o pozitie in text.
		std::pair<size_t, size_t> LineColumn(std::string_view text, size_t byte)
		{
			size_t line = 1, column = 1;
			for (size_t i = 0; i < byte && i < text.size(); ++i)
			{
				if (text[i] == '\n') { ++line; column = 1; }
				else { ++column; }
			}
			return { line, column };
		}
	}

	// --- Report ---

	void Report::Add(std::string path, std::string message)
	{
		m_issues.push_back(Issue{ std::move(path), std::move(message) });
	}

	std::string Report::Summary() const
	{
		std::string out;
		for (const auto& issue : m_issues)
		{
			if (!out.empty())
				out += '\n';
			out += issue.path.empty() ? "(fisier)" : issue.path;
			out += ": ";
			out += issue.message;
		}
		return out;
	}

	// --- Reader ---

	Reader::Reader(const Json& node, Report& report, std::string path)
		: m_node(&node), m_report(&report), m_path(std::move(path))
	{
		if (!m_node->is_object())
			m_report->Add(m_path, Expected("obiect", *m_node));
	}

	std::string Reader::KeyPath(std::string_view key) const
	{
		return m_path.empty() ? std::string(key) : m_path + "." + std::string(key);
	}

	const Json* Reader::Find(std::string_view key) const
	{
		if (!m_node->is_object())
			return nullptr;
		auto it = m_node->find(std::string(key));
		return it == m_node->end() ? nullptr : &*it;
	}

	const Json* Reader::Required(std::string_view key)
	{
		const Json* value = Find(key);
		if (!value && m_node->is_object())
			m_report->Add(KeyPath(key), "camp obligatoriu lipsa");
		return value;
	}

	bool Reader::Has(std::string_view key) const
	{
		return Find(key) != nullptr;
	}

	bool Reader::RequireBool(std::string_view key)
	{
		const Json* value = Required(key);
		if (!value)
			return false;
		if (!value->is_boolean())
		{
			m_report->Add(KeyPath(key), Expected("bool (true/false)", *value));
			return false;
		}
		return value->get<bool>();
	}

	int64_t Reader::RequireInt(std::string_view key, int64_t min, int64_t max)
	{
		const Json* value = Required(key);
		if (!value)
			return 0;
		if (!value->is_number_integer())
		{
			m_report->Add(KeyPath(key), Expected("numar intreg", *value));
			return 0;
		}
		if (value->is_number_unsigned() && value->get<uint64_t>() > static_cast<uint64_t>(INT64_MAX))
		{
			m_report->Add(KeyPath(key), "valoare prea mare");
			return 0;
		}
		const int64_t number = value->get<int64_t>();
		if (number < min || number > max)
		{
			m_report->Add(KeyPath(key), "valoarea " + std::to_string(number) + " e in afara intervalului ["
				+ std::to_string(min) + ", " + std::to_string(max) + "]");
			return 0;
		}
		return number;
	}

	std::string Reader::RequireString(std::string_view key, bool allowEmpty)
	{
		const Json* value = Required(key);
		if (!value)
			return {};
		if (!value->is_string())
		{
			m_report->Add(KeyPath(key), Expected("text", *value));
			return {};
		}
		std::string text = value->get<std::string>();
		if (!allowEmpty && text.empty())
		{
			m_report->Add(KeyPath(key), "textul nu poate fi gol");
			return {};
		}
		return text;
	}

	bool Reader::OptionalBool(std::string_view key, bool fallback)
	{
		return Has(key) ? RequireBool(key) : fallback;
	}

	int64_t Reader::OptionalInt(std::string_view key, int64_t fallback, int64_t min, int64_t max)
	{
		return Has(key) ? RequireInt(key, min, max) : fallback;
	}

	std::string Reader::OptionalString(std::string_view key, std::string fallback)
	{
		return Has(key) ? RequireString(key, true) : fallback;
	}

	std::optional<Reader> Reader::RequireObject(std::string_view key)
	{
		const Json* value = Required(key);
		if (!value)
			return std::nullopt;
		if (!value->is_object())
		{
			m_report->Add(KeyPath(key), Expected("obiect", *value));
			return std::nullopt;
		}
		return Reader(*value, *m_report, KeyPath(key));
	}

	std::vector<Reader> Reader::RequireArray(std::string_view key, size_t minCount)
	{
		std::vector<Reader> items;
		const Json* value = Required(key);
		if (!value)
			return items;
		if (!value->is_array())
		{
			m_report->Add(KeyPath(key), Expected("lista", *value));
			return items;
		}
		if (value->size() < minCount)
		{
			m_report->Add(KeyPath(key), "lista are " + std::to_string(value->size())
				+ " elemente, minimul e " + std::to_string(minCount));
		}
		const std::string base = KeyPath(key);
		for (size_t i = 0; i < value->size(); ++i)
			items.emplace_back((*value)[i], *m_report, base + "[" + std::to_string(i) + "]");
		return items;
	}

	void Reader::RejectUnknown(std::initializer_list<std::string_view> allowed)
	{
		if (!m_node->is_object())
			return;
		for (auto it = m_node->begin(); it != m_node->end(); ++it)
		{
			bool known = false;
			for (auto name : allowed)
			{
				if (it.key() == name) { known = true; break; }
			}
			if (!known)
				m_report->Add(KeyPath(it.key()), "camp necunoscut (greseala de scriere?)");
		}
	}

	void Reader::Fail(std::string_view key, std::string message)
	{
		m_report->Add(key.empty() ? m_path : KeyPath(key), std::move(message));
	}

	// --- Functii libere ---

	Common ReadCommon(Reader& root)
	{
		Common common;
		common.enabled = root.RequireBool("enabled");

		const std::string level = root.OptionalString("log_level", "info");
		common.logLevel = log::ParseLevel(level, log::Level::Info);
		if (log::ParseLevel(level, log::Level::Off) == log::Level::Off && level != "off")
			root.Fail("log_level", "nivel necunoscut \"" + level + "\" (trace, debug, info, warn, error, off)");

		return common;
	}

	std::string PathFor(std::string_view system)
	{
		return "conf/systems/" + std::string(system) + ".json";
	}

	std::optional<Json> Parse(std::string_view text, Report& report)
	{
		try
		{
			// al patrulea argument: comentariile // si /* */ sunt permise
			return Json::parse(text.begin(), text.end(), nullptr, true, true);
		}
		catch (const Json::parse_error& e)
		{
			const auto [line, column] = LineColumn(text, e.byte > 0 ? e.byte - 1 : 0);
			report.Add("", "JSON invalid la linia " + std::to_string(line) + ", coloana "
				+ std::to_string(column) + ": " + e.what());
			return std::nullopt;
		}
	}

	std::optional<Json> LoadFile(const std::string& path, Report& report)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			report.Add("", "nu pot deschide fisierul " + path);
			return std::nullopt;
		}
		std::ostringstream buffer;
		buffer << file.rdbuf();
		std::string text = buffer.str();

		// Un BOM UTF-8 la inceput (pus de unele editoare) nu e o eroare.
		if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
			&& static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
			text.erase(0, 3);

		return Parse(text, report);
	}
}
