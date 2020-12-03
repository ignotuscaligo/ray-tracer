#include "PhotonQueue.h"

PhotonBlock::PhotonBlock(size_t start, size_t end, std::vector<Photon>& queue)
    : m_start(start)
    , m_end(end)
    , m_queue(queue)
{
}

Photon& PhotonBlock::Iterator::operator*()
{
    return queue[index % queue.size()];
}

bool PhotonBlock::Iterator::operator!=(const Iterator& rhs)
{
    return index != rhs.index || end != rhs.end || &queue != &rhs.queue;
}

void PhotonBlock::Iterator::operator++()
{
    index = std::min(index + 1, end);
}

PhotonBlock::Iterator PhotonBlock::begin() const
{
    return Iterator{m_start, m_end, m_queue};
}

PhotonBlock::Iterator PhotonBlock::end() const
{
    return Iterator{m_end, m_end, m_queue};
}
