#pragma once
#include <map>
#include <set>

namespace std
{
    template<typename T>
    struct less<slgc::gc<T>>
    {
        bool operator()(const slgc::gc<T>& a, const slgc::gc<T>& b) const { return *a < *b; }
    };
}

namespace slgc
{
    template<typename C>
    struct PtrEnumerator : details::ClassInfo::PtrEnumerator
    {
        size_t i;
        details::ClassInfo* cls;
        char* o;
        PtrEnumerator(details::ClassInfo* cls_, char* o_) :cls(cls_), o(o_), i(0) {}
        bool hasNext() override { return i < cls->memPtrOffsets.size(); }
        const details::PtrBase* getNext() override { return ( details::PtrBase*)( o + cls->memPtrOffsets[i++] ); }
    };

    namespace details
    {
        template<typename C>
        struct ContainerPtrEnumerator : ClassInfo::PtrEnumerator
        {
            C* o;
            typename C::iterator it;
            ContainerPtrEnumerator(ClassInfo* cls, char* o_) :o((C*)o_) { it = o->begin(); }
            bool hasNext() override { return it != o->end(); }
            
        };

        template<typename T>
        ClassInfo* ClassInfo::get()
        {
            auto alloc = [](ClassInfo* cls) { return new char[sizeof(T)]; };
            auto destroy = [](ClassInfo* cls, void* obj) { ( (T*)obj )->~T(); delete[](char*)obj; };
            auto enumPtrs = [](ClassInfo* cls, char* o) { return ( ClassInfo::PtrEnumerator* ) new slgc::PtrEnumerator<T>(cls, o); };
            static ClassInfo i{ alloc, destroy, sizeof(T), enumPtrs };
            return &i;
        }

        template<typename T, typename... Args>
        ObjMeta* createObj(Args&&... args)
        {
            ClassInfo* cls = ClassInfo::get<T>();
            cls->isCreatingObj++;
            auto* meta = cls->allocObj();
            new ( meta->objPtr ) T(std::forward<Args>(args)...);
            cls->state = ClassInfo::State::Registered;
            cls->isCreatingObj--;
            return meta;
        }
    }
    

    template<typename T, typename... Args>
    gc<T> make_gc(Args&&... args)
    {
        return details::createObj<T>(std::forward<Args>(args)...);
    }

    /// ============= Vector ================

    template<typename T>
    struct gc_vector : gc<std::vector<gc<T>>>
    {
        using gc::gc;
        gc<T>& operator[](int idx) { return ( *p )[idx]; }
    };
    
    template<typename T>
    struct PtrEnumerator<std::vector<gc<T>>> : details::ContainerPtrEnumerator<std::vector<gc<T>>>
    {        
        using ContainerPtrEnumerator::ContainerPtrEnumerator;
        const details::PtrBase* getNext() override { return &*it++; }
    };
    
    template<typename T, typename... Args>
    gc_vector<T> make_gc_vec(Args&&... args)
    {        
        return details::createObj<std::vector<gc<T>>>(std::forward<Args>(args)...);
    }

    /// ============= Map ================
    
    // NOT support using gc object as key...

    template<typename K, typename V>
    struct gc_map : gc<std::map<K, gc<V>>>
    {
        using gc::gc;
        gc<V>& operator[](const K& k) { return ( *p )[k]; }
    };

    template<typename K, typename V>
    struct PtrEnumerator<std::map<K, gc<V>>> : details::ContainerPtrEnumerator<std::map<K,gc<V>>>
    {
        using ContainerPtrEnumerator::ContainerPtrEnumerator;
        const details::PtrBase* getNext() override{ auto* ret = &it->second; ++it; return ret; }
    };
    
    template<typename K, typename V, typename... Args>
    gc_map<K, V> make_gc_map(Args&&... args)
    {        
        return details::createObj<std::map<K,gc<V>>>(std::forward<Args>(args)...);
    }

    /// ============= Set ================
      

    template<typename V>
    using gc_set = gc<std::set<gc<V>>>;

    template<typename V>
    struct PtrEnumerator<std::set<gc<V>>> : details::ContainerPtrEnumerator<std::set<gc<V>>>
    {
        using ContainerPtrEnumerator::ContainerPtrEnumerator;
        const details::PtrBase* getNext() override { return &*it++; }
    };

    template<typename V, typename... Args>
    gc_set<V> make_gc_set(Args&&... args)
    {        
        return details::createObj<std::set<gc<V>>>(std::forward<Args>(args)...);
    }
}

