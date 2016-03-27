#include "pch.h"
#include "gcptr.h"
#include <set>
#include <algorithm>
#include <assert.h>

namespace gc
{
    using namespace details;

    enum class MarkColor : char { White, Gray, Black };

    struct MetaInfo
    {
        ClassInfo*      clsInfo;
        char*           objPtr;
        MarkColor       color;

        struct Less
        {
            bool operator()(MetaInfo* x, MetaInfo* y)const { return *x < *y; }
        };

        explicit MetaInfo(ClassInfo* c, char* objPtr_) : objPtr(objPtr_), clsInfo(c), color(MarkColor::White) {}
        ~MetaInfo() { if ( clsInfo->dctor ) clsInfo->dctor(clsInfo, objPtr); }
        bool operator<(MetaInfo& r) { return objPtr + clsInfo->size <= r.objPtr; }
    };
    
    struct GC
    {
        typedef std::set<MetaInfo*, MetaInfo::Less> ObjSet;
        enum class State { RootMarking, ChildMarking, Sweeping };

        std::vector<PtrBase*>   pointers;
        std::vector<MetaInfo*>  grayObjs;
        ObjSet				    metaInfoSet;
        size_t				    nextRootMarking;
        ObjSet::iterator	    nextSweeping;
        State				    state;

        GC() : state(State::RootMarking), nextRootMarking(0) {}
        ~GC() { gc::gc_collect((unsigned)-1); }
        static GC* get() { static GC i; return &i; }

        void onPtrChanged(PtrBase* p)
        {
            if ( !p->metaInfo )return;

            switch ( state ) {
            case State::RootMarking:	if ( p->index < nextRootMarking ) markAsRoot(p); break;
            case State::ChildMarking:	markAsRoot(p); break;
            case State::Sweeping:
                if ( p->metaInfo->color == MarkColor::White ) {
                    if ( *p->metaInfo < **nextSweeping ) {
                        // already white and ready for the next rootMarking.
                    } else {
                        // mark it alive to bypass sweeping.
                        p->metaInfo->color = MarkColor::Black;
                    }
                }
                break;
            }
        }
        void markAsRoot(PtrBase* p)
        {
            if ( p->isRoot == 1 ) {
                if ( p->metaInfo->color == MarkColor::White ) {
                    p->metaInfo->color = MarkColor::Gray;
                    grayObjs.push_back(p->metaInfo);
                }
            }
        }
        void registerPtr(PtrBase* p)
        {
            p->index = pointers.size();
            pointers.push_back(p);
        }
        void unregisterPtr(PtrBase* p)
        {
            std::swap(pointers[p->index], pointers.back());
            auto* pointer = pointers[p->index];
            pointer->index = p->index;
            pointers.pop_back();
            // pointers列表变动会影响rootMarking
            if ( !pointer->metaInfo ) return;
            if ( state == GC::State::RootMarking ) {
                if ( p->index < nextRootMarking ) {
                    markAsRoot(pointer);
                }
            }
        }
        void collect(int stepCnt)
        {
            switch ( state ) {

            _RootMarking:
            case State::RootMarking:
                for ( ; nextRootMarking < pointers.size() && stepCnt--; nextRootMarking++ ) {
                    auto p = pointers[nextRootMarking];
                    if ( !p->metaInfo ) continue;
                    markAsRoot(p);
                }
                if ( nextRootMarking >= pointers.size() ) {
                    state = State::ChildMarking;
                    nextRootMarking = 0;
                    goto _ChildMarking;
                }
                break;

            _ChildMarking:
            case State::ChildMarking:
                while ( grayObjs.size() && stepCnt-- ) {
                    MetaInfo* info = grayObjs.back();
                    grayObjs.pop_back();
                    info->color = MarkColor::Black;
                    for ( int i = 0; i < info->clsInfo->getSubPtrCnt(); i++ ) {
                        auto* subPtr = info->clsInfo->getSubPtr(info->objPtr, i);
                        if ( subPtr->metaInfo->color == MarkColor::White ) {
                            grayObjs.push_back(subPtr->metaInfo);
                        }
                    }
                }
                if ( !grayObjs.size() ) {
                    state = State::Sweeping;
                    nextSweeping = metaInfoSet.begin();
                    goto _Sweeping;
                }
                break;

            _Sweeping:
            case State::Sweeping:
                for ( ; nextSweeping != metaInfoSet.end() && stepCnt--;) {
                    MetaInfo* meta = *nextSweeping;
                    if ( meta->color == MarkColor::White ) {
                        delete meta;
                        nextSweeping = metaInfoSet.erase(nextSweeping);
                        continue;
                    }
                    meta->color = MarkColor::White;
                    ++nextSweeping;
                }
                if ( nextSweeping == metaInfoSet.end() ) {
                    state = State::RootMarking;
                    if ( metaInfoSet.size() )
                        goto _RootMarking;
                }
                break;
            }
        }
    };

    //////////////////////////////////////////////////////////////////////////

    MetaInfo DummyMetaInfo(&ClassInfo::Empty, nullptr);

    MetaInfo* findOwnerMeta(void* obj)
    {
        auto& objs = GC::get()->metaInfoSet;
        DummyMetaInfo.objPtr = (char*)obj;
        auto i = objs.lower_bound(&DummyMetaInfo);
        if ( i == objs.end() || !( *i )->clsInfo->containsPtr(( *i )->objPtr, (char*)obj) ) {
            return 0;
        }
        return *i;
    };

    void registerPtr(PtrBase* p)
    {
        GC::get()->registerPtr(p);
        if ( ClassInfo::isCreatingObj ) {
            // owner may not be the current one(e.g pointers on the stack of constructor)
            auto* owner = findOwnerMeta(p);
            if ( !owner ) return;
            p->isRoot = 0;
            owner->clsInfo->registerSubPtr(owner, p);
        }
    }

    PtrBase::PtrBase() : metaInfo(0), isRoot(1) { registerPtr(this); }
    PtrBase::PtrBase(void* obj) : isRoot(1) { registerPtr(this); metaInfo = findOwnerMeta(obj); }
    PtrBase::~PtrBase() { GC::get()->unregisterPtr(this); }
    void PtrBase::onPtrChanged() { GC::get()->onPtrChanged(this); }


    bool ClassInfo::isCreatingObj = false;
    ClassInfo ClassInfo::Empty{ 0, 0, 0 };
    ClassInfo::ClassInfo(ClassInfo::Alloc a, ClassInfo::Dctor d, int sz) : alloc(a), dctor(d), size(sz), memPtrState(MemPtrState::Unregistered) {}
    void ClassInfo::endNewObj() { memPtrState = MemPtrState::Registered; isCreatingObj = false; }

    void ClassInfo::registerSubPtr(MetaInfo* owner, PtrBase* p)
    {
        if ( memPtrState == ClassInfo::MemPtrState::Registered ) return;
        auto offset = (char*)p - (char*)owner->objPtr;
        assert(std::find(memPtrOffsets.begin(), memPtrOffsets.end(), offset) == memPtrOffsets.end());
        memPtrOffsets.push_back(offset);
    }

    void* ClassInfo::beginNewObj(int objSz, MetaInfo*& info)
    {
        isCreatingObj = true;
        char* buf = alloc(this, objSz);
        GC::get()->metaInfoSet.insert(info = new MetaInfo(this, buf));
        return buf;
    }

    void gc_collect(int step) { return GC::get()->collect(step); }
}
