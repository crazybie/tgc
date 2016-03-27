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
            enum class State : char { Unregistered, Registered };

            typedef void(*Dctor)( ClassInfo* cls, void* obj );
            typedef char* ( *Alloc )( ClassInfo* cls );
            typedef size_t (*GetSubPtrCnt)( ClassInfo* cls, char* obj );
            typedef PtrBase* ( *GetSubPtr )( ClassInfo* cls, char* obj, int i );

            Alloc               alloc;
            Dctor               dctor;
            GetSubPtrCnt        getSubPtrCnt;
            GetSubPtr           getSubPtr;
            size_t              size;
            size_t              objCnt;
            std::vector<int>    memPtrOffsets;
            State               state;

            static bool			isCreatingObj;
            static ClassInfo    Empty;

            ClassInfo::ClassInfo(Alloc a, Dctor d, GetSubPtrCnt getSubPtrCnt_, GetSubPtr getSubPtr_, int sz)
                : alloc(a), dctor(d), getSubPtrCnt(getSubPtrCnt_), getSubPtr(getSubPtr_), size(sz), state(State::Unregistered)
            {}
            char* createObj(MetaInfo*& meta);
            bool containsPtr(char* obj, char* ptr) { return obj <= ptr && ptr < obj + size; }
            void registerSubPtr(MetaInfo* owner, PtrBase* ptr);

            template<typename T>
            static ClassInfo* get()
            {
                auto alloc = [](ClassInfo* cls) { cls->objCnt++;  return new char[sizeof(T)]; };
                auto destroy = [](ClassInfo* cls, void* obj) { cls->objCnt--; ( (T*)obj )->~T(); delete[](char*)obj; };
                auto getSubPtrCnt = [](ClassInfo* cls, char* obj) { return cls->memPtrOffsets.size(); };
                auto getSubPtr = [](ClassInfo* cls, char* obj, int i) { return (PtrBase*)( obj + cls->memPtrOffsets[i] ); };

                static ClassInfo i{ alloc, destroy, getSubPtrCnt, getSubPtr, sizeof(T) };
                return &i;
            }
        };
    };


    template <typename T>
    class gc_ptr : public details::PtrBase
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
        gc_ptr& operator=(decltype( nullptr )) { metaInfo = 0; ptr = 0; return *this; }

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

    template<typename T>
    gc_ptr<T> gc_from(T* t) { return gc_ptr<T>(t); }

    void gc_collect(int step);


    template<typename T, typename... Args>
    gc_ptr<T> make_gc(Args&&... args)
    {
        using namespace details;

        ClassInfo* cls = ClassInfo::get<T>();
        cls->isCreatingObj = true;
        MetaInfo* meta;
        char* buf = cls->createObj(meta);
        T* obj = new ( buf )T(std::forward<Args>(args)...);
        cls->state = ClassInfo::State::Registered;
        cls->isCreatingObj = false;
        return gc_ptr<T>(obj, meta);
    }

    template<typename T>
    struct gc_vector : private std::vector<gc_ptr<T>>
    {
        typedef gc_ptr<T> ptr;
        typedef vector<gc_ptr<T>> super;

        void push_back(const ptr& t) { super::push_back(t); back().isRoot = 0; }
        using super::size;

    private:
        gc_vector() {}

        template<typename T>
        friend gc_ptr<gc_vector<T>> make_gc_vector();
    };

    template<typename T>
    gc_ptr<gc_vector<T>> make_gc_vector()
    {
        using namespace details;
        typedef gc_vector<T> Array;

        ClassInfo* cls = ClassInfo::get<Array>();
        if ( cls->state == ClassInfo::State::Unregistered ) {
            cls->state = ClassInfo::State::Registered;
            cls->getSubPtrCnt = [](ClassInfo* cls, char* obj) { return ( (Array*)obj )->size(); };
            cls->getSubPtr = [](ClassInfo* cls, char* obj, int i)->PtrBase* { return &( *(Array*)obj )[i]; };
        }
        MetaInfo* meta;
        auto obj = cls->createObj(meta);
        auto array = new ( obj ) Array();
        return gc_ptr<Array>(array, meta);
    }
}

