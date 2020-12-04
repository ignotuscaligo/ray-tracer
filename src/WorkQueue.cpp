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
    return m_queue[(startIndex + accessIndex) % m_queue.size()];
}

template<typename T>
size_t WorkQueue<T>::Block::size() const
{
    return endIndex - startIndex;
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

        size_t remaining = m_size - m_allocated;

        if (remaining > 0)
        {
            // std::cout << "memoryHead " << m_memoryHead;

            start = m_memoryHead;
            end = m_memoryHead + std::min(count, remaining);

            // if (m_memoryHead >= m_memoryTail)
            // {
            //     end = std::min(end, m_memoryTail + m_size);
            // }
            // else
            // {
            //     end = std::min(end, m_memoryTail);
            // }

            m_memoryHead = end % m_size;
            m_allocated += end - start;

            // std::cout << " -> " << m_memoryHead << std::endl;

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
            m_initializing.erase(it);

            size_t head = m_memoryHead;

            if (!m_initializing.empty())
            {
                head = *m_initializing.begin() % m_size;
            }

            // std::cout << "readyHead " << m_readyHead;

            size_t prev = m_readyHead;
            m_readyHead = head;

            if (head > prev)
            {
                m_available += m_readyHead - prev;
            }
            else if (head < prev)
            {
                m_available += (m_readyHead + m_size) - prev;
            }

            // std::cout << " -> " << m_readyHead << std::endl;
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

        if (m_available > 0)
        {
            // std::cout << "fetch(" << count << ")" << std::endl;

            start = m_readyTail;
            end = m_readyTail + std::min(count, m_available);

            // std::cout << "start: " << start << std::endl;
            // std::cout << "end:   " << end << std::endl;

            // if (m_readyHead >= m_readyTail)
            // {
            //     end = std::min(end, m_readyHead);
            // }
            // else
            // {
            //     end = std::min(end, m_readyHead + m_size);
            // }

            m_readyTail = end % m_size;
            m_available -= end - start;

            // std::cout << "readyTail " << start;
            // std::cout << " -> " << m_readyTail << std::endl;

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
                tail = *m_processing.begin() % m_size;
            }

            // std::cout << "memoryTail " << m_memoryTail;

            size_t prev = m_memoryTail;
            m_memoryTail = tail;

            if (tail > prev)
            {
                m_allocated -= m_memoryTail - prev;
            }
            else if (tail < prev)
            {
                m_allocated -= (m_memoryTail + m_size) - prev;
            }

            // std::cout << " -> " << m_memoryTail << std::endl;
        }
    }
}

template<typename T>
size_t WorkQueue<T>::capacity() const
{
    return m_size;
}

template<typename T>
size_t WorkQueue<T>::allocated() const
{
    size_t count = 0;

    {
        std::scoped_lock<std::mutex> lock(m_mutex);
        count = m_allocated;
    }

    return count;
}

template<typename T>
size_t WorkQueue<T>::available() const
{
    size_t count = 0;

    // std::cout << "WorkQueue::available()" << std::endl;

    {
        std::scoped_lock<std::mutex> lock(m_mutex);
        // std::cout << "Acquired lock" << std::endl;
        count = m_available;
    }

    // std::cout << "return count" << std::endl;

    return count;
}

template WorkQueue<Photon>::Block::Block(size_t start, size_t end, std::vector<Photon>& queue);
template Photon& WorkQueue<Photon>::Block::Iterator::operator*();
template bool WorkQueue<Photon>::Block::Iterator::operator!=(const Iterator& rhs);
template void WorkQueue<Photon>::Block::Iterator::operator++();
template typename WorkQueue<Photon>::Block::Iterator WorkQueue<Photon>::Block::begin() const;
template typename WorkQueue<Photon>::Block::Iterator WorkQueue<Photon>::Block::end() const;
template Photon& WorkQueue<Photon>::Block::operator[](size_t accessIndex);
template size_t WorkQueue<Photon>::Block::size() const;
template WorkQueue<Photon>::WorkQueue(size_t size);
template typename WorkQueue<Photon>::Block WorkQueue<Photon>::initialize(size_t count);
template void WorkQueue<Photon>::ready(Block block);
template typename WorkQueue<Photon>::Block WorkQueue<Photon>::fetch(size_t count);
template void WorkQueue<Photon>::release(Block block);
template size_t WorkQueue<Photon>::capacity() const;
template size_t WorkQueue<Photon>::allocated() const;
template size_t WorkQueue<Photon>::available() const;

template WorkQueue<PhotonHit>::Block::Block(size_t start, size_t end, std::vector<PhotonHit>& queue);
template PhotonHit& WorkQueue<PhotonHit>::Block::Iterator::operator*();
template bool WorkQueue<PhotonHit>::Block::Iterator::operator!=(const Iterator& rhs);
template void WorkQueue<PhotonHit>::Block::Iterator::operator++();
template typename WorkQueue<PhotonHit>::Block::Iterator WorkQueue<PhotonHit>::Block::begin() const;
template typename WorkQueue<PhotonHit>::Block::Iterator WorkQueue<PhotonHit>::Block::end() const;
template PhotonHit& WorkQueue<PhotonHit>::Block::operator[](size_t accessIndex);
template size_t WorkQueue<PhotonHit>::Block::size() const;
template WorkQueue<PhotonHit>::WorkQueue(size_t size);
template typename WorkQueue<PhotonHit>::Block WorkQueue<PhotonHit>::initialize(size_t count);
template void WorkQueue<PhotonHit>::ready(Block block);
template typename WorkQueue<PhotonHit>::Block WorkQueue<PhotonHit>::fetch(size_t count);
template void WorkQueue<PhotonHit>::release(Block block);
template size_t WorkQueue<PhotonHit>::capacity() const;
template size_t WorkQueue<PhotonHit>::allocated() const;
template size_t WorkQueue<PhotonHit>::available() const;
