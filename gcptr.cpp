#include "gcptr.h"
#include <set>
#include <limits.h>

namespace slgc
{
    using namespace details;

    struct Impl
    {
        typedef std::set<ObjMeta*, ObjMeta::Less> MetaSet;
        enum class State { RootMarking, ChildMarking, Sweeping };

        std::vector<PtrBase*>   pointers;
        std::vector<ObjMeta*>   grayObjs;
        MetaSet				    metaSet;
        size_t				    nextRootMarking;
        MetaSet::iterator	    nextSweeping;
        State				    state;

        Impl() : state(State::RootMarking), nextRootMarking(0) {}
        ~Impl() { collect(INT_MAX); }
        static Impl* get() { static Impl i; return &i; }

        void onPtrChanged(PtrBase* p)
        {
            if ( !p->meta )return;
            switch ( state ) {
            case State::RootMarking:	if ( p->index < nextRootMarking ) markAsRoot(p); break;
            case State::ChildMarking:	markAsRoot(p); break;
            case State::Sweeping:
                if ( p->meta->markState == ObjMeta::Unmarked ) {
                    if ( *p->meta < **nextSweeping ) {
                        // already white and ready for the next rootMarking.
                    } else {
                        // mark it alive to bypass sweeping.
                        p->meta->markState = ObjMeta::Alive;
                    }
                }
                break;
            }
        }
        void markAsRoot(PtrBase* p)
        {
            if ( p->isRoot == 1 ) {
                if ( p->meta->markState == ObjMeta::Unmarked ) {
                    p->meta->markState = ObjMeta::Gray;
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
            // changing of pointers may affect the rootMarking
            if ( !pointer->meta ) return;
            if ( state == State::RootMarking ) {
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
                    ObjMeta* o = grayObjs.back();
                    grayObjs.pop_back();
                    o->markState = ObjMeta::Alive;

                    auto cls = o->clsInfo;
                    auto iter = cls->enumPtrs(cls, o->objPtr);
                    for (; iter->hasNext(); stepCnt--) {
                        auto* ptr = iter->getNext();
                        // pointers in STL containers are originally treated as root pointers, so corrected here.
                        ptr->setAsLeaf(); 
                        auto* meta = ptr->meta;
                        if ( meta->markState == ObjMeta::Unmarked ) {
                            grayObjs.push_back(meta);
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
                for ( ; nextSweeping != metaSet.end() && stepCnt-- > 0;) {
                    ObjMeta* meta = *nextSweeping;
                    if ( meta->markState == ObjMeta::Unmarked ) {
                        delete meta;
                        nextSweeping = metaSet.erase(nextSweeping);
                        continue;
                    }
                    meta->markState = ObjMeta::Unmarked;
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
        
    void gc_collect(int steps) { return Impl::get()->collect(steps); }



    //////////////////////////////////////////////////////////////////////////

    bool ClassInfo::isCreatingObj = false;
    ClassInfo ClassInfo::Empty{ 0, 0, 0};
    ObjMeta DummyMetaInfo(&ClassInfo::Empty, nullptr);

    ObjMeta* findOwnerMeta(void* obj)
    {
        auto& objs = Impl::get()->metaSet;
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
        Impl::get()->registerPtr(p);
        if ( ClassInfo::isCreatingObj ) {
            // owner may not be the current one(e.g pointers on the stack of constructor)
            auto* owner = findOwnerMeta(p);
            if ( !owner ) return;
            p->setAsLeaf(); // we know it is leaf before tracing.
            owner->clsInfo->registerSubPtr(owner, p);
        }
    }

    PtrBase::PtrBase() : meta(0), isRoot(1) { registerPtr(this); }
    PtrBase::PtrBase(void* obj) : isRoot(1) { registerPtr(this); meta = findOwnerMeta(obj); }
    PtrBase::~PtrBase() { Impl::get()->unregisterPtr(this); }
    void PtrBase::onPtrChanged() { Impl::get()->onPtrChanged(this); }

    void ClassInfo::registerSubPtr(ObjMeta* owner, PtrBase* p)
    {
        if ( state == ClassInfo::State::Registered ) return;
        auto offset = (char*)p - (char*)owner->objPtr;
        memPtrOffsets.push_back(offset);
    }

    ObjMeta* ClassInfo::allocObj()
    {
        auto buf = alloc(this);
        auto meta = new ObjMeta(this, buf);
        Impl::get()->metaSet.insert(meta);
        return meta;
    }

    void* ClassInfo::PtrEnumerator::operator new( size_t sz)
    {
        static char buf[255];
        return sz < sizeof(buf) ? buf : nullptr;
    }

}
