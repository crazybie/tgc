/*
    A super lightweight incremental mark & sweep garbage collector.

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

namespace slgc
{
    struct Impl;

    namespace details
    {
        struct ObjMeta;
        
        class PtrBase
        {
            friend struct slgc::Impl;
        protected:
            mutable unsigned int    isRoot : 1;
            unsigned int            index : 31;
            ObjMeta*                meta;

            PtrBase();
            PtrBase(void* obj);
            ~PtrBase();
            void onPtrChanged();
        public:
            void setAsLeaf() const { isRoot = 0; }
        };

        struct ClassInfo
        {
            class PtrEnumerator
            {
            public:
                virtual ~PtrEnumerator() {}
                virtual bool hasNext() = 0;
                virtual const PtrBase* getNext() = 0;
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
            static int			isCreatingObj;
            static ClassInfo    Empty;

            ClassInfo(Alloc a, Dealloc d, int sz): alloc(a), dctor(d), size(sz), state(State::Unregistered){}
            ObjMeta* allocObj();
            bool containsPtr(char* obj, char* p) { return obj <= p && p < obj + size; }
            void registerSubPtr(ObjMeta* owner, PtrBase* p);

            template<typename T>
            static ClassInfo* get()
            {
                auto alloc = [](ClassInfo* cls) { return new char[sizeof(T)]; };
                auto destroy = [](ClassInfo* cls, void* obj) { ( (T*)obj )->~T(); delete[](char*)obj; };
                static ClassInfo i{ alloc, destroy, sizeof(T) };
                return &i;
            }
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

            explicit ObjMeta(ClassInfo* c, char* objPtr_) : objPtr(objPtr_), clsInfo(c), markState(MarkColor::Unmarked) {}
            ~ObjMeta() { if ( objPtr ) clsInfo->dctor(clsInfo, objPtr); }
            bool operator<(ObjMeta& r) const { return objPtr + clsInfo->size <= r.objPtr; }
        };
                

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

}




