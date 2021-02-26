#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((b) > (a) ? (a) : (b))

// https://www.youtube.com/watch?v=rtmaE7-jPjA
// https://github.com/gamozolabs/canon_pixma_mx492/blob/main/decompression/decompress_rle.c
u32 decompress(u8 *src_ptr, u32 src_size, u8 *dst_ptr, u32 dst_size)
{
  u8 *org_src_ptr = src_ptr;
  u8 *org_dst_ptr = dst_ptr;
  
  auto dst_end = dst_ptr + dst_size;
  auto src_end = src_ptr + src_size;
  do 
  {
    u32 byte = *src_ptr++;
    s32 raw_len = byte & 3;
    u32 offset_x256 = (byte >> 2) & 3;
    u32 decode_len = byte >> 4;

    if(raw_len == 0) raw_len = (s32)(u32)*src_ptr++;
    if(decode_len == 0)decode_len = *src_ptr++;

    if(raw_len && raw_len-1+src_ptr > src_end)
    {
      printf("Error: [overflow] Attempt to copy %d\n", raw_len-1);
      break;
    }

    while(--raw_len > 0)*dst_ptr++ = *src_ptr++;

    if(decode_len != 0)
    {
      u32 offset = *src_ptr++;
      if(offset_x256 == 3) offset_x256 = *src_ptr++;

      // printf("offset: %d\n", offset);
      auto p = dst_ptr - (offset_x256 * 0x100 + offset);
      decode_len += 2;
      while(decode_len--)*dst_ptr++ = *p++;
    }
  }
  while(src_ptr < src_end);

  auto in_size = src_ptr - org_src_ptr;
  auto out_size = dst_ptr - org_dst_ptr;

  auto ratio = (float)in_size/out_size;

  printf("(%%%.2f) %d ==> %d\n", 100.0f*ratio, in_size, out_size);

  return out_size;
}

u32 match(u8 *ptr1, u8 *ptr2, u32 len)
{
  u32 i=0;
  for(; i<len; i++)
  {
    if(ptr1[i] != ptr2[i])break;
  }
  return i;
}

/// This is a compressor that was coded based on the reversed-engineered decompress function
u32 compress(u8 *input, u32 input_size, u8 *output, u32 output_size)
{
  auto out_ptr = output;
  auto in_ptr = input;
  auto in_end = in_ptr + input_size;

  while(in_ptr < in_end)
  {
    u32 data_len = 0;
    u32 max_match_offset = 0;
    u32 max_match_len = 0;

    // raw_len cannot be larger than 255-1
    for(int i=0; i<254 && in_ptr+i < in_end; i++)
    {
      // the index of current character.
      u32 gi = in_ptr-input+i;
      // start from the first byte in the data but not more than 0xffff back from current location.
      u32 sj = MAX(0, (s32)gi-0xffff);

      // try to match against all past string sequences
      for(u32 j=sj; j<gi; j++)
      {
        // Cannot match more than 257 characters.
        u32 max_match = MIN(input_size - gi, 257);
        // find how many matching characters
        u32 match_len = match(input+j, input+gi, max_match);
        // cannot match less than 3 characters. grab the biggest match, or an equal match but closer to current character.
        if(match_len >= 3 && match_len >= max_match_len)
        {
          // record the offset of the match
          max_match_offset = gi - j;
          // set the current match as the max match.
          max_match_len = match_len;
        }
      }

      // found a match, encode it.
      if(max_match_len)break;

      // did not find a match, continue encoding as raw data.
      data_len++;
    }

    // output the raw data and match (if it exists) in the same format expected by the decompress function.
    u32 decode_len = max_match_len && max_match_len < 16 ? max_match_len-2 : 0;
    u32 offset_x256 = MIN(3, max_match_offset / 256);
    u32 raw_len = data_len > 2 ? 0 : data_len + 1;
    u32 byte = (decode_len << 4) | (offset_x256 << 2) | (raw_len);
    *out_ptr++ = byte;

    if(raw_len == 0)*out_ptr++ = data_len + 1;
    if(decode_len == 0)*out_ptr++ = max_match_len ? max_match_len-2 : 0;

    for(u32 i=0; i<data_len; i++)*out_ptr++ = *in_ptr++;

    in_ptr += max_match_len;

    if(max_match_len != 0)
    {
      *out_ptr++ = max_match_offset & 255;
      if(offset_x256 == 3)*out_ptr++ = max_match_offset / 256;
    }
  }

  auto out_size = out_ptr - output;
  return out_size;
}

struct ReadFileResult
{
  u8 *data;
  u32 size;
};
ReadFileResult read_file(char *filename){
  ReadFileResult result = {};
  auto file = fopen(filename, "rb");
  if(file){
    fseek(file, 0, SEEK_END);
    result.size = ftell(file);
    fseek(file, 0, SEEK_SET);
    result.data = (u8 *)calloc(1, result.size+1);
    fread(result.data, 1, result.size, file);
    result.data[result.size] = 0;
    fclose(file);
  }
  return result;
}

int main(){

  char raw[] = R"V0G0N(
  u32 compressed_buffer_size = 10 * 1024;
  auto compressed = (u8 *)calloc(1, compressed_buffer_size);

  auto compressed_size = compress((u8 *)raw, sizeof(raw), compressed, compressed_buffer_size);

  int output_buffer_size = 10 * 1024;
  auto output = (u8 *)calloc(1, output_buffer_size);

  // u8 input[] = {(1 << 4) | 2, 'A', 1, 2, 0, 0};
  printf("Decompressing\n");
  auto decompressed_size = decompress(compressed, compressed_size, output, output_buffer_size);
  if(decompressed_size != sizeof(raw))
  {
    printf("Error: LENGTH MISMATCH, Expected: %d, Got: %d\n", sizeof(raw), decompressed_size);
  }
  if(memcmp(raw, output, decompressed_size) != 0)
  {
    printf("Error: DATA MISMATCH\n");
  }
  )V0G0N";

#if 1
  auto read_res = read_file("clang-cpp.exe");
  auto data = read_res.data;
  auto data_size = read_res.size;
#else
  auto data = (u8 *)raw;
  auto data_size = sizeof(raw);
#endif

  u32 compressed_buffer_size = data_size * 2;
  auto compressed = (u8 *)calloc(1, compressed_buffer_size);

  auto compressed_size = compress(data, data_size, compressed, compressed_buffer_size);
  {
    auto ratio = (float)compressed_size/data_size;
    printf("compressed: (%%%.2f) %d ==> %d\n", 100.0f*ratio, data_size, compressed_size);
  }

  int output_buffer_size = data_size * 2;
  auto output = (u8 *)calloc(1, output_buffer_size);

  auto decompressed_size = decompress(compressed, compressed_size, output, output_buffer_size);
  if(decompressed_size != data_size)
  {
    printf("Error: LENGTH MISMATCH, Expected: %d, Got: %d\n", data_size, decompressed_size);
  }
  if(memcmp(data, output, decompressed_size) != 0)
  {
    printf("Error: DATA MISMATCH\n");
  }

  // for(int i=0; i<compressed_size; i++)
  // {
  //   // printf("%c", isprint(compressed[i])?compressed[i]: '`');
  // }
}