#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h> 
#include <unistd.h>
#include <sys/types.h>

#define read_size 1000 
#define n_items 10000 

int main()
{
  int i,read_count, fd;
  char * data = (char *) malloc (read_size);
  fd = open("/adsl-pool2/myfs/a", O_RDWR); 
  directio(fd, DIRECTIO_ON); 
  if (fd == -1)
    printf ("Open failed with error no %d", errno); 
  read_count = read(fd, data, read_size); 
  if (read_count > 0)
  { 
     printf ("Successfuly read %d count(s).\n", read_count);
     data[read_count] = 0; 
     printf ("File contents are : - \n");
     printf("%s", data);
  }
  else
    printf ("Read failed or something bad happened. btw, errno is %d.\n", errno);
  close (fd);
}
