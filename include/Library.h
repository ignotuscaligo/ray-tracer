#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

template<typename T>
class Library
{
public:
    void add(std::shared_ptr<T> data);
    size_t indexForName(const std::string& name) const;
    std::shared_ptr<T> fetch(const std::string& name) const;
    std::shared_ptr<T> fetchByIndex(size_t index) const;

private:
    std::vector<std::shared_ptr<T>> m_contents;
    std::unordered_map<std::string, size_t> m_indexMap;
};
