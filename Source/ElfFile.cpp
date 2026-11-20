#include "ElfFile.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>

CElfFileContainer::CElfFileContainer(Framework::CStream& input)
{
    m_size = input.GetLength();

    if(m_size == 0)
    {
        throw std::runtime_error(
            "Cannot load an empty ELF stream.");
    }

    if(m_size >
       static_cast<uint64>(
           std::numeric_limits<size_t>::max()))
    {
        throw std::length_error(
            "ELF image exceeds the addressable memory size.");
    }

    std::unique_ptr<uint8[]> content(
        new uint8[static_cast<size_t>(m_size)]);

    uint64 totalRead = 0;

    while(totalRead < m_size)
    {
        const uint64 remaining =
            m_size - totalRead;

        const uint64 bytesRead = input.Read(
            content.get() + static_cast<size_t>(totalRead),
            remaining);

        if(bytesRead == 0)
        {
            throw std::runtime_error(
                "Unexpected end of stream while reading ELF image.");
        }

        if(bytesRead > remaining)
        {
            throw std::runtime_error(
                "Invalid stream read length while reading ELF image.");
        }

        totalRead += bytesRead;
    }

    // Publikasikan buffer setelah seluruh ELF terbaca.
    m_content = content.release();
}

CElfFileContainer::~CElfFileContainer()
{
    delete[] m_content;
}

uint8* CElfFileContainer::GetFileContent() const
{
    return m_content;
}

uint64 CElfFileContainer::GetFileSize() const
{
    return m_size;
}
