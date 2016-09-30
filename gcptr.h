/*
    A super lightweight incremental mark & sweep garbage collector.

    Inspired by http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.

    TODO:
    - exception safe.
    - thread safe.

    by crazybie at soniced@sina.com
    */

#pragma once
#include <vector>


namespace slgc
{
    struct Impl;

    namespace details
    {
        struct Meta;
        
        class PtrBase
        {
            friend struct slgc::Impl;
        protected:
            unsigned int    isRoot : 1;
            unsigned int    index : 31;
            Meta*           meta;

            PtrBase();
            PtrBase(void* obj);
            ~PtrBase();
            void onPtrChanged();
        public:
            void setAsLeaf() { isRoot = 0; }
        };

        struct ClassInfo
        {
            class PtrEnumerator
            {
            public:
                virtual ~PtrEnumerator() {}
                virtual bool hasNext() = 0;
                virtual PtrBase* getNext() = 0;
                void* operator new( size_t );
            };

            enum class State : char { Unregistered, Registered };            
            typedef char* (*Alloc)(ClassInfo* cls);
            typedef void (*Dealloc)(ClassInfo* cls, void* obj);
            typedef PtrEnumerator* (*EnumPtrs)(ClassInfo* cls, char*);

            Alloc               alloc;
            Dealloc             dctor;
            EnumPtrs            enumPtrs;
            size_t              size;
            std::vector<int>    memPtrOffsets;
            State               state;
            static bool			isCreatingObj;
            static ClassInfo    Empty;

            ClassInfo(Alloc a, Dealloc d, EnumPtrs enumSubPtrs_, int sz)
                : alloc(a), dctor(d), enumPtrs(enumSubPtrs_), size(sz), state(State::Unregistered){}
            Meta* createObj();
            bool containsPtr(char* obj, char* p) { return obj <= p && p < obj + size; }
            void registerSubPtr(Meta* owner, PtrBase* p);

            template<typename T>
            static ClassInfo* get();
        };


        struct Meta
        {
            enum MarkColor : char { White, Gray, Black };

            ClassInfo*  clsInfo;
            char*       objPtr;
            MarkColor   color;

            struct Less
            {
                bool operator()(Meta* x, Meta* y)const { return *x < *y; }
            };

            explicit Meta(ClassInfo* c, char* objPtr_) : objPtr(objPtr_), clsInfo(c), color(MarkColor::White) {}
            ~Meta() { if ( objPtr ) clsInfo->dctor(clsInfo, objPtr); }
            bool operator<(Meta& r) const { return objPtr + clsInfo->size <= r.objPtr; }
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
        gc(details::Meta* info) { reset((T*)info->objPtr, info); }
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
        void reset(T* o, details::Meta* n) { p = o; meta = n; onPtrChanged(); }
        void swap(gc& r)
        {
            T* temp = p;
            auto* tinfo = meta;
            reset(r.p, r.meta);
            r.reset(temp, tinfo);
        }

    protected:        
        T*  p;
    };

    void gc_collect(int steps);
    
    template<typename T>
    gc<T> gc_from(T* t) { return gc<T>(t); }

    template<typename T, typename... Args>
    gc<T> make_gc(Args&&... args);

}

#include "gcptr.hpp"


