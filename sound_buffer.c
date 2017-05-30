#include "sound_buffer.h"
#include <string.h>

int sound_buffer_write(struct sound_buffer_t *buffer, const void *data, uint32_t writeOffset, uint32_t maxCount)
{
    int _count = maxCount;

    int offset = writeOffset % BUFFER_SIZE;

    if (_count > BUFFER_SIZE - offset)
    {
        _count = BUFFER_SIZE - offset;
    }

    memcpy(buffer->data + offset, data, _count);

    return _count;
}

int sound_buffer_get(struct sound_buffer_t *buffer, void **data, uint32_t readOffset, uint32_t maxCount)
{
    int _count = maxCount;

    int offset = readOffset % BUFFER_SIZE;

    if (_count > BUFFER_SIZE - offset)
    {
        _count = BUFFER_SIZE - offset;
    }

    *data = buffer->data + offset;

    return _count;
}


