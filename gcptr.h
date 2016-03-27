/*
    A super fast incremental mark & sweep garbage collector.
	  
    Based on http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.
    
	TODO:
    - exception safe.
    - thread safe.
    
    by crazybie at soniced@sina.com
    */

#pragma once
#include <vector>

namespace gc
{
    struct MetaInfo;

    namespace details
    {
        class PtrBase
        {
        public:
            unsigned int    isRoot : 1;
            unsigned int    index : 31;
            MetaInfo*       metaInfo;

            PtrBase();
            PtrBase(void* obj);
            ~PtrBase();
            void onPtrChanged();
        };

        struct ClassInfo
        {
            enum class MemPtrState { Unregistered, Registered };
            typedef void (*Dctor)(ClassInfo* cls, void* obj);
            typedef char* (*Alloc)(ClassInfo* cls, int sz);

            Alloc               alloc;
            Dctor               dctor;
            int                 size;
            int                 objCnt;
            std::vector<int>    memPtrOffsets;
            MemPtrState         memPtrState;

            static bool			isCreatingObj;
            static ClassInfo    Empty;

			ClassInfo(Alloc a, Dctor d, int sz);
            void* beginNewObj(int objSz, MetaInfo*& info);
            void endNewObj();

            bool containsPtr(char* obj, char* ptr) { return obj <= ptr && ptr < obj + size; }
            int getSubPtrCnt() { return memPtrOffsets.size(); }
            PtrBase* getSubPtr(char* base, int i) { return (PtrBase*)(base + memPtrOffsets[i]); }            
            void registerSubPtr(MetaInfo* owner, PtrBase* ptr);

            template<typename T>
            static ClassInfo* get()
            {
                auto alloc = [](ClassInfo* cls, int sz) { cls->objCnt++;  return new char[sz]; };
                auto destroy = [](ClassInfo* cls, void* obj) { cls->objCnt--; ((T*)obj)->~T(); delete[] (char*)obj; };
                static ClassInfo i{ alloc, destroy, sizeof(T) };
                return &i;
            }
        };
    };


    template <typename T>
    class gc_ptr : protected details::PtrBase
    {        
    public:
        // Constructors

        gc_ptr() : ptr(0) {}
        gc_ptr(T* obj, MetaInfo* info) { reset(obj, info); }
        explicit gc_ptr(T* obj) : PtrBase(obj), ptr(obj) {}
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
        T& operator*() const { return *ptr; }
        explicit operator bool() const { return ptr != 0; }
        bool operator==(const gc_ptr& r)const { return metaInfo == r.metaInfo; }
        bool operator!=(const gc_ptr& r)const { return metaInfo != r.metaInfo; }
        void operator=(T*) = delete;
        gc_ptr& operator=(decltype(nullptr)) { metaInfo = 0; ptr = 0; return *this; }

        // Methods

        void reset(T* o) { gc_ptr(o).swap(*this); }
        void reset(T* o, MetaInfo* n) { ptr = o; metaInfo = n; onPtrChanged(); }
        void swap(gc_ptr& r)
        {
            T* temp = ptr;
            MetaInfo* tinfo = metaInfo;
            reset(r.ptr, r.metaInfo);
            r.reset(temp, tinfo);
        }

    private:
        template<typename U>
        friend class gc_ptr;

        T*  ptr;
    };

    
    template<typename T, typename... Args>
    gc_ptr<T> make_gc(Args&&... args)
    {
        using namespace details;        
        MetaInfo* meta;
        ClassInfo* cls = ClassInfo::get<T>();
        auto* buf = cls->beginNewObj(sizeof(T), meta);
        T* obj = new (buf)T(std::forward<Args>(args)...);
        cls->endNewObj();
        return gc_ptr<T>(obj, meta);
    }

    template<typename T>
    gc_ptr<T> gc_from(T* t) { return gc_ptr<T>(t); }

    void gc_collect(int step);


    template<typename T>
    struct gc_array
    {
        T* buf;
        int length;
        //static
    };

    template<typename T>
    gc_array<T> make_array()
    {
        using namespace details;
        return gc_array<T>();
    }

        
}

