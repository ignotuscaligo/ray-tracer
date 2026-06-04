#include "BounceCloud.h"

#include <algorithm>

BounceCloud::BounceCloud(std::size_t capacity)
    : m_records(capacity)
    , m_capacity(capacity)
{
}

bool BounceCloud::append(const BounceRecord& record) noexcept
{
    // Claim the next slot. relaxed is sufficient: each writer touches a distinct
    // slot index (the fetch_add guarantees uniqueness), and no reader runs until
    // the pass has fully drained and the worker threads have been joined, which
    // establishes the necessary happens-before for the later dense read.
    const std::size_t index = m_writeCursor.fetch_add(1, std::memory_order_relaxed);

    if (index >= m_capacity)
    {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    m_records[index] = record;
    return true;
}

std::size_t BounceCloud::size() const noexcept
{
    return std::min(m_writeCursor.load(std::memory_order_relaxed), m_capacity);
}

std::size_t BounceCloud::capacity() const noexcept
{
    return m_capacity;
}

bool BounceCloud::budgetHit() const noexcept
{
    return m_dropped.load(std::memory_order_relaxed) > 0;
}

std::size_t BounceCloud::droppedCount() const noexcept
{
    return m_dropped.load(std::memory_order_relaxed);
}

std::size_t BounceCloud::memoryBytes() const noexcept
{
    return m_capacity * sizeof(BounceRecord);
}

const BounceRecord& BounceCloud::operator[](std::size_t index) const noexcept
{
    return m_records[index];
}

const BounceRecord* BounceCloud::data() const noexcept
{
    return m_records.data();
}
