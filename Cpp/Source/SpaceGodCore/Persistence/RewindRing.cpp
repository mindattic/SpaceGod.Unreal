// US-T99 (docs/REWIND.md, phase 2): the in-game keyframe ring. See RewindRing.h.

#include "Persistence/RewindRing.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <utility>

#include "Persistence/ObjectGraph.h"
#include "Persistence/SaveGame.h"

namespace SpaceGod::Core
{
	namespace
	{
		constexpr double Minute = 60, Hour = 3600, Day = 86400;

		// US-T454: the delta format. 'SGKD', version 1, varint base size, varint target size, FNV-1a 64 of each, then ops
		// until the target is whole: varint literal length, the literals, then (unless the target is done) varint copy
		// length and the zigzag varint of (copy start - where the last copy ended - the literal length).
		constexpr uint8_t KfDeltaMagic[4] = { 'S', 'G', 'K', 'D' };
		constexpr uint8_t KfDeltaVersion = 1;
		constexpr size_t KfBlock = 32;       // base blocks indexed by hash: any match of 2 * KfBlock - 1 bytes or more is found
		constexpr size_t KfRepeatMin = 8;    // a copy at the last copy's offset needs this many bytes (a value changed in place)
		constexpr size_t KfRepeatReach = 64; // how far into a literal run the last copy's offset is still tried
		constexpr uint32_t KfRollMul = 0x01000193u;

		uint64_t KfFnv(const std::vector<uint8_t>& b)
		{
			uint64_t h = 1469598103934665603ull;
			for (uint8_t c : b) h = (h ^ c) * 1099511628211ull;
			return h;
		}

		void KfPutVar(std::vector<uint8_t>& o, uint64_t v)
		{
			while (v >= 0x80) { o.push_back(uint8_t(v) | 0x80); v >>= 7; }
			o.push_back(uint8_t(v));
		}

		void KfPut64(std::vector<uint8_t>& o, uint64_t v)
		{
			for (int i = 0; i < 8; i++) o.push_back(uint8_t(v >> (8 * i)));
		}

		[[noreturn]] void KfBadDelta() { throw cs::FormatException("Rewind keyframe delta is damaged"); }

		struct KfDeltaReader
		{
			const uint8_t* p;
			const uint8_t* end;

			uint64_t Var()
			{
				uint64_t v = 0;
				for (int shift = 0; shift < 64; shift += 7)
				{
					if (p >= end) KfBadDelta();
					uint8_t c = *p++;
					v |= uint64_t(c & 0x7F) << shift;
					if ((c & 0x80) == 0) return v;
				}
				KfBadDelta();
			}

			uint64_t U64()
			{
				if (end - p < 8) KfBadDelta();
				uint64_t v = 0;
				for (int i = 0; i < 8; i++) v |= uint64_t(p[i]) << (8 * i);
				p += 8;
				return v;
			}
		};

		uint32_t KfBlockHash(const uint8_t* p)
		{
			uint32_t h = 0;
			for (size_t i = 0; i < KfBlock; i++) h = h * KfRollMul + p[i];
			return h;
		}

		uint32_t KfSlot(uint32_t h, uint32_t mask) { return (((h ^ (h >> 15)) * 0x9E3779B1u) >> 7) & mask; }

		// US-T484: the disk spill.
		namespace fs = std::filesystem;

		/// A UTF-8 path as a path (a narrow string would be read in the ANSI code page on Windows).
		fs::path KfPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

		std::string KfUtf8(const fs::path& p)
		{
			auto u = p.u8string();
			return std::string(u.begin(), u.end());
		}

		bool KfReady(const std::shared_future<std::vector<uint8_t>>& f)
		{
			return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
		}

		bool KfWrite(const std::string& path, const std::vector<uint8_t>& bytes)
		{
			std::ofstream f(KfPath(path), std::ios::binary | std::ios::trunc);
			if (!f) return false;
			f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			f.close();
			return !f.fail();
		}

		std::vector<uint8_t> KfRead(const std::string& path)
		{
			std::ifstream f(KfPath(path), std::ios::binary | std::ios::ate);
			if (!f) throw cs::IOException("Rewind keyframe spill file is missing: " + path);
			std::streamoff size = f.tellg();
			std::vector<uint8_t> bytes(size > 0 ? size_t(size) : 0);
			f.seekg(0);
			f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (size < 0 || f.gcount() != static_cast<std::streamsize>(bytes.size())) throw cs::IOException("Rewind keyframe spill file is short: " + path);
			return bytes;
		}

		void KfRemove(const std::string& path)
		{
			std::error_code ec;
			fs::remove(KfPath(path), ec);
		}
	}

	RewindBlob::~RewindBlob()
	{
		if (!SpillPath.empty()) KfRemove(SpillPath);
	}

	std::vector<uint8_t> RewindRing::Diff(const std::vector<uint8_t>& base, const std::vector<uint8_t>& target)
	{
		std::vector<uint8_t> out;
		out.reserve(target.size() / 16 + 64);
		out.insert(out.end(), KfDeltaMagic, KfDeltaMagic + 4);
		out.push_back(KfDeltaVersion);
		KfPutVar(out, base.size());
		KfPutVar(out, target.size());
		KfPut64(out, KfFnv(base));
		KfPut64(out, KfFnv(target));

		const size_t bn = base.size(), tn = target.size();
		const uint8_t* b = base.data();
		const uint8_t* t = target.data();
		constexpr uint32_t Empty = 0xFFFFFFFFu;
		uint32_t slots = 1024;
		while (slots < bn / KfBlock * 2 && slots < (1u << 30)) slots <<= 1;
		std::vector<uint32_t> table(bn >= KfBlock ? slots : 0, Empty);
		for (size_t pos = 0; bn >= KfBlock && pos + KfBlock <= bn; pos += KfBlock) table[KfSlot(KfBlockHash(b + pos), slots - 1)] = uint32_t(pos);
		uint32_t outPow = 1;
		for (size_t i = 0; i < KfBlock; i++) outPow *= KfRollMul;

		size_t lit = 0, prevEnd = 0, lastTEnd = 0;
		bool haveLast = false;
		size_t i = 0;
		uint32_t h = tn >= KfBlock ? KfBlockHash(t) : 0;
		while (i < tn)
		{
			size_t s = 0, ts = i, len = 0;
			// A value changed in place: the bytes after it match the base where the last copy left off, plus as far again.
			if (haveLast && i - lastTEnd <= KfRepeatReach)
			{
				size_t c = prevEnd + (i - lastTEnd);
				if (c + KfRepeatMin <= bn && i + KfRepeatMin <= tn && std::memcmp(b + c, t + i, KfRepeatMin) == 0) { s = c; len = KfRepeatMin; }
			}
			if (len == 0 && !table.empty() && i + KfBlock <= tn)
			{
				uint32_t c = table[KfSlot(h, slots - 1)];
				if (c != Empty && std::memcmp(b + c, t + i, KfBlock) == 0)
				{
					s = c;
					len = KfBlock;
					while (ts > lit && s > 0 && b[s - 1] == t[ts - 1]) { s--; ts--; len++; }
				}
			}
			if (len != 0)
			{
				while (ts + len < tn && s + len < bn && t[ts + len] == b[s + len]) len++;
				KfPutVar(out, ts - lit);
				out.insert(out.end(), t + lit, t + ts);
				KfPutVar(out, len);
				int64_t d = int64_t(s) - int64_t(prevEnd + (ts - lit));
				KfPutVar(out, (uint64_t(d) << 1) ^ uint64_t(d >> 63));
				prevEnd = s + len;
				i = ts + len;
				lit = lastTEnd = i;
				haveLast = true;
				if (i + KfBlock <= tn) h = KfBlockHash(t + i);
				continue;
			}
			if (i + KfBlock < tn) h = h * KfRollMul + t[i + KfBlock] - t[i] * outPow;
			i++;
		}
		if (lit < tn)
		{
			KfPutVar(out, tn - lit);
			out.insert(out.end(), t + lit, t + tn);
		}
		return out;
	}

	std::vector<uint8_t> RewindRing::Patch(const std::vector<uint8_t>& base, const uint8_t* delta, size_t size)
	{
		if (size < 5 || std::memcmp(delta, KfDeltaMagic, 4) != 0 || delta[4] != KfDeltaVersion) KfBadDelta();
		KfDeltaReader r{ delta + 5, delta + size };
		uint64_t baseSize = r.Var(), targetSize = r.Var(), baseHash = r.U64(), targetHash = r.U64();
		if (baseSize != base.size() || baseHash != KfFnv(base))
			throw cs::InvalidOperationException("Rewind keyframe delta applied to the wrong base keyframe");
		std::vector<uint8_t> out;
		out.reserve(size_t(targetSize));
		size_t prevEnd = 0;
		while (out.size() < targetSize)
		{
			uint64_t lit = r.Var();
			if (lit > targetSize - out.size() || lit > uint64_t(r.end - r.p)) KfBadDelta();
			out.insert(out.end(), r.p, r.p + lit);
			r.p += lit;
			if (out.size() == targetSize) break;
			uint64_t len = r.Var(), z = r.Var();
			int64_t d = int64_t(z >> 1) ^ -int64_t(z & 1);
			int64_t start = int64_t(prevEnd + lit) + d;
			if (len == 0 || start < 0 || uint64_t(start) + len > base.size() || len > targetSize - out.size()) KfBadDelta();
			out.insert(out.end(), base.begin() + start, base.begin() + start + int64_t(len));
			prevEnd = size_t(start) + size_t(len);
		}
		if (KfFnv(out) != targetHash) KfBadDelta();
		return out;
	}

	std::vector<uint8_t> RewindRing::Decode(const RewindBlob& blob)
	{
		// US-T484: the simulation thread may spill this blob (drop Packed) while a worker decodes it: take one or the
		// other under its lock. The file lives as long as the blob, which the caller holds.
		std::shared_future<std::vector<uint8_t>> mine;
		std::string path;
		{
			std::lock_guard<std::mutex> lock(blob.Lock);
			if (blob.Spilled) path = blob.SpillPath;
			else mine = blob.Packed;
		}
		std::vector<uint8_t> fromDisk;
		if (!path.empty()) fromDisk = KfRead(path);
		const std::vector<uint8_t>& packed = path.empty() ? mine.get() : fromDisk;
		std::vector<uint8_t> bytes = SaveGame::Inflate(packed.data(), packed.size());
		if (blob.Base == nullptr) return bytes;
		return Patch(Decode(*blob.Base), bytes.data(), bytes.size());
	}

	const std::vector<RewindRing::Tier>& RewindRing::Schedule()
	{
		static const std::vector<Tier> tiers = {
			{ Hour, Minute },
			{ Day, 10 * Minute },
			{ 7 * Day, Hour },
			{ 90 * Day, Day },
		};
		return tiers;
	}

	double RewindRing::SpacingFor(double ageS, bool battle)
	{
		if (battle && ageS <= BattleTier.AgeS) return BattleTier.SpacingS;
		for (const Tier& t : Schedule())
			if (ageS <= t.AgeS) return t.SpacingS;
		return 7 * Day;
	}

	std::vector<uint8_t> RewindRing::Pack(const std::vector<uint8_t>& graph) { return SaveGame::Deflate(graph); }

	const RewindKeyframe* RewindRing::Newest() const
	{
		if (head >= 0)
			if (const RewindKeyframe* h = Find(head)) return h;
		return frames.empty() ? nullptr : frames.back().get();
	}

	bool RewindRing::Due(double sessionS, double wallS, bool battle) const
	{
		const RewindKeyframe* last = Newest();
		if (last == nullptr) return true;
		double wallSince = head >= 0 ? wallS - std::max(last->WallS, headWallS) : wallS - last->WallS;
		if (wallSince < MinWallGapS) return false;
		return sessionS - last->SessionS >= SpacingFor(0, battle);
	}

	const RewindKeyframe& RewindRing::Capture(const cs::Ref<SaveData>& data, double sessionS, double jd, double wallS, bool battle)
	{
		// One timeline (the owner's call, 2026-11-05): play going on after a rewind replaces the future that was ahead.
		if (head >= 0)
		{
			if (const RewindKeyframe* h = Find(head))
			{
				double at = h->SessionS;
				frames.erase(std::remove_if(frames.begin(), frames.end(), [at](const std::shared_ptr<RewindKeyframe>& k) { return k->SessionS > at && !k->Pinned; }), frames.end());
			}
			head = -1;
		}
		// Never two keyframes at one moment (a capture right after a rewind, before the clock moved).
		frames.erase(std::remove_if(frames.begin(), frames.end(), [sessionS](const std::shared_ptr<RewindKeyframe>& k) { return k->SessionS >= sessionS && !k->Pinned; }), frames.end());

		const std::shared_ptr<RewindKeyframe> parent = frames.empty() ? nullptr : frames.back();
		auto k = std::make_shared<RewindKeyframe>();
		k->Id = nextId++;
		k->SessionS = sessionS;
		k->Jd = jd;
		k->WallS = wallS;
		k->Battle = battle;
		k->Pinned = frames.empty(); // the session start is always kept, whole
		auto started = std::chrono::steady_clock::now();
		cs::List<uint8_t> graph = ObjectGraph::ToBytes(data);
		k->CaptureMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		k->RawBytes = graph.Raw().size();
		auto raw = std::make_shared<const std::vector<uint8_t>>(std::move(graph.Raw()));
		// US-T454: a delta against the parent, unless this starts the session or a new chain. The walk has to happen here,
		// on the simulation thread; the compression does not.
		auto blob = std::make_shared<RewindBlob>();
		blob->Estimate = k->RawBytes;
		if (parent != nullptr && parent->Blob != nullptr && parent->Blob->Depth + 1 < WholeEvery)
		{
			k->DeltaOf = parent->Id;
			blob->Base = parent->Blob;
			blob->Depth = parent->Blob->Depth + 1;
			blob->Estimate = parent->StoredBytes() / 8;
			// The parent's graph is usually the one held raw (the newest, or the one just rewound to); else decode it there.
			std::shared_ptr<const std::vector<uint8_t>> known = lastBlob == parent->Blob ? lastRaw : nullptr;
			blob->Packed = std::async(std::launch::async, [raw, known, baseBlob = parent->Blob]() mutable
			{
				std::vector<uint8_t> delta = Diff(known != nullptr ? *known : Decode(*baseBlob), *raw);
				known.reset();
				baseBlob.reset();
				raw.reset();
				return Pack(delta);
			}).share();
		}
		else
			blob->Packed = std::async(std::launch::async, [raw]() { return Pack(*raw); }).share();
		k->Blob = blob;
		lastRaw = raw;
		lastBlob = blob;
		frames.push_back(k);
		Relink();
		Thin(sessionS);
		return *k;
	}

	const RewindKeyframe* RewindRing::Before(double sessionS) const
	{
		for (auto it = frames.rbegin(); it != frames.rend(); ++it)
			if ((*it)->SessionS < sessionS) return it->get();
		return nullptr;
	}

	const RewindKeyframe* RewindRing::Back(double sessionS, double backS) const
	{
		for (auto it = frames.rbegin(); it != frames.rend(); ++it)
			if ((*it)->SessionS <= sessionS - backS) return it->get();
		return frames.empty() ? nullptr : frames.front().get();
	}

	const RewindKeyframe* RewindRing::Find(int id) const
	{
		for (const auto& k : frames)
			if (k->Id == id) return k.get();
		return nullptr;
	}

	cs::Ref<SaveData> RewindRing::Restore(int id, double wallS)
	{
		const RewindKeyframe* k = Find(id);
		if (k == nullptr) return nullptr;
		if (k == nullptr || k->Blob == nullptr) return nullptr;
		auto graph = std::make_shared<const std::vector<uint8_t>>(Decode(*k->Blob));
		cs::Ref<SaveData> data = ObjectGraph::Read(graph->data(), graph->size()).As<SaveData>();
		if (data == nullptr) return nullptr;
		// US-T454: the next keyframe (play going on from here) is a delta against this one.
		lastRaw = graph;
		lastBlob = k->Blob;
		head = id;
		headWallS = wallS;
		return data;
	}

	void RewindRing::Clear()
	{
		frames.clear();
		head = -1;
		headWallS = 0;
		lastRaw = nullptr;
		lastBlob = nullptr;
	}

	size_t RewindRing::Bytes() const
	{
		size_t total = 0;
		for (const auto& k : frames)
			if (!k->OnDisk()) total += k->StoredBytes();
		return total + (lastRaw != nullptr ? lastRaw->size() : 0); // US-T454: the dictionary held raw counts too
	}

	size_t RewindRing::DiskBytes() const
	{
		size_t total = 0;
		for (const auto& k : frames)
			if (k->OnDisk()) total += k->StoredBytes();
		return total;
	}

	RewindRing::~RewindRing()
	{
		frames.clear();
		lastBlob = nullptr;
		lastRaw = nullptr;
		for (SpillJob& j : spills)
		{
			if (j.Done.valid()) j.Done.wait();
			KfRemove(j.Path);
		}
		spills.clear();
		if (!runFolder.empty())
		{
			std::error_code ec;
			fs::remove_all(KfPath(runFolder), ec);
		}
	}

	bool RewindRing::EnsureRunFolder()
	{
		if (!runFolder.empty()) return true;
		try
		{
			fs::path root = KfPath(SpillFolder);
			std::error_code ec;
			fs::create_directories(root, ec);
			if (ec) throw fs::filesystem_error("Rewind spill folder", root, ec);
			// Run folders a day old are left from runs that ended without cleaning up (a crash, a kill).
			auto now = fs::file_time_type::clock::now();
			for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
			{
				std::error_code ec2;
				if (!it->is_directory(ec2) || it->path().filename().u8string().rfind(u8"run-", 0) != 0) continue;
				auto at = fs::last_write_time(it->path(), ec2);
				if (!ec2 && now - at > std::chrono::hours(24)) fs::remove_all(it->path(), ec2);
			}
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			std::random_device rd;
			fs::path run = root / (std::string("run-") + std::to_string(ms) + "-" + std::to_string(rd() & 0xFFFF));
			if (!fs::create_directory(run, ec) || ec) throw fs::filesystem_error("Rewind run folder", run, ec);
			runFolder = KfUtf8(run);
			return true;
		}
		catch (const std::exception&)
		{
			spillFailed = true;
			return false;
		}
	}

	void RewindRing::CollectSpills()
	{
		for (auto it = spills.begin(); it != spills.end();)
		{
			if (it->Done.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				++it;
				continue;
			}
			bool ok = false;
			try { ok = it->Done.get(); }
			catch (const std::exception&) { ok = false; }
			std::shared_ptr<const RewindBlob> blob = it->Blob.lock();
			if (ok && blob != nullptr)
			{
				blob->PackedSize = it->Bytes;
				std::lock_guard<std::mutex> lock(blob->Lock);
				blob->SpillPath = it->Path;
				blob->Spilled = true;
				blob->Packed = {};
			}
			else
			{
				KfRemove(it->Path);
				if (!ok) spillFailed = true; // the disk is full or gone: RAM thinning takes over
			}
			it = spills.erase(it);
		}
	}

	bool RewindRing::Spill()
	{
		size_t pending = 0;
		for (const SpillJob& j : spills) pending += j.Bytes;
		size_t ram = Bytes();
		ram -= std::min(ram, pending); // being written: on its way out of RAM
		if (ram <= BudgetBytes) return true;
		if (!Spilling() || !EnsureRunFolder()) return false;
		size_t disk = DiskBytes() + pending;
		bool waiting = false;
		// The oldest first (furthest back, least likely to be restored); never the newest, the next delta's parent.
		for (size_t i = 0; i + 1 < frames.size() && ram > BudgetBytes; i++)
		{
			const std::shared_ptr<const RewindBlob>& blob = frames[i]->Blob;
			if (blob == nullptr || blob->Spilled) continue;
			bool writing = false;
			for (const SpillJob& j : spills)
				if (j.Blob.lock() == blob) { writing = true; break; }
			if (writing) continue;
			if (!KfReady(blob->Packed))
			{
				waiting = true; // still compressing: it can spill at a later pass
				continue;
			}
			size_t size = frames[i]->StoredBytes();
			if (disk + size > DiskBudgetBytes) return false;
			SpillJob job;
			job.Blob = blob;
			job.Path = KfUtf8(KfPath(runFolder) / (std::string("kf-") + std::to_string(nextSpill++) + ".bin"));
			job.Bytes = size;
			job.Done = std::async(std::launch::async, [packed = blob->Packed, path = job.Path]() { return KfWrite(path, packed.get()); });
			spills.push_back(std::move(job));
			ram -= std::min(ram, size);
			disk += size;
		}
		return ram <= BudgetBytes || waiting;
	}

	bool RewindRing::ThinPass(double nowSessionS, double scale)
	{
		if (frames.size() <= 2) return false;
		// Oldest first, so what was kept further back does not churn as new keyframes arrive: a keyframe stays while it is
		// at least its age's spacing after the last one kept. The start, the newest, pinned keyframes and the head stay.
		std::vector<std::shared_ptr<RewindKeyframe>> kept;
		kept.reserve(frames.size());
		kept.push_back(frames.front());
		for (size_t i = 1; i + 1 < frames.size(); i++)
		{
			const auto& k = frames[i];
			double spacing = SpacingFor(nowSessionS - k->SessionS, k->Battle) * scale;
			if (k->Pinned || k->Id == head || k->SessionS - kept.back()->SessionS >= spacing * 0.999) kept.push_back(k);
		}
		kept.push_back(frames.back());
		bool dropped = kept.size() != frames.size();
		frames = std::move(kept);
		return dropped;
	}

	void RewindRing::Thin(double nowSessionS)
	{
		ThinPass(nowSessionS, 1);
		// US-T484: over the RAM budget the oldest keyframes go to disk first. Only when nothing more can spill (spilling is
		// off or failed, or what is left is the newest) or the disk budget is spent is everything thinned further (never cut
		// off: the start stays, so the horizon is always the whole session).
		CollectSpills();
		for (double scale = 2; scale <= 1 << 20; scale *= 2)
		{
			bool ramOver = !Spill(); // never starts a write past the disk budget
			bool diskOver = DiskBytes() > DiskBudgetBytes;
			if (!ramOver && !diskOver) break;
			if (!ThinPass(nowSessionS, scale) && frames.size() <= 2) break;
		}
		Relink();
		Rebase();
	}

	void RewindRing::Rebase()
	{
		for (size_t i = 1; i < frames.size(); i++)
		{
			RewindKeyframe& k = *frames[i];
			const RewindKeyframe& parent = *frames[i - 1];
			if (k.Whole() || k.DeltaOf == parent.Id || k.Blob == nullptr || parent.Blob == nullptr) continue;
			// Thinning dropped its base: rebuild its graph through the old chain (kept alive by the old blob) and encode it
			// against its new parent, or whole when that would make the chain too long.
			auto blob = std::make_shared<RewindBlob>();
			blob->Estimate = k.StoredBytes() * 2;
			bool whole = parent.Blob->Depth + 1 >= WholeEvery;
			if (!whole)
			{
				blob->Base = parent.Blob;
				blob->Depth = parent.Blob->Depth + 1;
			}
			blob->Packed = std::async(std::launch::async, [old = k.Blob, base = blob->Base]() mutable
			{
				std::vector<uint8_t> raw = Decode(*old);
				old.reset();
				if (base == nullptr) return Pack(raw);
				std::vector<uint8_t> delta = Diff(Decode(*base), raw);
				base.reset();
				return Pack(delta);
			}).share();
			if (lastBlob == k.Blob) lastBlob = blob;
			k.DeltaOf = whole ? -1 : parent.Id;
			k.Blob = blob;
		}
	}

	void RewindRing::Relink()
	{
		for (size_t i = 0; i < frames.size(); i++) frames[i]->Parent = i == 0 ? -1 : frames[i - 1]->Id;
	}
}
