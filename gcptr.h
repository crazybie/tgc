/*
TGC: Tiny incremental mark & sweep Garbage Collector.
by crazybie at soniced@sina.com

NOTE:
- never construct gc object in global scope.
- TODO: exception safe.
- TODO: thread safe.

Useful refs:
http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.

*/

#pragma once

#include <cassert>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <typeinfo>
#include <unordered_map>
#include <vector>

//#define TGC_DEBUG

#ifdef TGC_DEBUG
#define TGC_DEBUG_CODE(...) __VA_ARGS__
#else
#define TGC_DEBUG_CODE(...)
#endif

namespace tgc {
namespace details {
using namespace std;

class ObjMeta;

//////////////////////////////////////////////////////////////////////////

class PtrBase {
  friend class Collector;
  friend class ClassInfo;

 protected:
  mutable unsigned int isRoot : 1;
  unsigned int index : 31;
  ObjMeta* meta;

  PtrBase();
  PtrBase(void* obj);
  ~PtrBase();
  void onPtrChanged();
};

//////////////////////////////////////////////////////////////////////////

class IPtrEnumerator {
 public:
  virtual ~IPtrEnumerator() {}
  virtual bool hasNext() = 0;
  virtual const PtrBase* getNext() = 0;

  void* operator new(size_t sz) {
    static char buf[255];
    assert(sz < sizeof(buf));
    return buf;
  }
  void operator delete(void*) {}
};

//////////////////////////////////////////////////////////////////////////

class ClassInfo {
 public:
  enum class State : char { Unregistered, Registered };
  enum class MemRequest { Alloc, Dctor, Dealloc, PtrEnumerator };
  typedef void* (*MemHandler)(ClassInfo* cls, MemRequest r, void* param);
  typedef IPtrEnumerator* (*EnumPtrs)(ObjMeta* meta);

  TGC_DEBUG_CODE(const char* name);
  MemHandler memHandler;
  State state : 2;
  size_t size : sizeof(void*) * 8 - 2;
  vector<int> subPtrOffsets;

  static int isCreatingObj;
  static ClassInfo Empty;

  ClassInfo() : memHandler(nullptr), size(0) {}
  ClassInfo(TGC_DEBUG_CODE(const char* name_, ) MemHandler h, int sz)
      : TGC_DEBUG_CODE(name(name_), ) memHandler(h),
        size(sz),
        state(State::Unregistered) {}

  ObjMeta* newMeta(int objCnt);
  void registerSubPtr(ObjMeta* owner, PtrBase* p);
  void beginObjCreating() { isCreatingObj++; }
  void endObjCreating() {
    isCreatingObj--;
    state = ClassInfo::State::Registered;
  }
  IPtrEnumerator* enumPtrs(ObjMeta* m) {
    return (IPtrEnumerator*)memHandler(this, MemRequest::PtrEnumerator, m);
  }
  template <typename T>
  static ClassInfo* get();
};

//////////////////////////////////////////////////////////////////////////

class ObjMeta {
 public:
  enum class MarkColor { Unmarked, Gray, Alive };

  ClassInfo* clsInfo;
  MarkColor markState : 2;
  unsigned int arrayLength : sizeof(void*) * 8 - 2;

  static char* dummyObjPtr;

  struct Less {
    bool operator()(ObjMeta* x, ObjMeta* y) const { return *x < *y; }
  };

  ObjMeta(ClassInfo* c, char* o, int cnt)
      : clsInfo(c), markState(MarkColor::Unmarked), arrayLength(cnt) {}

  ~ObjMeta() {
    if (arrayLength)
      destroy();
  }

  void operator delete(void* c) {
    auto* p = (ObjMeta*)c;
    p->clsInfo->memHandler(p->clsInfo, ClassInfo::MemRequest::Dealloc, p);
  }
  bool operator<(ObjMeta& r) const {
    return objPtr() + clsInfo->size * arrayLength <= r.objPtr();
  }
  bool containsPtr(char* p) {
    return objPtr() <= p && p < objPtr() + clsInfo->size * arrayLength;
  }
  char* objPtr() const {
    return !dummyObjPtr ? (char*)this + sizeof(ObjMeta) : dummyObjPtr;
  }
  void destroy() {
    if (!arrayLength)
      return;
    clsInfo->memHandler(clsInfo, ClassInfo::MemRequest::Dctor, this);
    arrayLength = 0;
  }
};

static_assert(sizeof(ObjMeta) <= sizeof(void*) * 2,
              "too large for small allocation");

//////////////////////////////////////////////////////////////////////////

template <typename T>
class GcPtr : public PtrBase {
 public:
  typedef T pointee;
  typedef T element_type;  // compatible with std::shared_ptr

  template <typename U>
  friend class GcPtr;

 public:
  // Constructors

  GcPtr() : p(nullptr) {}
  GcPtr(ObjMeta* meta) { reset((T*)meta->objPtr(), meta); }
  explicit GcPtr(T* obj) : PtrBase(obj), p(obj) {}
  template <typename U>
  GcPtr(const GcPtr<U>& r) {
    reset(r.p, r.meta);
  }
  GcPtr(const GcPtr& r) { reset(r.p, r.meta); }
  GcPtr(GcPtr&& r) {
    reset(r.p, r.meta);
    r = nullptr;
  }

  // Operators

  template <typename U>
  GcPtr& operator=(const GcPtr<U>& r) {
    reset(r.p, r.meta);
    return *this;
  }
  GcPtr& operator=(const GcPtr& r) {
    reset(r.p, r.meta);
    return *this;
  }
  GcPtr& operator=(GcPtr&& r) {
    reset(r.p, r.meta);
    r.meta = 0;
    r.p = 0;
    return *this;
  }
  T* operator->() const { return p; }
  T& operator*() const { return *p; }
  explicit operator bool() const { return p != 0; }
  bool operator==(const GcPtr& r) const { return meta == r.meta; }
  bool operator!=(const GcPtr& r) const { return meta != r.meta; }
  void operator=(T*) = delete;
  GcPtr& operator=(decltype(nullptr)) {
    meta = 0;
    p = 0;
    return *this;
  }
  bool operator<(const GcPtr& r) const { return *p < *r.p; }

  // Methods

  void reset(T* o) { GcPtr(o).swap(*this); }
  void reset(T* o, ObjMeta* n) {
    p = o;
    meta = n;
    onPtrChanged();
  }
  void swap(GcPtr& r) {
    auto* temp = p;
    auto* tempMeta = meta;
    reset(r.p, r.meta);
    r.reset(temp, tempMeta);
  }

  ObjMeta* getMeta() { return meta; }

 protected:
  T* p;
};

//////////////////////////////////////////////////////////////////////////

template <typename T>
class gc : public GcPtr<T> {
  using base = GcPtr<T>;

 public:
  using GcPtr<T>::GcPtr;
  gc() {}
  gc(nullptr_t) {}
  gc(ObjMeta* o) : base(o) {}
  gc(T* o) : base(o) {}
};

#define TGC_DECL_AUTO_BOX(T)                                 \
  template <>                                                \
  class details::gc<T> : public details::GcPtr<T> {          \
   public:                                                   \
    using GcPtr<T>::GcPtr;                                   \
    gc(const T& i) : GcPtr(details::gc_new_meta<T>(1, i)) {} \
    gc() {}                                                  \
    gc(nullptr_t) {}                                         \
    operator T&() { return operator*(); }                    \
    operator T&() const { return operator*(); }              \
  };

//////////////////////////////////////////////////////////////////////////

template <typename C>
class PtrEnumerator : public IPtrEnumerator {
 public:
  size_t subPtrIdx, arrayElemIdx;
  ObjMeta* meta;

  PtrEnumerator(ObjMeta* meta_) : meta(meta_), subPtrIdx(0), arrayElemIdx(0) {}

  bool hasNext() override {
    return arrayElemIdx < meta->arrayLength &&
           subPtrIdx < meta->clsInfo->subPtrOffsets.size();
  }
  const PtrBase* getNext() override {
    auto* clsInfo = meta->clsInfo;
    auto* obj = meta->objPtr() + arrayElemIdx * clsInfo->size;
    auto* subPtr = obj + clsInfo->subPtrOffsets[subPtrIdx];
    if (subPtrIdx++ >= clsInfo->subPtrOffsets.size())
      arrayElemIdx++;
    return (PtrBase*)subPtr;
  }
};

template <typename T>
class ClassInfoHolder {
 public:
  static void* MemHandler(ClassInfo* cls,
                          ClassInfo::MemRequest r,
                          void* param) {
    switch (r) {
      case ClassInfo::MemRequest::Alloc: {
        auto cnt = (int)param;
        auto* p = new char[cls->size * cnt + sizeof(ObjMeta)];
        return new (p) ObjMeta(cls, p + sizeof(ObjMeta), cnt);
      }
      case ClassInfo::MemRequest::Dealloc: {
        auto meta = (ObjMeta*)param;
        delete[](char*) meta;
      } break;
      case ClassInfo::MemRequest::Dctor: {
        auto meta = (ObjMeta*)param;
        auto p = (T*)meta->objPtr();
        for (size_t i = 0; i < meta->arrayLength; i++, p++) {
          p->~T();
        }
      } break;
      case ClassInfo::MemRequest::PtrEnumerator: {
        auto meta = (ObjMeta*)param;
        return new PtrEnumerator<T>(meta);
      } break;
    }
    return nullptr;
  }

  static ClassInfo inst;
};

template <typename T>
ClassInfo ClassInfoHolder<T>::inst{
    TGC_DEBUG_CODE(typeid(T).name(), ) MemHandler, sizeof(T)};

template <typename T>
ClassInfo* ClassInfo::get() {
  return &ClassInfoHolder<T>::inst;
}

void gc_collect(int steps = 256);

template <typename T, typename... Args>
ObjMeta* gc_new_meta(size_t len, Args&&... args) {
  auto* cls = ClassInfo::get<T>();
  cls->beginObjCreating();
  auto* meta = cls->newMeta(len);
  auto* p = (T*)meta->objPtr();
  for (size_t i = 0; i < len; i++, p++)
    new (p) T(forward<Args>(args)...);
  cls->endObjCreating();
  return meta;
}

template <typename T>
void gc_delete(gc<T>& c) {
  if (c) {
    c.getMeta()->destroy();
    c = nullptr;
  }
}

// used as shared_from_this
template <typename T>
gc<T> gc_from(T* o) {
  return gc<T>(o);
}

// used as std::shared_ptr
template <typename To, typename From>
gc<To> gc_static_pointer_cast(gc<From>& from) {
  return static_cast<To*>(from.operator->());
}

template <typename T, typename... Args>
gc<T> gc_new(Args&&... args) {
  return gc_new_meta<T>(1, forward<Args>(args)...);
}

template <typename T, typename... Args>
gc<T> gc_new_array(size_t len, Args&&... args) {
  return gc_new_meta<T>(len, forward<Args>(args)...);
}

//////////////////////////////////////////////////////////////////////////
/// Function

template <typename T>
class gc_function;

template <typename R, typename... A>
class gc_function<R(A...)> {
 public:
  gc_function() {}

  template <typename F>
  gc_function(F&& f) : callable(gc_new_meta<Imp<F>>(1, forward<F>(f))) {}

  template <typename F>
  void operator=(F&& f) {
    callable = gc_new_meta<Imp<F>>(1, forward<F>(f));
  }

  template <typename... U>
  R operator()(U&&... a) const {
    return callable->call(forward<U>(a)...);
  }

  explicit operator bool() const { return (bool)callable; }

 private:
  struct Callable {
    virtual ~Callable() {}
    virtual R call(A... a) = 0;
  };

  template <typename F>
  struct Imp : Callable {
    F f;
    Imp(F&& ff) : f(ff) {}
    R call(A... a) override { return f(a...); }
  };

 private:
  gc<Callable> callable;
};

//////////////////////////////////////////////////////////////////////////
// Wrap STL Containers
//////////////////////////////////////////////////////////////////////////

template <typename C>
class ContainerPtrEnumerator : public IPtrEnumerator {
 public:
  C* o;
  typename C::iterator it;
  ContainerPtrEnumerator(ObjMeta* m) : o((C*)m->objPtr()) { it = o->begin(); }
  bool hasNext() override { return it != o->end(); }
};

//////////////////////////////////////////////////////////////////////////
/// Vector
/// vector elements are not stored contiguously due to implementation
/// limitation. use gc_new_array for better performance.

template <typename T>
class gc_vector : public gc<vector<gc<T>>> {
 public:
  using gc<vector<gc<T>>>::gc;
  gc<T>& operator[](int idx) { return (*this->p)[idx]; }
};

template <typename T>
class PtrEnumerator<vector<gc<T>>>
    : public ContainerPtrEnumerator<vector<gc<T>>> {
 public:
  using ContainerPtrEnumerator<vector<gc<T>>>::ContainerPtrEnumerator;
  const PtrBase* getNext() override { return &*this->it++; }
};

template <typename T, typename... Args>
gc_vector<T> gc_new_vector(Args&&... args) {
  return gc_new<vector<gc<T>>>(forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_vector<T>& p) {
  for (auto& i : *p) {
    gc_delete(i);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// Deque

template <typename T>
class gc_deque : public gc<deque<gc<T>>> {
 public:
  using gc<deque<gc<T>>>::gc;
  gc<T>& operator[](int idx) { return (*this->p)[idx]; }
};

template <typename T>
class PtrEnumerator<deque<gc<T>>>
    : public ContainerPtrEnumerator<deque<gc<T>>> {
 public:
  using ContainerPtrEnumerator<deque<gc<T>>>::ContainerPtrEnumerator;
  const PtrBase* getNext() override { return &*this->it++; }
};

template <typename T, typename... Args>
gc_deque<T> gc_new_deque(Args&&... args) {
  return gc_new<deque<gc<T>>>(forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_deque<T>& p) {
  for (auto& i : *p) {
    gc_delete(i);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// List

template <typename T>
using gc_list = gc<list<gc<T>>>;

template <typename T>
class PtrEnumerator<list<gc<T>>> : public ContainerPtrEnumerator<list<gc<T>>> {
 public:
  using ContainerPtrEnumerator<list<gc<T>>>::ContainerPtrEnumerator;
  const PtrBase* getNext() override { return &*this->it++; }
};

template <typename T, typename... Args>
gc_list<T> gc_new_list(Args&&... args) {
  return gc_new<list<gc<T>>>(forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_list<T>& p) {
  for (auto& i : *p) {
    gc_delete(i);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// Map
/// TODO: NOT support using gc object as key...

template <typename K, typename V>
class gc_map : public gc<map<K, gc<V>>> {
 public:
  using gc<map<K, gc<V>>>::gc;
  gc<V>& operator[](const K& k) { return (*this->p)[k]; }
};

template <typename K, typename V>
class PtrEnumerator<map<K, gc<V>>>
    : public ContainerPtrEnumerator<map<K, gc<V>>> {
 public:
  using ContainerPtrEnumerator<map<K, gc<V>>>::ContainerPtrEnumerator;
  const PtrBase* getNext() override {
    auto* ret = &this->it->second;
    ++this->it;
    return ret;
  }
};

template <typename K, typename V, typename... Args>
gc_map<K, V> gc_new_map(Args&&... args) {
  return gc_new<map<K, gc<V>>>(forward<Args>(args)...);
}

template <typename K, typename V>
void gc_delete(gc_map<K, V>& p) {
  for (auto& i : *p) {
    gc_delete(i->value);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// HashMap
/// TODO: NOT support using gc object as key...

template <typename K, typename V>
class gc_unordered_map : public gc<unordered_map<K, gc<V>>> {
 public:
  using gc<unordered_map<K, gc<V>>>::gc;
  gc<V>& operator[](const K& k) { return (*this->p)[k]; }
};

template <typename K, typename V>
class PtrEnumerator<unordered_map<K, gc<V>>>
    : public ContainerPtrEnumerator<unordered_map<K, gc<V>>> {
 public:
  using ContainerPtrEnumerator<unordered_map<K, gc<V>>>::ContainerPtrEnumerator;
  const PtrBase* getNext() override {
    auto* ret = &this->it->second;
    ++this->it;
    return ret;
  }
};

template <typename K, typename V, typename... Args>
gc_unordered_map<K, V> gc_new_unordered_map(Args&&... args) {
  return gc_new<unordered_map<K, gc<V>>>(forward<Args>(args)...);
}
template <typename K, typename V>
void gc_delete(gc_unordered_map<K, V>& p) {
  for (auto& i : *p) {
    gc_delete(i.second);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// Set

template <typename V>
using gc_set = gc<set<gc<V>>>;

template <typename V>
class PtrEnumerator<set<gc<V>>> : public ContainerPtrEnumerator<set<gc<V>>> {
 public:
  using ContainerPtrEnumerator<set<gc<V>>>::ContainerPtrEnumerator;
  const PtrBase* getNext() override { return &*this->it++; }
};

template <typename V, typename... Args>
gc_set<V> gc_new_set(Args&&... args) {
  return gc_new<set<gc<V>>>(forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_set<T>& p) {
  for (auto i : *p) {
    gc_delete(i);
  }
  p->clear();
}

}  // namespace details

//////////////////////////////////////////////////////////////////////////
// Public APIs

using details::gc;
using details::gc_collect;
using details::gc_function;
using details::gc_new;
using details::gc_new_array;
using details::gc_static_pointer_cast;

using details::gc_deque;
using details::gc_new_deque;

using details::gc_list;
using details::gc_new_list;

using details::gc_map;
using details::gc_new_map;

using details::gc_new_set;
using details::gc_set;

using details::gc_new_unordered_map;
using details::gc_unordered_map;

using details::gc_new_vector;
using details::gc_vector;

TGC_DECL_AUTO_BOX(char);
TGC_DECL_AUTO_BOX(unsigned char);
TGC_DECL_AUTO_BOX(short);
TGC_DECL_AUTO_BOX(unsigned short);
TGC_DECL_AUTO_BOX(int);
TGC_DECL_AUTO_BOX(unsigned int);
TGC_DECL_AUTO_BOX(float);
TGC_DECL_AUTO_BOX(double);
TGC_DECL_AUTO_BOX(long);
TGC_DECL_AUTO_BOX(unsigned long);
TGC_DECL_AUTO_BOX(std::string);

using gc_string = details::gc<std::string>;

}  // namespace tgc
