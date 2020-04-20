#include "tgc.h"

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace tgc {
namespace details {

atomic<int> ClassInfo::isCreatingObj = 0;
ClassInfo ClassInfo::Empty;
ObjMeta DummyMetaInfo(&ClassInfo::Empty, 0, 0);
char* ObjMeta::dummyObjPtr = 0;
static Collector* collector = nullptr;
static const char* StateStr[(int)Collector::State::MaxCnt] = {
    "RootMarking", "ChildMarking", "Sweeping"};

//////////////////////////////////////////////////////////////////////////

PtrBase::PtrBase() : meta(0), isRoot(1) {
  auto c = collector ? collector : Collector::get();
  c->registerPtr(this);
}

PtrBase::PtrBase(void* obj) : isRoot(1) {
  auto c = collector ? collector : Collector::get();
  c->registerPtr(this);
  meta = c->globalFindOwnerMeta(obj);
}

PtrBase::~PtrBase() {
  collector->unregisterPtr(this);
}

void PtrBase::onPtrChanged() {
  collector->onPointerChanged(this);
}

//////////////////////////////////////////////////////////////////////////

// construct meta before object construction to ensure
// member pointers can find the owner.
ObjMeta* ClassInfo::newMeta(size_t objCnt) {
  auto c = collector ? collector : Collector::get();

  assert(memHandler && "should not be called in global scope (before main)");
  // allocate memory & meta ahead of time for owner meta finding.
  auto meta = (ObjMeta*)memHandler(this, MemRequest::Alloc,
                                   reinterpret_cast<void*>(objCnt));
  c->addMeta(meta);
  isCreatingObj++;
  return meta;
}

void ClassInfo::registerSubPtr(ObjMeta* owner, PtrBase* p) {
  short offset = 0;
  {
    shared_lock lk{mutex};
    if (state == ClassInfo::State::Registered)
      return;

    offset = (decltype(offset))((char*)p - owner->objPtr());

    // constructor recursed.
    if (subPtrOffsets.size() > 0 && offset <= subPtrOffsets.back())
      return;
  }

  unique_lock lk{mutex};
  subPtrOffsets.push_back(offset);
}

void ClassInfo::endNewMeta(ObjMeta* meta) {
  isCreatingObj--;
  {
    unique_lock lk{mutex};
    state = ClassInfo::State::Registered;
  }
  {
    unique_lock lk{collector->mutex, try_to_lock};
    collector->creatingObjs.remove(meta);
  }
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
  if (!collector) {
#ifdef _WIN32
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    collector = new Collector();
    atexit([] { delete collector; });
  }
  return collector;
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

  if (ClassInfo::isCreatingObj > 0) {
    if (auto* owner = findCreatingObj(p)) {
      p->isRoot = 0;
      owner->clsInfo->registerSubPtr(owner, p);
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
      pointer->index = p->index;
      pointers.pop_back();
      if (!pointer->meta)
        return;
    }
  }

  // changing of pointers may affect the rootMarking
  shared_lock lk{mutex, try_to_lock};
  if (state == State::RootMarking) {
    if (p->index < nextRootMarking) {
      tryMarkRoot(pointer);
    }
  }
}

void Collector::tryMarkRoot(PtrBase* p) {
  if (p->isRoot == 1) {
    if (p->meta->markState == ObjMeta::MarkColor::Unmarked) {
      p->meta->markState = ObjMeta::MarkColor::Gray;

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
    case State::ChildMarking:
      tryMarkRoot(p);
      break;
    case State::Sweeping:
      if (p->meta->markState == ObjMeta::MarkColor::Unmarked) {
        if (*p->meta < **nextSweeping) {
          // already white and ready for the next rootMarking.
        } else {
          // mark it alive to bypass sweeping.
          p->meta->markState = ObjMeta::MarkColor::Alive;
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

  DummyMetaInfo.dummyObjPtr = (char*)obj;
  auto i = metaSet.lower_bound(&DummyMetaInfo);
  DummyMetaInfo.dummyObjPtr = nullptr;
  if (i == metaSet.end() || !(*i)->containsPtr((char*)obj)) {
    return nullptr;
  }
  return *i;
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
      auto it = meta->clsInfo->enumPtrs(meta);
      for (; it->hasNext();) {
        it->getNext()->isRoot = 0;
      }
      delete it;
      tryMarkRoot(p);
    }
    if (nextRootMarking >= pointers.size()) {
      state = State::ChildMarking;
      nextRootMarking = 0;
      goto _ChildMarking;
    }
    break;

  _ChildMarking:
  case State::ChildMarking:
    while (grayObjs.size() && stepCnt-- > 0) {
      ObjMeta* o = grayObjs.back();
      grayObjs.pop_back();
      o->markState = ObjMeta::MarkColor::Alive;

      auto cls = o->clsInfo;
      auto it = cls->enumPtrs(o);
      for (; it->hasNext(); stepCnt--) {
        auto* ptr = it->getNext();
        auto* meta = ptr->meta;
        if (!meta)
          continue;
        if (meta->markState == ObjMeta::MarkColor::Unmarked) {
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
      if (meta->markState == ObjMeta::MarkColor::Unmarked) {
        nextSweeping = metaSet.erase(nextSweeping);
        delete meta;
        continue;
      }
      meta->markState = ObjMeta::MarkColor::Unmarked;
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
