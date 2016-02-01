/*
    A ref counted + mark & sweep garbage collector.

    Mostly objects will be recycled by the ref-counter,
    the circular referenced objects will be handled by the mark & sweep gc.

    Based on http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.
    Improvements:
    - add ref count
    - c++11 support
    - reduce unnecessary obj info searching.
    - replace std::map with hash table.
    - replace the operator new & delete with free factory function.
    TODO:
    - make it exception safe.
    - add compability for old compiler

    by crazybie at soniced@sina.com, shawn.li.song@gmail.com.
    */

#pragma once
#include <vector>

namespace gc
{
    namespace details
    {
        struct MetaInfo;

        class PointerBase
        {
        public:
            unsigned int    isRoot : 1;
            unsigned int    index : 31;
            MetaInfo*       metaInfo;

            PointerBase();
            PointerBase(void* obj);
            ~PointerBase();
            void onPointerChanged();
        };

        struct ClassInfo
        {
            enum class MemPtrState{ Unregistered, Registering, Registered };
            typedef void (*Dctor)(void*);

            Dctor               dctor;
            int                 size;
            std::vector<int>    memPtrOffsets;
            MemPtrState         memPtrState;
            static ClassInfo*   currentConstructing;
            static ClassInfo    Empty;

            ClassInfo(Dctor d, int sz);
            void* beginNewObj(int objSz, MetaInfo*& info);
            void endNewObj();
        };

        template<typename T>
        class ObjClassInfo
        {
        public:
            static void destroy(void* obj)
            {
                ((T*)obj)->~T();
            }
            static ClassInfo* get()
            {
                static ClassInfo i{ destroy, sizeof(T) };
                return &i;
            }
        };
    };


    template <typename T>
    class gc_ptr : protected details::PointerBase
    {
        typedef details::MetaInfo MetaInfo;
    public:
        // Constructors

        gc_ptr() : ptr(0) {}
        gc_ptr(T* obj, MetaInfo* info_) { reset(obj, info_); }
        explicit gc_ptr(T* obj) : PointerBase(obj), ptr(obj) {}
        template <typename U>
        gc_ptr(const gc_ptr<U>& r) { reset(r.ptr, r.metaInfo); }
        gc_ptr(const gc_ptr& r) { reset(r.ptr, r.metaInfo); }
        gc_ptr(gc_ptr&& r) { reset(r.ptr, r.metaInfo); r = nullptr; }

        // Operators

        template <typename U>
        gc_ptr& operator=(const gc_ptr<U>& r) { reset(r.ptr, r.metaInfo);  return *this; }
        gc_ptr& operator=(const gc_ptr& r) { reset(r.ptr, r.metaInfo);  return *this; }
        gc_ptr& operator=(gc_ptr&& r) { reset(r.ptr, r.metaInfo); r.metaInfo = 0; r.ptr = 0; return *this; }
        T* operator->() const { return ptr; }
        explicit operator bool() const { return ptr != 0; }
        bool operator==(const gc_ptr& r)const { return metaInfo == r.metaInfo; }
        bool operator!=(const gc_ptr& r)const { return metaInfo != r.metaInfo; }
        void operator=(T*) = delete;
        gc_ptr& operator=(decltype(nullptr)) { metaInfo = 0; ptr = 0; return *this; }

        // Methods

        void reset(T* o) { gc_ptr(o).swap(*this); }
        void reset(T* o, MetaInfo* n) { ptr = o; metaInfo = n; onPointerChanged(); }
        void swap(gc_ptr& r)
        {
            T* temp = ptr;
            MetaInfo* tinfo = metaInfo;
            reset(r.ptr, r.metaInfo);
            r.reset(temp, tinfo);
        }

    private:
        template <typename U>
        friend class gc_ptr;

        T*  ptr;
    };

    
    template<typename T, typename... Args>
    gc_ptr<T> make_gc(Args&&... args)
    {
        using namespace details;
        
        MetaInfo* meta;
        ClassInfo* cls = ObjClassInfo<T>::get();
        auto* buf = cls->beginNewObj(sizeof(T), meta);
        T* obj = new (buf)T(std::forward<Args>(args)...);
        cls->endNewObj();
        return gc_ptr<T>(obj, meta);
    }

    template<typename T>
    gc_ptr<T> gc_from_this(T* t) { return gc_ptr<T>(t); }

    void GcCollect(int step);
}

