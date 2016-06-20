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


namespace slgc
{
    struct Meta;
    struct Impl;

    namespace details
    {
        class PtrBase
        {
        protected:
            unsigned int    isRoot : 1;
            unsigned int    index : 31;
            Meta*           meta;

            PtrBase();
            PtrBase(void* obj);
            ~PtrBase();
            void onPtrChanged();

            friend struct Impl;

        public:
            void setAsRoot(bool v) { isRoot = v; }
        };

        struct ClassInfo
        {
            class SubPtrEnumerator
            {
            public:
                virtual ~SubPtrEnumerator() {}
                virtual bool hasNext() = 0;
                virtual PtrBase* getNext() = 0;
            };

            enum class State : char { Unregistered, Registered };

            typedef void(*Dctor)(ClassInfo* cls, void* obj);
            typedef char* (*Alloc)(ClassInfo* cls);
            typedef SubPtrEnumerator* (*EnumSubPtrs)(ClassInfo* cls, char*);

            Alloc               alloc;
            Dctor               dctor;
            EnumSubPtrs         enumSubPtrs;
            size_t              size;
            size_t              objCnt;
            std::vector<int>    memPtrOffsets;
            State               state;

            static bool			isCreatingObj;
            static ClassInfo    Empty;

            ClassInfo::ClassInfo(Alloc a, Dctor d, EnumSubPtrs enumSubPtrs_, int sz)
                : alloc(a), dctor(d), enumSubPtrs(enumSubPtrs_), size(sz), state(State::Unregistered)
            {
            }
            char* createObj(Meta*& meta);
            bool containsPtr(char* obj, char* p) { return obj <= p && p < obj + size; }
            void registerSubPtr(Meta* owner, PtrBase* p);

            template<typename T>
            static ClassInfo* get();
        };
    };


    template <typename T>
    class gc : public details::PtrBase
    {
    public:
        // Constructors

        gc() : p(0) {}
        gc(T* obj, Meta* info) { reset(obj, info); }
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
        void reset(T* o, Meta* n) { p = o; meta = n; onPtrChanged(); }
        void swap(gc& r)
        {
            T* temp = p;
            Meta* tinfo = meta;
            reset(r.p, r.meta);
            r.reset(temp, tinfo);
        }

    protected:
        template<typename U>
        friend class gc;

        T*  p;
    };

    template<typename T>
    gc<T> gc_from(T* t) { return gc<T>(t); }

    void gc_collect(int step);

    template<typename T, typename... Args>
    gc<T> make_gc(Args&&... args);

}

#include "gcptr.hpp"


