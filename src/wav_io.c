/* Copyright (C) 2002 Jean-Marc Valin
   File: wav_io.c
   Routines to handle wav (RIFF) headers

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include "wav_io.h"
#include "opus_header.h"

/* Adjust the stream->channel mapping to ensure the proper output order for
   WAV files. */
void adjust_wav_mapping(int mapping_family, int channels, unsigned char *stream_map)
{
   unsigned char new_stream_map[8];
   int i;
   /* If we aren't using one of the defined semantic channel maps, or we have
      more channels than we know what to do with, use a default 1-1 mapping. */
   if (mapping_family != 1 || channels > 8)
      return;
   for (i = 0; i < channels; i++)
   {
      new_stream_map[wav_permute_matrix[channels-1][i]] = stream_map[i];
   }
   memcpy(stream_map, new_stream_map, channels*sizeof(*stream_map));
}

static size_t fwrite_le32(opus_int32 i32, FILE *file)
{
   unsigned char buf[4];
   buf[0]=(unsigned char)(i32&0xFF);
   buf[1]=(unsigned char)(i32>>8&0xFF);
   buf[2]=(unsigned char)(i32>>16&0xFF);
   buf[3]=(unsigned char)(i32>>24&0xFF);
   return fwrite(buf,4,1,file);
}

static size_t fwrite_le16(int i16, FILE *file)
{
   unsigned char buf[2];
   buf[0]=(unsigned char)(i16&0xFF);
   buf[1]=(unsigned char)(i16>>8&0xFF);
   return fwrite(buf,2,1,file);
}

int write_wav_header(FILE *file, int rate, int mapping_family, int channels, int fp)
{
   int ret;
   int extensible;

   /* Multichannel files require a WAVEFORMATEXTENSIBLE header to declare the
      proper channel meanings. */
   extensible = mapping_family == 1 && 3 <= channels && channels <= 8;

   /* >16 bit audio also requires WAVEFORMATEXTENSIBLE. */
   extensible |= fp;

   ret = fprintf(file, "RIFF") >= 0;
   ret &= fwrite_le32(0x7fffffff, file);

   ret &= fprintf(file, "WAVEfmt ") >= 0;
   ret &= fwrite_le32(extensible ? 40 : 16, file);
   ret &= fwrite_le16(extensible ? 0xfffe : (fp?3:1), file);
   ret &= fwrite_le16(channels, file);
   ret &= fwrite_le32(rate, file);
   ret &= fwrite_le32((fp?4:2)*channels*rate, file);
   ret &= fwrite_le16((fp?4:2)*channels, file);
   ret &= fwrite_le16(fp?32:16, file);

   if (extensible)
   {
      static const unsigned char ksdataformat_subtype_pcm[16] =
      {
         0x01, 0x00, 0x00, 0x00,
         0x00, 0x00,
         0x10, 0x00,
         0x80, 0x00,
         0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
      };
      static const unsigned char ksdataformat_subtype_float[16] =
      {
         0x03, 0x00, 0x00, 0x00,
         0x00, 0x00,
         0x10, 0x00,
         0x80, 0x00,
         0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
      };
      static const int wav_channel_masks[8] =
      {
         4,                      /* 1.0 mono */
         1|2,                    /* 2.0 stereo */
         1|2|4,                  /* 3.0 channel ('wide') stereo */
         1|2|16|32,              /* 4.0 discrete quadrophonic */
         1|2|4|16|32,            /* 5.0 */
         1|2|4|8|16|32,          /* 5.1 */
         1|2|4|8|256|512|1024,   /* 6.1 */
         1|2|4|8|16|32|512|1024, /* 7.1 */
      };
      ret &= fwrite_le16(22, file);
      ret &= fwrite_le16(fp?32:16, file);
      ret &= fwrite_le32(wav_channel_masks[channels-1], file);
      if (!fp)
      {
         ret &= fwrite(ksdataformat_subtype_pcm, 16, 1, file);
      } else {
         ret &= fwrite(ksdataformat_subtype_float, 16, 1, file);
      }
   }

   ret &= fprintf(file, "data") >= 0;
   ret &= fwrite_le32(0x7fffffff, file);

   return !ret ? -1 : extensible ? 40 : 16;
}

/* format is 0 for raw PCM,
    16 for WAV with a minimal 16-byte format chunk,
    40 for WAV with a WAVEFORMATEXTENSIBLE 40-byte format chunk. */
int update_wav_header(FILE *file, int format, opus_int64 audio_size)
{
   opus_int32 write_val;

   if (format <= 0 || audio_size >= 0x7fffffff) return 0;
   if (fseek(file, 4, SEEK_SET) != 0) return -1;
   write_val = le_int((opus_int32)(audio_size + 20 + format));
   if (fwrite(&write_val, 4, 1, file) != 1) return -1;
   if (fseek(file, 16 + format, SEEK_CUR) != 0) return -1;
   write_val = le_int((opus_int32)audio_size);
   if (fwrite(&write_val, 4, 1, file) != 1) return -1;
   return 0;
}
