#include "pch.h"
#include "gcptr.h"
#include <set>
#include <algorithm>
#include <assert.h>

namespace gc
{
    using namespace details;

    struct MetaInfo
    {
        enum MarkColor : char { White, Gray, Black };

        ClassInfo*  clsInfo;
        char*       objPtr;
        MarkColor   color;

        struct Less
        {
            bool operator()(MetaInfo* x, MetaInfo* y)const { return *x < *y; }
        };

        explicit MetaInfo(ClassInfo* c, char* objPtr_) : objPtr(objPtr_), clsInfo(c), color(MarkColor::White) {}
        ~MetaInfo() { if ( objPtr ) clsInfo->dctor(clsInfo, objPtr); }
        bool operator<(MetaInfo& r) { return objPtr + clsInfo->size <= r.objPtr; }
    };


    struct GC
    {
        typedef std::set<MetaInfo*, MetaInfo::Less> MetaSet;
        enum class State { RootMarking, ChildMarking, Sweeping };

        std::vector<PtrBase*>   pointers;
        std::vector<MetaInfo*>  grayObjs;
        MetaSet				    metaSet;
        size_t				    nextRootMarking;
        MetaSet::iterator	    nextSweeping;
        State				    state;

        GC() : state(State::RootMarking), nextRootMarking(0) {}
        ~GC() { collect((unsigned)-1); }
        static GC* get() { static GC i; return &i; }

        void onPtrChanged(PtrBase* p)
        {
            if ( !p->metaInfo )return;

            switch ( state ) {
            case State::RootMarking:	if ( p->index < nextRootMarking ) markAsRoot(p); break;
            case State::ChildMarking:	markAsRoot(p); break;
            case State::Sweeping:
                if ( p->metaInfo->color == MetaInfo::White ) {
                    if ( *p->metaInfo < **nextSweeping ) {
                        // already white and ready for the next rootMarking.
                    } else {
                        // mark it alive to bypass sweeping.
                        p->metaInfo->color = MetaInfo::Black;
                    }
                }
                break;
            }
        }
        void markAsRoot(PtrBase* p)
        {
            if ( p->isRoot == 1 ) {
                if ( p->metaInfo->color == MetaInfo::White ) {
                    p->metaInfo->color = MetaInfo::Gray;
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
                    info->color = MetaInfo::Black;
                    // IMPORTANT
                    auto cls = info->clsInfo;
                    for ( size_t i = 0; i < cls->getSubPtrCnt(cls, info->objPtr); i++ ) {
                        auto* subPtr = cls->getSubPtr(cls, info->objPtr, i);
                        if ( subPtr->metaInfo->color == MetaInfo::White ) {
                            grayObjs.push_back(subPtr->metaInfo);
                        }
                    }
                }
                if ( !grayObjs.size() ) {
                    state = State::Sweeping;
                    nextSweeping = metaSet.begin();
                    goto _Sweeping;
                }
                break;

            _Sweeping:
            case State::Sweeping:
                for ( ; nextSweeping != metaSet.end() && stepCnt--;) {
                    MetaInfo* meta = *nextSweeping;
                    if ( meta->color == MetaInfo::White ) {
                        delete meta;
                        nextSweeping = metaSet.erase(nextSweeping);
                        continue;
                    }
                    meta->color = MetaInfo::White;
                    ++nextSweeping;
                }
                if ( nextSweeping == metaSet.end() ) {
                    state = State::RootMarking;
                    if ( metaSet.size() )
                        goto _RootMarking;
                }
                break;
            }
        }
    };    
        
    void gc_collect(int step) { return GC::get()->collect(step); }

    //////////////////////////////////////////////////////////////////////////

    bool ClassInfo::isCreatingObj = false;
    ClassInfo ClassInfo::Empty{ 0, 0, 0,0,0 };
    MetaInfo DummyMetaInfo(&ClassInfo::Empty, nullptr);

    MetaInfo* findOwnerMeta(void* obj)
    {
        auto& objs = GC::get()->metaSet;
        DummyMetaInfo.objPtr = (char*)obj;
        auto i = objs.lower_bound(&DummyMetaInfo);
        DummyMetaInfo.objPtr = 0;
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
            p->isRoot = 0; // IMPORTANT
            owner->clsInfo->registerSubPtr(owner, p);
        }
    }

    PtrBase::PtrBase() : metaInfo(0), isRoot(1) { registerPtr(this); }
    PtrBase::PtrBase(void* obj) : isRoot(1) { registerPtr(this); metaInfo = findOwnerMeta(obj); }
    PtrBase::~PtrBase() { GC::get()->unregisterPtr(this); }
    void PtrBase::onPtrChanged() { GC::get()->onPtrChanged(this); }

    
    void ClassInfo::registerSubPtr(MetaInfo* owner, PtrBase* p)
    {
        if ( state == ClassInfo::State::Registered ) return;

        auto offset = (char*)p - (char*)owner->objPtr;
        assert(std::find(memPtrOffsets.begin(), memPtrOffsets.end(), offset) == memPtrOffsets.end());
        memPtrOffsets.push_back(offset);
    }

    char* ClassInfo::createObj(MetaInfo*& meta)
    {
        auto buf = alloc(this);
        meta = new MetaInfo(this, buf);
        GC::get()->metaSet.insert(meta);
        return buf;
    }


}
