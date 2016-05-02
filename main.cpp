#include "gcptr.h"
#include <string>
#include <iostream>
#include <set>
#include <vector>
#include <windows.h>

#include <vld.h>


using namespace gc;
using std::string;
using std::cout;
using std::endl;

#ifndef _DEBUG
#define PROFILE
#endif

#ifdef PROFILE
#define PROFILE_LOOP for(int i=0;i<5000*(rand()%5+5);i++)
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
    gc_collect(2);    
#ifndef PROFILE
    printf("---- GC at line %d ----\n", line);    
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
        auto i = gc_from(this);
        objcount--;
	}
};

void test()
{
    PROFILE_LOOP
    {
        // Test "recursive" construction (insure that calls to collect during construction
        // do not cause premature collection of the node).
        ptr<rc> prc(make_gc<rc>());

        // The following lines may be uncommented to illustrate one behavior of the
        // bug in VC++ in handling an assignment to a "real pointer", which is clearly
        // an illegal operation.  In order for this to accurately illustrate the
        // bug you must leave the assignment operator commented out in the gc_ptr
        // class definition.  With out a defined assignment operator the following
        // code results in an INTERNAL COMPILER ERROR.
        //	gc_ptr<circ> pfail;
        //	pfail = gcnew circ("fail");

        ptr<string> p1;
        {
            ptr<d1> p2(make_gc<d1>("first"));
            ptr<b1> p3(p2);
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
            ptr<b1> p4(make_gc<d2>("second"));
            ptr<b2> pz(dynamic_cast<b2*>(&*p4));
            if (static_cast<void*>(p4.operator ->()) == static_cast<void*>(pz.operator ->()))
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

    ptr<circ> ptr;

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
        ptr<b1> p2 = p;
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
        ptr<b1> p(make_gc<b1>("a"));
        ptr<b1> emptry;
    }    
}

void testInsert()
{
#ifdef PROFILE
    std::vector<ptr<b1>> objs;
    objs.reserve(2000);
    for (int k = 0; k < 2000; k++) {
        ptr<b1> p(make_gc<b1>("a"));
        objs.push_back(p);
    }
    PROFILE_LOOP
    {
        ptr<b1> p = make_gc<b1>("a");
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
//gc_ptr<g> global(make_gc<g>());


struct ArrayTest
{
    vector_ptr<ptr<rc>> a;
    map_ptr<int, ptr<rc>> b;   
    map_ptr<int, rc> c;

    //vector_ptr<rc> b;
    void f()
    {
        a = make_gc_vec<rc>();
        a->push_back(make_gc<rc>());
        b = make_gc_map<int, rc>();
        (*b)[0] = make_gc<rc>();
        b()[1] = make_gc<rc>();

        b->find(1);

        c = make_gc<map<int, rc>>();        

        bar(b);
    }
    void bar(map_ptr<int, ptr<rc>> cc)
    {
        cc->insert(std::make_pair(1, make_gc<rc>()));
    }
};

ptr<ArrayTest> a;

void testArray()
{    
    a = make_gc<ArrayTest>();
    a->f();
}


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
        testArray();
    }
#undef cout    
    gc_collect(10);
    cout << (!objcount ? "ok" : "failed") << endl;
    return objcount;
}