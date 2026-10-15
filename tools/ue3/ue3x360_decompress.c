/* Decompresses the player's own fully compressed UE3 Xbox 360 packages (.xxx) into plain packages.
 *
 * File layout (big-endian): tag 0x9E2A83C1, block size, total compressed size, total
 * uncompressed size, then one (compressed, uncompressed) pair per block, then the blocks.
 * Each block is an XMemCompress LZX stream: frames prefixed by either FF + u16 uncompressed +
 * u16 compressed, or a bare u16 compressed size (frame of 0x8000 bytes). The reader below strips
 * those prefixes and hands libmspack's lzxd the plain bitstream (window 2^17).
 *
 * build against libmspack (LGPL, not included in this repository):
 *   clang -O2 -I<libmspack>/mspack ue3x360_decompress.c <libmspack>/mspack/lzxd.c -o out/ue3/ue3x360_decompress.exe
 * usage: ue3x360_decompress <in.xxx> <out.upk>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mspack.h"
#include "lzx.h"

typedef struct {
  const unsigned char* buf;
  size_t size;
  size_t pos;
  size_t rest; /* bytes left in the current frame */
} src_file;

typedef struct {
  unsigned char* buf;
  size_t size;
  size_t pos;
} dst_file;

static int src_read(struct mspack_file* file, void* buffer, int bytes) {
  src_file* f = (src_file*)file;
  if (f->rest == 0) {
    if (f->pos >= f->size) return 0;
    if (f->buf[f->pos] == 0xFF) {
      if (f->pos + 5 > f->size) return 0;
      f->rest = ((size_t)f->buf[f->pos + 3] << 8) | f->buf[f->pos + 4];
      f->pos += 5;
    } else {
      if (f->pos + 2 > f->size) return 0;
      f->rest = ((size_t)f->buf[f->pos] << 8) | f->buf[f->pos + 1];
      f->pos += 2;
    }
    if (f->rest > f->size - f->pos) f->rest = f->size - f->pos;
    if (f->rest == 0) return 0;
  }
  if ((size_t)bytes > f->rest) bytes = (int)f->rest;
  memcpy(buffer, f->buf + f->pos, (size_t)bytes);
  f->pos += (size_t)bytes;
  f->rest -= (size_t)bytes;
  return bytes;
}

static int dst_write(struct mspack_file* file, void* buffer, int bytes) {
  dst_file* f = (dst_file*)file;
  size_t n = (size_t)bytes;
  if (n > f->size - f->pos) n = f->size - f->pos;
  memcpy(f->buf + f->pos, buffer, n);
  f->pos += n;
  return (int)n;
}

static int rw_dispatch_read(struct mspack_file* file, void* buffer, int bytes) {
  return src_read(file, buffer, bytes);
}
static int rw_dispatch_write(struct mspack_file* file, void* buffer, int bytes) {
  return dst_write(file, buffer, bytes);
}
static void* sys_alloc(struct mspack_system* self, size_t bytes) {
  (void)self;
  return calloc(bytes, 1);
}
static void sys_free(void* p) { free(p); }
static void sys_copy(void* src, void* dst, size_t bytes) { memmove(dst, src, bytes); }
static void sys_msg(struct mspack_file* file, const char* format, ...) {
  (void)file;
  (void)format;
}

static unsigned int be32(const unsigned char* p) {
  return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <in.xxx> <out.upk>\n", argv[0]);
    return 2;
  }
  FILE* in = fopen(argv[1], "rb");
  if (!in) return 3;
  fseek(in, 0, SEEK_END);
  long insize = ftell(in);
  fseek(in, 0, SEEK_SET);
  unsigned char* data = (unsigned char*)malloc((size_t)insize);
  if (fread(data, 1, (size_t)insize, in) != (size_t)insize) return 4;
  fclose(in);
  if (be32(data) != 0x9E2A83C1u) {
    fprintf(stderr, "bad tag\n");
    return 5;
  }
  unsigned int block = be32(data + 4);
  unsigned int total_c = be32(data + 8);
  unsigned int total_u = be32(data + 12);
  unsigned int nblocks = (total_u + block - 1) / block;
  printf("block=0x%X compressed=%u uncompressed=%u blocks=%u\n", block, total_c, total_u, nblocks);
  unsigned char* out = (unsigned char*)malloc(total_u);
  size_t hdr = 16;
  size_t datapos = hdr + (size_t)nblocks * 8;
  size_t outpos = 0;

  struct mspack_system sys;
  memset(&sys, 0, sizeof(sys));
  sys.read = rw_dispatch_read;
  sys.write = rw_dispatch_write;
  sys.alloc = sys_alloc;
  sys.free = sys_free;
  sys.copy = sys_copy;
  sys.message = sys_msg;

  for (unsigned int b = 0; b < nblocks; ++b) {
    unsigned int csize = be32(data + hdr + b * 8);
    unsigned int usize = be32(data + hdr + b * 8 + 4);
    src_file src = {data + datapos, csize, 0, 0};
    dst_file dst = {out + outpos, usize, 0};
    struct lzxd_stream* lzx =
        lzxd_init(&sys, (struct mspack_file*)&src, (struct mspack_file*)&dst, 17, 0, 0x40000, usize, 0);
    if (!lzx) {
      fprintf(stderr, "lzxd_init failed at block %u\n", b);
      return 6;
    }
    int r = lzxd_decompress(lzx, usize);
    lzxd_free(lzx);
    if (r != MSPACK_ERR_OK || dst.pos != usize) {
      fprintf(stderr, "block %u: lzxd_decompress=%d wrote %zu of %u\n", b, r, dst.pos, usize);
      return 7;
    }
    datapos += csize;
    outpos += usize;
  }
  FILE* o = fopen(argv[2], "wb");
  if (!o) return 8;
  fwrite(out, 1, outpos, o);
  fclose(o);
  printf("wrote %zu bytes\n", outpos);
  return 0;
}
