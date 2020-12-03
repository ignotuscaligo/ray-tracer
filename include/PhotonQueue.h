#pragma once

#include "Photon.h"

#include <vector>

class PhotonBlock
{
public:
    PhotonBlock() = delete;
    PhotonBlock(size_t start, size_t end, std::vector<Photon>& queue);

    struct Iterator {
        size_t index;
        size_t end;
        std::vector<Photon>& queue;

        Photon& operator*();
        bool operator!=(const Iterator& rhs);
        void operator++();
    };

    Iterator begin() const;
    Iterator end() const;

private:
    const size_t m_start;
    const size_t m_end;
    std::vector<Photon>& m_queue;
};

class PhotonQueue
{
public:
    PhotonQueue(size_t count);

private:
    std::vector<Photon> m_queue;
    size_t m_memoryHead;
    size_t m_memoryTail;
    size_t m_readyHead;
    size_t m_readyTail;
};
