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

        // TODO: reduce this size by making the destroy & size & member pointers as class attributes.
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
        ObjInfo* kInvalidObjInfo = (ObjInfo*)-1;

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
            ObjInfo* newObjInfo(void* o, int sz, Dctor dctor, char* mem)
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
                if (isRoot(i)) {
                    if (i->objInfo->color == MarkColor::White) {
                        i->objInfo->color = MarkColor::Gray;
                        grayObjs.push_back(i->objInfo);
                    }
                }
            }
            bool isRoot(PointerBase* p)
            {
                // 不能放到指针注册的地方：
                // 因为owner的可能还没有调用newObjInfo注册到gc中
                if (p->owner == kInvalidObjInfo) {
                    p->owner = findOwnerObjInfo(p);
                    if (p->owner) {
                        p->owner->memberPointers.push_back(p);
                    }
                }
                return !p->owner;
            }
            int collect(int stepCnt)
            {
                int sweptCnt = 0;
                switch (state) {
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

        GC* getGC() { static GC i; return &i; }
        ObjInfo* newObjInfo(void* o, int sz, Dctor dctor, char* mem) { return getGC()->newObjInfo(o, sz, dctor, mem); }

        PointerBase::PointerBase() : objInfo(0), owner(kInvalidObjInfo) { getGC()->pointers.insert(this); }
        PointerBase::PointerBase(void* obj) : owner(kInvalidObjInfo), objInfo(getGC()->findOwnerObjInfo(obj)) { getGC()->pointers.insert(this); }
        PointerBase::~PointerBase() { getGC()->pointers.erase(this); }
        void PointerBase::onPointerUpdate() { getGC()->onPointerUpdate(this); }
    }

    int GcCollect(int step) { return details::getGC()->collect(step); }
}
