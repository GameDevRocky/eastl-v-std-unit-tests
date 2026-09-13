#include <cstddef>
#include <new>


void* operator new[](std::size_t size, const char*, int, unsigned int, const char*, int){
    return ::operator new[](size);
}

void* operator new[](std::size_t size, std::size_t, std::size_t, const char*, int, unsigned int, const char*, int){
    return ::operator new[](size);
}