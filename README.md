# TGC

## A Tiny incremental mark & sweep Garbage Collector.

Inspired by http://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.

### Hightlights
- no instrisive
- super lightweight
- smart point alike
- no other dependencies
- minimal overhead


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
