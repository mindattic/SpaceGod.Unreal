// US-T968: Light Society's save blob.

#include "LightSociety/Codec.h"

#include <bit>

namespace MindAttic::Society
{
	Writer::Writer()
	{
		U32(BlobMagic);
		U16(BlobVersion);
	}

	void Writer::F32(float v)
	{
		U32(std::bit_cast<uint32_t>(v));
	}

	void Writer::F64(double v)
	{
		U64(std::bit_cast<uint64_t>(v));
	}

	Reader::Reader(std::span<const uint8_t> blob)
		: data(blob)
	{
		if (U32() != BlobMagic) ok = false;
		version = U16();
		if (version == 0 || version > BlobVersion) ok = false;
	}

	float Reader::F32()
	{
		return std::bit_cast<float>(U32());
	}

	double Reader::F64()
	{
		return std::bit_cast<double>(U64());
	}

	bool Reader::Expect(uint32_t tag)
	{
		if (U32() != tag) ok = false;
		return ok;
	}

	uint32_t Reader::Count(size_t elementBytes, uint32_t max)
	{
		const uint32_t n = U32();
		if (!ok) return 0;
		if (n > max || (elementBytes > 0 && n > Remaining() / elementBytes))
		{
			ok = false;
			return 0;
		}
		return n;
	}
}
