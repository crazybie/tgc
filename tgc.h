/*

TGC: Tiny incremental mark & sweep Garbage Collector.

//////////////////////////////////////////////////////////////////////////

Copyright (C) 2018 soniced@sina.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once

//#define TGC_DEBUG
//#define TGC_MULTI_THREADED

#include <cassert>
#include <set>
#include <typeinfo>
#include <vector>
#ifdef TGC_MULTI_THREADED
#include <shared_mutex>
#endif

// for STL wrappers
#include <atomic>
#include <deque>
#include <list>
#include <map>
#include <string>
#include <unordered_map>

#ifdef TGC_DEBUG
#define TGC_DEBUG_CODE(...) __VA_ARGS__
#else
#define TGC_DEBUG_CODE(...)
#endif

namespace tgc {
namespace details {
using namespace std;

#ifndef TGC_MULTI_THREADED
constexpr int try_to_lock = 0;

struct shared_mutex {};
struct unique_lock {
  unique_lock(...) {}
};
struct shared_lock {
  shared_lock(...) {}
};
#endif

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
  enum class MemRequest { Alloc, Dctor, Dealloc, NewPtrEnumerator };
  typedef void* (*MemHandler)(ClassInfo* cls, MemRequest r, void* param);

  TGC_DEBUG_CODE(const char* name);
  MemHandler memHandler;
  vector<short> subPtrOffsets;
  State state : 2;
  unsigned int size : sizeof(unsigned int) * 8 - 2;
  shared_mutex mutex;

  static atomic<int> isCreatingObj;
  static ClassInfo Empty;

  ClassInfo() : memHandler(nullptr), size(0) {}
  ClassInfo(TGC_DEBUG_CODE(const char* name_, ) MemHandler h, int sz)
      : TGC_DEBUG_CODE(name(name_), ) memHandler(h),
        size(sz),
        state(State::Unregistered) {}

  ObjMeta* newMeta(size_t objCnt);
  void registerSubPtr(ObjMeta* owner, PtrBase* p);
  void endNewMeta(ObjMeta* meta);
  IPtrEnumerator* enumPtrs(ObjMeta* m) {
    return (IPtrEnumerator*)memHandler(this, MemRequest::NewPtrEnumerator, m);
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
  unsigned int arrayLength : sizeof(unsigned int) * 8 - 2;

  static char* dummyObjPtr;

  struct Less {
    bool operator()(ObjMeta* x, ObjMeta* y) const { return *x < *y; }
  };

  ObjMeta(ClassInfo* c, char* o, size_t cnt)
      : clsInfo(c), markState(MarkColor::Unmarked), arrayLength((int)cnt) {}

  ~ObjMeta() {
    if (arrayLength)
      destroy();
  }
  void operator delete(void* c) {
    auto* p = (ObjMeta*)c;
    p->clsInfo->memHandler(p->clsInfo, ClassInfo::MemRequest::Dealloc, p);
  }
  bool operator<(ObjMeta& r) const {
    return objPtr() + clsInfo->size * arrayLength <
           r.objPtr() + r.clsInfo->size * r.arrayLength;
  }
  bool containsPtr(char* p) {
    return objPtr() <= p && p < objPtr() + clsInfo->size * arrayLength;
  }
  char* objPtr() const {
    return clsInfo == &ClassInfo::Empty ? dummyObjPtr
                                        : (char*)this + sizeof(ObjMeta);
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

class ObjPtrEnumerator : public IPtrEnumerator {
  size_t subPtrIdx, arrayElemIdx, arrayLength;
  ObjMeta* meta;

 public:
  ObjPtrEnumerator(ObjMeta* meta_, int len = 0)
      : meta(meta_),
        subPtrIdx(0),
        arrayElemIdx(0),
        arrayLength(len ? len : meta_->arrayLength) {}

  bool hasNext() override {
    return arrayElemIdx < arrayLength &&
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
    reset(static_cast<T*>(r.p), r.meta);
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
  explicit operator bool() const { return p && meta; }
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

  void reset(T* o, ObjMeta* n) {
    p = o;
    meta = n;
    onPtrChanged();
  }

  ObjMeta* getMeta() { return meta; }

 protected:
  T* p;
};

//////////////////////////////////////////////////////////////////////////

class Collector {
 public:
  static Collector* get();
  void onPointerChanged(PtrBase* p);
  void registerPtr(PtrBase* p);
  void unregisterPtr(PtrBase* p);
  ObjMeta* globalFindOwnerMeta(void* obj);
  void collect(int stepCnt);
  void dumpStats();

  enum class State { RootMarking, ChildMarking, Sweeping, MaxCnt };

 private:
  Collector();
  ~Collector();

  void tryMarkRoot(PtrBase* p);
  ObjMeta* findCreatingObj(PtrBase* p);
  void addMeta(ObjMeta* meta);

 private:
  typedef set<ObjMeta*, ObjMeta::Less> MetaSet;
  friend class ClassInfo;

  vector<PtrBase*> pointers;
  vector<ObjMeta*> grayObjs;
  MetaSet metaSet;
  list<ObjMeta*> creatingObjs;
  MetaSet::iterator nextSweeping;
  size_t nextRootMarking = 0;
  State state = State::RootMarking;
  shared_mutex mutex;
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
  explicit gc(T* o) : base(o) {}
};

#define TGC_DECL_AUTO_BOX(T, GcAliasName)                    \
  template <>                                                \
  class details::gc<T> : public details::GcPtr<T> {          \
   public:                                                   \
    using GcPtr<T>::GcPtr;                                   \
    gc(const T& i) : GcPtr(details::gc_new_meta<T>(1, i)) {} \
    gc() {}                                                  \
    gc(nullptr_t) {}                                         \
    operator T&() { return operator*(); }                    \
    operator T&() const { return operator*(); }              \
  };                                                         \
  using gc_##GcAliasName = gc<T>;

//////////////////////////////////////////////////////////////////////////

template <typename T>
struct PtrEnumerator : ObjPtrEnumerator {
  using ObjPtrEnumerator::ObjPtrEnumerator;
};

template <typename T>
class ClassInfoHolder {
 public:
  static void* MemHandler(ClassInfo* cls,
                          ClassInfo::MemRequest r,
                          void* param) {
    switch (r) {
      case ClassInfo::MemRequest::Alloc: {
        auto cnt = (size_t)param;
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
      case ClassInfo::MemRequest::NewPtrEnumerator: {
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

//////////////////////////////////////////////////////////////////////////

inline void gc_collect(int steps = 256) {
  Collector::get()->collect(steps);
}

inline void gc_dumpStats() {
  Collector::get()->dumpStats();
}

template <typename T, typename... Args>
ObjMeta* gc_new_meta(size_t len, Args&&... args) {
  auto* cls = ClassInfo::get<T>();
  auto* meta = cls->newMeta(len);
  auto* p = (T*)meta->objPtr();
  for (size_t i = 0; i < len; i++, p++)
    new (p) T(forward<Args>(args)...);
  cls->endNewMeta(meta);
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
  return from;
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
  gc_function& operator=(F&& f) {
    callable = gc_new_meta<Imp<F>>(1, forward<F>(f));
    return *this;
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
using details::gc_dumpStats;
using details::gc_from;
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

TGC_DECL_AUTO_BOX(char, char);
TGC_DECL_AUTO_BOX(unsigned char, uchar);
TGC_DECL_AUTO_BOX(short, short);
TGC_DECL_AUTO_BOX(unsigned short, ushort);
TGC_DECL_AUTO_BOX(int, int);
TGC_DECL_AUTO_BOX(unsigned int, uint);
TGC_DECL_AUTO_BOX(float, float);
TGC_DECL_AUTO_BOX(double, double);
TGC_DECL_AUTO_BOX(long, long);
TGC_DECL_AUTO_BOX(unsigned long, ulong);
TGC_DECL_AUTO_BOX(std::string, string);

}  // namespace tgc
