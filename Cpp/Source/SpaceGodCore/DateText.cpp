// US-T617: date and time text; see DateText.h.

#include "DateText.h"

namespace SpaceGod::Core
{
	cs::String DateText::Date(const cs::DateTime& t, DateOrder order)
	{
		switch (order)
		{
		case DateOrder::DayMonthYear: return t.ToString("dd/MM/yyyy");
		case DateOrder::MonthDayYear: return t.ToString("MM/dd/yyyy");
		default: return t.ToString("yyyy-MM-dd");
		}
	}

	cs::String DateText::Time(const cs::DateTime& t, ClockFormat clock, bool seconds)
	{
		if (clock == ClockFormat::TwelveHour) return t.ToString(seconds ? "h:mm:ss tt" : "h:mm tt");
		return t.ToString(seconds ? "HH:mm:ss" : "HH:mm");
	}

	cs::String DateText::DateAndTime(const cs::DateTime& t, DateOrder order, ClockFormat clock, bool seconds, const char* gap)
	{
		return cs::Cat(Date(t, order), gap, Time(t, clock, seconds));
	}

	cs::String DateText::MonthDay(const cs::DateTime& t, DateOrder order, bool year, bool padDay)
	{
		cs::String day = t.ToString(padDay ? "dd" : "%d");
		cs::String month = t.ToString("MMM");
		if (order == DateOrder::DayMonthYear) return year ? cs::Cat(day, " ", month, " ", t.ToString("yyyy")) : cs::Cat(day, " ", month);
		if (!year) return cs::Cat(month, " ", day);
		return order == DateOrder::MonthDayYear ? cs::Cat(month, " ", day, " ", t.ToString("yyyy")) : cs::Cat(t.ToString("yyyy"), " ", month, " ", day);
	}
}
