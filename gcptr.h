/*
    TGC: Tiny incremental mark & sweep Garbage Collector.

    Inspired by http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.

    TODO:
    - exception safe.
    - thread safe.

    by crazybie at soniced@sina.com
    */

#pragma once
// TODO: crash if enable iterator debugging
#define _HAS_ITERATOR_DEBUGGING  0
#include <vector>
#include <map>
#include <set>
#include <list>
#include <deque>
#include <unordered_map>


namespace tgc
{
    struct Impl;

    namespace details
    {
        struct ObjMeta;
        
        class PtrBase
        {
            friend struct tgc::Impl;
        protected:
            mutable unsigned int    isRoot : 1;
            unsigned int            index : 31;
            ObjMeta*                meta;

            PtrBase();
            PtrBase(void* obj);
            ~PtrBase();
            void onPtrChanged();
        public:
            void setLeaf() const { isRoot = 0; }
        };

        class IPtrEnumerator
        {
        public:
            virtual ~IPtrEnumerator() {}
            virtual bool hasNext() = 0;
            virtual const PtrBase* getNext() = 0;
            void* operator new( size_t );
        };

        struct ClassInfo
        {
            enum class State : char { Unregistered, Registered };            
            typedef char* (*Alloc)(ClassInfo* cls);
            typedef void (*Dealloc)(ClassInfo* cls, void* obj);
            typedef IPtrEnumerator* (*EnumPtrs)(ClassInfo* cls, char*);

            Alloc               alloc;
            Dealloc             dctor;
            EnumPtrs            enumPtrs;
            size_t              size;
            std::vector<int>    memPtrOffsets;
            State               state;
            static int			isCreatingObj;
            static ClassInfo    Empty;

            ClassInfo(Alloc a, Dealloc d, int sz, EnumPtrs e): alloc(a), dctor(d), size(sz), enumPtrs(e),state(State::Unregistered){}
            ObjMeta* allocObj();
            bool containsPtr(char* obj, char* p) { return obj <= p && p < obj + size; }
            void registerSubPtr(ObjMeta* owner, PtrBase* p);

            template<typename T>
            static ClassInfo* get();
        };


        struct ObjMeta
        {            
            enum MarkColor : char { Unmarked,  Gray, Alive };

            ClassInfo*  clsInfo;
            char*       objPtr;
            MarkColor   markState;

            struct Less
            {
                bool operator()(ObjMeta* x, ObjMeta* y)const { return *x < *y; }
            };

            explicit ObjMeta(ClassInfo* c, char* o) : objPtr(o), clsInfo(c), markState(MarkColor::Unmarked) {}
            ~ObjMeta() { if ( objPtr ) clsInfo->dctor(clsInfo, objPtr); }
            bool operator<(ObjMeta& r) const { return objPtr + clsInfo->size <= r.objPtr; }
        };
                
    };


    template <typename T>
    class gc : public details::PtrBase
    {
    public:
        typedef T pointee;
        template<typename U> friend class gc;

    public:
        // Constructors

        gc() : p(nullptr) {}
        gc(details::ObjMeta* meta) { reset((T*)meta->objPtr, meta); }
        explicit gc(T* obj) : PtrBase(obj), p(obj) {}
        template <typename U>
        gc(const gc<U>& r) { reset(r.p, r.meta); }
        gc(const gc& r) { reset(r.p, r.meta); }
        gc(gc&& r) { reset(r.p, r.meta); r = nullptr; }

        // Operators

        template <typename U>
        gc& operator=(const gc<U>& r) { reset(r.p, r.meta);  return *this; }
        gc& operator=(const gc& r) { reset(r.p, r.meta);  return *this; }
        gc& operator=(gc&& r) { reset(r.p, r.meta); r.meta = 0; r.p = 0; return *this; }
        T* operator->() const { return p; }
        T& operator*() const { return *p; }
        explicit operator bool() const { return p != 0; }
        bool operator==(const gc& r)const { return meta == r.meta; }
        bool operator!=(const gc& r)const { return meta != r.meta; }
        void operator=(T*) = delete;
        gc& operator=(decltype(nullptr)) { meta = 0; p = 0; return *this; }

        // Methods

        void reset(T* o) { gc(o).swap(*this); }
        void reset(T* o, details::ObjMeta* n) { p = o; meta = n; onPtrChanged(); }
        void swap(gc& r)
        {
            auto* temp = p;
            auto* tempMeta = meta;
            reset(r.p, r.meta);
            r.reset(temp, tempMeta);
        }

    protected:        
        T*  p;
    };

    void gc_collect(int steps);
    
    template<typename T>
    gc<T> gc_from(T* t) { return gc<T>(t); }


    template<typename C>
    struct PtrEnumerator : details::IPtrEnumerator
    {
        size_t i;
        details::ClassInfo* cls;
        char* o;
        PtrEnumerator(details::ClassInfo* cls_, char* o_) :cls(cls_), o(o_), i(0) {}
        bool hasNext() override { return i < cls->memPtrOffsets.size(); }
        const details::PtrBase* getNext() override { return ( details::PtrBase* )( o + cls->memPtrOffsets[i++] ); }
    };

    namespace details
    {
        template<typename T>
        ClassInfo* ClassInfo::get()
        {
            auto alloc = [](ClassInfo* cls) { return new char[sizeof(T)]; };
            auto destroy = [](ClassInfo* cls, void* obj) { ( (T*)obj )->~T(); delete[](char*)obj; };
            auto enumPtrs = [](ClassInfo* cls, char* o) { return ( IPtrEnumerator* ) new PtrEnumerator<T>(cls, o); };
            static ClassInfo i{ alloc, destroy, sizeof(T), enumPtrs };
            return &i;
        }

        template<typename C>
        struct ContainerPtrEnumerator : IPtrEnumerator
        {
            C* o;
            typename C::iterator it;
            ContainerPtrEnumerator(ClassInfo* cls, char* o_) :o((C*)o_) { it = o->begin(); }
            bool hasNext() override { return it != o->end(); }
        };
    }

    template<typename T, typename... Args>
    gc<T> gc_new(Args&&... args)
    {
        using namespace details;
        ClassInfo* cls = ClassInfo::get<T>();
        cls->isCreatingObj++;
        auto* meta = cls->allocObj();
        new ( meta->objPtr ) T(std::forward<Args>(args)...);
        cls->state = ClassInfo::State::Registered;
        cls->isCreatingObj--;
        return meta;
    }
    



    //=================================================================================================
    // Wrap STL Containers
    //=================================================================================================

    template<typename T>
    struct gc_func;

    template<typename R, typename... A>
    struct gc_func<R(A...)>
    {
        struct Callable
        {
            virtual ~Callable(){}
            virtual R call(A... a) = 0;
        };

        gc<Callable> callable;

        template<typename F>
        struct Imp : Callable
        {
            F f;
            Imp(F& ff) : f(ff) {}
            R call(A... a) override { return f(a...); }
        };

        gc_func() {}

        template<typename F>
        void operator=(F& f) { callable = gc_new<Imp<F>>(f); }

        R operator()(A... a) { return callable->call(a...); }
    };

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
    gc_vector<T> gc_new_vector(Args&&... args)
    {
        return gc_new<std::vector<gc<T>>>(std::forward<Args>(args)...);
    }

    /// ============= Deque ================

    template<typename T>
    struct gc_deque: gc<std::deque<gc<T>>>
    {
        using gc::gc;
        gc<T>& operator[](int idx) { return ( *p )[idx]; }
    };

    template<typename T>
    struct PtrEnumerator<std::deque<gc<T>>> : details::ContainerPtrEnumerator<std::deque<gc<T>>>
    {
        using ContainerPtrEnumerator::ContainerPtrEnumerator;
        const details::PtrBase* getNext() override { return &*it++; }
    };

    template<typename T, typename... Args>
    gc_deque<T> gc_new_deque(Args&&... args)
    {
        return gc_new<std::deque<gc<T>>>(std::forward<Args>(args)...);
    }


    /// ============= List ================

    template<typename T>
    using gc_list = gc<std::list<gc<T>>>;

    template<typename T>
    struct PtrEnumerator<std::list<gc<T>>> : details::ContainerPtrEnumerator<std::list<gc<T>>>
    {
        using ContainerPtrEnumerator::ContainerPtrEnumerator;
        const details::PtrBase* getNext() override { return &*it++; }
    };

    template<typename T, typename... Args>
    gc_list<T> gc_new_list(Args&&... args)
    {
        return gc_new<std::list<gc<T>>>(std::forward<Args>(args)...);
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
    struct PtrEnumerator<std::map<K, gc<V>>> : details::ContainerPtrEnumerator<std::map<K, gc<V>>>
    {
        using ContainerPtrEnumerator::ContainerPtrEnumerator;
        const details::PtrBase* getNext() override { auto* ret = &it->second; ++it; return ret; }
    };

    template<typename K, typename V, typename... Args>
    gc_map<K, V> gc_new_map(Args&&... args)
    {
        return gc_new<std::map<K, gc<V>>>(std::forward<Args>(args)...);
    }

    /// ============= HashMap ================

    // NOT support using gc object as key...

    template<typename K, typename V>
    struct gc_unordered_map : gc<std::unordered_map<K, gc<V>>>
    {
        using gc::gc;
        gc<V>& operator[](const K& k) { return ( *p )[k]; }
    };

    template<typename K, typename V>
    struct PtrEnumerator<std::unordered_map<K, gc<V>>> : details::ContainerPtrEnumerator<std::unordered_map<K, gc<V>>>
    {
        using ContainerPtrEnumerator::ContainerPtrEnumerator;
        const details::PtrBase* getNext() override { auto* ret = &it->second; ++it; return ret; }
    };

    template<typename K, typename V, typename... Args>
    gc_unordered_map<K, V> gc_new_unordered_map(Args&&... args)
    {
        return gc_new<std::unordered_map<K, gc<V>>>(std::forward<Args>(args)...);
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
    gc_set<V> gc_new_set(Args&&... args)
    {
        return gc_new<std::set<gc<V>>>(std::forward<Args>(args)...);
    }
}


namespace std
{
    template<typename T, typename U>
    bool operator<(const tgc::gc<T>& a, const tgc::gc<U>& b) { return *a < *b; }
}

