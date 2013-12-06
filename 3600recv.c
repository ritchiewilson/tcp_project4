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

unsigned int sequence = 0;
window win;
int data_byte_at_start_of_window = 0;

int main() {
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

  // first, open a UDP socket  
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  // next, construct the local port
  struct sockaddr_in out;
  out.sin_family = AF_INET;
  out.sin_port = htons(0);
  out.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sock, (struct sockaddr *) &out, sizeof(out))) {
    perror("bind");
    exit(1);
  }

  struct sockaddr_in tmp;
  int len = sizeof(tmp);
  if (getsockname(sock, (struct sockaddr *) &tmp, (socklen_t *) &len)) {
    perror("getsockname");
    exit(1);
  }

  mylog("[bound] %d\n", ntohs(tmp.sin_port));

  // wait for incoming packets
  struct sockaddr_in in;
  socklen_t in_len = sizeof(in);

  // construct the socket set
  fd_set socks;

  // construct the timeout
  struct timeval t;
  t.tv_sec = 300;
  t.tv_usec = 0;


  initialize_window(&win);

  // wait to receive, or for a timeout
  while (1) {
    // our receive buffer
    int buf_len = 1500;
    void* buf = malloc(buf_len);

    FD_ZERO(&socks);
    FD_SET(sock, &socks);

    if (select(sock + 1, &socks, NULL, NULL, &t)) {
      int received;
      if ((received = recvfrom(sock, buf, buf_len, 0, (struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
        perror("recvfrom");
        exit(1);
      }

      header *myheader = get_header(buf);
      int window_pos = (myheader->sequence - win.data_offset_at_start_of_window) / 1460;
  
      if (myheader->magic != MAGIC || window_pos > win.size) {
        mylog("[recv corrupted packet]\n");
        continue;
      }
     
      if (win.frames[window_pos].is_free){
        add_frame_at_index(&win, *myheader, buf, window_pos);
      }
      
      int eof = 0;
      int i = 0;
      while(i < win.size && !win.frames[i].is_free){
        header *head = &(win.frames[i].head);
        char *data = get_data(win.frames[i].data);
        write(1, data, head->length);
        mylog("[recv data] %d (%d) %s\n", head->sequence, head->length, "ACCEPTED (in-order)");
        if(head->eof)
          eof = 1;
        i++;
      }
      int frames_printed = i;
      if (frames_printed > 0){ // some frames at start of window were printed, so window can shift
        header *last_header = &win.frames[frames_printed - 1].head;
        int ack_num = last_header->sequence + last_header->length;
        mylog("[send ack] %d\n", ack_num);
        header *responseheader = make_header(ack_num, 0, last_header->eof, 1);
        if (sendto(sock, responseheader, sizeof(header), 0, (struct sockaddr *) &in, (socklen_t) sizeof(in)) < 0) {
          perror("sendto");
          exit(1);
        }
        shift_window(&win, frames_printed);
      }
      
      if (eof) {
        mylog("[recv eof]\n");
        mylog("[completed]\n");
        exit(0);
      }
    }  else {
      mylog("[error] timeout occurred\n");
      exit(1);
    }
  }


  return 0;
}
