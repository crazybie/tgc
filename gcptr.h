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
#include <map>

namespace slgc
{
    struct Meta;

    namespace details
    {
        class PtrBase
        {
        public:
            unsigned int    isRoot : 1;
            unsigned int    index : 31;
            Meta*           meta;

            PtrBase();
            PtrBase(void* obj);
            ~PtrBase();
            void onPtrChanged();
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
            static ClassInfo* get()
            {
                auto alloc = [](ClassInfo* cls) { cls->objCnt++;  return new char[sizeof(T)]; };
                auto destroy = [](ClassInfo* cls, void* obj) { cls->objCnt--; ((T*)obj)->~T(); delete[](char*)obj; };
                auto enumSubPtrs = [](ClassInfo* cls, char* o) ->SubPtrEnumerator*
                {
                    struct T : SubPtrEnumerator
                    {
                        size_t i;
                        ClassInfo* cls;
                        char* o;
                        T(ClassInfo* cls_, char* o_) :cls(cls_), o(o_), i(0) {}
                        bool hasNext() { return i < cls->memPtrOffsets.size(); }
                        PtrBase* getNext() { return (PtrBase*)(o + cls->memPtrOffsets[i++]); }
                    };
                    return new T{ cls, o };
                };

                static ClassInfo i{ alloc, destroy, enumSubPtrs, sizeof(T) };
                return &i;
            }
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
    gc<T> make_gc(Args&&... args)
    {
        using namespace details;

        ClassInfo* cls = ClassInfo::get<T>();
        cls->isCreatingObj = true;
        Meta* meta;
        char* buf = cls->createObj(meta);
        T* obj = new (buf)T(std::forward<Args>(args)...);
        cls->state = ClassInfo::State::Registered;
        cls->isCreatingObj = false;
        return gc<T>(obj, meta);
    }

    /// ============= Vector ================

    template<typename T>
    struct vector : public std::vector<T> {};

    template<typename T>
    struct gc_vector : public gc<vector<gc<T>>>
    {
        using super = gc<vector<gc<T>>>;
        using gc::gc;

        const gc<T>& operator[](int i) const { return (*p)[i]; }
        gc<T>& operator[](int i) { return (*p)[i]; }
    };

    template<typename T>
    struct vector<gc<T>> : public std::vector<gc<T>>
    {
        typedef gc<T> elem;
        typedef std::vector<elem> super;

        void push_back(const elem& t) { super::push_back(t); back().isRoot = 0; }
        void push_back(elem&& t) { super::push_back(std::move(t)); back().isRoot = 0; }
        void resize(int sz) { super::resize(sz); for ( auto& i : *this ) i.isRoot = 0; }

    private:
        vector() {}

        template<typename T>
        friend gc_vector<T> make_gc_vec();
    };
    
    template<typename T>
    gc_vector<T> make_gc_vec()
    {
        using namespace details;
        typedef vector<gc<T>> C;

        ClassInfo* cls = ClassInfo::get<C>();
        if (cls->state == ClassInfo::State::Unregistered) {
            cls->state = ClassInfo::State::Registered;
            cls->enumSubPtrs = [](ClassInfo* cls, char* o) -> ClassInfo::SubPtrEnumerator*
            {
                struct T : ClassInfo::SubPtrEnumerator
                {
                    C* o;
                    typename C::iterator it;
                    T(C* o_) :o(o_) { it = o->begin(); }
                    bool hasNext() { return it != o->end(); }
                    PtrBase* getNext() { return &*it++; }
                };
                return new T((C*)o);
            };
        }
        Meta* meta;
        auto obj = cls->createObj(meta);
        return gc_vector<T>(new (obj)C(), meta);
    }

    /// ============= Map ================

    template<typename K, typename V>
    struct map : public std::map<K, V> {};

    template<typename K, typename V>
    struct gc_map : public gc<map<K, gc<V>>>
    {
        using gc::gc;
        gc<V>& operator[](const K& i) { auto& r = ( *p )[i]; r.isRoot = 0; return r; }
    };

    template<typename K, typename V>
    struct map<K, gc<V>> : public std::map<K, gc<V>>
    {
        typedef gc<V> elem;
        typedef std::map<K, elem> super;
        typedef typename super::value_type value_type;

        void insert(const value_type& v) { super::insert(v).first->second.isRoot = 0; }

    private:
        map() {}

        template<typename K, typename V>
        friend gc_map<K, V> make_gc_map();
    };

    template<typename K, typename V>
    gc_map<K, V> make_gc_map()
    {
        using namespace details;
        typedef map<K, gc<V>> C;

        ClassInfo* cls = ClassInfo::get<C>();
        if (cls->state == ClassInfo::State::Unregistered) {
            cls->state = ClassInfo::State::Registered;
            cls->enumSubPtrs = [](ClassInfo* cls, char* o) -> ClassInfo::SubPtrEnumerator*
            {
                struct T : ClassInfo::SubPtrEnumerator
                {
                    C* o;
                    typename C::iterator it;
                    T(C* o_) :o(o_) { it = o->begin(); }
                    bool hasNext() { return it != o->end(); }
                    PtrBase* getNext() { auto* ret = &it->second; ++it; return ret; }
                };
                return new T((C*)o);
            };
        }
        Meta* meta;
        auto obj = cls->createObj(meta);        
        return gc_map<K,V>(new (obj)C(), meta);
    }
}


