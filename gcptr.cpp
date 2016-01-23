#include "gcptr.h"
#include <set>
#include <stdint.h>
#include <unordered_set>
#include <vector>

namespace gc
{
    namespace details
    {
        enum class MarkColor : char { White, Black };

        struct ObjInfo
        {
            char*                       obj;
            Dctor                       destroy;  // call the proper destructor due to typeless char*.
            size_t                      size;
            std::vector<PointerBase*>   memberPointers;
            MarkColor                   color;

            struct Less
            {
                bool operator()(ObjInfo* x, ObjInfo* y) { return x->obj + x->size <= y->obj; }
            };
            
            ObjInfo(void* o, int sz, Dctor dctor) : obj((char*)o), size(sz), destroy(dctor), color(MarkColor::White) {}
            ~ObjInfo() { if (destroy) destroy(obj); }
            bool containsPointer(void* ptr) { return obj <= ptr && ptr < obj + size; }
        };

        const int ObjInfoSize = sizeof(ObjInfo);
        ObjInfo* kObjInfo_Uninit = (ObjInfo*)-1;

        struct GC
        {
            std::unordered_set<PointerBase*>    pointers;
            std::set<ObjInfo*, ObjInfo::Less>   objInfoSet;
            std::vector<ObjInfo*>               grayObjs;

            enum class State { Idle, Marking, Sweeping } state;

            GC()
            {
                state = State::Idle;
                grayObjs.reserve(1024);
                pointers.reserve(1024);
            }
            ~GC() 
            { 
                while (objInfoSet.size()) 
                    gc::GcCollect(INT32_MAX);
            }
            ObjInfo* findOwnerObjInfo(void* obj)
            {
                ObjInfo temp(obj, 0, 0);
                auto i = objInfoSet.lower_bound(&temp);
                if (i == objInfoSet.end() || !(*i)->containsPointer(obj))
                    return 0;
                return *i;
            }
            ObjInfo* registerObj(void* o, int sz, Dctor dctor, char* mem)
            {
                ObjInfo* objInfo = new (mem)ObjInfo(o, sz, dctor);
                objInfoSet.insert(objInfo);
                return objInfo;
            }
            void onPointerUpdate(PointerBase* ptr)
            {
                if (state == State::Marking) {
                    markAsRoot(ptr);
                }
            }
            void markAsRoot(PointerBase* i)
            {
                if (!i->objInfo) return;
                if (i->isRoot() && i->objInfo->color == MarkColor::White) {
                    grayObjs.push_back(i->objInfo);
                }
            }
            int collect(int stepCnt)
            {
                int sweptCnt = 0;
                switch(state)
                {
                case State::Idle: 
                    state = State::Marking;
                    for (auto i : pointers) {
                        markAsRoot(i);
                    }
                    break;

                case State::Marking:
                    while (grayObjs.size() && stepCnt--) {
                        ObjInfo* obj = grayObjs.back();
                        grayObjs.pop_back();
                        obj->color = MarkColor::Black;
                        for (auto j : obj->memberPointers) {
                            if (j->objInfo->color == MarkColor::White) {
                                grayObjs.push_back(j->objInfo);
                            }
                        }
                    }
                    if (grayObjs.size() == 0) state = State::Sweeping;
                    break;

                case State::Sweeping:
                    state = State::Idle;
                    for (auto i = objInfoSet.begin(); i != objInfoSet.end();) {
                        ObjInfo* obj = *i;
                        if (obj->color == MarkColor::White) {
                            sweptCnt++;
                            delete obj;
                            i = objInfoSet.erase(i);
                            continue;
                        }
                        obj->color = MarkColor::White;
                        ++i;
                    }
                    break;
                }
                return sweptCnt;
            }
        };

        static GC gGC;

        //////////////////////////////////////////////////////////////////////////

        ObjInfo* registerObj(void* o, int sz, Dctor dctor, char* mem) { return gGC.registerObj(o, sz, dctor, mem); }

        PointerBase::PointerBase() : objInfo(0), owner(kObjInfo_Uninit) { gGC.pointers.insert(this); }
        PointerBase::PointerBase(void* obj) : owner(kObjInfo_Uninit), objInfo(gGC.findOwnerObjInfo(obj)) { gGC.pointers.insert(this); }
        PointerBase::~PointerBase() { gGC.pointers.erase(this); }
        void PointerBase::onPointerUpdate() { gGC.onPointerUpdate(this); }

        bool PointerBase::isRoot()
        {
            if (owner == kObjInfo_Uninit) {
                owner = gGC.findOwnerObjInfo(this);
                if (owner) {                    
                    owner->memberPointers.push_back(this);
                }
            }
            return !owner;
        }
    }

    int GcCollect(int step) { return details::gGC.collect(step); }
}


