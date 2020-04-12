# TGC

## A Tiny, precise, incremental, mark & sweep, Garbage Collector for C++.

参考请注明出处，谢谢。

### Motivation
- scenarios that shared_ptr can't solve, e.g. object dependencies are dynamically constructed with no chance to recognize the usage of shared & weak pointers.
- try to make things simpler compared to shared_ptr, e.g. network programs using callbacks for async io opeartions heavily.
- a very good experiments to design a gc dedicated to the C++ language and see how the language features can help.    

### Hightlights
- Non-instrusive
    - use like shared_ptr.
    - do not need to replace the global new.
    - do not need to inherit from a common base.    
    - can even work with shared_ptr.   

- Incremental marking and sweeping
    - won't stop the world.
    - can specify number of steps used for each collecting.
    - can manually delete the obejct to control the destruction order.

- Super lightweight
    - auto discovery memory relations at runtime *without any extra code*.
    - only one header & cpp file, easier to integrate.
    - no extra threads to collect garbages.
    
- Support most of containers of STL.        
- Cross platform, No other dependencies, only dependent on STL.    
- Support multi-threads.

- Customization
    - can work with your own memory allocator or pool.
    - provide hooks to redirect memory allocation.    
    - can be extended to use your custom containers.
    
- Precise.
    - ensure no memory leaks as long as objects are correctly tracked.

### Internals
- use triple color, mark & sweep algorithgm.
- pointers are constructed as roots by default, unless detected as parentless.
- construct & copy & modify gc pointers are slower than shared_ptr, much slower than Boehm gc, so use reference to gc pointers as function parameters as much as possible.
    - since c++ donot support ref-quanlified constructors, initialize gc pointer need to construct a temperary pointer bringing in some valueless overhead.
    - modifying a gc pointer will trigger a gc color adjustment.
- each allocation has a small extra space overhead (size of two pointers) for memory tracking.
- marking & swapping are much faster than Boehm gc, due to the deterministic pointer management.
- can not use gc pointers as global variables.
- every class has a global object keeping the necessary meta informations used by gc, so programs using lambdas heavily may have noticeable memory overhead.
- to make objects in a tracking chain, use tgc wrappers of STL containers instead, otherwise memory leaks may occur.
- gc_vector stores pointers of elements making its storage not continuous as standard vector, this is necessary for the gc. Actually all wrapped containers of STL stores gc pointers as elements.
- manually call gc_delete to trigger the destrcution of the object, and left the gc to collect the memory automatically.
- double free is protected.

### Performance Advices
- performance is not the first goal of this library. Results from tests, a simple allocation of interger is about 10~20 slower than standard new, so donot use it in performance critical parts of the program.
- use reference to gc pointers as function parameters as much as possible. (see internals section)
- memories garanteed to have no pointers in it should use shared_ptr or raw pointers instead.
- single-thread version is faster than multi-threads version, define TGC_SINGLE_THREAD to enabled single-thread version.
- use gc_new_array to get a collectable continuous array, for better performance.

### Usage

Please see tests in test.cpp, and a small demo here: https://github.com/crazybie/AsioTest.git

### Refs

- https://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.
- Boehn GC: https://github.com/ivmai/bdwgc/

### License

The MIT License

```
Copyright (C) 2018 soniced@sina.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
