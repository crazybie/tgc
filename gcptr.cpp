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
                bool operator()(const ObjInfo* x, const ObjInfo* y) const { return x->obj + x->size <= y->obj; }
            };
            
            ObjInfo(void* o, int sz, Dctor dctor) : obj((char*)o), size(sz), destroy(dctor), color(MarkColor::White) {}
            ~ObjInfo() { if (destroy) destroy(obj); }
            bool containsPointer(void* ptr) { return obj <= ptr && ptr < obj + size; }
        };

        const int ObjInfoSize = sizeof(ObjInfo);

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
                for (auto i : pointers) {
                    if (objInfo->containsPointer(i)) {
                        objInfo->memberPointers.push_back(i);
                        i->owner = objInfo;
                    }
                }
                return objInfo;
            }
            void registerPointer(PointerBase* ptr)
            {
                auto obj = ptr->objInfo;
                pointers.insert(ptr);
                if (!obj) return;
                if (obj->color == MarkColor::Black) {
                    obj->color = MarkColor::Gray;
                    grayObjs.push_back(obj);
                }
            }
            void unregisterPointer(PointerBase* ptr)
            {
                pointers.erase(ptr);
                auto obj = ptr->objInfo;
                if (!obj) return;
                if (obj->color == MarkColor::Black) {
                    for (auto j : obj->memberPointers) {
                        if (j->objInfo->color == MarkColor::Black) {
                            j->objInfo->color = MarkColor::Gray;
                            grayObjs.push_back(j->objInfo);
                        }
                    }
                }
            }
            void IncrementalMark(int stepCnt)
            {
                if (grayObjs.size() == 0) {
                    for (auto i : pointers) {
                        if (!i->objInfo) continue;
                        if (i->objInfo->color == MarkColor::White && !i->owner) {
                            i->objInfo->color = MarkColor::Gray;
                            grayObjs.push_back(i->objInfo);
                        }
                    }
                }
                while (grayObjs.size() && stepCnt--) {
                    ObjInfo* obj = grayObjs.back();
                    grayObjs.pop_back();
                    obj->color = MarkColor::Black;
                    for (auto j : obj->memberPointers) {
                        if (j->objInfo->color == MarkColor::White) {
                            j->objInfo->color = MarkColor::Gray;
                            grayObjs.push_back(j->objInfo);
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

        PointerBase::PointerBase(void* obj) { owner = 0; objInfo = gGC.findOwnerObjInfo(obj); }
        PointerBase::~PointerBase() { gGC.unregisterPointer(this); }
        void PointerBase::registerPointer() { gGC.registerPointer(this);  }
        ObjInfo* registerObj(void* o, int sz, Dctor dctor, char* mem) { return gGC.registerObj(o, sz, dctor, mem); }
    }

    int GcCollect(int step) { return details::gGC.GcCollect(step); }
}


