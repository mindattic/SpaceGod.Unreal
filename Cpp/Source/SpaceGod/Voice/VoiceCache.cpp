#include "Voice/VoiceCache.h"

#include "SpaceGodLog.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <cstring>
#include <vector>

namespace SpaceGod::Voice
{
	namespace
	{
		/// Bytes in the folder as last counted or written; worker thread only.
		int64 DiskHeld = 0;

		std::uint32_t U32(const uint8* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
		std::uint16_t U16(const uint8* p) { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
		void Put32(TArray<uint8>& out, std::uint32_t v) { out.Append(reinterpret_cast<const uint8*>(&v), 4); }
		void Put16(TArray<uint8>& out, std::uint16_t v) { out.Append(reinterpret_cast<const uint8*>(&v), 2); }

		bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

		void Mix(std::uint64_t& h, const cs::String& part)
		{
			for (const char c : part)
			{
				h ^= static_cast<std::uint8_t>(c);
				h *= 1099511628211ull;
			}
			// A separator, so "ab"+"c" and "a"+"bc" differ.
			h ^= 0xFFu;
			h *= 1099511628211ull;
		}

		FString Dir()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoiceCache")));
		}

		FString PathOf(VoiceKey key)
		{
			return FPaths::Combine(Dir(), FString::Printf(TEXT("%016llx.wav"), static_cast<unsigned long long>(key)));
		}
	}

	cs::String VoiceCache::Normalise(const cs::String& text)
	{
		cs::String out;
		out.reserve(text.size());
		bool gap = false;
		for (const char c : text)
		{
			if (IsSpace(c))
			{
				gap = !out.empty();
				continue;
			}
			if (gap) out.push_back(' ');
			gap = false;
			out.push_back(c);
		}
		return out;
	}

	cs::String VoiceCache::EngineIdOf(ITtsEngine& engine)
	{
		engine.Available();
		return cs::String(engine.Name()) + "/" + engine.Version();
	}

	VoiceKey VoiceCache::KeyOf(const cs::String& engineId, const TtsRequest& request)
	{
		if (engineId.empty()) return 0;
		std::uint64_t h = 14695981039346656037ull;
		Mix(h, engineId);
		Mix(h, request.Voice.EngineVoice);
		Mix(h, TCHAR_TO_UTF8(*FString::FromInt(request.Voice.SpeakerId)));
		Mix(h, TCHAR_TO_UTF8(*FString::Printf(TEXT("%.4f"), request.Voice.LengthScale)));
		Mix(h, TCHAR_TO_UTF8(*FString::FromInt(request.Effect ? EffectVersion : 0)));
		Mix(h, Normalise(request.Text));
		return h ? h : 1;
	}

	bool VoiceCache::Find(VoiceKey key, Audio::SoundClip& clip)
	{
		const Entry* entry = Clips.Find(key);
		if (!entry) return false;
		clip = entry->Clip;
		Order.Remove(key);
		Order.Add(key);
		return true;
	}

	void VoiceCache::Put(VoiceKey key, const Audio::SoundClip& clip)
	{
		if (!key || !clip.IsValid()) return;
		const int64 bytes = static_cast<int64>(clip.Samples->size()) * 2;
		if (bytes > MemoryBytes) return;
		if (const Entry* old = Clips.Find(key))
		{
			Held -= old->Bytes;
			Order.Remove(key);
		}
		Clips.Add(key, Entry { clip, bytes });
		Order.Add(key);
		Held += bytes;
		while (Held > MemoryBytes && Order.Num() > 0)
		{
			const VoiceKey oldest = Order[0];
			Order.RemoveAt(0);
			if (Entry dropped; Clips.RemoveAndCopyValue(oldest, dropped)) Held -= dropped.Bytes;
		}
	}

	void VoiceCache::Clear()
	{
		Clips.Reset();
		Order.Reset();
		Held = 0;
	}

	bool VoiceCache::ReadWav(const TArray<uint8>& bytes, Audio::SoundClip& clip)
	{
		const int32 n = bytes.Num();
		const uint8* b = bytes.GetData();
		if (n < 12 || std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0) return false;
		int format = 0, channels = 0, bits = 0, rate = 0;
		int64 pos = 12;
		while (pos + 8 <= n)
		{
			const std::uint32_t size = U32(b + pos + 4);
			const int64 body = pos + 8;
			if (std::memcmp(b + pos, "fmt ", 4) == 0)
			{
				if (size < 16 || body + 16 > n) return false;
				format = U16(b + body);
				channels = U16(b + body + 2);
				rate = static_cast<int>(U32(b + body + 4));
				bits = U16(b + body + 14);
			}
			else if (std::memcmp(b + pos, "data", 4) == 0)
			{
				if (format != 1 || channels != 1 || bits != 16 || rate <= 0 || body + size > n) return false;
				auto samples = std::make_shared<std::vector<std::int16_t>>(size / 2);
				if (!samples->empty()) std::memcpy(samples->data(), b + body, samples->size() * 2);
				clip.Samples = samples;
				clip.SampleRate = rate;
				return true;
			}
			pos = body + size + (size & 1);
		}
		return false;
	}

	bool VoiceCache::Load(VoiceKey key, Audio::SoundClip& clip)
	{
		if (!key) return false;
		const FString path = PathOf(key);
		TArray<uint8> bytes;
		if (!FFileHelper::LoadFileToArray(bytes, *path, FILEREAD_Silent)) return false;
		Audio::SoundClip read;
		if (!ReadWav(bytes, read) || !read.IsValid())
		{
			IFileManager::Get().Delete(*path, false, false, true);
			return false;
		}
		read.Name = "voice-cache";
		clip = read;
		IFileManager::Get().SetTimeStamp(*path, FDateTime::UtcNow());
		return true;
	}

	void VoiceCache::Store(VoiceKey key, const Audio::SoundClip& clip)
	{
		if (!key || !clip.IsValid()) return;
		const std::uint32_t data = static_cast<std::uint32_t>(clip.Samples->size() * 2);
		TArray<uint8> bytes;
		bytes.Reserve(44 + data);
		bytes.Append(reinterpret_cast<const uint8*>("RIFF"), 4);
		Put32(bytes, 36 + data);
		bytes.Append(reinterpret_cast<const uint8*>("WAVEfmt "), 8);
		Put32(bytes, 16);
		Put16(bytes, 1);
		Put16(bytes, 1);
		Put32(bytes, static_cast<std::uint32_t>(clip.SampleRate));
		Put32(bytes, static_cast<std::uint32_t>(clip.SampleRate * 2));
		Put16(bytes, 2);
		Put16(bytes, 16);
		bytes.Append(reinterpret_cast<const uint8*>("data"), 4);
		Put32(bytes, data);
		bytes.Append(reinterpret_cast<const uint8*>(clip.Samples->data()), data);

		const FString path = PathOf(key);
		const FString temp = path + TEXT(".tmp");
		IFileManager::Get().MakeDirectory(*Dir(), true);
		if (!FFileHelper::SaveArrayToFile(bytes, *temp) || !IFileManager::Get().Move(*path, *temp, true, true, false, true))
		{
			IFileManager::Get().Delete(*temp, false, false, true);
			UE_LOG(LogSpaceGod, Warning, TEXT("[Voice] Could not write the cached line %s."), *path);
			return;
		}
		DiskHeld += bytes.Num();
		if (DiskHeld > DiskBytes) Prune();
	}

	void VoiceCache::Prune()
	{
		struct File
		{
			FString Path;
			FDateTime Time;
			int64 Size = 0;
		};
		TArray<File> files;
		int64 total = 0;
		IFileManager::Get().IterateDirectoryStat(*Dir(), [&](const TCHAR* path, const FFileStatData& stat)
		{
			if (!stat.bIsDirectory)
			{
				files.Add(File { path, stat.ModificationTime, stat.FileSize });
				total += stat.FileSize;
			}
			return true;
		});
		if (total > DiskBytes)
		{
			files.Sort([](const File& a, const File& b) { return a.Time < b.Time; });
			int removed = 0;
			for (const File& file : files)
			{
				if (total <= DiskBytes) break;
				if (IFileManager::Get().Delete(*file.Path, false, false, true))
				{
					total -= file.Size;
					++removed;
				}
			}
			UE_LOG(LogSpaceGod, Display, TEXT("[Voice] Pruned %d cached lines; %lld bytes remain."), removed, total);
		}
		DiskHeld = total;
	}
}
