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
    gc<T> createObj(details::ClassInfo* cls, Args&&... args)
    {
        using namespace details;
        cls->isCreatingObj = true;
        auto* meta = cls->allocObj();
        new ( meta->objPtr ) T(std::forward<Args>(args)...);
        cls->state = ClassInfo::State::Registered;
        cls->isCreatingObj = false;
        return gc<T>(meta);
    }

    template<typename T, typename... Args>
    gc<T> make_gc(Args&&... args)
    {
        using namespace details;
        ClassInfo* cls = ClassInfo::get<T>();
        return createObj<T>(cls, std::forward<Args>(args)...);
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
    using gc_vector = gc<std::vector<gc<T>>>;

    
    template<typename T>
    gc_vector<T> make_gc_vec()
    {
        using namespace details;
        typedef typename gc_vector<T>::pointee C;

        ClassInfo* cls = ClassInfo::get<C>();
        cls->enumPtrs = [](ClassInfo* cls, char* o) {
            struct E : ContainerPtrEnumerator<C>
            {
                using ContainerPtrEnumerator<C>::ContainerPtrEnumerator;
                PtrBase* getNext() { return &*it++; }
            };
            return ( ClassInfo::PtrEnumerator* ) new E(o);
        };
        return createObj<C>(cls);
    }

    /// ============= Map ================
    
    template<typename K, typename V>
    using gc_map = gc<std::map<K, gc<V>>>;

    template<typename K, typename V>
    gc_map<K, V> make_gc_map()
    {
        using namespace details;
        typedef typename gc_map<K, V>::pointee C;

        ClassInfo* cls = ClassInfo::get<C>();
        cls->enumPtrs = [](ClassInfo* cls, char* o) {
            struct E : ContainerPtrEnumerator<C>
            {
                using ContainerPtrEnumerator::ContainerPtrEnumerator;
                PtrBase* getNext() { auto* ret = &it->second; ++it; return ret; }
            };
            return ( ClassInfo::PtrEnumerator* )new E(o);
        };
        return createObj<C>(cls);
    }
}

