#include "gcptr.h"
#include <set>
#include <stdint.h>
#include <unordered_map>
#include <unordered_set>

namespace gc
{
    namespace details
    {
        typedef void(*Dctor)(void*);

        enum class MarkColor : char { White, Gray, Black };

        struct ObjInfo
        {
            char*       object;
            Dctor       destroy;  // call the proper destructor due to typeless char*.
            size_t      size;
            int         refcnt;
            MarkColor   color;
            bool        destructing;

            struct Less
            {
                bool operator()(const ObjInfo* x, const ObjInfo* y) const { return x->object + x->size <= y->object; }
            };
            
            ObjInfo(void* obj, int size_, Dctor dctor) : object((char*)obj), size(size_), destroy(dctor), color(MarkColor::White), refcnt(0), destructing(false) {}
            ~ObjInfo() { if (destroy) destroy(object); }
            bool containsPointer(void* ptr) { return object <= ptr && ptr < object + size; }
        };

        const int ObjInfoSize = sizeof(ObjInfo);

        struct GC
        {
            typedef std::set<ObjInfo*, ObjInfo::Less>           ObjInfoSet;
            typedef std::unordered_map<PointerBase*, ObjInfo*>  GcPointers;
            
            GcPointers                      pointers;
            ObjInfoSet                      objInfoSet;
            std::unordered_set<ObjInfo*>    grayObjs;
            bool                            collecting;

            GC() : collecting(false) 
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
                ObjInfo* objInfo = objInfoMem ? new (objInfoMem)ObjInfo(obj, size, destroy): new ObjInfo(obj, size, destroy);
                objInfoSet.insert(objInfo);
                return objInfo;
            }
            void registerPointer(PointerBase* ptr, ObjInfo* node)
            {
                if (node->color == MarkColor::Black) {
                    node->color = MarkColor::Gray;
                    grayObjs.insert(node);
                }
                pointers[ptr] = node;
            }
            void unregisterPointer(PointerBase* ptr, ObjInfo* node)
            {
                pointers.erase(ptr);
                if (node->color == MarkColor::Black) {
                    for (auto j : pointers) {
                        if (node->containsPointer(j.first)) {
                            j.second->color = MarkColor::Gray;
                            grayObjs.insert(j.second);
                        }
                    }
                }
            }
            void IncrementalMark(int stepCnt)
            {
                if (grayObjs.size() == 0) {
                    for (auto i : pointers) {
                        if (!findOwnerObjInfo(i.first) && i.second->color == MarkColor::White) {
                            i.second->color = MarkColor::Gray;
                            grayObjs.insert(i.second);
                        }
                    }
                }
                while (grayObjs.size() && stepCnt--) {
                    auto i = grayObjs.begin();
                    ObjInfo* node = *i;
                    grayObjs.erase(i);
                    node->color = MarkColor::Black;
                    for (auto j : pointers) {
                        if (node->containsPointer(j.first)) {
                            if (j.second->color == MarkColor::White) {
                                j.second->color = MarkColor::Gray;
                                grayObjs.insert(j.second);
                            }
                        }
                    }
                }
            }

            int GcCollect(int stepCnt)
            {
                IncrementalMark(stepCnt);
                if (grayObjs.size() != 0) return 0;

                collecting = true;
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
                collecting = false;
                return sweptCnt;
            }
        };

        static GC gGC;

        //////////////////////////////////////////////////////////////////////////

        ObjInfo* registerObj(void* obj, int size, void(*destroy)(void*), char* objInfoMem) { return gGC.registerObj(obj, size, destroy, objInfoMem); }
        void incRef(ObjInfo* n) { if (!n) return; n->refcnt++; }
        ObjInfo* PointerBase::rebindObj(void* obj) { ObjInfo* r = gGC.findOwnerObjInfo(obj); gGC.registerPointer(this, r); return r; }
        void PointerBase::setObjInfo(ObjInfo* n) { gGC.registerPointer(this, n);  }

        void PointerBase::unsetObjInfo(ObjInfo* n)
        {
            gGC.unregisterPointer(this, n);
        }

        void decRef(ObjInfo* n)
        {
            if (!n) return;
            if (gGC.collecting) return;
            if (--n->refcnt > 0) return;
            // a new ref in destructor?
            if (!n->destructing) {
                if (gGC.grayObjs.find(n) != gGC.grayObjs.end()) {
                    return; // in marking, let the gc handle it.
                }
                auto it = gGC.objInfoSet.find(n);
                n->destructing = true;
                delete n; // may create new ref to itself in the destructor                
                gGC.objInfoSet.erase(it);
            }
        }        
    }

    int GcCollect(int step) { return details::gGC.GcCollect(step); }
}


