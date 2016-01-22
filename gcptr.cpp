#include "gcptr.h"
#include <set>
#include <stdint.h>
#include <unordered_set>
#include <vector>

namespace gc
{
    namespace details
    {
        typedef void(*Dctor)(void*);

        enum class MarkColor : char { White, Gray, Black };

        struct ObjInfo
        {
            char*                       object;
            Dctor                       destroy;  // call the proper destructor due to typeless char*.
            size_t                      size;
            MarkColor                   color;
            std::vector<PointerBase*>   memberPointers;

            struct Less
            {
                bool operator()(const ObjInfo* x, const ObjInfo* y) const { return x->object + x->size <= y->object; }
            };
            
            ObjInfo(void* obj, int size_, Dctor dctor) : object((char*)obj), size(size_), destroy(dctor), color(MarkColor::White) {}
            ~ObjInfo() { if (destroy) destroy(object); }
            bool containsPointer(void* ptr) { return object <= ptr && ptr < object + size; }
        };

        const int ObjInfoSize = sizeof(ObjInfo);

        struct GC
        {
            typedef std::set<ObjInfo*, ObjInfo::Less>   ObjInfoSet;
            typedef std::unordered_set<PointerBase*>    GcPointers;
            
            GcPointers              pointers;
            ObjInfoSet              objInfoSet;
            std::vector<ObjInfo*>   grayObjs;

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
            ObjInfo* registerObj(void* obj, int size, Dctor destroy, char* objInfoMem)
            {
                ObjInfo* objInfo = objInfoMem ? 
                    new (objInfoMem)ObjInfo(obj, size, destroy) : new ObjInfo(obj, size, destroy);
                objInfoSet.insert(objInfo);
                for (auto i : pointers) {
                    if (objInfo->containsPointer(i)) {
                        objInfo->memberPointers.push_back(i);
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
                        if (i->objInfo->color == MarkColor::White && !findOwnerObjInfo(i)) {
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

        PointerBase::PointerBase(void* obj) { objInfo = gGC.findOwnerObjInfo(obj); }
        PointerBase::~PointerBase() { gGC.unregisterPointer(this); }
        void PointerBase::registerPointer() { gGC.registerPointer(this);  }
        ObjInfo* registerObj(void* obj, int size, void(*destroy)(void*), char* objInfoMem) { return gGC.registerObj(obj, size, destroy, objInfoMem); }
    }

    int GcCollect(int step) { return details::gGC.GcCollect(step); }
}


