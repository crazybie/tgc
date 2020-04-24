#include "tgc.h"

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace tgc {
namespace details {

#ifndef TGC_MULTI_THREADED
shared_mutex ClassMeta::mutex;
#endif
atomic<int> ClassMeta::isCreatingObj = 0;
ClassMeta ClassMeta::dummy;
char* ObjMeta::dummyObjPtr = nullptr;
Collector* Collector::inst = nullptr;

static const char* StateStr[(int)Collector::State::MaxCnt] = {
    "RootMarking", "LeafMarking", "Sweeping"};

//////////////////////////////////////////////////////////////////////////

char* ObjMeta::objPtr() const {
  return klass == &ClassMeta::dummy ? dummyObjPtr
                                    : (char*)this + sizeof(ObjMeta);
}

void ObjMeta::destroy() {
  if (!arrayLength)
    return;
  klass->memHandler(klass, ClassMeta::MemRequest::Dctor, this);
  arrayLength = 0;
}

void ObjMeta::operator delete(void* p) {
  auto* m = (ObjMeta*)p;
  m->klass->memHandler(m->klass, ClassMeta::MemRequest::Dealloc, m);
}

bool ObjMeta::operator<(ObjMeta& r) const {
  return objPtr() + klass->size * arrayLength <
         r.objPtr() + r.klass->size * r.arrayLength;
}

bool ObjMeta::containsPtr(char* p) {
  auto* o = objPtr();
  return o <= p && p < o + klass->size * arrayLength;
}

//////////////////////////////////////////////////////////////////////////

bool ObjPtrEnumerator::hasNext() {
  if (auto* subPtrs = meta->klass->subPtrOffsets)
    return arrayElemIdx < meta->arrayLength && subPtrIdx < subPtrs->size();
  return false;
}

const PtrBase* ObjPtrEnumerator::getNext() {
  auto* klass = meta->klass;
  auto* obj = meta->objPtr() + arrayElemIdx * klass->size;
  auto* subPtr = obj + (*klass->subPtrOffsets)[subPtrIdx];
  if (subPtrIdx++ >= klass->subPtrOffsets->size())
    arrayElemIdx++;
  return (PtrBase*)subPtr;
}

//////////////////////////////////////////////////////////////////////////

PtrBase::PtrBase() : meta(0), isRoot(1) {
  auto* c = Collector::inst ? Collector::inst : Collector::get();
  c->registerPtr(this);
}

PtrBase::PtrBase(void* obj) : isRoot(1) {
  auto* c = Collector::inst ? Collector::inst : Collector::get();
  meta = c->globalFindOwnerMeta(obj);
  c->registerPtr(this);
}

PtrBase::~PtrBase() {
  Collector::inst->unregisterPtr(this);
}

void PtrBase::onPtrChanged() {
  Collector::inst->onPointerChanged(this);
}

//////////////////////////////////////////////////////////////////////////

ObjMeta* ClassMeta::newMeta(size_t objCnt) {
  assert(memHandler && "should not be called in global scope (before main)");
  auto* meta = (ObjMeta*)memHandler(this, MemRequest::Alloc,
                                    reinterpret_cast<void*>(objCnt));

  try {
    auto* c = Collector::inst ? Collector::inst : Collector::get();
    // Allow using gc_from(this) in the constructor of the creating object.
    c->addMeta(meta);
  } catch (std::bad_alloc&) {
    memHandler(this, MemRequest::Dealloc, meta);
    throw;
  }

  isCreatingObj++;
  return meta;
}

void ClassMeta::endNewMeta(ObjMeta* meta, bool failed) {
  isCreatingObj--;
  if (!failed) {
    unique_lock lk{mutex};
    state = ClassMeta::State::Registered;
  }

  {
    auto* c = Collector::inst;
    unique_lock lk{c->mutex, try_to_lock};
    c->creatingObjs.remove(meta);
    if (failed) {
      c->metaSet.erase(meta);
      memHandler(this, MemRequest::Dealloc, meta);
    }
  }
}

void ClassMeta::registerSubPtr(ObjMeta* owner, PtrBase* p) {
  auto offset = (OffsetType)((char*)p - owner->objPtr());

  {
    shared_lock lk{mutex};

    if (state == ClassMeta::State::Registered)
      return;
    // constructor recursed.
    if (subPtrOffsets && offset <= subPtrOffsets->back())
      return;
  }

  unique_lock lk{mutex};
  if (!subPtrOffsets)
    subPtrOffsets = new vector<OffsetType>();
  subPtrOffsets->push_back(offset);
}

//////////////////////////////////////////////////////////////////////////

Collector::Collector() {
  pointers.reserve(1024 * 5);
  grayObjs.reserve(1024 * 2);
}

Collector::~Collector() {
  for (auto i = metaSet.begin(); i != metaSet.end();) {
    delete *i;
    i = metaSet.erase(i);
  }
}

Collector* Collector::get() {
  if (!inst) {
#ifdef _WIN32
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    inst = new Collector();
    atexit([] { delete inst; });
  }
  return inst;
}

void Collector::addMeta(ObjMeta* meta) {
  unique_lock lk{mutex, try_to_lock};
  metaSet.insert(meta);
  creatingObjs.push_back(meta);
}

void Collector::registerPtr(PtrBase* p) {
  p->index = pointers.size();
  {
    unique_lock lk{mutex, try_to_lock};
    pointers.push_back(p);
  }

  if (ClassMeta::isCreatingObj > 0) {
    if (auto* owner = findCreatingObj(p)) {
      p->isRoot = 0;
      owner->klass->registerSubPtr(owner, p);
    }
  }
}

void Collector::unregisterPtr(PtrBase* p) {
  PtrBase* pointer;
  {
    unique_lock lk{mutex, try_to_lock};

    if (p == pointers.back()) {
      pointers.pop_back();
      return;
    } else {
      swap(pointers[p->index], pointers.back());
      pointer = pointers[p->index];
      pointers.pop_back();
      pointer->index = p->index;
    }
  }
  if (!pointer->meta)
    return;
  shared_lock lk{mutex, try_to_lock};
  if (state == State::RootMarking) {
    if (p->index < nextRootMarking) {
      tryMarkRoot(pointer);
    }
  }
}

void Collector::tryMarkRoot(PtrBase* p) {
  if (p->isRoot == 1) {
    if (p->meta->color == ObjMeta::Color::White) {
      p->meta->color = ObjMeta::Color::Gray;

      unique_lock lk{mutex, try_to_lock};
      grayObjs.push_back(p->meta);
    }
  }
}

void Collector::onPointerChanged(PtrBase* p) {
  if (!p->meta)
    return;

  shared_lock lk{mutex, try_to_lock};
  switch (state) {
    case State::RootMarking:
      if (p->index < nextRootMarking)
        tryMarkRoot(p);
      break;
    case State::LeafMarking:
      tryMarkRoot(p);
      break;
    case State::Sweeping:
      if (p->meta->color == ObjMeta::Color::White) {
        if (*p->meta < **nextSweeping) {
          // already passed sweeping stage.
        } else {
          // delay to the next collection.
          p->meta->color = ObjMeta::Color::Black;
        }
      }
      break;
  }
}

ObjMeta* Collector::findCreatingObj(PtrBase* p) {
  shared_lock lk{mutex, try_to_lock};
  // owner may not be the current one(e.g. constructor recursed)
  for (auto i = creatingObjs.rbegin(); i != creatingObjs.rend(); ++i) {
    if ((*i)->containsPtr((char*)p))
      return *i;
  }
  return nullptr;
}

ObjMeta* Collector::globalFindOwnerMeta(void* obj) {
  shared_lock lk{mutex, try_to_lock};

  ObjMeta dummyMeta(&ClassMeta::dummy, 0, 0);
  dummyMeta.dummyObjPtr = (char*)obj;
  auto i = metaSet.lower_bound(&dummyMeta);
  if (i != metaSet.end() && (*i)->containsPtr((char*)obj)) {
    return *i;
  } else {
    return nullptr;
  }
}

void Collector::collect(int stepCnt) {
  unique_lock lk{mutex};

  switch (state) {
  _RootMarking:
  case State::RootMarking:
    for (; nextRootMarking < pointers.size() && stepCnt-- > 0;
         nextRootMarking++) {
      auto p = pointers[nextRootMarking];
      auto meta = p->meta;
      if (!meta)
        continue;
      // for containers
      auto it = meta->klass->enumPtrs(meta);
      for (; it->hasNext();) {
        it->getNext()->isRoot = 0;
      }
      delete it;
      tryMarkRoot(p);
    }
    if (nextRootMarking >= pointers.size()) {
      state = State::LeafMarking;
      nextRootMarking = 0;
      goto _ChildMarking;
    }
    break;

  _ChildMarking:
  case State::LeafMarking:
    while (grayObjs.size() && stepCnt-- > 0) {
      ObjMeta* o = grayObjs.back();
      grayObjs.pop_back();
      o->color = ObjMeta::Color::Black;

      auto cls = o->klass;
      auto it = cls->enumPtrs(o);
      for (; it->hasNext(); stepCnt--) {
        auto* ptr = it->getNext();
        auto* meta = ptr->meta;
        if (!meta)
          continue;
        if (meta->color == ObjMeta::Color::White) {
          meta->color = ObjMeta::Color::Gray;
          grayObjs.push_back(meta);
        }
      }
      delete it;
    }
    if (!grayObjs.size()) {
      state = State::Sweeping;
      nextSweeping = metaSet.begin();
      goto _Sweeping;
    }
    break;

  _Sweeping:
  case State::Sweeping:
    for (; nextSweeping != metaSet.end() && stepCnt-- > 0;) {
      ObjMeta* meta = *nextSweeping;
      if (meta->color == ObjMeta::Color::White) {
        nextSweeping = metaSet.erase(nextSweeping);
        delete meta;
        continue;
      }
      meta->color = ObjMeta::Color::White;
      ++nextSweeping;
    }
    if (nextSweeping == metaSet.end()) {
      state = State::RootMarking;
      if (metaSet.size())
        goto _RootMarking;
    }
    break;
  }
}

void Collector::dumpStats() {
  shared_lock lk{mutex, try_to_lock};

  printf("========= [gc] ========\n");
  printf("[total pointers ] %3d\n", (unsigned)pointers.size());
  printf("[total meta     ] %3d\n", (unsigned)metaSet.size());
  printf("[total gray meta] %3d\n", (unsigned)grayObjs.size());
  auto liveCnt = 0;
  for (auto i : metaSet)
    if (i->arrayLength)
      liveCnt++;
  printf("[live objects   ] %3d\n", liveCnt);
  printf("[collector state] %s\n", StateStr[(int)state]);
  printf("=======================\n");
}

}  // namespace details
}  // namespace tgc
