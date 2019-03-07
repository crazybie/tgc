# TGC

## A Tiny incremental mark & sweep Garbage Collector.

Inspired by http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.

Warning: This project is only used in small products without heavy tests, take your own risk. 

参考请注明出处，谢谢。

### Hightlights
- Auto discovery memory relations *without any extra code*
    - this makes TGC much convenient to work with.
- Super lightweight
    - only one header & cpp file, easier to integrate.
    - no extra threads to collect garbages.
- Non-instrusive
    - do not need to replace the globoal new.
    - can work with raw pointers perfectly.
    - will not affect third party libraries.
    - can use in a dependent scope.    
- Incremental marking and sweeping
    - won't stop the world.
    - can control the steps of each collecting call.
- No other dependencies
    - easier to integrate.
- Can work with your own memory manage system
    - TGC only takes over the collecting task.
    - can even works with std::shared_ptr, memory pool, etc. (TBD)
    

### TODO
- exception safe.
- thread safe.
- performance optimization

``` c++

int main() { 
    {
        gc<int> v = gc_new<int>(1);
    }
    gc_collect();    
    return 0;
}

```
See main.cpp for more tests.

### License

The MIT License

```
Copyright (C) 2018 soniced@sina.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
