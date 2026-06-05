#pragma once

#include <typeinfo>
#include <unordered_set>

class TypedObject
{
public:
    TypedObject() = default;

    // Root of the polymorphic Object/Light/Volume hierarchy. Derived types are
    // created and destroyed through base handles (std::make_shared<SpotLight>,
    // etc.), so the base needs a virtual destructor — otherwise deleting a
    // derived object via a base path is UB (-Wdelete-non-abstract-non-virtual-dtor).
    virtual ~TypedObject() = default;

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
