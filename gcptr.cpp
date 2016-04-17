#include "pch.h"
#include "gcptr.h"
#include <set>
#include <limits.h>

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
        ~GC() { collect(INT_MAX); }
        static GC* get() { static GC i; return &i; }

        void onPtrChanged(PtrBase* p)
        {
            if ( !p->meta )return;
            switch ( state ) {
            case State::RootMarking:	if ( p->index < nextRootMarking ) markAsRoot(p); break;
            case State::ChildMarking:	markAsRoot(p); break;
            case State::Sweeping:
                if ( p->meta->color == MetaInfo::White ) {
                    if ( *p->meta < **nextSweeping ) {
                        // already white and ready for the next rootMarking.
                    } else {
                        // mark it alive to bypass sweeping.
                        p->meta->color = MetaInfo::Black;
                    }
                }
                break;
            }
        }
        void markAsRoot(PtrBase* p)
        {
            if ( p->isRoot == 1 ) {
                if ( p->meta->color == MetaInfo::White ) {
                    p->meta->color = MetaInfo::Gray;
                    grayObjs.push_back(p->meta);
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
            if ( !pointer->meta ) return;
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
                for ( ; nextRootMarking < pointers.size() && stepCnt-- >0; nextRootMarking++ ) {
                    auto p = pointers[nextRootMarking];
                    if ( !p->meta ) continue;
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
                while ( grayObjs.size() && stepCnt-- >0 ) {
                    MetaInfo* info = grayObjs.back();
                    grayObjs.pop_back();
                    info->color = MetaInfo::Black;
                    auto cls = info->clsInfo;
                    auto iter = cls->enumSubPtrs(cls, info->objPtr);
                    while ( iter->hasNext() && stepCnt-- >0 ) {
                        auto* subPtr = iter->getNext();
                        if ( subPtr->meta->color == MetaInfo::White ) {
                            grayObjs.push_back(subPtr->meta);
                        }
                    }
                    delete iter;
                }
                if ( !grayObjs.size() ) {
                    state = State::Sweeping;
                    nextSweeping = metaSet.begin();
                    goto _Sweeping;
                }
                break;

            _Sweeping:
            case State::Sweeping:
                for ( ; nextSweeping != metaSet.end() && stepCnt-- > 0;) {
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
    ClassInfo ClassInfo::Empty{ 0, 0, 0, 0 };
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
            p->isRoot = 0;
            owner->clsInfo->registerSubPtr(owner, p);
        }
    }

    PtrBase::PtrBase() : meta(0), isRoot(1) { registerPtr(this); }
    PtrBase::PtrBase(void* obj) : isRoot(1) { registerPtr(this); meta = findOwnerMeta(obj); }
    PtrBase::~PtrBase() { GC::get()->unregisterPtr(this); }
    void PtrBase::onPtrChanged() { GC::get()->onPtrChanged(this); }

    void ClassInfo::registerSubPtr(MetaInfo* owner, PtrBase* p)
    {
        if ( state == ClassInfo::State::Registered ) return;
        auto offset = (char*)p - (char*)owner->objPtr;
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
