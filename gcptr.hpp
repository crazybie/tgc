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
    namespace details
    {
        template<typename T>
        ClassInfo* ClassInfo::get()
        {
            auto alloc = [](ClassInfo* cls) { return new char[sizeof(T)]; };
            auto destroy = [](ClassInfo* cls, void* obj) { ( (T*)obj )->~T(); delete[](char*)obj; };            
            static ClassInfo i{ alloc, destroy, sizeof(T) };
            return &i;
        }
        
        template<typename T, typename... Args>
        ObjMeta* createObj(ClassInfo* cls, Args&&... args)
        {
            cls->isCreatingObj++;
            auto* meta = cls->allocObj();
            new ( meta->objPtr ) T(std::forward<Args>(args)...);
            cls->state = ClassInfo::State::Registered;
            cls->isCreatingObj--;
            return meta;
        }

        template<typename C>
        struct ContainerPtrEnumerator : ClassInfo::PtrEnumerator
        {
            C* o;
            typename C::iterator it;
            ContainerPtrEnumerator(char* o_) :o((C*)o_) { it = o->begin(); }
            bool hasNext() override { return it != o->end(); }
        };
    }
    

    template<typename T, typename... Args>
    gc<T> make_gc(Args&&... args)
    {
        using namespace details;
        ClassInfo* cls = ClassInfo::get<T>();
        cls->enumPtrs = [](ClassInfo* cls, char* o) {
            struct E : ClassInfo::PtrEnumerator
            {
                size_t i;
                ClassInfo* cls;
                char* o;
                E(ClassInfo* cls_, char* o_) :cls(cls_), o(o_), i(0) {}
                bool hasNext() { return i < cls->memPtrOffsets.size(); }
                PtrBase* getNext() { return (PtrBase*)( o + cls->memPtrOffsets[i++] ); }
            };
            return ( ClassInfo::PtrEnumerator* )new E{ cls, o };
        };
        return createObj<T>(cls, std::forward<Args>(args)...);
    }

    /// ============= Vector ================

    template<typename T>
    struct gc_vector : gc<std::vector<gc<T>>>
    {
        using gc::gc;
        gc<T>& operator[](int idx) { return ( *p )[idx]; }
    };

    
    template<typename T, typename... Args>
    gc_vector<T> make_gc_vec(Args&&... args)
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
        return createObj<C>(cls, std::forward<Args>(args)...);
    }

    /// ============= Map ================
    
    // NOT support using gc object as key...

    template<typename K, typename V>
    struct gc_map : gc<std::map<K, gc<V>>>
    {
        using gc::gc;
        gc<V>& operator[](const K& k) { return ( *p )[k]; }
    };

    template<typename K, typename V, typename... Args>
    gc_map<K, V> make_gc_map(Args&&... args)
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
        return createObj<C>(cls, std::forward<Args>(args)...);
    }

    /// ============= Set ================

  

    template<typename V>
    using gc_set = gc<std::set<gc<V>>>;

    template<typename V, typename... Args>
    gc_set<V> make_gc_set(Args&&... args)
    {
        using namespace details;
        typedef typename gc_set<V>::pointee C;

        ClassInfo* cls = ClassInfo::get<C>();
        cls->enumPtrs = [](ClassInfo* cls, char* o) {
            struct E : ContainerPtrEnumerator<C>
            {
                using ContainerPtrEnumerator::ContainerPtrEnumerator;
                const PtrBase* getNext() { return &*it++; }
            };
            return ( ClassInfo::PtrEnumerator* )new E(o);
        };
        return createObj<C>(cls, std::forward<Args>(args)...);
    }
}

