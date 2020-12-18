#pragma once

#include <vector>
#include <set>
#include <mutex>

template<typename T>
class WorkQueue
{
public:
    class Block
    {
    public:
        Block() = delete;
        Block(size_t start, size_t end, std::vector<T>& queue);

        struct Iterator {
            size_t index;
            size_t end;
            std::vector<T>& queue;

            T& operator*();
            bool operator!=(const Iterator& rhs);
            void operator++();
        };

        Iterator begin() const;
        Iterator end() const;

        T& operator[](size_t accessIndex);
        T& at(size_t accessIndex);

        size_t size() const;

        std::vector<T> toVector() const;

        const size_t startIndex;
        const size_t endIndex;

    private:
        std::vector<T>& m_queue;
    };

    WorkQueue(size_t size);

    Block initialize(size_t count);
    void ready(Block block);
    Block fetch(size_t count);
    void release(Block block);

    size_t capacity() const;
    size_t freeSpace() const;
    size_t allocated() const;
    size_t available() const;

private:
    const size_t m_size;
    std::vector<T> m_queue;
    std::set<size_t> m_initializing;
    std::set<size_t> m_processing;
    size_t m_memoryHead;
    size_t m_memoryTail;
    size_t m_readyHead;
    size_t m_readyTail;
    std::atomic<size_t> m_allocated;
    std::atomic<size_t> m_available;
    mutable std::mutex m_mutex;
};
