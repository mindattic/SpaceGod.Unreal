// US-T474 (docs/REWIND.md): the intervention log. See InterventionLog.h.

#include "Persistence/InterventionLog.h"

#include <charconv>
#include <string_view>
#include <system_error>

#include "Session/CommandCodec.h"
#include "Session/Commands.h"

namespace SpaceGod::Core
{
	namespace
	{
		bool KindOf(char c, InterventionKind& kind)
		{
			switch (c)
			{
			case 'c': kind = InterventionKind::Control; return true;
			case 'm': kind = InterventionKind::Command; return true;
			case 'o': kind = InterventionKind::Order; return true;
			case 'k': kind = InterventionKind::Clock; return true;
			default: return false;
			}
		}

		// The next space-ended field of s from at (moved past the space, or to the end when there is none).
		std::string_view Field(std::string_view s, size_t& at)
		{
			size_t space = s.find(' ', at);
			std::string_view f = s.substr(at, space == std::string_view::npos ? std::string_view::npos : space - at);
			at = space == std::string_view::npos ? s.size() : space + 1;
			return f;
		}

		template <typename T>
		bool ParseWhole(std::string_view s, T& out)
		{
			if (s.empty()) return false;
			auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
			return ec == std::errc() && end == s.data() + s.size();
		}

		void AppendLine(cs::String& out, long long delta, const InterventionEntry& e)
		{
			out += std::to_string(delta);
			out += ' ';
			out += InterventionLog::KindLetter(e.Kind);
			out += ' ';
			out += std::to_string(e.Seat);
			if (!e.Text.empty())
			{
				out += ' ';
				out += e.Text;
			}
			out += '\n';
		}
	}

	char InterventionLog::KindLetter(InterventionKind kind)
	{
		switch (kind)
		{
		case InterventionKind::Control: return 'c';
		case InterventionKind::Command: return 'm';
		case InterventionKind::Order: return 'o';
		case InterventionKind::Clock: return 'k';
		}
		return 'm';
	}

	bool InterventionLog::Add(long long tick, InterventionKind kind, int seat, const cs::String& text)
	{
		if (!entries.empty() && tick < entries.back().Tick)
			throw cs::ArgumentException(cs::Cat("Tick ", cs::Str(tick), " is before the log's last (", cs::Str(entries.back().Tick), ")"));
		if (text.find_first_of("\r\n") != cs::String::npos)
			throw cs::ArgumentException("An intervention's text must be one line");
		if (kind == InterventionKind::Control)
		{
			auto last = lastControl.find(seat);
			if (last != lastControl.end() && last->second == text) return false; // a stick stays where it is put
			lastControl[seat] = text;
		}
		entries.push_back(InterventionEntry{ tick, kind, seat, text });
		return true;
	}

	bool InterventionLog::AddCommand(long long tick, const cs::Ref<IGameCommand>& command)
	{
		cs::String line = CommandCodec::Write(command);
		InterventionKind kind = cs::As<SeatControlCommand>(command) ? InterventionKind::Control : InterventionKind::Command;
		return Add(tick, kind, command->Player().Seat, line);
	}

	cs::Ref<IGameCommand> InterventionLog::CommandOf(const InterventionEntry& entry) { return CommandCodec::Read(entry.Text); }

	void InterventionLog::DropFrom(long long tick)
	{
		size_t keep = entries.size();
		while (keep > 0 && entries[keep - 1].Tick >= tick) keep--;
		if (keep == entries.size()) return;
		entries.resize(keep);
		RebuildLastControl();
	}

	void InterventionLog::DropBefore(long long tick)
	{
		// Each seat's last control frame before tick stays: it is the stick's position at the horizon.
		std::map<int, size_t> lastBefore;
		size_t cut = 0;
		for (; cut < entries.size() && entries[cut].Tick < tick; cut++)
			if (entries[cut].Kind == InterventionKind::Control) lastBefore[entries[cut].Seat] = cut;
		if (cut == 0) return;
		std::vector<InterventionEntry> kept;
		kept.reserve(lastBefore.size() + entries.size() - cut);
		for (size_t i = 0; i < cut; i++)
		{
			const InterventionEntry& e = entries[i];
			if (e.Kind == InterventionKind::Control && lastBefore[e.Seat] == i) kept.push_back(e);
		}
		for (size_t i = cut; i < entries.size(); i++) kept.push_back(std::move(entries[i]));
		entries = std::move(kept);
		// The last control frame per seat is unchanged: whichever was last is still there.
	}

	void InterventionLog::Clear()
	{
		entries.clear();
		lastControl.clear();
	}

	void InterventionLog::RebuildLastControl()
	{
		lastControl.clear();
		for (const InterventionEntry& e : entries)
			if (e.Kind == InterventionKind::Control) lastControl[e.Seat] = e.Text;
	}

	cs::String InterventionLog::Write() const
	{
		cs::String out;
		long long previous = 0;
		for (const InterventionEntry& e : entries)
		{
			AppendLine(out, e.Tick - previous, e);
			previous = e.Tick;
		}
		return out;
	}

	InterventionLog InterventionLog::Read(const cs::String& text)
	{
		InterventionLog log;
		std::string_view all(text);
		long long tick = 0;
		size_t start = 0;
		while (start < all.size())
		{
			size_t end = all.find('\n', start);
			std::string_view line = all.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
			start = end == std::string_view::npos ? all.size() : end + 1;
			if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
			if (line.empty()) continue;

			size_t at = 0;
			long long delta = 0;
			int seat = 0;
			InterventionKind kind;
			std::string_view kindField;
			if (!ParseWhole(Field(line, at), delta) || delta < 0 || (kindField = Field(line, at)).size() != 1
				|| !KindOf(kindField[0], kind) || !ParseWhole(Field(line, at), seat))
				throw cs::FormatException(cs::Cat("Not an intervention: '", cs::String(line), "'"));
			tick += delta;
			InterventionEntry e{ tick, kind, seat, cs::String(line.substr(at)) };
			// Pushed as written: a written log already dropped its repeats, and Read must give back what Write had.
			log.entries.push_back(std::move(e));
		}
		log.RebuildLastControl();
		return log;
	}

	size_t InterventionLog::ByteSize() const
	{
		size_t bytes = 0;
		long long previous = 0;
		for (const InterventionEntry& e : entries)
		{
			bytes += std::to_string(e.Tick - previous).size() + 1 + 1 + 1 + std::to_string(e.Seat).size()
				+ (e.Text.empty() ? 0 : 1 + e.Text.size()) + 1;
			previous = e.Tick;
		}
		return bytes;
	}
}
