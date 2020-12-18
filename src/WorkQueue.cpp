#include "WorkQueue.h"

#include "Photon.h"

#include <algorithm>

#include <iostream>

template<typename T>
WorkQueue<T>::Block::Block(size_t start, size_t end, std::vector<T>& queue)
    : startIndex(start)
    , endIndex(end)
    , m_queue(queue)
{
}

template<typename T>
T& WorkQueue<T>::Block::Iterator::operator*()
{
    return queue[index % queue.size()];
}

template<typename T>
bool WorkQueue<T>::Block::Iterator::operator!=(const Iterator& rhs)
{
    return index != rhs.index || end != rhs.end || &queue != &rhs.queue;
}

template<typename T>
void WorkQueue<T>::Block::Iterator::operator++()
{
    index = std::min(index + 1, end);
}

template<typename T>
typename WorkQueue<T>::Block::Iterator WorkQueue<T>::Block::begin() const
{
    return Iterator{startIndex, endIndex, m_queue};
}

template<typename T>
typename WorkQueue<T>::Block::Iterator WorkQueue<T>::Block::end() const
{
    return Iterator{endIndex, endIndex, m_queue};
}

template<typename T>
T& WorkQueue<T>::Block::operator[](size_t accessIndex)
{
    return at(accessIndex);
}

template<typename T>
T& WorkQueue<T>::Block::at(size_t accessIndex)
{
    return m_queue[(startIndex + accessIndex) % m_queue.size()];
}

template<typename T>
size_t WorkQueue<T>::Block::size() const
{
    return endIndex - startIndex;
}

template<typename T>
std::vector<T> WorkQueue<T>::Block::toVector() const
{
    size_t size = endIndex - startIndex;
    std::vector<T> objects;
    objects.reserve(size);

    for (size_t i = 0; i < size; ++i)
    {
        objects.push_back(m_queue[(startIndex + i) % m_queue.size()]);
    }

    return objects;
}

template<typename T>
WorkQueue<T>::WorkQueue(size_t size)
    : m_queue(size)
    , m_size(size)
    , m_memoryHead(0)
    , m_memoryTail(0)
    , m_readyHead(0)
    , m_readyTail(0)
    , m_allocated(0)
    , m_available(0)
{
}

template<typename T>
typename WorkQueue<T>::Block WorkQueue<T>::initialize(size_t count)
{
    size_t start = 0;
    size_t end = 0;

    if (count > 0)
    {
        std::scoped_lock<std::mutex> lock(m_mutex);

        size_t remaining = m_size - m_allocated.load();

        if (remaining > 0)
        {
            start = m_memoryHead;
            end = m_memoryHead + std::min(count, remaining);

            m_memoryHead = end % m_size;
            m_allocated.fetch_add(end - start);

            m_initializing.insert(end);
        }
    }

    return {
        start,
        end,
        m_queue
    };
}

template<typename T>
void WorkQueue<T>::ready(Block block)
{
    {
        std::scoped_lock<std::mutex> lock(m_mutex);

        if (block.startIndex != block.endIndex)
        {
            auto it = m_initializing.find(block.endIndex);

            size_t head = m_memoryHead;

            if (!m_initializing.empty())
            {
                head = *m_initializing.begin();
            }

            m_initializing.erase(it);

            size_t prev = m_readyHead;
            m_readyHead = head % m_size;

            m_available.fetch_add(block.endIndex - block.startIndex);
        }
    }
}

template<typename T>
typename WorkQueue<T>::Block WorkQueue<T>::fetch(size_t count)
{
    size_t start = 0;
    size_t end = 0;

    if (count > 0)
    {
        std::scoped_lock<std::mutex> lock(m_mutex);

        if (m_available.load() > 0)
        {
            start = m_readyTail;
            end = m_readyTail + std::min(count, m_available.load());

            m_readyTail = end % m_size;
            m_available.fetch_sub(end - start);

            m_processing.insert(start);
        }
    }

    return {
        start,
        end,
        m_queue
    };
}

template<typename T>
void WorkQueue<T>::release(Block block)
{
    {
        std::scoped_lock<std::mutex> lock(m_mutex);

        if (block.startIndex != block.endIndex)
        {
            auto it = m_processing.find(block.startIndex);
            m_processing.erase(it);

            size_t tail = m_readyTail;

            if (!m_processing.empty())
            {
                tail = *m_processing.begin();
            }

            size_t prev = m_memoryTail;
            m_memoryTail = tail % m_size;

            m_allocated.fetch_sub(block.endIndex - block.startIndex);
        }
    }
}

template<typename T>
size_t WorkQueue<T>::capacity() const
{
    return m_size;
}

template<typename T>
size_t WorkQueue<T>::freeSpace() const
{
    return m_size - allocated();
}

template<typename T>
size_t WorkQueue<T>::allocated() const
{
    return m_allocated.load();
}

template<typename T>
size_t WorkQueue<T>::available() const
{
    return m_available.load();
}

template WorkQueue<Photon>::Block::Block(size_t start, size_t end, std::vector<Photon>& queue);
template Photon& WorkQueue<Photon>::Block::Iterator::operator*();
template bool WorkQueue<Photon>::Block::Iterator::operator!=(const Iterator& rhs);
template void WorkQueue<Photon>::Block::Iterator::operator++();
template typename WorkQueue<Photon>::Block::Iterator WorkQueue<Photon>::Block::begin() const;
template typename WorkQueue<Photon>::Block::Iterator WorkQueue<Photon>::Block::end() const;
template Photon& WorkQueue<Photon>::Block::operator[](size_t accessIndex);
template Photon& WorkQueue<Photon>::Block::at(size_t accessIndex);
template size_t WorkQueue<Photon>::Block::size() const;
template std::vector<Photon> WorkQueue<Photon>::Block::toVector() const;
template WorkQueue<Photon>::WorkQueue(size_t size);
template typename WorkQueue<Photon>::Block WorkQueue<Photon>::initialize(size_t count);
template void WorkQueue<Photon>::ready(Block block);
template typename WorkQueue<Photon>::Block WorkQueue<Photon>::fetch(size_t count);
template void WorkQueue<Photon>::release(Block block);
template size_t WorkQueue<Photon>::capacity() const;
template size_t WorkQueue<Photon>::freeSpace() const;
template size_t WorkQueue<Photon>::allocated() const;
template size_t WorkQueue<Photon>::available() const;

template WorkQueue<PhotonHit>::Block::Block(size_t start, size_t end, std::vector<PhotonHit>& queue);
template PhotonHit& WorkQueue<PhotonHit>::Block::Iterator::operator*();
template bool WorkQueue<PhotonHit>::Block::Iterator::operator!=(const Iterator& rhs);
template void WorkQueue<PhotonHit>::Block::Iterator::operator++();
template typename WorkQueue<PhotonHit>::Block::Iterator WorkQueue<PhotonHit>::Block::begin() const;
template typename WorkQueue<PhotonHit>::Block::Iterator WorkQueue<PhotonHit>::Block::end() const;
template PhotonHit& WorkQueue<PhotonHit>::Block::operator[](size_t accessIndex);
template PhotonHit& WorkQueue<PhotonHit>::Block::at(size_t accessIndex);
template size_t WorkQueue<PhotonHit>::Block::size() const;
template std::vector<PhotonHit> WorkQueue<PhotonHit>::Block::toVector() const;
template WorkQueue<PhotonHit>::WorkQueue(size_t size);
template typename WorkQueue<PhotonHit>::Block WorkQueue<PhotonHit>::initialize(size_t count);
template void WorkQueue<PhotonHit>::ready(Block block);
template typename WorkQueue<PhotonHit>::Block WorkQueue<PhotonHit>::fetch(size_t count);
template void WorkQueue<PhotonHit>::release(Block block);
template size_t WorkQueue<PhotonHit>::capacity() const;
template size_t WorkQueue<PhotonHit>::freeSpace() const;
template size_t WorkQueue<PhotonHit>::allocated() const;
template size_t WorkQueue<PhotonHit>::available() const;
