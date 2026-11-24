#include "Settings/ShippedDefaults.h"
#include "Shared/Json/MiniJson.h"

#include <algorithm>

namespace SpaceGod::Core
{
	using SpaceGod::Shared::Json::JsonObject;
	using SpaceGod::Shared::Json::MiniJson;

	cs::Dictionary<cs::String, cs::String> ShippedDefaults::Parse(const cs::String& json)
	{
		cs::Dictionary<cs::String, cs::String> defaults;
		auto o = MiniJson::ParseObject(json);
		for (auto [key, value] : *o)
		{
			if (auto s = value.AsString()) defaults[key] = *s;
			else if (value.IsBool()) defaults[key] = value.ToString();
			else if (auto n = value.AsNumber()) defaults[key] = n->Raw;
		}
		return defaults;
	}

	cs::String ShippedDefaults::Write(const cs::Dictionary<cs::String, cs::String>& defaults)
	{
		auto keys = defaults.Keys();
		std::sort(keys.Raw().begin(), keys.Raw().end(), [](const cs::String& a, const cs::String& b) { return cs::Strings::CompareOrdinal(a, b) < 0; });
		JsonObject o;
		for (const auto& key : keys) o.Set(key, defaults[key]);
		return MiniJson::Write(o);
	}
}
