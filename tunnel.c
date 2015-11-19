/**
 *  tunnel.c
 */

#include "icmp.h"
#include "tunnel.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/wait.h>

/**
 * Function to allocate a tunnel
 */
int tun_alloc(char *dev, int flags)
{
  struct ifreq ifr;
  int tun_fd, err;
  char *clonedev = "/dev/net/tun";
  if (DEBUG){
    printf("[DEBUG] Allocatating tunnel\n");
  }
  tun_fd = open(clonedev, O_RDWR);

  if(tun_fd == -1) {
    perror("Unable to open clone device\n");
    exit(-1);
  }
  
  memset(&ifr, 0, sizeof(ifr));

  ifr.ifr_flags = flags;

  if (*dev) {
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
  }

  if ((err=ioctl(tun_fd, TUNSETIFF, (void *)&ifr)) < 0) {
    close(tun_fd);
    printf("Error: %d\n", err);
    exit(-1);
  }

  if (DEBUG){
    printf("[DEBUG] Allocatating tunnel2");
    printf("[DEBUG] Created tunnel %s\n", dev);
  }
  return tun_fd;
}

/**
 * Function to read from a tunnel
 */
int tun_read(int tun_fd, char *buffer, int length)
{
  int bytes_read;

  if (DEBUG){
    printf("[DEBUG] Reading from tunnel\n");
  }
  bytes_read = read(tun_fd, buffer, length);

  if (bytes_read == -1) {
    perror("Unable to read from tunnel\n");
    exit(-1);
  }
  else {
    return bytes_read;
  }
}

/**
 * Function to write to a tunnel
 */
int tun_write(int tun_fd, char *buffer, int length)
{
  int bytes_written;
  if (DEBUG){
    printf("[DEBUG] Writing to tunnel\n");
  }
  bytes_written = write(tun_fd, buffer, length);

  if (bytes_written == -1) {
    perror("Unable to write to tunnel\n");
    exit(-1);
  }
  else {
    return bytes_written;
  }
}

/**
 * Function to configure the network
 */
void configure_network(int server)
{
  int pid, status;
  char path[100];
  char *const args[] = {path, NULL};

  if (server) {
    strcpy(path, SERVER_SCRIPT);
  }
  else {
    strcpy(path, CLIENT_SCRIPT);
  }

  pid = fork();

  if (pid == -1) {
    perror("Unable to fork\n");
    exit(-1);
  }
  
  if (pid==0) {
    // Child process, run the script
    exit(execv(path, args));
  }
  else {
    // Parent process
    waitpid(pid, &status, 0);
    if (WEXITSTATUS(status) == 0) {
      // Script executed correctly
      if (DEBUG){
        printf("[DEBUG] Script ran successfully\n");
      }
    }
    else {
      // Some error
      if (DEBUG){
        printf("[DEBUG] Error in running script\n");
      }
    }
  }
}


/**
 * Function to run the tunnel
 */
void run_tunnel(char *dest, int server)
{
  struct icmp_packet packet;
  int tun_fd, sock_fd;

  fd_set fs;

  tun_fd = tun_alloc("tun0", IFF_TUN | IFF_NO_PI);

  if (DEBUG){
    printf("[DEBUG] Starting tunnel - Dest: %s, Server: %d\n", dest, server);
    printf("[DEBUG] Opening ICMP socket\n");
  }
  sock_fd = open_icmp_socket();

  if (server) {
    if (DEBUG){
      printf("[DEBUG] Binding ICMP socket\n");
    }
    bind_icmp_socket(sock_fd);
  }

  configure_network(server);

  while (1) {
    FD_ZERO(&fs);
    FD_SET(tun_fd, &fs);
    FD_SET(sock_fd, &fs);

    select(tun_fd>sock_fd?tun_fd+1:sock_fd+1, &fs, NULL, NULL, NULL);

    if (FD_ISSET(tun_fd, &fs)) {

      if (DEBUG){
        printf("[DEBUG] Data needs to be readed from tun device\n");
      }
      // Reading data from tun device and sending ICMP packet

      if (DEBUG){
        printf("[DEBUG] Preparing ICMP packet to be sent\n");
      }
      // Preparing ICMP packet to be sent
      memset(&packet, 0, sizeof(struct icmp_packet));
      if (DEBUG){
        printf("[DEBUG] Destination address: %s\n", dest);
      }
      strcpy(packet.src_addr, "0.0.0.0");
      strcpy(packet.dest_addr, dest);
      if(server) {
        set_reply_type(&packet);
      }
      else {
        set_echo_type(&packet);
      }
      packet.payload = malloc(MTU);
      packet.payload_size  = tun_read(tun_fd, packet.payload, MTU);
      if(packet.payload_size  == -1) {
        printf("Error while reading from tun device\n");
        exit(-1);
      }

      if (DEBUG){
        printf("[DEBUG] Sending ICMP packet with payload_size: %d, payload: %s\n", packet.payload_size, packet.payload);
      }
      // Sending ICMP packet
      send_icmp_packet(sock_fd, &packet);

      free(packet.payload);
    }

    if (FD_ISSET(sock_fd, &fs)) {
      if (DEBUG){
        printf("[DEBUG] Received ICMP packet\n");
      }
      // Reading data from remote socket and sending to tun device

      // Getting ICMP packet
      memset(&packet, 0, sizeof(struct icmp_packet));
      receive_icmp_packet(sock_fd, &packet);

      if (DEBUG){
        printf("[DEBUG] Read ICMP packet with src: %s, dest: %s, payload_size: %d, payload: %s\n", packet.src_addr, packet.dest_addr, packet.payload_size, packet.payload);
      }
      // Writing out to tun device
      tun_write(tun_fd, packet.payload, packet.payload_size);

      if (DEBUG){
        printf("[DEBUG] Src address being copied: %s\n", packet.src_addr);
      }
      strcpy(dest, packet.src_addr);
    }
  }

}
