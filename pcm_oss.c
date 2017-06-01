#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#include "shared_resource.h"
#include "sound_buffer.h"



struct sparkle_sound_shared_t
{
    uint32_t queuedBytes;
    uint32_t expectedPlayedBytes;
    uint32_t playedBytes;
    int play;

    struct sound_buffer_t buffer;
};


typedef struct snd_pcm_oss
{
	snd_pcm_ioplug_t io;

	unsigned int frame_bytes;

    struct timespec start;

    struct shared_resource_t *sparkle_sound;
    struct sparkle_sound_shared_t *shared;

} snd_pcm_oss_t;


static snd_pcm_sframes_t oss_write(snd_pcm_ioplug_t *io,
				   const snd_pcm_channel_area_t *areas,
				   snd_pcm_uframes_t offset,
				   snd_pcm_uframes_t size)
{
	snd_pcm_oss_t *oss = io->private_data;

	const char *buf;
	ssize_t result;

	/* we handle only an interleaved buffer */
	buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;
	size *= oss->frame_bytes;

	//result = write(oss->fd, buf, size);

    result = sound_buffer_write(&oss->shared->buffer, buf, oss->shared->queuedBytes, size);
    oss->shared->queuedBytes += result;

	if (result <= 0)
		return result;

	return result / oss->frame_bytes;
}

static snd_pcm_sframes_t oss_read(snd_pcm_ioplug_t *io,
				  const snd_pcm_channel_area_t *areas,
				  snd_pcm_uframes_t offset,
				  snd_pcm_uframes_t size)
{
	snd_pcm_oss_t *oss = io->private_data;

	char *buf;
	ssize_t result;

	/* we handle only an interleaved buffer */
	buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;
	size *= oss->frame_bytes;

	//result = read(oss->fd, buf, size);

    result = size;

	if (result <= 0)
		return result;

	return result / oss->frame_bytes;
}

static snd_pcm_sframes_t oss_pointer(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

    if (!oss->shared->play)
    {
        return 0;
    }

    struct timespec current;
    clock_gettime(CLOCK_REALTIME, &current);

    uint64_t elapsed = 0;
    elapsed += 1000LL * (current.tv_sec - oss->start.tv_sec);
    elapsed += (current.tv_nsec - oss->start.tv_nsec) / 1000000;

    int frames = elapsed * 44100 / 1000;

    oss->shared->expectedPlayedBytes = frames * 4;

	return frames;
}

static int oss_start(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

    clock_gettime(CLOCK_REALTIME, &oss->start);

    oss->shared->play = 1;

	return 0;
}

static int oss_stop(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

    oss->shared->play = 0;
    oss->shared->queuedBytes = 0;
    oss->shared->expectedPlayedBytes = 0;
    oss->shared->playedBytes = 0;

	return 0;
}

static int oss_drain(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

	return 0;
}

static int oss_prepare(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

	return 0;
}

static int oss_hw_params(snd_pcm_ioplug_t *io,
			 snd_pcm_hw_params_t *params ATTRIBUTE_UNUSED)
{
	snd_pcm_oss_t *oss = io->private_data;

	oss->frame_bytes = (snd_pcm_format_physical_width(io->format) * io->channels) / 8;

    if (io->format != SND_PCM_FORMAT_S16_LE)
    {
		fprintf(stderr, "SPARKLE SOUND: Unsupported format!\n", snd_pcm_format_name(io->format));
		return -EINVAL;
    }

	return 0;
}

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

static int oss_hw_constraint(snd_pcm_oss_t *oss)
{
	snd_pcm_ioplug_t *io = &oss->io; 

	static const snd_pcm_access_t access_list[] = {
		SND_PCM_ACCESS_RW_INTERLEAVED,
	};

	unsigned int nformats;
	unsigned int format[5];
	unsigned int nchannels;
	unsigned int channel[6];

	/* period and buffer bytes must be power of two */
	static const unsigned int bytes_list[] = {
		1U<<8, 1U<<9, 1U<<10, 1U<<11, 1U<<12, 1U<<13, 1U<<14, 1U<<15,
		1U<<16, 1U<<17, 1U<<18, 1U<<19, 1U<<20, 1U<<21, 1U<<22, 1U<<23
	};
	int i, err, tmp;

	/* access type - interleaved only */
	if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
						 ARRAY_SIZE(access_list), access_list)) < 0)
		return err;

	/* supported formats */
    nformats = 0;
    format[nformats++] = SND_PCM_FORMAT_S16_LE;

	if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
						 nformats, format)) < 0)
		return err;
	
	/* supported channels */
	nchannels = 0;
	if (!nchannels) /* assume 2ch stereo */
		err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS, 2, 2);

	if (err < 0)
		return err;

	/* supported rates */
	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE, 44100, 44100);

	if (err < 0)
		return err;

	/* period size (in power of two) */
	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, ARRAY_SIZE(bytes_list), bytes_list);
	if (err < 0)
		return err;

	/* periods */
	err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS, 2, 1024);
	if (err < 0)
		return err;

	/* buffer size (in power of two) */
	err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES, ARRAY_SIZE(bytes_list), bytes_list);
	if (err < 0)
		return err;

	return 0;
}


static int oss_close(snd_pcm_ioplug_t *io)
{
	snd_pcm_oss_t *oss = io->private_data;

    if (oss->sparkle_sound)
    {
		shared_resource_close(oss->sparkle_sound);
    }
	free(oss);

	return 0;
}

static const snd_pcm_ioplug_callback_t oss_playback_callback = {
	.start = oss_start,
	.stop = oss_stop,
	.transfer = oss_write,
	.pointer = oss_pointer,
	.close = oss_close,
	.hw_params = oss_hw_params,
	.prepare = oss_prepare,
	.drain = oss_drain,
};

static const snd_pcm_ioplug_callback_t oss_capture_callback = {
	.start = oss_start,
	.stop = oss_stop,
	.transfer = oss_read,
	.pointer = oss_pointer,
	.close = oss_close,
	.hw_params = oss_hw_params,
	.prepare = oss_prepare,
	.drain = oss_drain,
};


SND_PCM_PLUGIN_DEFINE_FUNC(oss)
{
	snd_config_iterator_t i, next;

	const char *device;

	int err;
	snd_pcm_oss_t *oss;
	
	snd_config_for_each(i, next, conf)
    {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;

		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "device") == 0)
        {
			if (snd_config_get_string(n, &device) < 0)
            {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	oss = calloc(1, sizeof(*oss));
	if (!oss)
    {
		SNDERR("cannot allocate");
		return -ENOMEM;
	}


    oss->sparkle_sound = shared_resource_open("/dev/sparkle_sound", sizeof(struct sparkle_sound_shared_t), 1, (void **)&oss->shared);
    if (!oss->sparkle_sound)
    {
		SNDERR("Failed to open shared resources");
		return -EINVAL;
    }

    oss->shared->play = 0;
    oss->shared->queuedBytes = 0;
    oss->shared->expectedPlayedBytes = 0;
    oss->shared->playedBytes = 0;


	oss->io.version = SND_PCM_IOPLUG_VERSION;
	oss->io.name = "ALSA <-> OSS PCM I/O Plugin";
	oss->io.callback = stream == SND_PCM_STREAM_PLAYBACK ? &oss_playback_callback : &oss_capture_callback;
	oss->io.private_data = oss;

	err = snd_pcm_ioplug_create(&oss->io, name, stream, mode);
	if (err < 0)
		goto error;

	if ((err = oss_hw_constraint(oss)) < 0) {
		snd_pcm_ioplug_delete(&oss->io);
		return err;
	}

	*pcmp = oss->io.pcm;

	return 0;

 error:
    if (oss->sparkle_sound)
    {
		shared_resource_close(oss->sparkle_sound);
    }
	free(oss);
	return err;
}

SND_PCM_PLUGIN_SYMBOL(oss);


