#pragma once
// US-T968: Light Society's save blob. Little-endian whatever the host, a magic and version header, then sections each
// part of the engine writes in turn (the agent store now; graph, queue, models and readout later). The reader is
// robust to truncated or corrupt input: a failed read sets a sticky error and returns zero, and counts are checked
// against the bytes left before anything is allocated.

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace MindAttic::Society
{
	constexpr uint32_t FourCC(char a, char b, char c, char d)
	{
		return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
			| (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
	}

	inline constexpr uint32_t BlobMagic = FourCC('L', 'S', 'O', 'C');
	/// <summary>The blob version this build writes; it reads this one and every older one.</summary>
	inline constexpr uint16_t BlobVersion = 1;

	class Writer
	{
	public:
		/// <summary>Starts a blob with its header.</summary>
		Writer();

		void U8(uint8_t v) { Put(v); }
		void U16(uint16_t v) { Put(v); }
		void U32(uint32_t v) { Put(v); }
		void U64(uint64_t v) { Put(v); }
		void I8(int8_t v) { Put(v); }
		void I16(int16_t v) { Put(v); }
		void I32(int32_t v) { Put(v); }
		void F32(float v);
		void F64(double v);
		void Bool(bool v) { Put(static_cast<uint8_t>(v ? 1 : 0)); }

		/// <summary>A u32 count, then each value.</summary>
		template <class T>
		void Array(std::span<const T> values)
		{
			static_assert(std::is_integral_v<T>);
			U32(static_cast<uint32_t>(values.size()));
			for (const T v : values) Put(v);
		}

		const std::vector<uint8_t>& Bytes() const { return bytes; }
		std::vector<uint8_t> Take() { return std::move(bytes); }

	private:
		template <class T>
		void Put(T v)
		{
			const uint64_t u = static_cast<uint64_t>(static_cast<std::make_unsigned_t<T>>(v));
			for (size_t i = 0; i < sizeof(T); i++) bytes.push_back(static_cast<uint8_t>(u >> (8 * i)));
		}

		std::vector<uint8_t> bytes;
	};

	class Reader
	{
	public:
		/// <summary>Reads the header; <see cref="Ok"/> is false on a bad magic, a newer version or a short blob.</summary>
		explicit Reader(std::span<const uint8_t> blob);

		bool Ok() const { return ok; }
		void Fail() { ok = false; }
		uint16_t Version() const { return version; }
		size_t Remaining() const { return ok ? data.size() - pos : 0; }
		bool AtEnd() const { return ok && pos == data.size(); }

		uint8_t U8() { return Get<uint8_t>(); }
		uint16_t U16() { return Get<uint16_t>(); }
		uint32_t U32() { return Get<uint32_t>(); }
		uint64_t U64() { return Get<uint64_t>(); }
		int8_t I8() { return Get<int8_t>(); }
		int16_t I16() { return Get<int16_t>(); }
		int32_t I32() { return Get<int32_t>(); }
		float F32();
		double F64();
		bool Bool() { return Get<uint8_t>() != 0; }

		/// <summary>Reads a u32 tag and fails unless it is <paramref name="tag"/>.</summary>
		bool Expect(uint32_t tag);

		/// <summary>
		/// Reads a u32 count of elements each at least <paramref name="elementBytes"/> long; fails (and returns 0) when
		/// it is over <paramref name="max"/> or more than the bytes left could hold.
		/// </summary>
		uint32_t Count(size_t elementBytes, uint32_t max);

		/// <summary>Reads what <see cref="Writer::Array"/> wrote, at most <paramref name="max"/> values.</summary>
		template <class T>
		bool Array(std::vector<T>& out, uint32_t max)
		{
			static_assert(std::is_integral_v<T>);
			const uint32_t n = Count(sizeof(T), max);
			if (!ok) return false;
			out.resize(n);
			for (T& v : out) v = Get<T>();
			return ok;
		}

	private:
		template <class T>
		T Get()
		{
			if (!ok || data.size() - pos < sizeof(T))
			{
				ok = false;
				return T{};
			}
			uint64_t u = 0;
			for (size_t i = 0; i < sizeof(T); i++) u |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
			pos += sizeof(T);
			return static_cast<T>(static_cast<std::make_unsigned_t<T>>(u));
		}

		std::span<const uint8_t> data;
		size_t pos = 0;
		uint16_t version = 0;
		bool ok = true;
	};
}
