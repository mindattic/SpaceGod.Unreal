// US-T845: faction style profiles (see FactionStyles.h).

#include "Assembly/FactionStyles.h"

#include "Combat/Outfitting.h"
#include "Shared/Json/JsonObject.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace SpaceGod::Core
{
	using Shared::Json::JsonArray;
	using Shared::Json::JsonObject;

	namespace
	{
		bool ReadStyleText(const cs::String& file, cs::String& text)
		{
			std::ifstream f(std::filesystem::path(std::u8string(file.begin(), file.end())), std::ios::binary);
			if (!f) return false;
			text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
				static_cast<unsigned char>(text[2]) == 0xBF)
				text.erase(0, 3);
			return true;
		}

		bool SameStyleName(const cs::String& a, const cs::String& b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
				{ return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
		}

		double StyleWeight(const cs::Ref<JsonObject>& o, const char* key, double fallback)
		{
			double d = o->GetDouble(key, fallback);
			return std::isfinite(d) ? std::clamp(d, 0.0, 1.0) : fallback;
		}

		std::vector<cs::String> StyleNames(const cs::Ref<JsonArray>& a)
		{
			std::vector<cs::String> names;
			if (!a) return names;
			for (int i = 0; i < a->Count(); i++)
				if (const cs::String* s = (*a)[i].AsString(); s && !s->empty()) names.push_back(*s);
			return names;
		}

		FactionStyle ReadScaleStyle(const cs::Ref<JsonObject>& o, const cs::String& id)
		{
			FactionStyle s;
			s.Id = id;
			if (!o) return s;
			s.Packs = StyleNames(o->GetArray("packs"));
			for (auto& p : s.Packs) std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			s.Families = StyleNames(o->GetArray("families"));
			s.ImportedOnly = o->GetBool("importedOnly", false);
			s.Length = StyleWeight(o, "length", s.Length);
			s.Wings = StyleWeight(o, "wings", s.Wings);
			s.Armament = StyleWeight(o, "armament", s.Armament);
			s.Variety = StyleWeight(o, "variety", s.Variety);
			s.LaunchTubes = std::clamp(o->GetInt("launchTubes", 0), 0, 64);
			return s;
		}

		int StyleHexDigit(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		FactionTint ReadStyleTint(const cs::String& hex)
		{
			FactionTint t;
			cs::String h = !hex.empty() && hex[0] == '#' ? hex.substr(1) : hex;
			if (h.size() != 6) return t;
			double c[3];
			for (int i = 0; i < 3; i++)
			{
				int hi = StyleHexDigit(h[i * 2]), lo = StyleHexDigit(h[i * 2 + 1]);
				if (hi < 0 || lo < 0) return t;
				c[i] = (hi * 16 + lo) / 255.0;
			}
			t.R = c[0];
			t.G = c[1];
			t.B = c[2];
			t.Set = true;
			return t;
		}

		void ReplaceStyleToken(cs::String& text, const cs::String& token, const cs::String& with)
		{
			for (size_t at = text.find(token); at != cs::String::npos; at = text.find(token, at + with.size()))
				text.replace(at, token.size(), with);
		}
	}

	const FactionStyle& FactionStyleProfile::For(ShipClass cls) const { return Loadout::IsSmallCraft(cls) ? Fighter : Capital; }

	cs::String FactionStyleProfile::HullNumber(uint64_t seed) const
	{
		int digits = std::clamp(Markings.Digits, 1, 9);
		uint64_t modulo = 1;
		for (int i = 0; i < digits; i++) modulo *= 10;
		cs::String number = std::to_string(seed % modulo);
		if (static_cast<int>(number.size()) < digits) number.insert(0, digits - number.size(), '0');
		cs::String text = Markings.Format.empty() ? cs::String("{number}") : Markings.Format;
		ReplaceStyleToken(text, "{prefix}", Markings.Prefix);
		ReplaceStyleToken(text, "{number}", number);
		size_t first = text.find_first_not_of(" -"), last = text.find_last_not_of(" -");
		return first == cs::String::npos ? number : text.substr(first, last - first + 1);
	}

	FactionStyleBook FactionStyleBook::Load(const cs::String& file)
	{
		FactionStyleBook book;
		book.path = file;
		cs::String text;
		if (!ReadStyleText(file, text))
		{
			book.problem = cs::Cat("no file at ", file);
			return book;
		}
		cs::Ref<JsonObject> root;
		try
		{
			root = JsonObject::Parse(text);
		}
		catch (const std::exception& e)
		{
			book.problem = cs::Cat(file, " does not parse: ", e.what());
			return book;
		}
		auto styles = root ? root->GetArray("styles") : nullptr;
		if (styles)
			for (int i = 0; i < styles->Count(); i++)
			{
				auto o = (*styles)[i].AsObject();
				if (!o) continue;
				FactionStyleProfile p;
				p.Id = o->GetString("id");
				if (p.Id.empty()) continue;
				p.Faction = o->GetString("faction");
				p.Kind = o->GetString("kind");
				p.Fighter = ReadScaleStyle(o->GetObject("fighter"), p.Id);
				p.Capital = ReadScaleStyle(o->GetObject("capital"), p.Id);
				if (auto palette = o->GetObject("palette"))
				{
					cs::String colour = palette->GetString("colour");
					if (!colour.empty()) p.Palette.Colour = colour;
					p.Palette.Primary = ReadStyleTint(palette->GetString("primary"));
					p.Palette.Trim = ReadStyleTint(palette->GetString("trim"));
				}
				if (auto markings = o->GetObject("markings"))
				{
					p.Markings.Prefix = markings->GetString("prefix");
					p.Markings.Format = markings->GetString("format", p.Markings.Format);
					p.Markings.Digits = markings->GetInt("digits", p.Markings.Digits);
					p.Markings.Decal = markings->GetString("decal");
				}
				book.profiles.push_back(std::move(p));
			}
		if (book.profiles.empty()) book.problem = cs::Cat(file, " has no styles");
		return book;
	}

	const FactionStyleProfile& FactionStyleBook::For(const cs::String& factionName, const cs::String& kindName) const
	{
		if (!factionName.empty())
			for (const auto& p : profiles)
				if (SameStyleName(p.Faction, factionName)) return p;
		if (!kindName.empty())
			for (const auto& p : profiles)
				if (p.Faction.empty() && SameStyleName(p.Kind, kindName)) return p;
		return neutral;
	}

	const FactionStyle& FactionStyleBook::For(const cs::String& factionName, const cs::String& kindName, ShipClass cls) const
	{
		return For(factionName, kindName).For(cls);
	}

	const FactionStyleProfile* FactionStyleBook::Find(const cs::String& id) const
	{
		for (const auto& p : profiles)
			if (SameStyleName(p.Id, id)) return &p;
		return nullptr;
	}
}
