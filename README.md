# TGC

## A Tiny, precise, incremental, mark & sweep, Garbage Collector for C++.

参考请注明出处，谢谢。

### Motivation
- Scenarios that shared_ptr can't solve, e.g. object dependencies are dynamically constructed with no chance to recognize the usage of shared & weak pointers.
- Try to make things simpler compared to shared_ptr and Oilpan, e.g. networking programs using callbacks for async io opeartions heavily.     
- A very good experiment to design a gc dedicated to the C++ language and see how the language features can help.    

### Hightlights
- Non-instrusive
    - Use like shared_ptr.
    - Do not need to replace the global new.
    - Do not need to inherit from a common base.    
    - Can even work with shared_ptr.   
- Incremental marking and sweeping
    - Won't stop the world.
    - Can specify number of steps used for each collecting.
    - Can manually delete the object to control the destruction order.
- Super lightweight    
    - Only one header & cpp file, easier to integrate.
    - No extra threads to collect garbages.    
- Support most of containers of STL.        
- Cross platform, no other dependencies, only dependent on STL.    
- Support multi-threads.
- Customization
    - Can work with your own memory allocator and pool.
    - Provide hooks to redirect memory allocation.    
    - Can be extended to use your custom containers.    
- Precise.
    - Ensure no memory leaks as long as objects are correctly tracked.

### Comparation
-  Pros over shared_ptr:
    - no need of weak_ptr to break the circular references.
    - no shared_from_this is needed for gc pointer.
    - gc_from(this) works in the constructor where shared_ptr is not.
    - construct gc pointer from raw pointer is safe to call any times where shared_ptr is not because it will reset the ref counter    
    - cann't predicate the number of objects destructed in complex scenarios when clear a shared_ptr, but not for gc pointers as you can control the collection steps to run.

### Internals
- Use triple color, mark & sweep algorithgm.
- Pointers are constructed as roots by default, unless detected as children.
- Construct & copy & modify gc pointers are slower than shared_ptr, much slower than raw pointers(Boehm gc).
    - Since c++ donot support ref-quanlified constructors, create object to initialize gc pointer need to construct temporary pointer bringing in some meaningless overhead. Use gc_new_meta to initialize gc pointers can bypass construction of this temporary pointer which make things a bit faster.
    - Modifying a gc pointer will trigger a gc color adjustment which may not be cheap as well.
- Each allocation has a few extra space overhead (size of two pointers), which is used for memory tracking.
- Marking & swapping should be much faster than Boehm gc, due to the deterministic pointer management.
- Every class has a global info object keeping the necessary meta informations used by gc, so programs using lambdas heavily may have noticeable memory overhead. Besides, you can not use gc pointers as global variables, as the class info objects are global objects, all global objects are constructed in undefined order. Don't worry, inside the system there is an assert checking this rule.
- To make objects in a tracking chain, use tgc wrappers of STL containers instead, otherwise memory leaks may occur.
- gc_vector stores pointers of elements making its storage not continuous as standard vector, this is necessary for the gc. Actually all wrapped containers of STL stores gc pointers as elements.
- Can manually call gc_delete to trigger the destrcution of the object, and leave the gc to collect the memory automatically.
- Double free is safe.
- For the multi-threaded version, the collection function should be invoked to run in the main thread therefore the destructors can be triggered in the main thread as well.


### Performance Advices
- Performance is not the first goal of this library. Results from tests, a simple allocation of interger is about 8~10 slower than standard new, so benchmark your program if gc pointers are used in the performance critical parts(e.g. VM of another language).
- Use reference to gc pointers as much as possible. (e.g. function parameters, see internals section)
- Memories garanteed to have no pointers in it can use shared_ptr or raw pointers to make recliaming faster.
- Single-threaded version (by default) is faster than multi-threads version because no locks are required. Define TGC_MULTI_THREADED to enable the multi-threaded version.
- Use gc_new_array to get a collectable continuous array for better performance.
- Tranditional dynamic languages will create huge number of heap objects which will give large pressure to the gc, but this won't happen in C++ as it has RAII and do not use heap objects everywhere. So the throughput of this triple-color gc is efficient enough. 
- For realtime applications:
    - Static strategy: just call gc_collect with a suitable step count regulaly in each frame of the event loop.
    - Dynamic strategy: you can specify a small step count(the default 255) for one collecting call and time it to see if still has  time left to collect again, otherwise do collecting at the next time.

### Usage

Please see tests in test.cpp, and a small demo here: https://github.com/crazybie/AsioTest.git

### Refs

- https://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.
- Boehn GC: https://github.com/ivmai/bdwgc/
- Oilpan GC: https://chromium.googlesource.com/chromium/src/+/master/third_party/blink/renderer/platform/heap/BlinkGCDesign.md#Threading-model

### License

The MIT License

```
Copyright (C) 2018 soniced@sina.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
