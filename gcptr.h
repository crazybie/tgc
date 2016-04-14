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
            MetaInfo*       metaInfo;

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
                virtual ~SubPtrEnumerator(){}
                virtual bool hasNext() = 0;
                virtual PtrBase* getNext() = 0;
            };

            enum class State : char { Unregistered, Registered };

            typedef void(*Dctor)( ClassInfo* cls, void* obj );
            typedef char* ( *Alloc )( ClassInfo* cls );
            typedef SubPtrEnumerator* (*EnumSubPtrs )( ClassInfo* cls, char* );

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
            {}
            char* createObj(MetaInfo*& meta);
            bool containsPtr(char* obj, char* ptr) { return obj <= ptr && ptr < obj + size; }
            void registerSubPtr(MetaInfo* owner, PtrBase* ptr);

            template<typename T>
            static ClassInfo* get()
            {
                auto alloc = [](ClassInfo* cls) { cls->objCnt++;  return new char[sizeof(T)]; };
                auto destroy = [](ClassInfo* cls, void* obj) { cls->objCnt--; ( (T*)obj )->~T(); delete[](char*)obj; };
                auto enumSubPtrs = [] (ClassInfo* cls, char* o) ->SubPtrEnumerator*
                { 
                    struct T : SubPtrEnumerator
                    {
                        size_t i;
                        ClassInfo* cls;
                        char* o;
                        T(ClassInfo* cls_, char* o_):cls(cls_),o(o_),i(0){}
                        bool hasNext() { return i < cls->memPtrOffsets.size(); }
                        PtrBase* getNext() { return (PtrBase*)( o + cls->memPtrOffsets[i++] ); }
                    };
                    return new T{cls, o };
                };

                static ClassInfo i{ alloc, destroy, enumSubPtrs, sizeof(T) };
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
    struct vector : public std::vector<gc_ptr<T>>
    {
        typedef gc_ptr<T> elem;
        typedef std::vector<elem> super;

        void push_back(const elem& t) { super::push_back(t); back().isRoot = 0; }
        
    private:
        vector() {}

        template<typename T>
        friend gc_ptr<vector<T>> make_gc_vec();
    };

    template<typename T>
    using gc_vector = gc_ptr<vector<T>>;

    template<typename T>
    gc_vector<T> make_gc_vec()
    {
        using namespace details;
        typedef vector<T> C;

        ClassInfo* cls = ClassInfo::get<C>();
        if ( cls->state == ClassInfo::State::Unregistered ) {
            cls->state = ClassInfo::State::Registered;
            cls->enumSubPtrs = [](ClassInfo* cls, char* o) -> ClassInfo::SubPtrEnumerator* {
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
        auto array = new ( obj ) C();
        return gc_ptr<C>(array, meta);
    }


    template<typename K, typename V>
    struct map : public std::map<K, gc_ptr<V>>
    {
        typedef gc_ptr<V> elem;
        typedef std::map<K, elem> super;

        elem& operator[](const K& k) { auto& i = this->super::operator[](k); i.isRoot = 0; return i; }
        
    private:
        map() {}

        template<typename K, typename V>
        friend gc_ptr<map<K, V>> make_gc_map();
    };

    template<typename K, typename V>
    using gc_map = gc_ptr<map<K,V>>;

    template<typename K, typename V>
    gc_map<K,V> make_gc_map()
    {
        using namespace details;
        typedef map<K, V> C;

        ClassInfo* cls = ClassInfo::get<C>();
        if ( cls->state == ClassInfo::State::Unregistered ) {
            cls->state = ClassInfo::State::Registered;
            cls->enumSubPtrs = [](ClassInfo* cls, char* o) -> ClassInfo::SubPtrEnumerator* {
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
        auto array = new ( obj ) C();
        return gc_ptr<C>(array, meta);
    }
}


