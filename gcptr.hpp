#pragma once
#include <map>



namespace std
{
    template<typename T>
    class SimpleAllocator
    {
    public:
        typedef size_t      size_type;
        typedef ptrdiff_t   difference_type;
        typedef T*          pointer;
        typedef const T*    const_pointer;
        typedef T&          reference;
        typedef const T&    const_reference;
        typedef T           value_type;

        template<typename U>
        struct rebind
        {
            typedef allocator<U> other;
        };

        pointer allocate(size_t n) { return reinterpret_cast<pointer>( ::operator new( sizeof(value_type) * n ) ); }
        pointer address(reference __x) const { return &__x; }
        const_pointer address(const_reference x) const { return &x; }
        void deallocate(pointer p, size_type n) { ::operator delete( p ); }
        size_type max_size() const throw( ) { return std::numeric_limits<size_type>::max(); }
        void destroy(pointer p) { p->~value_type(); }
        void construct(pointer p, value_type&& v) { ::new( p ) value_type(std::move(v)); }
    };

    template<typename T>
    class allocator< slgc::gc<T> > : public SimpleAllocator<slgc::gc<T>>
    {
    public:
        void construct(pointer p, value_type&& v)
        {
            auto r = ::new( p ) value_type(std::move(v));
            r->setAsLeaf();
        }
    };

    template<typename K, typename T>
    using map_node = typename _Tree_node<pair<K, T>, typename allocator<T>::pointer>;

    template<typename T>
    class Allocator : public SimpleAllocator<T>{};

    template<typename K, typename T>
    class Allocator<pair<const K, slgc::gc<T> >> : public SimpleAllocator<pair<const K, slgc::gc<T>>>
    {        
    public:  
        template<typename U>
        struct rebind
        {
            typedef Allocator<U> other;
        };

        Allocator()
        {

        }
        void construct(pointer p, value_type&& v)
        {
            auto r = ::new( p ) value_type(std::move(v));
            r._Myval.second->setAsLeaf();
        }
    };
}

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
        Meta* meta = cls->createObj();
        T* obj = new ( meta->objPtr )T(std::forward<Args>(args)...);
        cls->state = ClassInfo::State::Registered;
        cls->isCreatingObj = false;
        return{ meta };
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
        Meta* meta = cls->createObj();
        new ( meta->objPtr )C();
        return{ meta };
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
        Meta* meta = cls->createObj();
        new ( meta->objPtr )C();
        return { meta };
    }
}

