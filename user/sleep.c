#include "kernel/types.h" // user.h 自定义了数据类型
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "usage: sleep [seconds]\n");
    exit(1);
  }

  sleep(atoi(argv[1]));
  exit(0);

}
