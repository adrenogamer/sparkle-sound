#ifndef SOUND_BUFFER_H
#define SOUND_BUFFER_H

#include <stdint.h>

#define BUFFER_SIZE 1024*1024

struct sound_buffer_t
{
    char data[BUFFER_SIZE];
};

int sound_buffer_write(struct sound_buffer_t *buffer, const void *data, uint32_t writeOffset, uint32_t maxCount);
int sound_buffer_get(struct sound_buffer_t *buffer, void **data, uint32_t readOffset, uint32_t maxCount);


#endif //SOUND_BUFFER_H


