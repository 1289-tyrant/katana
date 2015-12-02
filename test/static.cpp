// std_tr1__type_traits__is_pod.cpp 

#include "Galois/Substrate/PtrLock.h"
#include "Galois/Substrate/SimpleLock.h"
#include "Galois/Substrate/StaticInstance.h"

#include <type_traits>
#include <iostream> 

using namespace Galois::Substrate;

int main() 
{ 
  std::cout << "is_pod PtrLock<int> == " << std::boolalpha 
	    << std::is_pod<PtrLock<int> >::value << std::endl; 

  std::cout << "is_pod SimpleLock == " << std::boolalpha 
	    << std::is_pod<SimpleLock >::value << std::endl; 
  std::cout << "is_pod DummyLock == " << std::boolalpha 
	    << std::is_pod<DummyLock >::value << std::endl; 

  std::cout << "is_pod StaticInstance<int> == " << std::boolalpha 
	    << std::is_pod<StaticInstance<int> >::value << std::endl; 
  std::cout << "is_pod StaticInstance<std::iostream> == " << std::boolalpha 
	    << std::is_pod<StaticInstance<std::iostream> >::value << std::endl; 

  std::cout << "is_pod volatile int == " << std::boolalpha 
   	    << std::is_pod<volatile int>::value << std::endl; 
  std::cout << "is_pod int == " << std::boolalpha 
   	    << std::is_pod<int>::value << std::endl; 

  // std::cout << "is_pod<throws> == " << std::boolalpha 
  // 	    << std::is_pod<throws>::value << std::endl; 
  
  return (0); 
} 
