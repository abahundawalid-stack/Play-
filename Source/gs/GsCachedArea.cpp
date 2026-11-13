#include <algorithm>
#include <cassert>
#include <cstring>

#include "maybe_unused.h"
#include "GsCachedArea.h"
#include "GsPixelFormats.h"

namespace
{
    constexpr uint64 GS_VRAM_SIZE = CGSHandler::RAMSIZE;
    constexpr uint64 GS_PAGE_SIZE = CGsPixelFormats::PAGESIZE;

    struct MemorySegment
    {
        uint64 start;
        uint64 end;
        uint64 logicalOffset;
    };

    static unsigned int SplitWrappedRange(
        uint32 start,
        uint64 size,
        MemorySegment (&segments)[2])
    {
        if(size == 0)
        {
            return 0;
        }

        start %= static_cast<uint32>(GS_VRAM_SIZE);

        const uint64 firstSize = std::min<uint64>(
            size,
            GS_VRAM_SIZE - start);

        segments[0] = {
            start,
            static_cast<uint64>(start) + firstSize,
            0
        };

        if(firstSize == size)
        {
            return 1;
        }

        const uint64 secondSize = size - firstSize;

        segments[1] = {
            0,
            secondSize,
            firstSize
        };

        return 2;
    }
}

CGsCachedArea::CGsCachedArea()
{
    ClearDirtyPages();
}

void CGsCachedArea::SetArea(
    uint32 psm,
    uint32 bufPtr,
    uint32 bufWidth,
    uint32 height)
{
    m_psm = psm;
    m_bufPtr = bufPtr % static_cast<uint32>(GS_VRAM_SIZE);
    m_bufWidth = bufWidth;
    m_height = height;

    ClearDirtyPages();

    FRAMEWORK_MAYBE_UNUSED auto pageRect =
        GetAreaPageRect();

    assert(
        static_cast<uint64>(pageRect.width) *
        pageRect.height <= MAX_DIRTYPAGES);
}

CGsCachedArea::PageRect CGsCachedArea::GetAreaPageRect() const
{
    const auto texturePageSize =
        CGsPixelFormats::GetPsmPageSize(m_psm);

    const uint32 pageCountX =
        (m_bufWidth + texturePageSize.first - 1) /
        texturePageSize.first;

    const uint32 pageCountY =
        (m_height + texturePageSize.second - 1) /
        texturePageSize.second;

    return PageRect{
        0,
        0,
        pageCountX,
        pageCountY
    };
}

CGsCachedArea::PageRect CGsCachedArea::GetDirtyPageRect() const
{
    const auto areaRect = GetAreaPageRect();

    uint32 startX = 0;
    uint32 startY = 0;

    for(startY = 0; startY < areaRect.height; startY++)
    {
        bool found = false;

        for(startX = 0; startX < areaRect.width; startX++)
        {
            const uint32 pageIndex =
                startX + startY * areaRect.width;

            if(IsPageDirty(pageIndex))
            {
                found = true;
                break;
            }
        }

        if(found)
        {
            break;
        }
    }

    if(startX == areaRect.width ||
       startY == areaRect.height)
    {
        return PageRect{0, 0, 0, 0};
    }

    const auto getHorizontalSpan =
        [&](uint32 x, uint32 y)
        {
            uint32 span = 0;

            for(; x < areaRect.width; x++)
            {
                const uint32 pageIndex =
                    x + y * areaRect.width;

                if(!IsPageDirty(pageIndex))
                {
                    break;
                }

                span++;
            }

            return span;
        };

    const uint32 spanX =
        getHorizontalSpan(startX, startY);

    uint32 spanY = 1;

    for(uint32 y = startY + 1;
        y < areaRect.height;
        y++)
    {
        if(getHorizontalSpan(startX, y) < spanX)
        {
            break;
        }

        spanY++;
    }

    return PageRect{
        startX,
        startY,
        spanX,
        spanY
    };
}

uint32 CGsCachedArea::GetPageCount() const
{
    const auto areaRect = GetAreaPageRect();

    return areaRect.width * areaRect.height;
}

uint32 CGsCachedArea::GetSize() const
{
    return GetPageCount() *
           CGsPixelFormats::PAGESIZE;
}

void CGsCachedArea::Invalidate(
    uint32 memoryStart,
    uint32 memorySize)
{
    if(memorySize == 0)
    {
        return;
    }

    const uint32 areaPageCount = GetPageCount();

    if(areaPageCount == 0)
    {
        return;
    }

    const uint64 areaSize = GetSize();

    // A write covering the entire VRAM, or a cached area
    // spanning the entire VRAM, can affect every cached page.
    if(memorySize >= GS_VRAM_SIZE ||
       areaSize >= GS_VRAM_SIZE)
    {
        const uint32 dirtyCount =
            std::min<uint32>(
                areaPageCount,
                MAX_DIRTYPAGES);

        for(uint32 page = 0; page < dirtyCount; page++)
        {
            SetPageDirty(page);
        }

        return;
    }

    MemorySegment areaSegments[2] = {};
    MemorySegment writeSegments[2] = {};

    const unsigned int areaSegmentCount =
        SplitWrappedRange(
            m_bufPtr,
            areaSize,
            areaSegments);

    const unsigned int writeSegmentCount =
        SplitWrappedRange(
            memoryStart,
            memorySize,
            writeSegments);

    for(unsigned int areaIndex = 0;
        areaIndex < areaSegmentCount;
        areaIndex++)
    {
        const auto& area = areaSegments[areaIndex];

        for(unsigned int writeIndex = 0;
            writeIndex < writeSegmentCount;
            writeIndex++)
        {
            const auto& write = writeSegments[writeIndex];

            const uint64 overlapStart =
                std::max(area.start, write.start);

            const uint64 overlapEnd =
                std::min(area.end, write.end);

            if(overlapStart >= overlapEnd)
            {
                continue;
            }

            // Convert the overlapping VRAM address back
            // into the logical cached-area page offset.
            const uint64 relativeStart =
                area.logicalOffset +
                (overlapStart - area.start);

            const uint64 relativeEnd =
                area.logicalOffset +
                (overlapEnd - area.start);

            const uint64 firstPage =
                relativeStart / GS_PAGE_SIZE;

            const uint64 lastPage =
                (relativeEnd - 1) / GS_PAGE_SIZE;

            for(uint64 page = firstPage;
                page <= lastPage;
                page++)
            {
                if(page >= areaPageCount ||
                   page >= MAX_DIRTYPAGES)
                {
                    break;
                }

                SetPageDirty(
                    static_cast<uint32>(page));
            }
        }
    }
}

bool CGsCachedArea::IsPageDirty(
    uint32 pageIndex) const
{
    if(pageIndex >= MAX_DIRTYPAGES)
    {
        assert(false);
        return false;
    }

    const unsigned int section =
        pageIndex / (sizeof(m_dirtyPages[0]) * 8);

    const unsigned int bit =
        pageIndex % (sizeof(m_dirtyPages[0]) * 8);

    return (
        m_dirtyPages[section] &
        (1ULL << bit)) != 0;
}

void CGsCachedArea::SetPageDirty(
    uint32 pageIndex)
{
    if(pageIndex >= MAX_DIRTYPAGES)
    {
        assert(false);
        return;
    }

    const unsigned int section =
        pageIndex / (sizeof(m_dirtyPages[0]) * 8);

    const unsigned int bit =
        pageIndex % (sizeof(m_dirtyPages[0]) * 8);

    m_dirtyPages[section] |=
        (1ULL << bit);
}

bool CGsCachedArea::HasDirtyPages() const
{
    for(unsigned int i = 0;
        i < MAX_DIRTYPAGES_SECTIONS;
        i++)
    {
        if(m_dirtyPages[i] != 0)
        {
            return true;
        }
    }

    return false;
}

void CGsCachedArea::ClearDirtyPages()
{
    std::memset(
        m_dirtyPages,
        0,
        sizeof(m_dirtyPages));
}

void CGsCachedArea::ClearDirtyPages(
    const PageRect& rect)
{
    const auto areaRect = GetAreaPageRect();

    const uint32 endX =
        std::min(
            rect.x + rect.width,
            areaRect.width);

    const uint32 endY =
        std::min(
            rect.y + rect.height,
            areaRect.height);

    for(uint32 y = rect.y; y < endY; y++)
    {
        for(uint32 x = rect.x; x < endX; x++)
        {
            const uint32 pageIndex =
                x + y * areaRect.width;

            if(pageIndex >= MAX_DIRTYPAGES)
            {
                assert(false);
                continue;
            }

            const unsigned int section =
                pageIndex /
                (sizeof(m_dirtyPages[0]) * 8);

            const unsigned int bit =
                pageIndex %
                (sizeof(m_dirtyPages[0]) * 8);

            m_dirtyPages[section] &=
                ~(1ULL << bit);
        }
    }
}
