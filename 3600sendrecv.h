/*
 * CS3600, Spring 2013
 * Project 4 Starter Code
 * (c) 2013 Alan Mislove
 *
 */

#ifndef __3600SENDRECV_H__
#define __3600SENDRECV_H__

#include <stdio.h>
#include <stdarg.h>

typedef struct header_t {
  unsigned int magic:14;
  unsigned int ack:1;
  unsigned int eof:1;
  unsigned short length;
  unsigned int sequence;
} header;

#define WINDOW_SIZE 100
#define SEND_TIMEOUT 3
#define RECV_TIMEOUT 30


typedef struct window_frame_t {
  header head;
  char * data;
  unsigned int is_free;
} window_frame;


typedef struct window_t {
  int size;
  window_frame frames[WINDOW_SIZE];
  int next_available_frame;
  unsigned int data_offset_at_start_of_window;
} window;


int initialize_window(window *w);
int window_has_free_space(window w);
int shift_window(window *w, int shift);
int add_frame_at_index(window *w, header h, char * data, int index);
int append_new_frame(window *w, header h, char * data);


unsigned int MAGIC;

void dump_packet(unsigned char *data, int size);
header *make_header(int sequence, int length, int eof, int ack);
header *get_header(void *data);
char *get_data(void *data);
char *timestamp();
void mylog(char *fmt, ...);

#endif

