#include "gcptr.h"
#include <string>
#include <iostream>
#include <set>
#include <vector>
#include <windows.h>
#include <functional>

#include <vld.h>


using namespace slgc;
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

void collect() { gc_collect(2); }

struct b1
{
	b1(const string& s) : name(s)
	{
		cout << "Creating b1(" << name << ")." << endl;
	}
	virtual ~b1()
	{
		cout << "Destroying b1(" << name << ")." << endl;
	}

	string name;
};


struct b2
{
	b2(const string& s) : name(s)
	{
		cout << "Creating b2(" << name << ")." << endl;
	}
	virtual ~b2()
	{
		cout << "Destroying b2(" << name << ")." << endl;
	}

	string name;
};

struct d1 : public b1
{
	d1(const string& s) : b1(s)
	{
		cout << "Creating d1(" << name << ")." << endl;
	}
	virtual ~d1()
	{
		cout << "Destroying d1(" << name << ")." << endl;
	}
};

struct d2 : public b1, public b2
{
	d2(const string& s) : b1(s), b2(s)
	{
		cout << "Creating d2(" << b1::name << ")." << endl;
	}
	virtual ~d2()
	{
		cout << "Destroying d2(" << b1::name << ")." << endl;
	}
};



struct rc
{
    int  a = 11;
	rc()
	{
		collect();
	}
	~rc()
	{
        auto i = gc_from(this);
	}
};

void test()
{
    PROFILE_LOOP
    {
        // Test "recursive" construction (insure that calls to collect during construction
        // do not cause premature collection of the node).
        gc<rc> prc(make_gc<rc>());

        // The following lines may be uncommented to illustrate one behavior of the
        // bug in VC++ in handling an assignment to a "real pointer", which is clearly
        // an illegal operation.  In order for this to accurately illustrate the
        // bug you must leave the assignment operator commented out in the gc_ptr
        // class definition.  With out a defined assignment operator the following
        // code results in an INTERNAL COMPILER ERROR.
        //	gc_ptr<circ> pfail;
        //	pfail = gcnew circ("fail");

        gc<string> p1;
        {
            gc<d1> p2(make_gc<d1>("first"));
            gc<b1> p3(p2);
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
            gc<b1> p4(make_gc<d2>("second"));
            gc<b2> pz(dynamic_cast<b2*>(&*p4));
            if ((void*)&*p4 == (void*)&*pz)
                throw std::runtime_error("unexpected");


            p3 = p2;

            collect();
        }
    }
	collect();
}

struct circ
{
    circ(const string& s) : name(s)
    {
        cout << "Creating circ(" << name << ")." << endl;
    }
    ~circ()
    {
        cout << "Destroying circ(" << name << ")." << endl;
    }

    gc<circ> ptr;

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

            collect();
        }
    }
    collect();
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
        gc<b1> p2 = p;
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
        gc<b1> p(make_gc<b1>("a"));
        gc<b1> emptry;
    }    
}

void testInsert()
{
#ifdef PROFILE
    std::vector<gc<b1>> objs;
    objs.reserve(2000);
    for (int k = 0; k < 2000; k++) {
        gc<b1> p(make_gc<b1>("a"));
        objs.push_back(p);
    }
    PROFILE_LOOP
    {
        gc<b1> p = make_gc<b1>("a");
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
    gc_vector<rc> a;
    gc_map<int, rc> b;   
    gc_map<int, rc> c;
        
    void f()
    {
        a = make_gc_vec<rc>();
        a->push_back(make_gc<rc>());
        b = make_gc_map<int, rc>();
        (*b)[0] = make_gc<rc>();
        (*b)[1] = make_gc<rc>();

        b->find(1);
        bar(b);
    }
    void bar(gc_map<int, rc> cc)
    {
        cc->insert(std::make_pair(1, make_gc<rc>()));
    }
};

gc<ArrayTest> a;

void testArray()
{    
    a = make_gc<ArrayTest>();
    a->f();    
}


int main()
{    
    b1 b("test");    
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
}