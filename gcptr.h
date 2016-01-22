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


namespace gc
{
    namespace details
    {
        struct ObjInfo;
        extern const int ObjInfoSize;
        void incRef(ObjInfo* n);
        void decRef(ObjInfo* n);
        ObjInfo* registerObj(void* obj, int size, void(*destroy)(void*), char* objInfoMem);

        struct PointerBase
        {
            void setObjInfo(ObjInfo* n);
            ObjInfo* rebindObj(void* obj);
        };
    };
    
    int GcCollect(int step);
    
    using details::ObjInfo;


    template <typename T>
    class gc_ptr : protected details::PointerBase
    {
    public:
        // Constructors

        gc_ptr() : info(0) {}
        gc_ptr(T* obj, ObjInfo* info_) : ptr(obj), info(info_) { setObjInfo(info); incRef(info); }
        explicit gc_ptr(T* obj) : ptr(obj) { info = rebindObj(obj); incRef(info); }
        template <typename U>
        gc_ptr(const gc_ptr<U>& r) : info(0) { reset(r.ptr, r.info); incRef(info); }
        gc_ptr(const gc_ptr& r) :info(0) { reset(r.ptr, r.info); incRef(info); }
        gc_ptr(gc_ptr&& r) { reset(r.ptr, r.info); r.info = 0; }
        ~gc_ptr() { decRef(info); setObjInfo(0); }

        // Operators

        template <typename U>
        gc_ptr& operator=(const gc_ptr<U>& r) { decRef(info); reset(r.ptr, r.info); incRef(info); return *this; }
        gc_ptr& operator=(const gc_ptr& r) { decRef(info); reset(r.ptr, r.info); incRef(info); return *this; }
        gc_ptr& operator=(gc_ptr&& r) { decRef(info); reset(r.ptr, r.info); r.info = 0; return *this; }
        T& operator*() const { return *ptr; }
        T* operator->() const { return ptr; }
        T* get() const { return ptr; }
        explicit operator bool() const { return ptr != 0; }
        bool operator==(const gc_ptr& r)const { return info == r.info; }
        bool operator!=(const gc_ptr& r)const { return info != r.info; }
        void operator=(T*) = delete;

        // Methods

        static void destroy(void* obj) { ((T*)obj)->~T(); }
        void reset(T* obj) { gc_ptr(obj).swap(*this); }
        void reset(T* obj, ObjInfo* n) { ptr = obj; info = n; setObjInfo(n); }
        void swap(gc_ptr& other)
        {
            T* temp = ptr;
            ObjInfo* tinfo = info;
            reset(other.ptr, other.info);
            other.reset(temp, tinfo);
        }

    private:
        template <typename U>
        friend class gc_ptr;

        T*          ptr;
        ObjInfo*    info;
    };


    template<typename T, typename... Args>
    gc_ptr<T> make_gc(Args&&... args)
    {
        char* buf = new char[sizeof(T) + details::ObjInfoSize];
        T* obj = new (buf + details::ObjInfoSize) T(std::forward<Args>(args)...);
        return gc_ptr<T>(obj, details::registerObj(obj, sizeof(T), &gc_ptr<T>::destroy, buf));
    }

    template<typename T>
    gc_ptr<T> gc_ptr_from_this(T* t) { return gc_ptr<T>(t); }
}

