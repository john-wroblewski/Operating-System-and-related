#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int fd;
  struct stat st;

  if(argc != 2)
  {
    printf(2, "usage: stat [input file]\n");
    exit();
  }

  if((fd = open(argv[1], 0)) < 0)
  {
    printf(2, "stat: cannot open %s\n", argv[1]);
    exit();
  }

  if(fstat(fd, &st) < 0)
  {
    printf(2, "stat: cannot fstat %s\n", argv[1]);
    close(fd);
    exit();
  }
  close(fd);
  printf(1, "global checksum: %x\n", st.checksum);

  exit();
}
