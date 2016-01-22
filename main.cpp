#include "gcptr.h"
#include <string>
#include <iostream>
#include <set>
#include <vector>
#include <windows.h>


using namespace std;
using namespace gc;

#ifndef _DEBUG
#define PROFILE
#endif

#ifdef PROFILE
#define PROFILE_LOOP for(int i=0;i<20000*(rand()%5+5);i++)
#define cout comment(/)
#define comment(a) /a
#else
#define PROFILE_LOOP for(int i=0;i<2;i++)
#endif

static int objcount = 0;
static int gcobjcount = 0;

#define GC() _GC(__LINE__)


void _GC(int line)
{
    auto freeCnt = GcCollect(112);
    gcobjcount += freeCnt;
#ifndef PROFILE
    printf("---- GC at line %d ----\n", line);
    printf("GC: swept %d objects\n", freeCnt);
    printf("Obj left:%d \n", objcount);
#endif
}



struct b1
{
	b1(const string& s) : name(s)
	{
		cout << "Creating b1(" << name << ")." << endl;
		objcount++;
	}
	virtual ~b1()
	{
		cout << "Destroying b1(" << name << ")." << endl;
		objcount--;
	}

	string name;
};

struct b2
{
	b2(const string& s) : name(s)
	{
		cout << "Creating b2(" << name << ")." << endl;
		objcount++;
	}
	virtual ~b2()
	{
		cout << "Destroying b2(" << name << ")." << endl;
		objcount--;
	}

	string name;
};

struct d1 : public b1
{
	d1(const string& s) : b1(s)
	{
		cout << "Creating d1(" << name << ")." << endl;
		objcount++;
	}
	virtual ~d1()
	{
		cout << "Destroying d1(" << name << ")." << endl;
		objcount--;
	}
};

struct d2 : public b1, public b2
{
	d2(const string& s) : b1(s), b2(s)
	{
		cout << "Creating d2(" << b1::name << ")." << endl;
		objcount++;
	}
	virtual ~d2()
	{
		cout << "Destroying d2(" << b1::name << ")." << endl;
		objcount--;
	}
};



struct rc
{
    int  a = 11;
	rc()
	{
		objcount++;        
		GC();
	}
	~rc()
	{
        auto i = gc_ptr_from_this(this);
        objcount--;
	}
};

void test()
{
    PROFILE_LOOP
    {
        // Test "recursive" construction (insure that calls to collect during construction
        // do not cause premature collection of the node).
        gc_ptr<rc> prc(make_gc<rc>());

        // The following lines may be uncommented to illustrate one behavior of the
        // bug in VC++ in handling an assignment to a "real pointer", which is clearly
        // an illegal operation.  In order for this to accurately illustrate the
        // bug you must leave the assignment operator commented out in the gc_ptr
        // class definition.  With out a defined assignment operator the following
        // code results in an INTERNAL COMPILER ERROR.
        //	gc_ptr<circ> pfail;
        //	pfail = gcnew circ("fail");

        gc_ptr<string> p1;
        {
            gc_ptr<d1> p2(make_gc<d1>("first"));
            gc_ptr<b1> p3(p2);
            p1.reset(&p3->name);
            // The following line may be uncommented to illustrate another behavior of the
            // bug in VC++ in handling an assignment to a "real pointer", which is clearly
            // an illegal operation.  In order for this to accurately illustrate the
            // bug you must leave the assignment operator commented out in the gc_ptr
            // class definition.  With out a defined assignment operator the following
            // code results in no errors or warnings during compilation, but no machine
            // code is generated for this instruction, as can be seen by stepping through
            // the code in Debug.  To really see the danger here comment out the previous
            // line of code as well and notice the behavior change at run time.
            //		p1 = &p3->name;
            gc_ptr<b1> p4(make_gc<d2>("second"));
            gc_ptr<b2> pz(dynamic_cast<b2*>(p4.get()));
            if (static_cast<void*>(p4.get()) == static_cast<void*>(pz.get()))
                throw std::runtime_error("unexpected");


            p3 = p2;

            GC();

        }
    }
	GC();
}

struct circ
{
    circ(const string& s) : name(s)
    {
        cout << "Creating circ(" << name << ")." << endl;
        objcount++;
    }
    ~circ()
    {
        cout << "Destroying circ(" << name << ")." << endl;
        objcount--;
    }

    gc_ptr<circ> ptr;

    string name;
};
void testCirc()
{   
    PROFILE_LOOP        
    {
        auto p5 = make_gc<circ>("root");
        {
            auto p6 = make_gc<circ>("first");
            auto p7 = make_gc<circ>("second");

            p5->ptr = p6;

            p6->ptr = p7;
            p7->ptr = p6;

            GC();
        }
    }
    GC();
}

void testMoveCtor()
{
    PROFILE_LOOP
    {
        auto f = []
        {
            auto t = make_gc<b1>("");
            return std::move(t);
        };

        auto p = f();
        gc_ptr<b1> p2 = p;
        p2 = f();
    }
}

void testMakeGcObj()
{
    PROFILE_LOOP
    {
        auto a = make_gc<b1>("test");
    }
}

void testEmpty()
{
    PROFILE_LOOP
    {
        gc_ptr<b1> p(make_gc<b1>("a"));
        gc_ptr<b1> emptry;
    }    
}

void testInsert()
{
#ifdef PROFILE
    std::vector<gc_ptr<b1>> objs;
    objs.reserve(2000);
    for (int k = 0; k < 2000; k++) {
        gc_ptr<b1> p(make_gc<b1>("a"));
        objs.push_back(p);
    }
    PROFILE_LOOP
    {
        gc_ptr<b1> p = make_gc<b1>("a");
        objs[rand() % objs.size()] = p;
    }
#endif
}

// Test global instances.
struct g
{
	g()
	{
	}
	~g()
	{
		// To verify that global objects are deleted set a breakpoint here.
		// Note:  We do this because cout doesn't work properly here.
		int i = 0;
	}
};
gc_ptr<g> global(make_gc<g>());

int main()
{    
#ifdef PROFILE
    for (int i = 0; i < 10; i++) 
#endif
    {
        testInsert();
        testEmpty();
        test();
        testMoveCtor();
        testCirc();
    }
#undef cout
    GcCollect(99999999);
	cout << ((objcount == 0) ? "success!" : "fail!") << endl;
    cout << "gc obj cnt" << gcobjcount << endl;
    return 0;
}