// Test executable only. Separate TU keeps the allocator replacement opaque to included production code.
#include <cstdlib>
#include <new>
bool canaryNativeAllocationFault();
void *operator new(std::size_t n){if(canaryNativeAllocationFault())throw std::bad_alloc();if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete[](void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}
void operator delete[](void *p,std::size_t)noexcept{std::free(p);}
