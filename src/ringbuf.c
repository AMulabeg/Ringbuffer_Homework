#include "../include/ringbuf.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

void ringbuffer_init(rbctx_t *context, void *buffer_location,
                     size_t buffer_size) {

  context->begin = buffer_location;
  context->read = buffer_location;
  context->write = buffer_location;
  context->end = buffer_location + buffer_size;
  pthread_mutex_init(&context->mtx, NULL);
  pthread_cond_init(&context->sig, NULL);
}

int ringbuffer_write(rbctx_t *context, void *message, size_t message_len) {
  size_t space_left = context->end - context->write;
  size_t space_total = context->end - context->begin;
  size_t space_used;
  if (context->write >= context->read) {
    space_used = context->write - context->read;
  } else {
    space_used =
        (context->end - context->read) + (context->write - context->begin);
  }

  size_t space_free = space_total - space_used;

  if (space_free <= message_len + sizeof(size_t)) {
    return RINGBUFFER_FULL;
  }

  if (space_total < message_len + sizeof(size_t)) {
    return RINGBUFFER_FULL;
  }

  pthread_mutex_lock(&context->mtx);
  while (space_free <= message_len + sizeof(size_t)) {
    struct timespec sec;
    clock_gettime(CLOCK_REALTIME, &sec);
    sec.tv_sec += 1;
    int response = pthread_cond_timedwait(&context->sig, &context->mtx, &sec);
    if (response == ETIMEDOUT) {
      pthread_exit(NULL);
    }
  }

  if (space_left >= message_len + sizeof(size_t)) {
    memcpy(context->write, (uint8_t *)&(message_len), sizeof(size_t));
    context->write += sizeof(size_t);
    memcpy(context->write, ((uint8_t *)message), message_len);
    context->write += message_len;

    if (context->write == context->end) {
      context->write = context->begin;
    };

  }

  else {
    size_t split1 = space_left;
    size_t split2 = sizeof(size_t) - split1;
    if (space_left < sizeof(size_t)) {
      memcpy(context->write, (uint8_t *)&(message_len), split1);
      memcpy(context->begin, (uint8_t *)&(message_len) + split1, split2);
      context->write = context->begin + split2;
      memcpy(context->write, ((uint8_t *)message), message_len);
      context->write += message_len;

    } else {

      memcpy(context->write, (uint8_t *)&(message_len), sizeof(size_t));
      split1 -= sizeof(size_t);
      split2 = message_len - split1;
      context->write += sizeof(size_t);
      if (split1 == 0) {
        context->write = context->begin;
        memcpy(context->write, ((uint8_t *)message), message_len);
        context->write += message_len;

      } else {
        memcpy(context->write, ((uint8_t *)message), split1);
        context->write = context->begin;
        memcpy(context->write, ((uint8_t *)message) + split1, split2);
        context->write += split2;
      }
    }
  }
  if (context->write == context->end) {
    context->write = context->begin;
  };
  pthread_mutex_unlock(&context->mtx);
  pthread_cond_signal(&context->sig);
  return SUCCESS;
}

int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len) {
  size_t message;
  size_t space_left = context->end - context->read;
  int ifwrapped = 0;
  if (context->read == context->write) {
    return RINGBUFFER_EMPTY;
  }
  uint8_t *temp = context->read;

  if (space_left >= sizeof(size_t)) {
    message = *((size_t *)context->read);
    ifwrapped = 8;
  } else {

    size_t split1 = space_left;
    size_t split2 = sizeof(size_t) - split1;

    memcpy(&message, temp, split1);
    memcpy(((uint8_t *)&message) + split1, context->begin, split2);
    ifwrapped = split2;
  }
  if (*buffer_len < message) {
    return OUTPUT_BUFFER_TOO_SMALL;
  }

  pthread_mutex_lock(&context->mtx);
  while (context->read == context->write) {
    struct timespec sec;
    clock_gettime(CLOCK_REALTIME, &sec);
    sec.tv_sec += 1;
    int response = pthread_cond_timedwait(&context->sig, &context->mtx, &sec);
    if (response == ETIMEDOUT) {
      pthread_exit(NULL);
    }
  }

  if (ifwrapped < 8) {
    context->read = context->begin + ifwrapped;
  } else {

    context->read += sizeof(size_t);
  }
  *buffer_len = message;
  space_left = context->end - context->read;
  if (space_left >= message) {

    memcpy(buffer, context->read, message);
    context->read += message;
    if (context->read == context->end) {
      context->read = context->begin;
    }

  } else {
    size_t part1 = space_left;
    size_t part2 = message - part1;

    memcpy(buffer, context->read, part1);
    memcpy(((uint8_t *)buffer) + part1, context->begin, part2);
    context->read = context->begin + part2;
  }

  if (context->read == context->end) {
    context->read = context->begin;
  }
  pthread_mutex_unlock(&context->mtx);
  pthread_cond_signal(&context->sig);

  return SUCCESS;
}

void ringbuffer_destroy(rbctx_t *context) { /* your solution here */
  pthread_mutex_destroy(&context->mtx);
  pthread_cond_destroy(&context->sig);
}
