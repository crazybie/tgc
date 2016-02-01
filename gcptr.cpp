#include "gcptr.h"
#include <set>
#include <deque>
#include <algorithm>


namespace gc
{
    namespace details
    {
        enum class MarkColor : char { White, Gray, Black };

        struct MetaInfo
        {
            ClassInfo*      clsInfo;
            MarkColor       color;
            bool            useMockObj;
            static char*    mockObj;

            struct Less
            {
                bool operator()(MetaInfo* x, MetaInfo* y) { return *x < *y; }
            };

            explicit MetaInfo(ClassInfo* c) : clsInfo(c), color(MarkColor::White), useMockObj(false){}
            ~MetaInfo() { if (clsInfo->dctor) clsInfo->dctor(getObj()); }
            char* getObj() { return useMockObj ? mockObj : (char*)this + sizeof(MetaInfo); }
            bool containsPointer(void* ptr) { return getObj() <= ptr && ptr < getObj() + clsInfo->size; }
            bool operator<(MetaInfo& r) { return getObj() + clsInfo->size <= r.getObj(); }
        };

        char* MetaInfo::mockObj = nullptr;
        MetaInfo DummyMetaInfo(&ClassInfo::Empty);

        struct GC
        {
            typedef std::set<MetaInfo*, MetaInfo::Less> ObjSet;

            std::deque<PointerBase*>            pointers;
            std::deque<MetaInfo*>               grayObjs;
            std::set<MetaInfo*, MetaInfo::Less> metaInfoSet;
            size_t                              nextRootMarking;
            ObjSet::iterator                    nextSweeping;

            enum class State { RootMarking, ChildMarking, Sweeping } state;


            GC() : state(State::RootMarking), nextRootMarking(0) {}

            ~GC()
            {
                // full collect
                gc::GcCollect((unsigned)-1);
            }
            void onPointerChanged(PointerBase* p)
            {
                if (!p->metaInfo)return;
                switch (state) {
                case State::RootMarking:
                    if (p->index < nextRootMarking) {
                        markAsRoot(p);
                    }
                    break;
                case State::ChildMarking:
                    markAsRoot(p);
                    break;
                case State::Sweeping:
                    if (p->metaInfo->color == MarkColor::White) {
                        if (*p->metaInfo < **nextSweeping) {
                            // already white and ready for the next rootMarking.
                        } else {
                            // mark it alive to bypass sweeping.
                            p->metaInfo->color = MarkColor::Black;
                        }
                    }
                    break;
                }
            }
            MetaInfo* findOwnerMeta(void* obj)
            {
                DummyMetaInfo.mockObj = (char*)obj;
                DummyMetaInfo.useMockObj = true;
                auto i = metaInfoSet.lower_bound(&DummyMetaInfo);
                if (i == metaInfoSet.end() || !(*i)->containsPointer(obj)) {
                    return 0;
                }
                return *i;
            }
            MetaInfo* newMetaInfo(ClassInfo* clsInfo, char* mem)
            {
                MetaInfo* info = new (mem)MetaInfo(clsInfo);
                metaInfoSet.insert(info);
                return info;
            }
            void markAsRoot(PointerBase* p)
            {
                if (!p->metaInfo) return;
                if (p->isRoot == 1) {
                    if (p->metaInfo->color == MarkColor::White) {
                        p->metaInfo->color = MarkColor::Gray;
                        grayObjs.push_back(p->metaInfo);
                    }
                }
            }
            void registerPointerToOwner(PointerBase* p, MetaInfo* owner)
            {
                if (!owner) return;
                p->isRoot = 0;

                auto* clsInfo = owner->clsInfo;
                if (clsInfo->memPtrState == ClassInfo::MemPtrState::Registered) return;

                auto offset = (char*)p - (char*)owner->getObj();
                auto& offsets = clsInfo->memPtrOffsets;
                if (std::find(offsets.begin(), offsets.end(), offset) == offsets.end()) {
                    offsets.push_back(offset);
                }
            }
            void registerPointer(PointerBase* p)
            {
                p->index = pointers.size();
                pointers.push_back(p);

                // is constructing object member pointer?
                if (ClassInfo::currentConstructing) {
                    // owner may not be the current one(e.g pointers on the stack of constructor)
                    registerPointerToOwner(p, findOwnerMeta(p));
                }
            }
            void unregisterPointer(PointerBase* p)
            {
                std::swap(pointers[p->index], pointers.back());
                auto* pointer = pointers[p->index];
                pointer->index = p->index;
                pointers.pop_back();

                // pointers列表变动会影响rootMarking
                if (state == GC::State::RootMarking) {
                    if (p->index < nextRootMarking) {
                        markAsRoot(pointer);
                    }
                }
            }
            void collect(int stepCnt)
            {
                switch (state) {
                _RootMarking:
                case State::RootMarking:
                    for (; nextRootMarking < pointers.size() && stepCnt--; nextRootMarking++) {
                        markAsRoot(pointers[nextRootMarking]);
                    }
                    if (nextRootMarking >= pointers.size()) {
                        state = State::ChildMarking;
                        nextRootMarking = 0;
                        goto _ChildMarking;
                    }
                    break;

                _ChildMarking:
                case State::ChildMarking:
                    while (grayObjs.size() && stepCnt--) {
                        MetaInfo* info = grayObjs.back();
                        grayObjs.pop_back();
                        info->color = MarkColor::Black;
                        if (!info->clsInfo->memPtrOffsets.size()) continue;
                        for (auto memPtrOffset : info->clsInfo->memPtrOffsets) {
                            auto* mp = (PointerBase*)(info->getObj() + memPtrOffset);
                            if (mp->metaInfo->color == MarkColor::White) {
                                grayObjs.push_back(mp->metaInfo);
                            }
                        }
                    }
                    if (!grayObjs.size()) {
                        state = State::Sweeping;
                        nextSweeping = metaInfoSet.begin();
                        goto _Sweeping;
                    }
                    break;

                _Sweeping:
                case State::Sweeping:
                    for (; nextSweeping != metaInfoSet.end() && stepCnt--;) {
                        MetaInfo* obj = *nextSweeping;
                        if (obj->color == MarkColor::White) {
                            obj->~MetaInfo();
                            delete[](char*)obj;
                            nextSweeping = metaInfoSet.erase(nextSweeping);
                            continue;
                        }
                        obj->color = MarkColor::White;
                        ++nextSweeping;
                    }
                    if (nextSweeping == metaInfoSet.end()) {
                        state = State::RootMarking;
                        if (metaInfoSet.size())
                            goto _RootMarking;
                    }
                    break;
                }
            }
        };

        static GC i;
        GC* getGC() {  return &i; }

        PointerBase::PointerBase() : metaInfo(0), isRoot(1) { getGC()->registerPointer(this); }
        PointerBase::PointerBase(void* obj) : isRoot(1) { auto* gc = getGC(); gc->registerPointer(this); metaInfo = gc->findOwnerMeta(obj); }
        PointerBase::~PointerBase() { getGC()->unregisterPointer(this); }
        void PointerBase::onPointerChanged() { getGC()->onPointerChanged(this); }

        //////////////////////////////////////////////////////////////////////////

        ClassInfo* ClassInfo::currentConstructing = 0;
        ClassInfo ClassInfo::Empty{ 0, 0 };

        ClassInfo::ClassInfo(void(*d)(void*), int sz) : dctor(d), size(sz), memPtrState(MemPtrState::Unregistered) {}

        void* ClassInfo::beginNewObj(int objSz, MetaInfo*& info)
        {
            currentConstructing = this;
            if (memPtrState == MemPtrState::Unregistered) {
                memPtrState = MemPtrState::Registering;
            }
            // 对象和元信息放在一起，减少内存申请调用
            char* buf = new char[objSz + sizeof(MetaInfo)];
            // 先注册元信息再构造对象，方便此对象内的指针在构造的时候反注册
            info = getGC()->newMetaInfo(this, buf);
            return buf + sizeof(MetaInfo);
        }

        void ClassInfo::endNewObj()
        {
            if (memPtrState == MemPtrState::Registering) {
                memPtrState = MemPtrState::Registered;
            }
            currentConstructing = 0;
        }
    }

    void GcCollect(int step) { return details::getGC()->collect(step); }
}
