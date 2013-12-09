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
window win;

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

  header stored_header = *myheader;
  get_header(&stored_header); // dumb hack to change endianess
  
  append_new_frame(&win, stored_header, data);

  *len = sizeof(header) + data_len;

  return packet;
}

int send_packet(int sock, struct sockaddr_in out, void *packet, int packet_len, int seq) {

  if (packet == NULL) 
    return 0;
  
  mylog("[send data] %d (%d)\n", seq, packet_len - sizeof(header));

  if (sendto(sock, packet, packet_len, 0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
    perror("sendto");
    exit(1);
  }

  return 1;
  
}

int send_next_packet(int sock, struct sockaddr_in out) {
  int packet_len = 0;
  void *packet = get_next_packet(&packet_len);

  if (packet == NULL) 
    return 0;
  
  return send_packet(sock, out, packet, packet_len, sequence);

}

/*
 * Retransmit the packet at window index i
 */
int retransmit_packet(int sock, struct sockaddr_in out, int i){
  header myheader = win.frames[i].head;
  int data_len = myheader.length;
  int seq = data_len + myheader.sequence;
  get_header(&myheader); // dumb hack to flip endianess
  void *packet = malloc(DATA_SIZE);
  memcpy(packet, &myheader, sizeof(header));
  memcpy(((char *) packet) + sizeof(header), win.frames[i].data, data_len);

  int packet_len = data_len + sizeof(header);
  return send_packet(sock, out, packet, packet_len, seq);

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

  initialize_window(&win);

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
  t.tv_sec = SEND_TIMEOUT;
  t.tv_usec = 0;

  // Keep looping while there is more data to be read, or more packets to
  // restransmit.
  while(send_next_packet(sock, out) || !win.frames[0].is_free){
    
    // send as many packets as can fit in the window and can be read
    while(window_has_free_space(win) && send_next_packet(sock, out)){}

    int retransmited = 0;
    int no_new_acks = 0; // times nothing new has been acked. Resend on 3
    int done = 0;
    while (! done) {
      FD_ZERO(&socks);
      FD_SET(sock, &socks);

      t.tv_sec = SEND_TIMEOUT;
      t.tv_usec = 0;

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

        // handle corruped packets
        if ((myheader->magic != MAGIC) && 
            (myheader->sequence < win.data_offset_at_start_of_window) &&
            (myheader->ack != 1)) {
          mylog("[recv corrupted ack] %x %d\n", MAGIC, sequence);
          continue;
        }

        // handle valid packets
        mylog("[recv ack] %d\n", myheader->sequence);
        // figure out how many window frames have now been acked
        int acked = 0;
        while (!win.frames[acked].is_free &&
               win.frames[acked].head.sequence < myheader->sequence)
          acked++;
        mylog("[send - acked: %d]\n", acked);

        // removed ACKd frames from window
        if (acked > 0){
          shift_window(&win, acked);
          no_new_acks = 0;
        }
        else
          no_new_acks++;

        // if there is no empty frame at end of window, not done
        done = win.frames[WINDOW_SIZE - 1].is_free;

        // if there have been three loops with no new acks, resend first
        // packet.
        if (no_new_acks >= 2){
          retransmit_packet(sock, out, 0);
          retransmit_packet(sock, out, 1);
          retransmited = 1;
          done = 0;
        }
      }
      else {
        // on timeout, retransmit
        mylog("[error] timeout occurred\n");
        retransmit_packet(sock, out, 0);
        retransmit_packet(sock, out, 1);
        retransmited = 1;
        done = 0;
      }
    }
  }

  send_final_packet(sock, out);

  mylog("[completed]\n");

  return 0;
}
