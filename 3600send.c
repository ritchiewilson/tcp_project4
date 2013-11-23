/*
 * CS3600, Spring 2013
 * Project 4 Starter Code
 * (c) 2013 Alan Mislove
 *
 */

#include <math.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "3600sendrecv.h"

static int DATA_SIZE = 1460;

unsigned int sequence = 0;
#define WINDOW_SIZE 20
header window_headers[WINDOW_SIZE];
void *window[WINDOW_SIZE];
unsigned int data_byte_at_start_of_window = 0;
int last_used_window_frame = 0;

void usage() {
  printf("Usage: 3600send host:port\n");
  exit(1);
}

/**
 * Reads the next block of data from stdin
 */
int get_next_data(char *data, int size) {
  return read(0, data, size);
}

/**
 * Builds and returns the next packet, or NULL
 * if no more data is available.
 */
void *get_next_packet(int *len) {
  char *data = malloc(DATA_SIZE);
  int data_len = get_next_data(data, DATA_SIZE);

  if (data_len == 0) {
    free(data);
    return NULL;
  }

  header *myheader = make_header(sequence, data_len, 0, 0);
  void *packet = malloc(sizeof(header) + data_len);
  memcpy(packet, myheader, sizeof(header));
  memcpy(((char *) packet) +sizeof(header), data, data_len);
  sequence += data_len;

  // save this data in case we need to retransmit
  int i = 0;
  while (window[i] == NULL && i < WINDOW_SIZE)
    i++;
  window_headers[i] = *myheader;
  window[i] = data;

  *len = sizeof(header) + data_len;

  return packet;
}

int send_next_packet(int sock, struct sockaddr_in out) {
  int packet_len = 0;
  void *packet = get_next_packet(&packet_len);

  if (packet == NULL) 
    return 0;

  mylog("[send data] %d (%d)\n", sequence, packet_len - sizeof(header));

  if (sendto(sock, packet, packet_len, 0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
    perror("sendto");
    exit(1);
  }

  return 1;
}

void send_final_packet(int sock, struct sockaddr_in out) {
  header *myheader = make_header(sequence+1, 0, 1, 0);
  mylog("[send eof]\n");

  if (sendto(sock, myheader, sizeof(header), 0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
    perror("sendto");
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  /**
   * I've included some basic code for opening a UDP socket in C, 
   * binding to a empheral port, printing out the port number.
   * 
   * I've also included a very simple transport protocol that simply
   * acknowledges every received packet.  It has a header, but does
   * not do any error handling (i.e., it does not have sequence 
   * numbers, timeouts, retries, a "window"). You will
   * need to fill in many of the details, but this should be enough to
   * get you started.
   */

  // extract the host IP and port
  if ((argc != 2) || (strstr(argv[1], ":") == NULL)) {
    usage();
  }

  char *tmp = (char *) malloc(strlen(argv[1])+1);
  strcpy(tmp, argv[1]);

  char *ip_s = strtok(tmp, ":");
  char *port_s = strtok(NULL, ":");
 
  // first, open a UDP socket  
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  // next, construct the local port
  struct sockaddr_in out;
  out.sin_family = AF_INET;
  out.sin_port = htons(atoi(port_s));
  out.sin_addr.s_addr = inet_addr(ip_s);

  // socket for received packets
  struct sockaddr_in in;
  socklen_t in_len = sizeof(in);

  // construct the socket set
  fd_set socks;

  // construct the timeout
  struct timeval t;
  t.tv_sec = 30;
  t.tv_usec = 0;


  while (send_next_packet(sock, out)) {
    int done = 0;

    while(last_used_window_frame < WINDOW_SIZE && 
          send_next_packet(sock, out)){
      last_used_window_frame++;
    }

    while (! done) {
      FD_ZERO(&socks);
      FD_SET(sock, &socks);

      // wait to receive, or for a timeout
      if (select(sock + 1, &socks, NULL, NULL, &t)) {
        unsigned char buf[10000];
        int buf_len = sizeof(buf);
        int received;
        if ((received = recvfrom(sock, &buf, buf_len, 0, (struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
          perror("recvfrom");
          exit(1);
        }

        header *myheader = get_header(buf);

        if ((myheader->magic == MAGIC) && (myheader->sequence >= data_byte_at_start_of_window) 
            && (myheader->ack == 1)) {
          mylog("[recv ack] %d\n", myheader->sequence);
          data_byte_at_start_of_window = myheader->sequence;
          // figure out how many saved packets have now been acked
          int acked = 0;
          while (window_headers[acked].sequence < myheader->sequence && acked < WINDOW_SIZE)
            acked++;
          // shift all the pointers round in the window
          for(int i = 0; i < WINDOW_SIZE; i++){
            if (i < acked){
              data_byte_at_start_of_window += window_headers[i].length;
              free(window[i]);
            }
            else if ( i < WINDOW_SIZE - acked){
              window_headers[i - acked] = window_headers[i];
              window[i - acked] = window[i];
            }
            else
              window[i] = NULL;
          }
          last_used_window_frame -= acked;
          done = 1;
        } else {
          mylog("[recv corrupted ack] %x %d\n", MAGIC, sequence);
        }
      } else {
        mylog("[error] timeout occurred\n");
      }
    }
  }

  send_final_packet(sock, out);

  mylog("[completed]\n");

  return 0;
}
