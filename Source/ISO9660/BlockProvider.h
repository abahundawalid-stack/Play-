#pragma once

#include <memory>
#include <cassert>
#include "Types.h"
#include "Stream.h"
#include <stdexcept>

namespace ISO9660
{
	class CBlockProvider
	{
	public:
		enum BLOCKSIZE
		{
			BLOCKSIZE = 0x800ULL
		};

		virtual ~CBlockProvider() = default;
		virtual void ReadBlock(uint32, void*) = 0;
		virtual void ReadMediaBlock(uint32, void*) = 0;
		virtual uint32 GetBlockCount() = 0;
		virtual uint32 GetMediaBlockSize() const = 0;
	};
	
    class CBlockProvider2048 : public CBlockProvider
    {
    public:
        typedef std::shared_ptr<Framework::CStream> StreamPtr;

        // blockCount = 0 means all remaining sectors.
        // A non-zero value creates a bounded ISO region.
        CBlockProvider2048(
            const StreamPtr& stream,
            uint32 offset = 0,
            uint32 blockCount = 0)
            : m_stream(stream)
            , m_offset(offset)
            , m_blockCount(blockCount)
        {
        }

        void ReadBlock(uint32 address, void* block) override
        {
            if(address >= GetBlockCount())
            {
                throw std::runtime_error(
                    "ISO9660: sector read outside provider bounds.");
            }

            const uint64 blockIndex =
    static_cast<uint64>(address) + m_offset;

            const uint64 byteOffset =
                blockIndex * BLOCKSIZE;

            const uint64 imageSize =
                m_stream->GetLength();

            if(byteOffset > imageSize ||
               BLOCKSIZE > (imageSize - byteOffset))
            {
                throw std::runtime_error(
                    "ISO9660: sector read outside image bounds.");
            }

            m_stream->Seek(
                byteOffset,
    Framework::STREAM_SEEK_SET);

            const uint64 bytesRead =
                m_stream->Read(block, BLOCKSIZE);

            if(bytesRead != BLOCKSIZE)
            {
                throw std::runtime_error(
                    "ISO9660: incomplete sector read.");
            }
        }

        void ReadMediaBlock(uint32 address, void* block) override
        {
            ReadBlock(address, block);
        }

        uint32 GetBlockCount() override
        {
            const uint64 imageSize = m_stream->GetLength();

            assert((imageSize % BLOCKSIZE) == 0);

            const uint64 totalBlocks = imageSize / BLOCKSIZE;

            if(m_offset >= totalBlocks)
            {
                return 0;
            }

            const uint64 availableBlocks =
                totalBlocks - m_offset;

            const uint64 allowedBlocks =
                (m_blockCount == 0 ||
                 m_blockCount > availableBlocks)
                    ? availableBlocks
                    : m_blockCount;

            return static_cast<uint32>(
                allowedBlocks > 0xFFFFFFFFULL
                    ? 0xFFFFFFFFULL
                    : allowedBlocks);
        }

        uint32 GetMediaBlockSize() const override
        {
            return BLOCKSIZE;
        }

    private:
        StreamPtr m_stream;
        uint32 m_offset = 0;
        uint32 m_blockCount = 0;
    };

	template <uint64 INTERNAL_BLOCKSIZE, uint64 MEDIA_BLOCKSIZE, uint64 BLOCKHEADER_SIZE>
	class CBlockProviderCustom : public CBlockProvider
	{
	public:
		//INTERNAL_BLOCKSIZE = Unit size of the stream (ex.: CHD uses 2448)
		//MEDIA_BLOCKSIZE = Unit size of the media (ex.: CD has 2352 bytes sectors)
		//BLOCKHEADER_SIZE = Size of the header before the data (CD MODE1 & MODE2 = 16 bytes, CD MODE 2/XA = 24 bytes)

		typedef std::shared_ptr<Framework::CStream> StreamPtr;

		CBlockProviderCustom(const StreamPtr& stream)
		    : m_stream(stream)
		{
		}

		void ReadBlock(uint32 address, void* block) override
		{
			m_stream->Seek((static_cast<uint64>(address) * INTERNAL_BLOCKSIZE) + BLOCKHEADER_SIZE, Framework::STREAM_SEEK_SET);
			m_stream->Read(block, BLOCKSIZE);
		}

		void ReadMediaBlock(uint32 address, void* block) override
		{
			m_stream->Seek(static_cast<uint64>(address) * INTERNAL_BLOCKSIZE, Framework::STREAM_SEEK_SET);
			m_stream->Read(block, MEDIA_BLOCKSIZE);
		}

		uint32 GetBlockCount() override
		{
			uint64 imageSize = m_stream->GetLength();
			assert((imageSize % INTERNAL_BLOCKSIZE) == 0);
			return static_cast<uint32>(imageSize / INTERNAL_BLOCKSIZE);
		}

		uint32 GetMediaBlockSize() const override
		{
			return MEDIA_BLOCKSIZE;
		}

	private:
		StreamPtr m_stream;
	};

	typedef CBlockProviderCustom<0x930ULL, 0x930ULL, 0x18ULL> CBlockProviderCDROMXA;
}
