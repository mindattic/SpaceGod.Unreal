#pragma once
// US-T617: how a date and a time of day are written on the HUD and the screens. The caller passes the clock and the date
// order (the game module reads them from the player's Gameplay options); the first of each is the game's original form.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	enum class ClockFormat : int { TwentyFourHour = 0, TwelveHour = 1 };
	enum class DateOrder : int { YearMonthDay = 0, DayMonthYear = 1, MonthDayYear = 2 };

	class SPACEGODCORE_API DateText final
	{
	public:
		/// "2026-11-24", "24/11/2026" or "11/24/2026".
		static cs::String Date(const cs::DateTime& t, DateOrder order);
		/// "14:05" or "2:05 PM"; with seconds "14:05:09" or "2:05:09 PM".
		static cs::String Time(const cs::DateTime& t, ClockFormat clock, bool seconds = false);
		/// The date, the gap, then the time.
		static cs::String DateAndTime(const cs::DateTime& t, DateOrder order, ClockFormat clock, bool seconds = false, const char* gap = " ");
		/// The month by name: "Nov 24" or "24 Nov" (day-first order), the day padded to two digits when padDay; with the year,
		/// "2026 Nov 24", "24 Nov 2026" or "Nov 24 2026".
		static cs::String MonthDay(const cs::DateTime& t, DateOrder order, bool year = false, bool padDay = true);
	};
}
