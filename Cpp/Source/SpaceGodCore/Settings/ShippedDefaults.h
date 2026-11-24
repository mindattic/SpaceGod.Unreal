#pragma once
// Shipped defaults (US-T569): a flat JSON object of setting key to the text GameSetting::Format writes, which
// SettingsRegistry::ApplyShippedDefaults lays over the declared defaults. Text in, text out: reading or writing the
// file is the caller's.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class SPACEGODCORE_API ShippedDefaults final
	{
	public:
		/// <summary>The key/text pairs of <paramref name="json"/>, whose top level must be an object. A string value is
		/// taken as it is, a bool or number as its text; any other value (null, object, array) is skipped. Throws
		/// JsonFormatException on text that is not a JSON object.</summary>
		static cs::Dictionary<cs::String, cs::String> Parse(const cs::String& json);

		/// <summary><paramref name="defaults"/> as indented JSON text, every value a string, keys in ordinal order.</summary>
		static cs::String Write(const cs::Dictionary<cs::String, cs::String>& defaults);
	};
}
