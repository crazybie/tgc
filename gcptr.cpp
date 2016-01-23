#include "gcptr.h"
#include <set>
#include <stdint.h>
#include <unordered_set>
#include <vector>

namespace gc
{
    namespace details
    {

        enum class MarkColor : char { White, Gray, Black };

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

            GC()
            {
                grayObjs.reserve(1000);
                pointers.reserve(1000);
            }
            ~GC() 
            { 
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
                ObjInfo* objInfo = mem ? new (mem)ObjInfo(o, sz, dctor) : new ObjInfo(o, sz, dctor);
                objInfoSet.insert(objInfo);
                return objInfo;
            }
            void registerPointer(PointerBase* ptr)
            {
                pointers.insert(ptr);
                auto obj = ptr->getObjInfo();
                if (!obj) return;
                if (obj->color == MarkColor::Black) {
                    obj->color = MarkColor::Gray;
                    grayObjs.push_back(obj);
                }
            }
            void unregisterPointer(PointerBase* ptr)
            {
                pointers.erase(ptr);
                auto obj = ptr->getObjInfo();
                if (!obj) return;
                if (obj->color == MarkColor::Black) {
                    for (auto j : obj->memberPointers) {
                        auto o2 = j->getObjInfo();
                        if (o2->color == MarkColor::Black) {
                            o2->color = MarkColor::Gray;
                            grayObjs.push_back(o2);
                        }
                    }
                }
            }
            void IncrementalMark(int stepCnt)
            {
                if (grayObjs.size() == 0) {
                    for (auto i : pointers) {
                        auto o = i->getObjInfo();
                        if (!o) continue;                        
                        if (i->isRoot() && o->color == MarkColor::White) {
                            o->color = MarkColor::Gray;
                            grayObjs.push_back(o);
                        }
                    }
                }
                while (grayObjs.size() && stepCnt--) {
                    ObjInfo* obj = grayObjs.back();
                    grayObjs.pop_back();
                    obj->color = MarkColor::Black;
                    for (auto j : obj->memberPointers) {
                        auto o = j->getObjInfo();
                        if (o->color == MarkColor::White) {
                            o->color = MarkColor::Gray;
                            grayObjs.push_back(o);
                        }
                    }
                }
            }

            int GcCollect(int stepCnt)
            {
                IncrementalMark(stepCnt);
                if (grayObjs.size() != 0) return 0;
                int sweptCnt = 0;
                for (auto i = objInfoSet.begin(); i != objInfoSet.end();) {
                    ObjInfo* node = *i;
                    if (node->color == MarkColor::White) {
                        sweptCnt++;
                        delete node;
                        i = objInfoSet.erase(i);
                        continue;
                    }
                    node->color = MarkColor::White;
                    ++i;
                }
                return sweptCnt;
            }
        };

        static GC gGC;

        //////////////////////////////////////////////////////////////////////////
        ObjInfo* registerObj(void* o, int sz, Dctor dctor, char* mem) { return gGC.registerObj(o, sz, dctor, mem); }
        PointerBase::PointerBase(void* obj) { owner = kObjInfo_Uninit; objInfo = gGC.findOwnerObjInfo(obj); }
        PointerBase::~PointerBase() { gGC.unregisterPointer(this); }
        void PointerBase::registerPointer() { gGC.registerPointer(this);  }

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

    int GcCollect(int step) { return details::gGC.GcCollect(step); }
}


