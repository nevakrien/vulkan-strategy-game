#include <iostream>
#include "platform.hpp"

int main() {
  std::cout << "hello, world 👋 \n";

  print_libs();
  
  if(!platform_init())
    return 1;
  
  std::cout << "platform inilized \n";


  while(!platform_should_quit()){

  }

  std::cout << "cleanup \n";

  platform_shutdown();

  return 0;
}
