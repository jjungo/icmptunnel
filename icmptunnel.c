/**
 * icmp_tunnel.c
 */

#include "tunnel.h"
#include "config.h"

#include <string.h>

int main(int argc, char *argv[])
{
  run_tunnel(argv[2], !strcmp(argv[1], "-s"));
  return 0;
}
