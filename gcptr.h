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
            MetaInfo*       meta;

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
            char* createObj(MetaInfo*& meta);
            bool containsPtr(char* obj, char* p) { return obj <= p && p < obj + size; }
            void registerSubPtr(MetaInfo* owner, PtrBase* p);

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
    class ptr : public details::PtrBase
    {
    public:
        // Constructors

        ptr() : p(0) {}
        ptr(T* obj, MetaInfo* info) { reset(obj, info); }
        explicit ptr(T* obj) : PtrBase(obj), p(obj) {}
        template <typename U>
        ptr(const ptr<U>& r) { reset(r.p, r.meta); }
        ptr(const ptr& r) { reset(r.p, r.meta); }
        ptr(ptr&& r) { reset(r.p, r.meta); r = nullptr; }

        // Operators

        template <typename U>
        ptr& operator=(const ptr<U>& r) { reset(r.p, r.meta);  return *this; }
        ptr& operator=(const ptr& r) { reset(r.p, r.meta);  return *this; }
        ptr& operator=(ptr&& r) { reset(r.p, r.meta); r.meta = 0; r.p = 0; return *this; }
        T* operator->() const { return p; }
        T& operator*() const { return *p; }
        T& operator()() const { return *p; } // support vec_ptr()[1] rather than (*vec_ptr)[1]
        explicit operator bool() const { return p != 0; }
        bool operator==(const ptr& r)const { return meta == r.meta; }
        bool operator!=(const ptr& r)const { return meta != r.meta; }
        void operator=(T*) = delete;
        ptr& operator=(decltype(nullptr)) { meta = 0; p = 0; return *this; }

        // Methods

        void reset(T* o) { ptr(o).swap(*this); }
        void reset(T* o, MetaInfo* n) { p = o; meta = n; onPtrChanged(); }
        void swap(ptr& r)
        {
            T* temp = p;
            MetaInfo* tinfo = meta;
            reset(r.p, r.meta);
            r.reset(temp, tinfo);
        }

    private:
        template<typename U>
        friend class ptr;

        T*  p;
    };

    template<typename T>
    ptr<T> gc_from(T* t) { return ptr<T>(t); }

    void gc_collect(int step);

    template<typename T, typename... Args>
    ptr<T> make_gc(Args&&... args)
    {
        using namespace details;

        ClassInfo* cls = ClassInfo::get<T>();
        cls->isCreatingObj = true;
        MetaInfo* meta;
        char* buf = cls->createObj(meta);
        T* obj = new (buf)T(std::forward<Args>(args)...);
        cls->state = ClassInfo::State::Registered;
        cls->isCreatingObj = false;
        return ptr<T>(obj, meta);
    }

    /// ============= Vector ================

    template<typename T>
    struct vector : public std::vector<T> {};

    template<typename T>
    using vector_ptr = ptr<vector<T>>;

    template<typename T>
    struct vector<ptr<T>> : public std::vector<ptr<T>>
    {
        typedef ptr<T> elem;
        typedef std::vector<elem> super;

        void push_back(const elem& t) { super::push_back(t); back().isRoot = 0; }
        void push_back(elem&& t) { super::push_back(t); back().isRoot = 0; }

    private:
        vector() {}

        template<typename T>
        friend vector_ptr<ptr<T>> make_gc_vec();
    };
    
    template<typename T>
    vector_ptr<ptr<T>> make_gc_vec()
    {
        using namespace details;
        typedef vector<ptr<T>> C;

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
        MetaInfo* meta;
        auto obj = cls->createObj(meta);
        return ptr<C>(new (obj)C(), meta);
    }

    /// ============= Map ================

    template<typename K, typename V>
    struct map : public std::map<K, V> {};

    template<typename K, typename V>
    using map_ptr = ptr<map<K, V>>;

    template<typename K, typename V>
    struct map<K, ptr<V>> : public std::map<K, ptr<V>>
    {
        typedef ptr<V> elem;
        typedef std::map<K, elem> super;
        typedef typename super::value_type value_type;

        elem& operator[](const K& k) { auto& i = super::operator[](k); i.isRoot = 0; return i; }
        void insert(const value_type& v) { super::insert(v)->second.isRoot = 0; }

    private:
        map() {}

        template<typename K, typename V>
        friend map_ptr<K, ptr<V> > make_gc_map();
    };

    template<typename K, typename V>
    map_ptr<K, ptr<V>> make_gc_map()
    {
        using namespace details;
        typedef map<K, ptr<V>> C;

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
        MetaInfo* meta;
        auto obj = cls->createObj(meta);        
        return ptr<C>(new (obj)C(), meta);
    }
}


