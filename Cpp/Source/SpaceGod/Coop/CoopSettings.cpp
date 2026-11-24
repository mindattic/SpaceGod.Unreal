// US-T44: CoopSettings.cs (US-T43) in C++; see CoopSettings.h.

#include "Coop/CoopSettings.h"

#include "Coop/CoopPeer.h"
#include "Coop/CoopWire.h"

#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "Settings/Tuning.h"

#include <exception>
#include <map>
#include <set>

using namespace SpaceGod::Core;

namespace SpaceGod::Coop::CoopSettings
{
	namespace
	{
		bool hooked = false, applying = false;
		int applied = 0;
		std::map<cs::String, cs::String> shared;
		std::set<cs::String> watched;

		const std::set<cs::String>& SharedGroups()
		{
			static const std::set<cs::String> groups = {
				SettingGroups::Gameplay /* US-T253: the host's difficulty is everyone's */, SettingGroups::Streaming, SettingGroups::Simulation, SettingGroups::Ftl, SettingGroups::Civilizations, SettingGroups::Ai,
				SettingGroups::Dogfighting, SettingGroups::Broadsides, SettingGroups::Weapons, SettingGroups::Economy,
				SettingGroups::Developer /* US-T525: the host's developer flags are everyone's */,
			};
			return groups;
		}

		/// A text setting's value as one space-free word for the "set <key> <value>" entry and the snapshot's lines:
		/// '%', white space and line breaks as %XX, and the empty text as a lone "%".
		cs::String EscapeText(const cs::String& text)
		{
			if (text.empty()) return "%";
			static const char* hex = "0123456789ABCDEF";
			cs::String out;
			for (char c : text)
			{
				if (c == '%' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
				{
					out += '%';
					out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
					out += hex[static_cast<unsigned char>(c) & 0xF];
				}
				else out += c;
			}
			return out;
		}

		cs::String UnescapeText(const cs::String& word)
		{
			if (word == "%") return cs::String();
			auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
			cs::String out;
			for (size_t i = 0; i < word.size(); i++)
			{
				int hi = 0, lo = 0;
				if (word[i] == '%' && i + 2 < word.size() && (hi = digit(word[i + 1])) >= 0 && (lo = digit(word[i + 2])) >= 0)
				{
					out += static_cast<char>(hi * 16 + lo);
					i += 2;
				}
				else out += word[i];
			}
			return out;
		}

		void Set(const cs::Ref<GameSetting>& s, const cs::String& value)
		{
			if (auto t = cs::As<ToggleSetting>(s)) t->Set(value == "1");
			else if (auto r = cs::As<RangeSetting>(s)) r->Set(Wire::ParseD(value));
			else if (auto c = cs::As<ChoiceSetting>(s)) c->Set(Wire::ParseI(value));
			else if (auto x = cs::As<TextSetting>(s))
			{
				if (!x->Set(UnescapeText(value))) throw cs::ArgumentException("not a valid value");
			}
		}

		void Changed(const cs::Ref<GameSetting>& s)
		{
			if (applying) return;
			CoopPeer* peer = CoopPeer::Current();
			cs::String value = ValueOf(s);
			if (peer == nullptr)
			{
				shared[s->Key] = value;
				return;
			}
			if (peer->IsHost())
			{
				shared[s->Key] = value;
				peer->QueueEntry(cs::Cat("set ", s->Key, " ", value));
				return;
			}
			// A client's own change would run here ahead of everyone: put back, and asked of the host.
			peer->RequestSetting(s->Key, value);
			auto was = shared.find(s->Key);
			if (was != shared.end() && was->second != value)
			{
				applying = true;
				try { Set(s, was->second); }
				catch (...) { applying = false; throw; }
				applying = false;
			}
		}
	}

	int Applied() { return applied; }

	bool IsShared(const cs::Ref<GameSetting>& s)
	{
		return s != nullptr && (SharedGroups().count(s->Group) > 0 || (Tuning::IsTuningGroup(s->Group) && Tuning::IsSimulationScope(s->Key)));
	}

	cs::String ValueOf(const cs::Ref<GameSetting>& s)
	{
		if (auto t = cs::As<ToggleSetting>(s)) return t->Value ? "1" : "0";
		if (auto r = cs::As<RangeSetting>(s)) return Wire::D(r->Value);
		if (auto c = cs::As<ChoiceSetting>(s)) return Wire::I(c->Index);
		if (auto x = cs::As<TextSetting>(s)) return EscapeText(x->Value);
		return cs::String();
	}

	void Hook()
	{
		if (!hooked)
		{
			hooked = true;
			SettingsRegistry::DiscoverLoadedDeclarations();
		}
		for (const auto& s : SettingsRegistry::Default()->All())
		{
			if (!IsShared(s) || !watched.insert(s->Key).second) continue;
			shared[s->Key] = ValueOf(s);
			s->Changed += [](cs::Ref<GameSetting> g) { Changed(g); };
		}
	}

	void Apply(const cs::String& key, const cs::String& value)
	{
		auto s = SettingsRegistry::Default()->Find(key);
		if (s == nullptr || !IsShared(s))
		{
			CoopPeer::Warn(cs::Cat("setting '", key, "' is not a shared one here"));
			return;
		}
		shared[key] = value;
		if (ValueOf(s) == value) return;
		applying = true;
		try { Set(s, value); }
		catch (const std::exception& e) { CoopPeer::Warn(cs::Cat("could not set '", key, "' to ", value, ": ", e.what())); }
		applying = false;
		applied++;
	}

	cs::String Snapshot()
	{
		Hook();
		cs::String sb;
		for (const auto& s : SettingsRegistry::Default()->All())
		{
			if (!IsShared(s)) continue;
			if (!sb.empty()) sb += '\n';
			sb += cs::Cat(s->Key, " ", ValueOf(s));
		}
		return sb;
	}

	int ApplySnapshot(const std::vector<cs::String>& lines)
	{
		Hook();
		int before = applied;
		for (const auto& l : lines)
		{
			size_t sp = l.find(' ');
			if (sp != cs::String::npos && sp > 0) Apply(l.substr(0, sp), l.substr(sp + 1));
		}
		return applied - before;
	}

	bool IsSharedKey(const cs::String& key) { return IsShared(SettingsRegistry::Default()->Find(key)); }
}
