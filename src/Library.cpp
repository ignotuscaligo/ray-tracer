#include "Library.h"

#include "Material.h"

template<typename T>
void Library<T>::add(std::shared_ptr<T> data)
{
    if (m_indexMap.count(data->name()) > 0)
    {
        throw std::runtime_error("Library already contains data named " + data->name());
    }

    m_indexMap.insert_or_assign(data->name(), m_contents.size());
    m_contents.push_back(data);
}

template<typename T>
size_t Library<T>::indexForName(const std::string& name) const
{
    if (m_indexMap.count(name) == 0)
    {
        throw std::runtime_error("Library does not contain item named " + name);
    }

    return m_indexMap.at(name);
}

template<typename T>
std::shared_ptr<T> Library<T>::fetch(const std::string& name) const
{
    return m_contents[indexForName(name)];
}

template<typename T>
std::shared_ptr<T> Library<T>::fetchByIndex(size_t index) const
{
    if (index >= m_contents.size())
    {
        throw std::runtime_error("Library does not contain item with index " + std::to_string(index));
    }

    return m_contents[index];
}

template void Library<Material>::add(std::shared_ptr<Material> data);
template size_t Library<Material>::indexForName(const std::string& name) const;
template std::shared_ptr<Material> Library<Material>::fetch(const std::string& name) const;
template std::shared_ptr<Material> Library<Material>::fetchByIndex(size_t index) const;
