# TGC

## A Tiny, precise, incremental, mark & sweep, Garbage Collector for C++.

参考请注明出处，谢谢。

### Motivation
- Scenarios that shared_ptr can't solve, e.g. object dependencies are dynamically constructed with no chance to recognize the usage of shared & weak pointers.
- Try to make things simpler compared to shared_ptr and Oilpan, e.g. network programs using callbacks for async io opeartions heavily.
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
    - Can manually delete the obejct to control the destruction order.

- Super lightweight
    - Auto discovery memory relations at runtime *without any extra code*.
    - Only one header & cpp file, easier to integrate.
    - No extra threads to collect garbages.
    
- Support most of containers of STL.        
- Cross platform, no other dependencies, only dependent on STL.    
- Support multi-threads.

- Customization
    - Can work with your own memory allocator or pool.
    - Provide hooks to redirect memory allocation.    
    - Can be extended to use your custom containers.
    
- Precise.
    - Ensure no memory leaks as long as objects are correctly tracked.

### Internals
- Use triple color, mark & sweep algorithgm.
- Pointers are constructed as roots by default, unless detected as children.
- Construct & copy & modify gc pointers are slower than shared_ptr, much slower than raw pointers(Boehm gc).
    - Since c++ donot support ref-quanlified constructors, create object to initialize gc pointer need to construct temperary pointer bringing in some valueless overhead.
    - Modifying a gc pointer will trigger a gc color adjustment which is not cheap as well.
- Each allocation has a few extra space overhead (size of two pointers), which is used for memory tracking.
- Marking & swapping are much faster than Boehm gc, due to the deterministic pointer management.
- Can not use gc pointers as global variables.
- Every class has a global object keeping the necessary meta informations used by gc, so programs using lambdas heavily may have noticeable memory overhead.
- To make objects in a tracking chain, use tgc wrappers of STL containers instead, otherwise memory leaks may occur.
- gc_vector stores pointers of elements making its storage not continuous as standard vector, this is necessary for the gc. Actually all wrapped containers of STL stores gc pointers as elements.
- Can manually call gc_delete to trigger the destrcution of the object, and leave the gc to collect the memory automatically.
- Double free is safe.

### Performance Advices
- Performance is not the first goal of this library. Results from tests, a simple allocation of interger is about ~10 slower than standard new, so donot use it in performance critical parts of the program, e.g. VMs of other languages.
- Use reference to gc pointers as much as possible. (e.g. function parameters, see internals section)
- Memories garanteed to have no pointers in it can use shared_ptr or raw pointers to make recliaming faster.
- Single-threaded version (by default) is faster than multi-threads version because no locks are required. Define TGC_MULTI_THREADED to enabled multi-threaded version.
- Use gc_new_array to get a collectable continuous array for better performance.

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
