#include <iostream>
#include "platform.hpp"

int main() {
  std::cout << "hello, world 👋 \n";

  
  if(!platform_init())
    return 1;
  print_libs();
  
  while(!platform_should_quit()){

  }

  platform_shutdown();

  return 0;
}
