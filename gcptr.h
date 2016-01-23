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
        typedef void(*Dctor)(void*);        
        struct ObjInfo;
        ObjInfo* registerObj(void* obj, int size, Dctor dctor, char* mem);

        extern const int ObjInfoSize;
        extern ObjInfo* kObjInfo_Uninit;

        struct PointerBase
        {
        protected:
            ObjInfo* objInfo;
            ObjInfo* owner;

            PointerBase() : objInfo(0), owner(kObjInfo_Uninit) {}
            PointerBase(void* obj);
#ifdef _DEBUG
            virtual ~PointerBase();
#else
            ~PointerBase();
#endif // _DEBUG
            void registerPointer(); 
        public:
            bool isRoot();            
            ObjInfo* getObjInfo() { return objInfo; }
        };
    };
    
    int GcCollect(int step);
    
    using details::ObjInfo;


    template <typename T>
    class gc_ptr : public details::PointerBase
    {
    public:
        // Constructors

        gc_ptr():ptr(0) { registerPointer(); }
        gc_ptr(T* obj, ObjInfo* info_) { reset(obj, info_); }
        explicit gc_ptr(T* obj) : PointerBase(obj), ptr(obj) { registerPointer(); }
        template <typename U>
        gc_ptr(const gc_ptr<U>& r) { reset(r.ptr, r.objInfo);  }
        gc_ptr(const gc_ptr& r) { reset(r.ptr, r.objInfo);  }
        gc_ptr(gc_ptr&& r) { reset(r.ptr, r.objInfo); r.objInfo = 0; }

        // Operators

        template <typename U>
        gc_ptr& operator=(const gc_ptr<U>& r) { reset(r.ptr, r.objInfo);  return *this; }
        gc_ptr& operator=(const gc_ptr& r) { reset(r.ptr, r.objInfo);  return *this; }
        gc_ptr& operator=(gc_ptr&& r) { reset(r.ptr, r.objInfo); r.objInfo = 0; return *this; }
        T& operator*() const { return *ptr; }
        T* operator->() const { return ptr; }
        T* get() const { return ptr; }
        explicit operator bool() const { return ptr != 0; }
        bool operator==(const gc_ptr& r)const { return objInfo == r.objInfo; }
        bool operator!=(const gc_ptr& r)const { return objInfo != r.objInfo; }
        void operator=(T*) = delete;

        // Methods

        static void destroy(void* obj) { ((T*)obj)->~T(); }
        void reset(T* o) { gc_ptr(o).swap(*this); }
        void reset(T* o, ObjInfo* n) { ptr = o; objInfo = n; registerPointer(); }
        void swap(gc_ptr& r)
        {
            T* temp = ptr;
            ObjInfo* tinfo = objInfo;
            reset(r.ptr, r.objInfo);
            r.reset(temp, tinfo);
        }

    private:
        template <typename U>
        friend class gc_ptr;

        T*  ptr;
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

