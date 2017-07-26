/*
    TGC: Tiny incremental mark & sweep Garbage Collector.

    Inspired by http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.

    TODO:
    - exception safe.
    - thread safe.

    by crazybie at soniced@sina.com
    */

#pragma once
// TODO: fix crash when enable iterator debugging
#define _HAS_ITERATOR_DEBUGGING  0
#include <vector>
#include <map>
#include <set>
#include <list>
#include <deque>
#include <unordered_map>


namespace tgc
{
    namespace details
    {
        using namespace std;

        class Impl;
        class ObjMeta;

        class PtrBase
        {
            friend class tgc::details::Impl;
        protected:
            mutable unsigned int    isRoot : 1;
            unsigned int            index : 31;
            ObjMeta*                meta;

            PtrBase();
            PtrBase(void* obj);
            ~PtrBase();
            void onPtrChanged();
        };

        class IPtrEnumerator
        {
        public:
            virtual ~IPtrEnumerator() {}
            virtual bool hasNext() = 0;
            virtual const PtrBase* getNext() = 0;
            void* operator new( size_t );
        };

        class ClassInfo
        {
        public:
            enum class State : char { Unregistered, Registered };
            typedef void(*Dealloc)( ClassInfo* cls, void* obj );
            typedef IPtrEnumerator* ( *EnumPtrs )( ClassInfo* cls, char* );

            Dealloc             dctor;
            EnumPtrs            enumPtrs;
            size_t              size;
            vector<int>         memPtrOffsets;
            State               state;
            static int			isCreatingObj;
            static ClassInfo    Empty;

            ClassInfo(Dealloc d, int sz, EnumPtrs e) : dctor(d), size(sz), enumPtrs(e), state(State::Unregistered) {}
            ObjMeta* allocObj(int size);
            bool containsPtr(char* obj, char* p) { return obj <= p && p < obj + size; }
            void registerSubPtr(ObjMeta* owner, PtrBase* p);

            template<typename T>
            static ClassInfo* get();
        };


        class ObjMeta
        {
        public:
            enum MarkColor : char { Unmarked, Gray, Alive };

            ClassInfo*  clsInfo;
            char*       objPtr;
            MarkColor   markState;

            class Less
            {
            public:
                bool operator()(ObjMeta* x, ObjMeta* y)const { return *x < *y; }
            };

            explicit ObjMeta(ClassInfo* c, char* o) : objPtr(o), clsInfo(c), markState(MarkColor::Unmarked) {}
            ~ObjMeta() { if ( objPtr ) clsInfo->dctor(clsInfo, objPtr); }
            bool operator<(ObjMeta& r) const { return objPtr + clsInfo->size <= r.objPtr; }
        };


        template <typename T>
        class GcPtr : public PtrBase
        {
        public:
            typedef T pointee;
            template<typename U> friend class GcPtr;

        public:
            // Constructors

			GcPtr():p(nullptr) {}
			GcPtr(ObjMeta* meta) { reset((T*)meta->objPtr, meta); }
			explicit GcPtr(T* obj) : PtrBase(obj), p(obj) {}
			template <typename U>
			GcPtr(const GcPtr<U>& r) { reset(r.p, r.meta); }
			GcPtr(const GcPtr& r) { reset(r.p, r.meta); }
			GcPtr(GcPtr&& r) { reset(r.p, r.meta); r = nullptr; }
            
            // Operators

            template <typename U>
            GcPtr& operator=(const GcPtr<U>& r) { reset(r.p, r.meta);  return *this; }
            GcPtr& operator=(const GcPtr& r) { reset(r.p, r.meta);  return *this; }
            GcPtr& operator=(GcPtr&& r) { reset(r.p, r.meta); r.meta = 0; r.p = 0; return *this; }
            T* operator->() const { return p; }
            T& operator*() const { return *p; }
            explicit operator bool() const { return p != 0; }
            bool operator==(const GcPtr& r)const { return meta == r.meta; }
            bool operator!=(const GcPtr& r)const { return meta != r.meta; }
            void operator=(T*) = delete;
            GcPtr& operator=(decltype( nullptr )) { meta = 0; p = 0; return *this; }
			bool operator<(const GcPtr& r) const { return *p < *r.p; }

            // Methods

            void reset(T* o) { GcPtr(o).swap(*this); }
            void reset(T* o, ObjMeta* n) { p = o; meta = n; onPtrChanged(); }
            void swap(GcPtr& r)
            {
                auto* temp = p;
                auto* tempMeta = meta;
                reset(r.p, r.meta);
                r.reset(temp, tempMeta);
            }

        protected:
            T*  p;
        };

		
		template<typename T>
		class gc : public GcPtr<T>
		{
			using base = GcPtr;
		public:
			using GcPtr::GcPtr;
			gc(){}
			gc(ObjMeta* o):base(o){}
		};




        void gc_collect(int steps);

        template<typename T>
        gc<T> gc_from(T* t) { return gc<T>(t); }


        template<typename C>
        class PtrEnumerator : public IPtrEnumerator
        {
        public:
            size_t i;
            ClassInfo* cls;
            char* o;
            PtrEnumerator(ClassInfo* cls_, char* o_) :cls(cls_), o(o_), i(0) {}
            bool hasNext() override { return i < cls->memPtrOffsets.size(); }
            const PtrBase* getNext() override { return ( PtrBase* )( o + cls->memPtrOffsets[i++] ); }
        };


        template<typename T>
        ClassInfo* ClassInfo::get()
        {
            auto destroy = [](ClassInfo* cls, void* obj) { ( (T*)obj )->~T(); delete[](char*)obj; };
            auto enumPtrs = [](ClassInfo* cls, char* o) { return ( IPtrEnumerator* ) new PtrEnumerator<T>(cls, o); };
            static ClassInfo i{ destroy, sizeof(T), enumPtrs };
            return &i;
        }
        
        template<typename T, typename... Args>
        gc<T> gc_new(Args&&... args)
        {            
            ClassInfo* cls = ClassInfo::get<T>();
            cls->isCreatingObj++;
            auto* meta = cls->allocObj(sizeof(T));
            new ( meta->objPtr ) T(forward<Args>(args)...);
            cls->state = ClassInfo::State::Registered;
            cls->isCreatingObj--;
            return meta;
        }





        //=================================================================================================
        // Wrap STL Containers
        //=================================================================================================


        template<typename C>
        class ContainerPtrEnumerator : public IPtrEnumerator
        {
        public:
            C* o;
            typename C::iterator it;
            ContainerPtrEnumerator(ClassInfo* cls, char* o_) :o((C*)o_) { it = o->begin(); }
            bool hasNext() override { return it != o->end(); }
        };


        /// ============= Vector ================

        template<typename T>
        class gc_vector : public gc<vector<gc<T>>>
        {
        public:
            using gc::gc;
            gc<T>& operator[](int idx) { return ( *p )[idx]; }
        };

        template<typename T>
        class PtrEnumerator<vector<gc<T>>> : public ContainerPtrEnumerator<vector<gc<T>>>
        {
        public:
            using ContainerPtrEnumerator::ContainerPtrEnumerator;
            const PtrBase* getNext() override { return &*it++; }
        };

        template<typename T, typename... Args>
        gc_vector<T> gc_new_vector(Args&&... args)
        {
            return gc_new<vector<gc<T>>>(forward<Args>(args)...);
        }

        /// ============= Deque ================

        template<typename T>
        class gc_deque : public gc<deque<gc<T>>>
        {
        public:
            using gc::gc;
            gc<T>& operator[](int idx) { return ( *p )[idx]; }
        };

        template<typename T>
        class PtrEnumerator<deque<gc<T>>> : public ContainerPtrEnumerator<deque<gc<T>>>
        {
        public:
            using ContainerPtrEnumerator::ContainerPtrEnumerator;
            const PtrBase* getNext() override { return &*it++; }
        };

        template<typename T, typename... Args>
        gc_deque<T> gc_new_deque(Args&&... args)
        {
            return gc_new<deque<gc<T>>>(forward<Args>(args)...);
        }

        // ========= function ================================

        template<typename T>
        class gc_func;

        template<typename R, typename... A>
        class gc_func<R(A...)>
        {
        public:
            gc_func() {}

            template<typename F>
            void operator=(F& f) { callable = gc_new<Imp<F>>(f); }

            R operator()(A... a) { return callable->call(a...); }

        private:
            class Callable
            {
            public:
                virtual ~Callable() {}
                virtual R call(A... a) = 0;
            };

            gc<Callable> callable;

            template<typename F>
            class Imp : public Callable
            {
            public:
                F f;
                Imp(F& ff) : f(ff) {}
                R call(A... a) override { return f(a...); }
            };
        };

        /// ============= List ================

        template<typename T>
        using gc_list = gc<list<gc<T>>>;

        template<typename T>
        class PtrEnumerator<list<gc<T>>> : public ContainerPtrEnumerator<list<gc<T>>>
        {
        public:
            using ContainerPtrEnumerator::ContainerPtrEnumerator;
            const PtrBase* getNext() override { return &*it++; }
        };

        template<typename T, typename... Args>
        gc_list<T> gc_new_list(Args&&... args)
        {
            return gc_new<list<gc<T>>>(forward<Args>(args)...);
        }

        /// ============= Map ================

        // TODO: NOT support using gc object as key...

        template<typename K, typename V>
        class gc_map : public gc<map<K, gc<V>>>
        {
        public:
            using gc::gc;
            gc<V>& operator[](const K& k) { return ( *p )[k]; }
        };

        template<typename K, typename V>
        class PtrEnumerator<map<K, gc<V>>> : public ContainerPtrEnumerator<map<K, gc<V>>>
        {
        public:
            using ContainerPtrEnumerator::ContainerPtrEnumerator;
            const PtrBase* getNext() override { auto* ret = &it->second; ++it; return ret; }
        };

        template<typename K, typename V, typename... Args>
        gc_map<K, V> gc_new_map(Args&&... args)
        {
            return gc_new<map<K, gc<V>>>(forward<Args>(args)...);
        }

        /// ============= HashMap ================

        // NOT support using gc object as key...

        template<typename K, typename V>
        class gc_unordered_map : public gc<unordered_map<K, gc<V>>>
        {
        public:
            using gc::gc;
            gc<V>& operator[](const K& k) { return ( *p )[k]; }
        };

        template<typename K, typename V>
        class PtrEnumerator<unordered_map<K, gc<V>>> : public ContainerPtrEnumerator<unordered_map<K, gc<V>>>
        {
        public:
            using ContainerPtrEnumerator::ContainerPtrEnumerator;
            const PtrBase* getNext() override { auto* ret = &it->second; ++it; return ret; }
        };

        template<typename K, typename V, typename... Args>
        gc_unordered_map<K, V> gc_new_unordered_map(Args&&... args)
        {
            return gc_new<unordered_map<K, gc<V>>>(forward<Args>(args)...);
        }

        /// ============= Set ================


        template<typename V>
        using gc_set = gc<set<gc<V>>>;

        template<typename V>
        class PtrEnumerator<set<gc<V>>> : public ContainerPtrEnumerator<set<gc<V>>>
        {
        public:
            using ContainerPtrEnumerator::ContainerPtrEnumerator;
            const PtrBase* getNext() override { return &*it++; }
        };

        template<typename V, typename... Args>
        gc_set<V> gc_new_set(Args&&... args)
        {
            return gc_new<set<gc<V>>>(forward<Args>(args)...);
        }
    }

    // ================ Public APIs =======================

    using details::gc;
    using details::gc_vector;
    using details::gc_list;
    using details::gc_deque;
    using details::gc_unordered_map;
    using details::gc_set;
    using details::gc_map;
    using details::gc_func;
    
    using details::gc_new;
    using details::gc_collect;
    using details::gc_from;

    using details::gc_new_list;
    using details::gc_new_deque;
    using details::gc_new_vector;
    using details::gc_new_map;
    using details::gc_new_unordered_map;    
	using details::gc_new_set;


#define GC_AUTO_BOX(T)\
	template<> \
	class gc<T> : public details::GcPtr<T> \
	{ \
	public: \
		using GcPtr::GcPtr; \
		gc(T i): GcPtr(gc_new<T>(i)) {} \
		gc() {} \
		operator T& () { return GcPtr::operator*(); } \
	};	

	GC_AUTO_BOX(int);
	GC_AUTO_BOX(float);
	GC_AUTO_BOX(std::string);

	using gc_string = gc<std::string>;
}

