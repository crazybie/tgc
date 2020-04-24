# TGC

## A Tiny, precise, incremental, mark & sweep, Garbage Collector for C++.

参考请注明出处，谢谢。

### Motivation
- Scenarios that shared_ptr can't solve, e.g. object dependencies are dynamically constructed with no chance to recognize the usage of shared & weak pointers.
- Try to make things simpler compared to shared_ptr and Oilpan, e.g. networking programs using callbacks for async io operations heavily.     
- A very good experiment to design a GC dedicated to the C++ language and see how the language features can help.    

### Highlights
- Non-intrusive
    - Use like shared_ptr.
    - Do not need to replace the global new operator.
    - Do not need to inherit from a common base.    
    - It can even work with shared_ptr.   
- Incremental marking and sweeping
    - Won't stop the world.
    - Can specify the number of steps used for each collecting.
    - Can manually delete the object to control the destruction order.
- Super lightweight    
    - Only one header & CPP file, easier to integrate.
    - No extra threads to collect garbage.    
- Support most of the containers of STL.        
- Cross-platform, no other dependencies, only dependent on STL.    
- Support multi-threads.
- Customization
    - It can work with other memory allocators and pool.
    - Provide hooks to redirect memory allocation.    
    - It can be extended to use your custom containers.    
- Precise.
    - Ensure no memory leaks as long as objects are correctly tracked.

### Comparison
-  Pros over shared_ptr:
    - no need for weak_ptr to break the circular references.
    - no shared_from_this is needed for GC pointer.
    - gc_from(this) works in the constructor where shared_ptr is not.
    - construct GC pointer from the raw pointer is safe to call many times where shared_ptr is not because it will reset the ref counter    
    - can't predicate the number of objects destructed in complex scenarios when clear a shared_ptr, but not for GC pointers as you can control the collection steps to run.
    
- Pros over Oilpan GC:
    - Easier to use, only one kind of GC pointer to be used.
    - More general and usable for wider scenarios.

### Internals
- This collector uses the triple color, mark & sweep algorithm internally.    
- Pointers are constructed as roots by default unless detected as children.
- Every class has a global meta-object keeping the necessary meta-information (e.g. class size and offsets of member pointers) used by GC, so programs using lambdas heavily may have some memory overhead. Besides, as the initialization order of global objects is not well defined, you should not use GC pointers as global variables too. Don't worry, inside the system, there is an assert checking this rule.
- Construct & copy & modify GC pointers are slower than shared_ptr, much slower than raw pointers(Boehm GC).
    - Every GC pointer must register itself to the collector and unregister on destruction as well.
    - Since C++ does not support ref-qualified constructors, the gc_new returns a temporary GC pointer bringing in some meaningless overhead. Instead, using gc_new_meta can bypass the construction of the temporary making things a bit faster.
    - Member pointers offsets of one class are calculated and recorded at the first time of creating the instance of that class.
    - Modifying a GC pointer will trigger a GC color adjustment which may not be cheap as well.
- Each allocation has a few extra space overhead (size of two pointers at most), which is used for memory tracing.
- Marking & swapping should be much faster than Boehm GC, due to the deterministic pointer management, no scanning inside the memories at all, just iterating pointers registered in the GC.
- To make objects in proper tracing chain, you must use GC wrappers of STL containers instead, otherwise, memory leaks may occur.
- gc_vector stores pointers of elements making its storage not continuous as a standard vector, this is necessary for the GC. All wrapped containers of STL stores GC pointers as elements.
- You can manually call gc_delete to trigger the destructor of an object and let the GC claim the memory automatically. Besides, double free is also safe.
- For the multi-threaded version, the collection function should be invoked to run in the main thread therefore the destructors can be triggered in the main thread as well.


### Performance Advice
- Performance is not the first goal of this library. 
    - Results from tests, a simple allocation of an integer is about 8~10 slower than standard new(see test), so benchmark your program if GC pointers are heavily used in the performance-critical parts(e.g. VM of another language).
    - Use the references to GC pointers as much as possible. (e.g. function parameters, see internals section)
    - Use gc_new_array to get a collectible continuous array for better performance in some special cases (see internals section).
    - Continuous efforts will be put to optimize the performance at a later time.
    - Languages with GC built-in prefer to create a huge number of heap objects which will give large pressure to the GC, some languages even use pointer escaping analyzing algorithm to increase the recycling efficiency, but it's not a serious problem to C++ as it has RAII and does not use heap objects everywhere. So the throughput of this triple-color GC should be efficient enough. 
- For real-time applications:
    - Static strategy: just call gc_collect with a suitable step count regularly in each frame of the event loop.
    - Dynamic strategy: you can specify a small step count(default is 255) for one collecting call and time it to see if still has time left to collect again, otherwise do collecting at the next time.    
- As memories are managed by GC, you can not release them immediately. If you want to get rid of the risk of OOM on some resource-limited system, memories guaranteed to have no pointers in it can be managed by shared_ptrs or raw pointers.
- The single-threaded version(by default) should be much faster than the multi-threaded version because no locks are required at all. Please define TGC_MULTI_THREADED to enable the multi-threaded version.


### Usage

Please see the tests in 'test.cpp'
Another small demo here: https://github.com/crazybie/AsioTest.git

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
