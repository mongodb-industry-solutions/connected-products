////////////////////////////////////////////////////////////////////////////
//
// Copyright 2021 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#ifndef realm_persisted_hpp
#define realm_persisted_hpp

#include <cpprealm/type_info.hpp>

namespace realm {

struct FieldValue;
template <type_info::Persistable T>
struct persisted;

template <realm::type_info::Persistable T>
struct persisted_base {
    using Result = T;
    using type = typename realm::type_info::persisted_type<T>::type;

    persisted_base(const T& value);
    persisted_base(T&& value);
    persisted_base(const char* value) requires realm::type_info::StringPersistable<T>;
    template <typename S>
    requires (type_info::ObjectPersistable<T>)
    persisted_base(std::optional<S> value);

    persisted_base();
    ~persisted_base();
    persisted_base(const persisted_base& o);
    persisted_base(persisted_base&& o);

    template <typename S>
    requires (type_info::StringPersistable<T>) && std::is_same_v<S, const char*>
    persisted_base& operator=(S o);
    persisted_base& operator=(const T& o);
    persisted_base& operator=(const persisted_base& o);
    persisted_base& operator=(persisted_base&& o);

    T operator *() const;
protected:
    union {
        T unmanaged;
        realm::ColKey managed;
    };

    template <StringLiteral Name, auto Ptr, bool>
    friend struct property;
    template <StringLiteral, type_info::Propertyable ...Properties>
    friend struct schema;
    template <type_info::TimestampPersistable X, typename U, typename V>
    friend persisted<X>& operator +=(persisted<X>& a, std::chrono::duration<U, V> b);
    type as_core_type() const;
    void assign(const Obj& object, const ColKey& col_key);
    std::optional<Obj> m_obj;
};

/// `realm::persisted` is used to declare properties on `realm::object` subclasses which should be
/// managed by Realm.
///
/// Example of usage:
/// ```cpp
/// class MyModel: realm::object {
///     // A basic property declaration. A property with no
///     // default value supplied will default to `nil` for
///     // Optional types, zero for numeric types, false for Bool,
///     // an empty string/data, and a new random value for UUID
///     // and ObjectID.
///     realm::persisted<int> basic_int_property;
///
///     // Custom default values can be specified with the
///     // standard c++ syntax
///     realm::persisted<int> int_with_custom_default = 5;
///
///     // Primary key properties can be picked in schema.
///     realm::persisted<int> var _id;
///
///     // Properties which are not marked with `persisted` will
///     // be ignored entirely by Realm.
///     bool ignoredProperty = true
///
///     using schema = realm::schema<"MyModel",
///                             realm::property<"basic_int_property", &MyModel::basic_int_property>,
///                             realm::property<"int_with_custom_default", &MyModel::int_with_custom_default>,
///                             realm::property<"_id", &MyModel::_id, true>>; // primary key property
/// }
/// ```
///
///  A property can be set as the class's primary key by passing `true`
///  into its schema property. Compound primary keys are not supported, and setting
///  more than one property as the primary key will throw an exception at
///  runtime. Only Int, String, UUID and ObjectID properties can be made the
///  primary key, and when using MongoDB Realm, the primary key must be named
///  `_id`. The primary key property can only be mutated on unmanaged objects,
///  and mutating it on an object which has been added to a Realm will throw an
///  exception.
///
///  Properties can optionally be given a default value using the standard C++20
///  syntax. If no default value is given, a value will be generated on first
///  access: `nil` for all Optional types, zero for numeric types, false for
///  Bool, an empty string/data, and a new random value for UUID and ObjectID.
///  Doing so will work, but will result in worse performance when accessing
///  objects managed by a Realm. Similarly, ObjectID properties *should not* be initialized to
///  `ObjectID.generate()`, as doing so will result in extra ObjectIDs being
///  generated and then discarded when reading from a Realm.
template <type_info::Persistable T>
struct persisted : public persisted_base<T> {
    using persisted_base<T>::persisted_base;
};

template <type_info::NonContainerPersistable T>
struct persisted_noncontainer_base : public persisted_base<T> {
    using persisted_base<T>::persisted_base;
    using persisted_base<T>::operator=;
    using persisted_base<T>::operator*;
    using type = typename realm::type_info::persisted_type<T>::type;

    void operator -=(const T& a) requires (type_info::IntPersistable<T> || type_info::DoublePersistable<T>);
    void operator --() requires (type_info::IntPersistable<T> || type_info::DoublePersistable<T>);
    T operator -() requires (type_info::IntPersistable<T> || type_info::DoublePersistable<T>);
    void operator +=(const T& a) requires (type_info::AddAssignable<T>);
    T operator *(const T& a) requires (type_info::AddAssignable<T>);
    void operator ++() requires (type_info::AddAssignable<T>);
    bool operator <(const T& a) requires (type_info::Comparable<T>);
    bool operator >(const T& a) requires (type_info::Comparable<T>);
    bool operator <=(const T& a) requires (type_info::Comparable<T>);
    bool operator >=(const T& a) requires (type_info::Comparable<T>);
};

template <type_info::TimestampPersistable T, typename U, typename V>
persisted<T>& operator +=(persisted<T>& a, std::chrono::duration<U, V> b) {
    if (auto m_obj = a.m_obj) {
        auto ts = m_obj->template get<Timestamp>(a.managed);
        m_obj->template set(a.managed, Timestamp(ts.get_time_point() + b));
    } else {
        a.unmanaged += b;
    }
    return a;
}

template <type_info::NonContainerPersistable T>
struct persisted<T> : public persisted_noncontainer_base<T> {
    using persisted_noncontainer_base<T>::persisted_noncontainer_base;
    using persisted_noncontainer_base<T>::operator=;
};

// MARK: Persisted List

template <realm::type_info::ListPersistable T>
struct persisted_container_base : public persisted_base<T> {
    using value_type = typename T::value_type;
    using size_type = typename T::size_type;

    class iterator {
    public:
        using difference_type = size_type;
        using value_type = value_type;
        using pointer = value_type*;
        using reference = value_type&;
        using iterator_category = std::input_iterator_tag;

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

        bool operator==(const iterator& other) const
        {
            return (m_parent == other.m_parent) && (m_idx == other.m_idx);
        }

        reference operator*() noexcept
        {
            value = (*m_parent)[m_idx];
            return value;
        }

        pointer operator->() const noexcept
        {
            value = (*m_parent)[m_idx];
            return &value;
        }

        iterator& operator++()
        {
            m_idx++;
            return *this;
        }

        iterator operator++(int i)
        {
            m_idx += i;
            return *this;
        }
    private:
        friend struct persisted_container_base<T>;

        iterator(size_t idx, persisted_container_base<T>* parent)
                : m_idx(idx)
                , m_parent(parent)
        {
        }

        size_t m_idx;
        persisted_container_base<T>* m_parent;
        value_type value;
    };

    iterator begin()
    {
        return iterator(0, this);
    }

    iterator end()
    {
        return iterator(size(), this);
    }

    size_t size()
    {
        if (this->m_obj) {
            auto lst = this->m_obj->template get_list<typename type_info::persisted_type<typename T::value_type>::type>(this->managed);
            return lst.size();
        } else {
            return this->unmanaged.size();
        }
    }
    typename T::value_type operator[](size_type pos) requires (type_info::PrimitivePersistable<value_type>);
    typename T::value_type operator[](size_type pos) requires (type_info::ObjectPersistable<value_type>);

    void push_back(const value_type& a) requires (type_info::PrimitivePersistable<value_type>);
    void push_back(value_type& a) requires (type_info::ObjectPersistable<value_type>);
    void push_back(value_type&& a) requires (type_info::ObjectPersistable<value_type>);
};

template <realm::type_info::ListPersistable T>
struct persisted<T> : public persisted_container_base<T> {
    using persisted_container_base<T>::persisted_container_base;
    using persisted_container_base<T>::operator=;
};

template <realm::type_info::BinaryPersistable T>
struct persisted_binary_base : public persisted_base<T> {
    using value_type = typename T::value_type;
    using size_type = typename T::size_type;

    typename T::value_type operator[](size_type pos)
    {
        if (this->m_obj) {
            return this->m_obj->template get<BinaryData>(this->managed)[pos];
        } else {
            return this->unmanaged[pos];
        }
    }
    void push_back(value_type a)
    {
        if (this->m_obj) {
            BinaryData data = this->m_obj->template get<BinaryData>(this->managed);
            std::string data_tmp = data.data();
            data_tmp += a;
            data = BinaryData(data_tmp);
            this->m_obj->set(this->managed, data);
        } else {
            this->unmanaged.push_back(a);
        }
    }
};

template <realm::type_info::BinaryPersistable T>
struct persisted<T> : public persisted_binary_base<T> {
    using persisted_binary_base<T>::persisted_binary_base;
    using persisted_binary_base<T>::operator=;
};

// MARK: Implementation

template <realm::type_info::Persistable T>
persisted_base<T>::persisted_base() {
    new (&unmanaged) T();
}
template <realm::type_info::Persistable T>
persisted_base<T>::persisted_base(const T& value) {
    unmanaged = value;
}
template <realm::type_info::Persistable T>
persisted_base<T>::persisted_base(T&& value) {
    unmanaged = std::move(value);
}
template <realm::type_info::Persistable T>
persisted_base<T>::persisted_base(const char* value) requires realm::type_info::StringPersistable<T> {
    new (&unmanaged) std::string(value);
}
template <realm::type_info::Persistable T>
template <typename S>
requires (type_info::ObjectPersistable<T>)
persisted_base<T>::persisted_base(std::optional<S> value) {
    unmanaged = value;
}
template <realm::type_info::Persistable T>
persisted_base<T>::~persisted_base()
{
    if constexpr (realm::type_info::property_type<T>() == PropertyType::String) {
        using std::string;
        if (!m_obj)
            unmanaged.~string();
    }
}
template <realm::type_info::Persistable T>
persisted_base<T>::persisted_base(const persisted_base& o) {
    *this = o;
}
template <realm::type_info::Persistable T>
persisted_base<T>::persisted_base(persisted_base&& o) {
    *this = std::move(o);
}
template <realm::type_info::Persistable T>
template <typename S>
requires (type_info::StringPersistable<T>) && std::is_same_v<S, const char*>
persisted_base<T>& persisted_base<T>::operator=(S o) {
    if (auto obj = m_obj) {
        obj->template set<type>(managed, o);
    } else {
        unmanaged = o;
    }
    return *this;
}

template <realm::type_info::Persistable T>
persisted_base<T>& persisted_base<T>::operator=(const T& o) {
    if (auto obj = m_obj) {
        // if parent is managed...
        if constexpr (type_info::OptionalObjectPersistable<T>) {
            // if object...
            if (auto link = o) {
                // if non null object is being assigned...
                if (link->m_obj) {
                    // if object is managed, we will to set the link
                    // to the new target's key
                    obj->template set<type>(managed, link->m_obj->get_key());
                } else {
                    // else new unmanaged object is being assigned.
                    // we must assign the values to this object's fields
                    // TODO:
                }
            } else {
                // else null link is being assigned to this field
                // e.g., `person.dog = std::nullopt;`
                // set the parent column to null and unset the co
                obj->set_null(managed);
            }
        } else {
            obj->template set<type>(managed, o);
        }
    } else {
        new (&unmanaged) T(o);
    }
    return *this;
}

template <realm::type_info::Persistable T>
persisted_base<T>& persisted_base<T>::operator=(const persisted_base& o) {
    if (auto obj = o.m_obj) {
        m_obj = obj;
        new (&managed) ColKey(o.managed);
    } else {
        new (&unmanaged) T(o.unmanaged);
    }
    return *this;
}

template <realm::type_info::Persistable T>
persisted_base<T>& persisted_base<T>::operator=(persisted_base&& o) {
    if (o.m_obj) {
        m_obj = o.m_obj;
        new (&managed) ColKey(std::move(o.managed));
    } else {
        new (&unmanaged) T(std::move(o.unmanaged));
    }
    return *this;
}

template <realm::type_info::Persistable T>
T persisted_base<T>::operator *() const
{
    if (m_obj) {
        if constexpr (type_info::OptionalPersistable<T>) {
            if constexpr (type_info::ObjectPersistable<typename T::value_type>) {
                return T::value_type::schema::create(m_obj->get_linked_object(managed), nullptr);
            } else {
                auto value = m_obj->template get<type>(managed);
                // convert optionals
                if (value) {
                    return T(*value);
                } else {
                    return T();
                }
            }
        } else {
            if constexpr (type_info::ListPersistable<T>) {
                T v;
                auto lst = m_obj->template get_list_values<typename type_info::persisted_type<typename T::value_type>::type>(managed);
                for (size_t i; i < lst.size(); i++) {
                    if constexpr (type_info::ObjectPersistable<typename T::value_type>) {
                        auto obj = lst.get_object(i);
                        v.push_back(T::value_type::schema::create(obj, obj.get_table(), nullptr));
                    } else {
                        v.push_back(static_cast<typename T::value_type>(lst[i]));
                    }
                }

                return v;
            } else {
                return static_cast<T>(m_obj->template get<type>(managed));
            }
        }
    } else {
        return unmanaged;
    }
}
// MARK: Equatable
template <typename T>
concept Equatable = requires (T a) {
    a == a;
};

template <realm::type_info::Persistable T>
bool operator==(const persisted<T>& a, const T& b) requires (Equatable<T>)
{
    return *a == b;
}
template <realm::type_info::Persistable T>
bool operator!=(const persisted<T>& a, const T& b) requires (Equatable<T>)
{
    return !(a == b);
}
template <realm::type_info::Persistable T>
bool operator==(const persisted<T>& a, const char* b) requires (Equatable<T>)
{
    return *a == b;
}
template <realm::type_info::Persistable T>
bool operator!=(const persisted<T>& a, const char* b) requires (Equatable<T>)
{
    return !(a == b);
}
template <realm::type_info::Persistable T>
typename persisted_base<T>::type persisted_base<T>::as_core_type() const
{
    if constexpr (type_info::ObjectPersistable<T>) {
        if (m_obj) {
            return m_obj->template get<type>(managed);
        } else {
            REALM_ASSERT(false);
            return ObjKey{}; /* should never happen */
        }
    } else {
        if (m_obj) {
            if constexpr (type_info::ListPersistable<T>) {
                return m_obj->template get_list_values<typename type_info::persisted_type<typename T::value_type>::type>(managed);
            } else {
                return m_obj->template get<type>(managed);
            }
        } else {
            return type_info::convert_if_required<T>(unmanaged);
        }
    }
}

template <type_info::NonContainerPersistable T>
void persisted_noncontainer_base<T>::operator -=(const T& a) requires (type_info::IntPersistable<T> || type_info::DoublePersistable<T>) {
    if (this->m_obj) {
        this->m_obj->template set<type>(this->managed, *(*this) - a);
    } else {
        this->unmanaged -= a;
    }
}

template <realm::type_info::NonContainerPersistable T>
void persisted_noncontainer_base<T>::operator --() requires (type_info::IntPersistable<T> || type_info::DoublePersistable<T>) {
    *this -= 1;
}

template <realm::type_info::NonContainerPersistable T>
T persisted_noncontainer_base<T>::operator -() requires (type_info::IntPersistable<T> || type_info::DoublePersistable<T>) {
    return *this * -1;
}

template <realm::type_info::NonContainerPersistable T>
void persisted_noncontainer_base<T>::operator +=(const T& a) requires (type_info::AddAssignable<T>) {
    if (this->m_obj) {
        this->m_obj->template set<type>(this->managed, *(*this) + a);
    } else {
        this->unmanaged += a;
    }
}

template <realm::type_info::NonContainerPersistable T>
T persisted_noncontainer_base<T>::operator *(const T& a) requires (type_info::AddAssignable<T>) {
    return **this * a;
}

template <realm::type_info::NonContainerPersistable T>
void persisted_noncontainer_base<T>::operator ++() requires (type_info::AddAssignable<T>) {
    *this += 1;
}
template <realm::type_info::NonContainerPersistable T>
bool persisted_noncontainer_base<T>::operator <(const T& a) requires (type_info::Comparable<T>) {
    return **this < a;
}
template <realm::type_info::NonContainerPersistable T>
bool persisted_noncontainer_base<T>::operator >(const T& a) requires (type_info::Comparable<T>) {
    return **this > a;
}
template <realm::type_info::NonContainerPersistable T>
bool persisted_noncontainer_base<T>::operator <=(const T& a) requires (type_info::Comparable<T>) {
    return **this <= a;
}
template <realm::type_info::NonContainerPersistable T>
bool persisted_noncontainer_base<T>::operator >=(const T& a) requires (type_info::Comparable<T>) {
    return **this >= a;
}

template <realm::type_info::ListPersistable T>
void persisted_container_base<T>::push_back(const typename T::value_type& a) requires (type_info::PrimitivePersistable<typename T::value_type>) {
    if (this->m_obj) {
        auto as_core_type = static_cast<typename type_info::persisted_type<typename T::value_type>::type>(a);
        auto lst = this->m_obj->template get_list<typename type_info::persisted_type<typename T::value_type>::type>(this->managed);
        lst.add(as_core_type);
    } else {
        this->unmanaged.push_back(a);
    }
}

template <realm::type_info::ListPersistable T>
void persisted_container_base<T>::push_back(typename T::value_type& a)
requires (type_info::ObjectPersistable<typename T::value_type>) {
    if (this->m_obj) {
        auto lst = this->m_obj->template get_list<typename type_info::persisted_type<typename T::value_type>::type>(this->managed);
        if (!a.m_obj) {
            T::value_type::schema::add(a, this->m_obj->get_table()->get_link_target(this->managed), nullptr);
        }
        lst.add(a.m_obj->get_key());
    } else {
        this->unmanaged.push_back(a);
    }
}

template <realm::type_info::ListPersistable T>
void persisted_container_base<T>::push_back(typename T::value_type&& a)
requires (type_info::ObjectPersistable<typename T::value_type>) {
    push_back(a);
}


template <realm::type_info::ListPersistable T>
typename T::value_type persisted_container_base<T>::operator[](typename T::size_type a)
requires (type_info::PrimitivePersistable<typename T::value_type>) {
    if (this->m_obj) {
        auto as_core_type = static_cast<typename type_info::persisted_type<typename T::value_type>::type>(a);
        auto lst = this->m_obj->template get_list<typename type_info::persisted_type<typename T::value_type>::type>(this->managed);
        return static_cast<typename T::value_type>(lst[a]);
    } else {
        return this->unmanaged[a];
    }
}

template <realm::type_info::ListPersistable T>
typename T::value_type persisted_container_base<T>::operator[](typename T::size_type a)
requires (type_info::ObjectPersistable<typename T::value_type>) {
    if (this->m_obj) {
        auto lst = this->m_obj->get_linklist(this->managed);
        return T::value_type::schema::create(lst.get_object(a), nullptr);
    } else {
        return this->unmanaged[a];
    }
}

template <realm::type_info::Persistable T>
void persisted_base<T>::assign(const Obj& object, const ColKey& col_key) {
    m_obj = object;
    new (&managed) ColKey(col_key);
}

}

#endif /* Header_h */
