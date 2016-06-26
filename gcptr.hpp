#pragma once
#include <map>

namespace slgc
{
    template<typename T>
    details::ClassInfo* details::ClassInfo::get()
    {
        auto alloc = [](ClassInfo* cls) { return new char[sizeof(T)]; };
        auto destroy = [](ClassInfo* cls, void* obj) { ( (T*)obj )->~T(); delete[](char*)obj; };
        auto enumSubPtrs = [](ClassInfo* cls, char* o) ->PtrEnumerator* {
            struct E : details::ClassInfo::PtrEnumerator
            {
                size_t i;
                ClassInfo* cls;
                char* o;
                E(ClassInfo* cls_, char* o_) :cls(cls_), o(o_), i(0) {}
                bool hasNext() { return i < cls->memPtrOffsets.size(); }
                PtrBase* getNext() { return (PtrBase*)( o + cls->memPtrOffsets[i++] ); }
            };
            return new E{ cls, o };
        };

        static ClassInfo i{ alloc, destroy, enumSubPtrs, sizeof(T) };
        return &i;
    }

    template<typename T, typename... Args>
    gc<T> make_gc(Args&&... args)
    {
        using namespace details;

        ClassInfo* cls = ClassInfo::get<T>();
        cls->isCreatingObj = true;
        Meta* meta;
        char* buf = cls->createObj(meta);
        T* obj = new ( buf )T(std::forward<Args>(args)...);
        cls->state = ClassInfo::State::Registered;
        cls->isCreatingObj = false;
        return{ obj, meta };
    }

    template<typename C>
    struct ContainerPtrEnumerator : details::ClassInfo::PtrEnumerator
    {
        C* o;
        typename C::iterator it;
        ContainerPtrEnumerator(char* o_) :o((C*)o_) { it = o->begin(); }
        bool hasNext() override { return it != o->end(); }        
    };

    /// ============= Vector ================

    template<typename T>
    struct vector : public std::vector<T> {};

    template<typename T>
    struct gc_vector : public gc<vector<gc<T>>>
    {
        using super = gc<vector<gc<T>>>;
        using gc::gc;

        const gc<T>& operator[](int i) const { return ( *p )[i]; }
        gc<T>& operator[](int i) { return ( *p )[i]; }
    };

    template<typename T>
    struct vector<gc<T>> : public std::vector<gc<T>>
    {
        typedef gc<T> elem;
        typedef std::vector<elem> super;

        void push_back(const elem& t) { super::push_back(t); back().setNonRoot(); }
        void push_back(elem&& t) { super::push_back(std::move(t)); back().setNonRoot(); }
        void resize(int sz) { super::resize(sz); for ( auto& i : *this ) i.setNonRoot(); }
        // TODO: other insert functions        

    private:
        vector() {}

        template<typename T>
        friend gc_vector<T> make_gc_vec();
    };

    template<typename T>
    gc_vector<T> make_gc_vec()
    {
        using namespace details;
        typedef vector<gc<T>> C;

        ClassInfo* cls = ClassInfo::get<C>();
        cls->enumPtrs = [](ClassInfo* cls, char* o) {
            struct E : ContainerPtrEnumerator<C>
            {
                using ContainerPtrEnumerator::ContainerPtrEnumerator;
                PtrBase* getNext() { return &*it++; }
            };
            return ( ClassInfo::PtrEnumerator* ) new E(o);
        };
        Meta* meta;
        auto obj = cls->createObj(meta);
        return{ new ( obj )C(), meta };
    }

    /// ============= Map ================

    template<typename K, typename V>
    struct map : public std::map<K, V> {};

    template<typename K, typename V>
    struct gc_map : public gc<map<K, gc<V>>>
    {
        using gc::gc;
        gc<V>& operator[](const K& i) { auto& r = ( *p )[i]; r.setNonRoot(); return r; }
    };

    template<typename K, typename V>
    struct map<K, gc<V>> : public std::map<K, gc<V>>
    {
        typedef gc<V> elem;
        typedef std::map<K, elem> super;
        typedef typename super::value_type value_type;

        void insert(const value_type& v) { super::insert(v).first->second.setNonRoot(); }
        // TODO: other insert functions

    private:
        map() {}

        template<typename K, typename V>
        friend gc_map<K, V> make_gc_map();
    };

    template<typename K, typename V>
    gc_map<K, V> make_gc_map()
    {
        using namespace details;
        typedef map<K, gc<V>> C;

        ClassInfo* cls = ClassInfo::get<C>();
        cls->enumPtrs = [](ClassInfo* cls, char* o) {
            struct E : ContainerPtrEnumerator<C>
            {
                using ContainerPtrEnumerator::ContainerPtrEnumerator;
                PtrBase* getNext() { auto* ret = &it->second; ++it; return ret; }
            };
            return ( ClassInfo::PtrEnumerator* )new E(o);
        };
        Meta* meta;
        auto obj = cls->createObj(meta);
        return { new ( obj )C(), meta };
    }
}

