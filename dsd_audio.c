/*
 * Copyright (C) 2010 DSD Author
 * GPG Key ID: 0x3F1D7FD0 (74EF 430D F7F2 0A48 FCE6  F630 FAA2 635D 3F1D 7FD0)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "dsd.h"

int audio_initialized = FALSE;

void
initAudio()
{
  if (audio_initialized == TRUE)
    {
      return;
    }
  PaError err = Pa_Initialize();
  if (err != paNoError)
    {
      printf("PortAudio error: %s\n", Pa_GetErrorText(err));
      exit (1);
    }

  audio_initialized = TRUE;
}

void
terminateAudio(dsd_opts * opts)
{
  if (audio_initialized == FALSE)
    {
      return;
    }

  if (opts->audio_out_fd)
    {
	  Pa_StopStream(opts->audio_out_fd);
	  Pa_CloseStream(opts->audio_out_fd);
    }

  if (opts->audio_in_fd)
    {
      Pa_StopStream(opts->audio_in_fd);
	  Pa_CloseStream(opts->audio_in_fd);
    }

  Pa_Terminate();
}

void
handlePaError(PaError err, int terminate)
{
  if (err != paNoError)
    {
      printf("PortAudio error: %s\n", Pa_GetErrorText(err));
      if (terminate == TRUE)
        {
          exit (1);
        }
    }
}


void
processAudio (dsd_opts * opts, dsd_state * state)
{

  int i, n;
  float aout_abs, max, gainfactor, gaindelta, maxbuf;

  if (opts->audio_gain == (float) 0)
    {
      // detect max level
      max = 0;
      state->audio_out_temp_buf_p = state->audio_out_temp_buf;
      for (n = 0; n < 160; n++)
        {
          aout_abs = fabsf (*state->audio_out_temp_buf_p);
          if (aout_abs > max)
            {
              max = aout_abs;
            }
          state->audio_out_temp_buf_p++;
        }
      *state->aout_max_buf_p = max;
      state->aout_max_buf_p++;
      state->aout_max_buf_idx++;
      if (state->aout_max_buf_idx > 24)
        {
          state->aout_max_buf_idx = 0;
          state->aout_max_buf_p = state->aout_max_buf;
        }

      // lookup max history
      for (i = 0; i < 25; i++)
        {
          maxbuf = state->aout_max_buf[i];
          if (maxbuf > max)
            {
              max = maxbuf;
            }
        }

      // determine optimal gain level
      if (max > (float) 0)
        {
          gainfactor = ((float) 30000 / max);
        }
      else
        {
          gainfactor = (float) 50;
        }
      if (gainfactor < state->aout_gain)
        {
          state->aout_gain = gainfactor;
          gaindelta = (float) 0;
        }
      else
        {
          if (gainfactor > (float) 50)
            {
              gainfactor = (float) 50;
            }
          gaindelta = gainfactor - state->aout_gain;
          if (gaindelta > ((float) 0.05 * state->aout_gain))
            {
              gaindelta = ((float) 0.05 * state->aout_gain);
            }
        }
      gaindelta /= (float) 160;
    }
  else
    {
      gaindelta = (float) 0;
    }

  // adjust output gain
  state->audio_out_temp_buf_p = state->audio_out_temp_buf;
  for (n = 0; n < 160; n++)
    {
      *state->audio_out_temp_buf_p = (state->aout_gain + ((float) n * gaindelta)) * (*state->audio_out_temp_buf_p);
      state->audio_out_temp_buf_p++;
    }
  state->aout_gain += ((float) 160 * gaindelta);

  // copy audio datat to output buffer and upsample if necessary
  state->audio_out_temp_buf_p = state->audio_out_temp_buf;
  if (opts->split == 0)
    {
      for (n = 0; n < 160; n++)
        {
          upsample (state, *state->audio_out_temp_buf_p);
          state->audio_out_temp_buf_p++;
          state->audio_out_float_buf_p += 6;
          state->audio_out_idx += 6;
          state->audio_out_idx2 += 6;
        }
      state->audio_out_float_buf_p -= (960 + opts->playoffset);
      // copy to output (short) buffer
      for (n = 0; n < 960; n++)
        {
          if (*state->audio_out_float_buf_p > (float) 32760)
            {
              *state->audio_out_float_buf_p = (float) 32760;
            }
          else if (*state->audio_out_float_buf_p < (float) -32760)
            {
              *state->audio_out_float_buf_p = (float) -32760;
            }
          *state->audio_out_buf_p = (short) *state->audio_out_float_buf_p;
          state->audio_out_buf_p++;
          state->audio_out_float_buf_p++;
        }
      state->audio_out_float_buf_p += opts->playoffset;
    }
  else
    {
      for (n = 0; n < 160; n++)
        {
          if (*state->audio_out_temp_buf_p > (float) 32760)
            {
              *state->audio_out_temp_buf_p = (float) 32760;
            }
          else if (*state->audio_out_temp_buf_p < (float) -32760)
            {
              *state->audio_out_temp_buf_p = (float) -32760;
            }
          *state->audio_out_buf_p = (short) *state->audio_out_temp_buf_p;
          state->audio_out_buf_p++;
          state->audio_out_temp_buf_p++;
          state->audio_out_idx++;
          state->audio_out_idx2++;
        }
    }
}

void
writeSynthesizedVoice (dsd_opts * opts, dsd_state * state)
{

  int n;
  short aout_buf[160];
  short *aout_buf_p;
  ssize_t result;

  aout_buf_p = aout_buf;
  state->audio_out_temp_buf_p = state->audio_out_temp_buf;
  for (n = 0; n < 160; n++)
    {
      if (*state->audio_out_temp_buf_p > (float) 32760)
        {
          *state->audio_out_temp_buf_p = (float) 32760;
        }
      else if (*state->audio_out_temp_buf_p < (float) -32760)
        {
          *state->audio_out_temp_buf_p = (float) -32760;
        }
      *aout_buf_p = (short) *state->audio_out_temp_buf_p;
      aout_buf_p++;
      state->audio_out_temp_buf_p++;
    }

  result = write (opts->wav_out_fd, aout_buf, 320);
  fflush (opts->wav_out_f);
  state->wav_out_bytes += 320;
}

void
playSynthesizedVoice (dsd_opts * opts, dsd_state * state)
{

  ssize_t result;

  if (state->audio_out_idx > opts->delay)
    {
      // output synthesized speech to sound card
	  Pa_WriteStream(opts->audio_out_fd, (state->audio_out_buf_p - state->audio_out_idx), (state->audio_out_idx * 2));
      state->audio_out_idx = 0;
    }

  if (state->audio_out_idx2 >= 800000)
    {
      state->audio_out_float_buf_p = state->audio_out_float_buf + 100;
      state->audio_out_buf_p = state->audio_out_buf + 100;
      memset (state->audio_out_float_buf, 0, 100 * sizeof (float));
      memset (state->audio_out_buf, 0, 100 * sizeof (short));
      state->audio_out_idx2 = 0;
    }
}

void
openAudioOutDevice (dsd_opts * opts, double speed)
{
  initAudio();
  PaStreamParameters outputParameters;
  outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
  outputParameters.device = 2;
  if (outputParameters.device == paNoDevice)
    {
      fprintf(stderr,"PortAudio error: No default output device.\n");
      terminateAudio(opts);
      exit(1);
    }
  outputParameters.channelCount = 1;
  outputParameters.sampleFormat = paInt16;
  outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultHighOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  printf ("Audio Out Device: %s\n", Pa_GetDeviceInfo(outputParameters.device)->name);

  PaError err;
  err = Pa_OpenStream(
              &opts->audio_out_fd,
              NULL, /* no input */
              &outputParameters,
              speed,
              1024,
              paClipOff,
              NULL,
              NULL);
  if( err != paNoError )
    {
      printf("PortAudio error: %s\n", Pa_GetErrorText(err));
      terminateAudio(opts);
      exit(1);
    }

  err = Pa_StartStream(opts->audio_out_fd);
  if( err != paNoError )
    {
      printf("PortAudio error: %s\n", Pa_GetErrorText(err));
      terminateAudio(opts);
      exit(1);
    }
}

void
openAudioInDevice (dsd_opts * opts)
{
  initAudio();
  PaStreamParameters inputParameters;
  inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
  if (inputParameters.device == paNoDevice)
    {
      fprintf(stderr,"PortAudio error: No default output device.\n");
      terminateAudio(opts);
      exit(1);
    }

  inputParameters.channelCount = 1;
  inputParameters.sampleFormat = paInt16;
  inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultHighOutputLatency;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  printf ("Audio In Device: %s\n", Pa_GetDeviceInfo(inputParameters.device)->name);

  PaError err;
  err = Pa_OpenStream(
              &opts->audio_in_fd,
              &inputParameters,
              NULL, /* no output */
              44100,
              1024,
              paClipOff,
              NULL,
              NULL );
  if( err != paNoError )
    {
      printf("PortAudio error: %s\n", Pa_GetErrorText(err));
      terminateAudio(opts);
      exit(1);
    }

  err = Pa_StartStream(opts->audio_in_fd);
  if( err != paNoError )
    {
      printf("PortAudio error: %s\n", Pa_GetErrorText(err));
      terminateAudio(opts);
      exit(1);
    }
}

void
loopAudio(dsd_opts * opts)
{
	short *sample = (short*) malloc(1);
    Pa_ReadStream(opts->audio_in_fd, sample, 1);
    printf("Am citit: %hi\n", sample[0]);
	Pa_WriteStream(opts->audio_out_fd, sample, 1);
	free(sample);
}
