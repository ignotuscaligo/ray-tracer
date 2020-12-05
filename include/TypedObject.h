#pragma once

#include <typeinfo>
#include <unordered_set>

class TypedObject
{
public:
    TypedObject() = default;

    template<typename T>
    bool hasType()
    {
        return m_types.count(typeid(T).hash_code()) > 0;
    }

protected:
    template<typename T>
    void registerType()
    {
        m_types.insert(typeid(T).hash_code());
    }

private:
    std::unordered_set<size_t> m_types;
};
