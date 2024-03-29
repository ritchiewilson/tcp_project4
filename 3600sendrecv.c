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
#include <stdarg.h>
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

unsigned int MAGIC = 0x0bee;

char ts[16];

/**
 * Returns a properly formatted timestamp
 */
char *timestamp() {
  time_t ltime;
  ltime=time(NULL);
  struct tm *tm;
  tm=localtime(&ltime);
  struct timeval tv1;
  gettimeofday(&tv1, NULL);

  sprintf(ts,"%02d:%02d:%02d %03d.%03d", tm->tm_hour, tm->tm_min, tm->tm_sec, (int) (tv1.tv_usec/1000), (int) (tv1.tv_usec % 1000));
  return ts;
}

/**
 * Logs debugging messages.  Works like printf(...)
 */
void mylog(char *fmt, ...) {
  va_list args;
  va_start(args,fmt);
  fprintf(stderr, "%s: ", timestamp());
  vfprintf(stderr, fmt,args);
  va_end(args);
}

/**
 * This function takes in a bunch of header fields and 
 * returns a brand new header.  The caller is responsible for
 * eventually free-ing the header.
 */
header *make_header(int sequence, int length, int eof, int ack) {
  header *myheader = (header *) malloc(sizeof(header));
  myheader->magic = MAGIC;
  myheader->eof = eof;
  myheader->sequence = htonl(sequence);
  myheader->length = htons(length);
  myheader->ack = ack;

  return myheader;
}

/**
 * This function takes a returned packet and returns a header pointer.  It
 * does not allocate any new memory, so no free is needed.
 */
header *get_header(void *data) {
  header *h = (header *) data;
  h->sequence = ntohl(h->sequence);
  h->length = ntohs(h->length);

  return h;
}

/**
 * This function takes a returned packet and returns a pointer to the data.  It
 * does not allocate any new memory, so no free is needed.
 */
char *get_data(void *data) {
  return (char *) data + sizeof(header);
}

/**
 * This function will print a hex dump of the provided packet to the screen
 * to help facilitate debugging.  
 *
 * DO NOT MODIFY THIS FUNCTION
 *
 * data - The pointer to your packet buffer
 * size - The length of your packet
 */
void dump_packet(unsigned char *data, int size) {
    unsigned char *p = data;
    unsigned char c;
    int n;
    char bytestr[4] = {0};
    char addrstr[10] = {0};
    char hexstr[ 16*3 + 5] = {0};
    char charstr[16*1 + 5] = {0};
    for(n=1;n<=size;n++) {
        if (n%16 == 1) {
            /* store address for this line */
            snprintf(addrstr, sizeof(addrstr), "%.4x",
               ((unsigned int)(intptr_t) p-(unsigned int)(intptr_t) data) );
        }
            
        c = *p;
        if (isalnum(c) == 0) {
            c = '.';
        }

        /* store hex str (for left side) */
        snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
        strncat(hexstr, bytestr, sizeof(hexstr)-strlen(hexstr)-1);

        /* store char str (for right side) */
        snprintf(bytestr, sizeof(bytestr), "%c", c);
        strncat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);

        if(n%16 == 0) { 
            /* line completed */
            printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
            hexstr[0] = 0;
            charstr[0] = 0;
        } else if(n%8 == 0) {
            /* half line: add whitespaces */
            strncat(hexstr, "  ", sizeof(hexstr)-strlen(hexstr)-1);
            strncat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
        }
        p++; /* next byte */
    }

    if (strlen(hexstr) > 0) {
        /* print rest of buffer if not empty */
        printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
    }
}


int initialize_window(window *w){
  w->size = WINDOW_SIZE;
  w->next_available_frame = 0;
  w->data_offset_at_start_of_window = 0;
  int i;
  for (i = 0; i < w->size; i++)
    w->frames[i].is_free = 1;
  return 0;
}


/*
 * Returns 1 if window has free space left. Returns 0 if it does not.
 */
int window_has_free_space(window w){
  if (w.next_available_frame < w.size)
    return 1;
  return 0;
}

/*
 * Shift the widown by the given number of frames. The first frames will be
 * shifted out of the window, so they can freed and overwritten. The rest are
 * moved back. The final frames will be left as free
 */
int shift_window(window *w, int shift){
  if (shift == 0)
    return 0;
  int i;
  for(i = 0; i < w->size; i++){
    if (i < shift){
      w->data_offset_at_start_of_window += w->frames[i].head.length;
      free(w->frames[i].data);
    }
    else 
      w->frames[i - shift] = w->frames[i];
    w->frames[i].is_free = 1;
  }
  w->next_available_frame -= shift;
  return 0;
}

/*
 * Put an incoming packet at a certain index in the window. This only used by
 * the receiver since the packets can come in in any order.
 */
int add_frame_at_index(window *w, header h, char * data, int index){
  w->frames[index].head = h;
  w->frames[index].data = data;
  w->frames[index].is_free = 0;
  return 0;
}

/*
 * appends the header and data as a new frame in the window, and increments
 * last used frame. Only called from the sender since it only ever adds the end
 * of the frame.
 *
 * Returns 0 on success
 * Returns -1 if there is no free space
 */
int append_new_frame(window *w, header h, char * data){
  if (! window_has_free_space(*w))
    return -1;
  add_frame_at_index(w, h, data, w->next_available_frame);
  w->next_available_frame++;
  return 0;
}

