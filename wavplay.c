/*-
 * wavplay - a C library to play WAV sound via OSS/ALSA
 *
 * Copyright (c) 2011 Zhihao Yuan.
 * All rights reserved.
 *
 * This file is distributed under the 2-clause BSD License.
 */

#include "wavplay.h"
#include <string.h>
#define BUF_SIZE 4096
#define PERIOD   2
#define eputs(s) (fprintf(stderr, "%s: " s "\n", __func__))

#if !defined(USE_ALSA)

#define WAV_FMT_8      AFMT_U8;
#define WAV_FMT_16     AFMT_S16_LE;
#define WAV_FMT_24     AFMT_S24_LE;
#define WAV_FMT_32     AFMT_S32_LE;
#define WAV_FMT_A_LAW  AFMT_A_LAW;
#define WAV_FMT_MU_LAW AFMT_MU_LAW;

static int devfd = -1;

int snd_init(void) {
	if (devfd > -1) snd_end();
	devfd = open(DEV_NAME, O_WRONLY);
	return devfd;
}

void snd_set(int format, int nchannels, int framerate) {
	ioctl(devfd, SNDCTL_DSP_RESET, NULL);
	ioctl(devfd, SNDCTL_DSP_SETFMT, &format);
	ioctl(devfd, SNDCTL_DSP_CHANNELS, &nchannels);
	ioctl(devfd, SNDCTL_DSP_SPEED, &framerate);
}

void snd_end(void) {
	close(devfd);
	devfd = -1;
}

void snd_play(FILE *fp, size_t n) {
	unsigned char buf[BUF_SIZE];
	size_t i = 0;
	while (!feof(fp)) {
		fread(buf, sizeof(buf), 1, fp);
		write(devfd, buf, n - i < sizeof(buf) ? n - i : sizeof(buf));
		i += sizeof(buf);
	}
}

#else

#define WAV_FMT_8      SND_PCM_FORMAT_U8;
#define WAV_FMT_16     SND_PCM_FORMAT_S16;
#define WAV_FMT_24     SND_PCM_FORMAT_S24;
#define WAV_FMT_32     SND_PCM_FORMAT_S32;
#define WAV_FMT_A_LAW  SND_PCM_FORMAT_A_LAW;
#define WAV_FMT_MU_LAW SND_PCM_FORMAT_MU_LAW;

static snd_pcm_t *pcm = NULL;

int snd_init(void) {
	if (pcm) snd_end();
	return snd_pcm_open(&pcm, DEV_NAME, SND_PCM_STREAM_PLAYBACK, 0);
}

void snd_set(int format, int nchannels, int framerate) {
	unsigned int val = framerate;
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_any(pcm, params);
	snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(pcm, params, format);
	snd_pcm_hw_params_set_channels(pcm, params, nchannels);
	snd_pcm_hw_params_set_rate_near(pcm, params, &val, 0);
	snd_pcm_hw_params_set_periods(pcm, params, PERIOD, 0);
	snd_pcm_hw_params(pcm, params);
}

void snd_end(void) {
	snd_pcm_close(pcm);
	pcm = NULL;
}

void snd_play(FILE *fp, size_t n) {
	unsigned char buf[BUF_SIZE * 2];
	size_t i = 0;
	snd_pcm_format_t format;
	unsigned int nchannels;
	snd_pcm_hw_params_t *params;
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_current(pcm, params);
	snd_pcm_hw_params_get_format(params, &format);
	snd_pcm_hw_params_get_channels(params, &nchannels);
	while (!feof(fp)) {
		fread(buf, sizeof(buf), 1, fp);
		snd_pcm_prepare(pcm);
		snd_pcm_writei(pcm, buf, (n - i < sizeof(buf) ? 
				n - i : sizeof(buf)) / (format * nchannels));
		i += sizeof(buf);
	}
	snd_pcm_drain(pcm);
}

#endif

int wav_getfmt(int comptype, int bitdepth) {
	switch (comptype) {
	case 1:
		bitdepth += bitdepth % 8;
		switch (bitdepth) {
		case 8:  return WAV_FMT_8;
		case 16: return WAV_FMT_16;
		case 24: return WAV_FMT_24;
		case 32: return WAV_FMT_32;
		default: return -1;
		}
	case 6: return WAV_FMT_A_LAW;
	case 7: return WAV_FMT_MU_LAW;
	default: return -1;
	}
}

size_t wav_read(FILE *fp) {
	riffchunk_t ck;
	wavheader_t header;
#define skip(n) (fseek(fp, (long) (n), SEEK_CUR))
#define read2(t) (fread(&t, sizeof(t), 1, fp))
#define chkid(s) (!strncmp(ck.id, s, 4))
	read2(ck);
	if (ferror(fp)) {
		perror(__func__);
		return 0;
	}
	if (!chkid("RIFF")) {
		eputs("Not an RIFF file");
		return 0;
	}
	read2(ck.id);
	if (!chkid("WAVE")) {
		eputs("Not a WAVE file");
		return 0;
	}
	while (read2(ck)) {
		if (ck.size < 0) {
			eputs("RIFF chunk size > 2GB");
			return 0;
		}
		else if (chkid("fmt ")) {
			read2(header);
			if (ck.size < sizeof(header))
				eputs("Bad format chunk");
			else
				skip(ck.size - sizeof(header));
		}
		else if (chkid("data")) break;
		else skip(ck.size);
	}
	if (!chkid("data")) {
		eputs("Malformed RIFF file");
		return 0;
	}
#undef skip
#undef read2
#undef chkid
	int format = wav_getfmt(header.comptype, header.bitdepth);
	if (format < 0) {
		eputs("Unsupported PCM format");
		return 0;
	} else {
		snd_set(format, header.nchannels, header.framerate);
		return ck.size;
	}
}

void wav_play(const char *fn) {
	FILE *fp = fn ? fopen(fn, "rb") : stdin;
	if (fp) {
		size_t size = wav_read(fp);
		if (size)
			snd_play(fp, size);
		else
			fprintf(stderr, "%s: Skipping file `%s'\n",
					__func__, fn ? fn : "STDIN");
		fclose(fp);
	} else perror(__func__);
}

