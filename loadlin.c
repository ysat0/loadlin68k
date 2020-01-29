#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iocslib.h>
#include <errno.h>

/*
 * gzip declarations
 */

#define OF(args)  args
#define STATIC static
#define INIT

#define PREBOOT
/* Pre-boot environment: included */

/* prevent inclusion of _LINUX_KERNEL_H in pre-boot environment: lots
 * errors about console_printk etc... on ARM */
#define _LINUX_KERNEL_H

#include "zconf.h"

typedef unsigned char u8;

#include "inftrees.c"
#include "inffast.c"
#include "inflate.c"

#define GZIP_IOBUF_SIZE (16*1024)

static long nofill(void *buf, unsigned long size)
{
	return -1;
}

/* Included from initramfs et al code */
STATIC int INIT __gunzip(unsigned char *buf, long len,
		       long (*fill)(void*, unsigned long),
		       long (*flush)(void*, unsigned long),
		       unsigned char *out_buf, long out_len,
		       long *pos,
		       void(*error)(char *x)) {
	u8 *zbuf;
	struct z_stream_s *strm;
	int rc;

	rc = -1;
	if (flush) {
		out_len = 0x8000; /* 32 K */
		out_buf = malloc(out_len);
	} else {
		if (!out_len)
			out_len = ((size_t)~0) - (size_t)out_buf; /* no limit */
	}
	if (!out_buf) {
		error("Out of memory while allocating output buffer");
		goto gunzip_nomem1;
	}

	if (buf)
		zbuf = buf;
	else {
		zbuf = malloc(GZIP_IOBUF_SIZE);
		len = 0;
	}
	if (!zbuf) {
		error("Out of memory while allocating input buffer");
		goto gunzip_nomem2;
	}

	strm = malloc(sizeof(*strm));
	if (strm == NULL) {
		error("Out of memory while allocating z_stream");
		goto gunzip_nomem3;
	}

	strm->workspace = malloc(flush ? zlib_inflate_workspacesize() :
				 sizeof(struct inflate_state));
	if (strm->workspace == NULL) {
		error("Out of memory while allocating workspace");
		goto gunzip_nomem4;
	}

	if (!fill)
		fill = nofill;

	if (len == 0)
		len = fill(zbuf, GZIP_IOBUF_SIZE);

	/* verify the gzip header */
	if (len < 10 ||
	   zbuf[0] != 0x1f || zbuf[1] != 0x8b || zbuf[2] != 0x08) {
		if (pos)
			*pos = 0;
		error("Not a gzip file");
		goto gunzip_5;
	}

	/* skip over gzip header (1f,8b,08... 10 bytes total +
	 * possible asciz filename)
	 */
	strm->next_in = zbuf + 10;
	strm->avail_in = len - 10;
	/* skip over asciz filename */
	if (zbuf[3] & 0x8) {
		do {
			/*
			 * If the filename doesn't fit into the buffer,
			 * the file is very probably corrupt. Don't try
			 * to read more data.
			 */
			if (strm->avail_in == 0) {
				error("header error");
				goto gunzip_5;
			}
			--strm->avail_in;
		} while (*strm->next_in++);
	}

	strm->next_out = out_buf;
	strm->avail_out = out_len;

	rc = zlib_inflateInit2(strm, -MAX_WBITS);

	if (!flush) {
		WS(strm)->inflate_state.wsize = 0;
		WS(strm)->inflate_state.window = NULL;
	}

	while (rc == Z_OK) {
		if (strm->avail_in == 0) {
			/* TODO: handle case where both pos and fill are set */
			len = fill(zbuf, GZIP_IOBUF_SIZE);
			if (len < 0) {
				rc = -1;
				error("read error");
				break;
			}
			strm->next_in = zbuf;
			strm->avail_in = len;
		}
		rc = zlib_inflate(strm, 0);

		/* Write any data generated */
		if (flush && strm->next_out > out_buf) {
			long l = strm->next_out - out_buf;
			if (l != flush(out_buf, l)) {
				rc = -1;
				error("write error");
				break;
			}
			strm->next_out = out_buf;
			strm->avail_out = out_len;
		}

		/* after Z_FINISH, only Z_STREAM_END is "we unpacked it all" */
		if (rc == Z_STREAM_END) {
			rc = 0;
			break;
		} else if (rc != Z_OK) {
			error("uncompression error");
			rc = -1;
		}
	}

	zlib_inflateEnd(strm);
	if (pos)
		/* add + 8 to skip over trailer */
		*pos = strm->next_in - zbuf+8;

gunzip_5:
	free(strm->workspace);
gunzip_nomem4:
	free(strm);
gunzip_nomem3:
	if (!buf)
		free(zbuf);
gunzip_nomem2:
	if (flush)
		free(out_buf);
gunzip_nomem1:
	return rc; /* returns Z_OK (0) if successful */
}

#ifndef PREBOOT
STATIC int INIT gunzip(unsigned char *buf, long len,
		       long (*fill)(void*, unsigned long),
		       long (*flush)(void*, unsigned long),
		       unsigned char *out_buf,
		       long *pos,
		       void (*error)(char *x))
{
	return __gunzip(buf, len, fill, flush, out_buf, 0, pos, error);
}
#else
STATIC int INIT __decompress(unsigned char *buf, long len,
			   long (*fill)(void*, unsigned long),
			   long (*flush)(void*, unsigned long),
			   unsigned char *out_buf, long out_len,
			   long *pos,
			   void (*error)(char *x))
{
	return __gunzip(buf, len, fill, flush, out_buf, out_len, pos, error);
}
#endif

FILE *stream_fp;
char *kernel;
char *raw_ptr;
unsigned long raw_size;

static void error(char *x)
{
	puts(x);
	exit(1);
}

static long read_stream(void *buf, unsigned long size)
{
	return fread(buf, 1, size, stream_fp);
}

static long write_raw(void *buf, unsigned long size)
{
	memcpy(raw_ptr, buf, size);
	raw_ptr += size;
	raw_size += size;
	fputc('.', stderr);
	return size;
}

#define KERNEL_SIZE 0x00200000

void decompress_kernel(char *name, char *load)
{
	FILE *fp;
	stream_fp = fopen(name, "rb");
	if (stream_fp == NULL) {
		fprintf(stderr, "\n%s open failed %d\n", name, errno);
		exit(1);
	}
	raw_ptr = load;
	raw_size = 0;
	__decompress(NULL, 0, read_stream, write_raw, NULL, 0, NULL, error);
	fclose(stream_fp);
	fputs("ok", stderr);
}

void start_kernel_000(void *kernel, unsigned long size, char *args);
void start_kernel_030(void *kernel, unsigned long size);

struct bi_record {
	unsigned short tag;			/* tag ID */
	unsigned short size;			/* size of record (in bytes) */
	unsigned long data[0];			/* data */
};

struct mem_info {
	unsigned long addr;			/* physical address of memory chunk */
	unsigned long size;			/* length of memory chunk (in bytes) */
};

#define BI_LAST			0x0000	/* last record (sentinel) */
#define BI_MACHTYPE		0x0001	/* machine type (__be32) */
#define BI_CPUTYPE		0x0002	/* cpu type (__be32) */
#define BI_FPUTYPE		0x0003	/* fpu type (__be32) */
#define BI_MMUTYPE		0x0004	/* mmu type (__be32) */
#define BI_MEMCHUNK		0x0005	/* memory chunk address and size */
					/* (struct mem_info) */
#define BI_RAMDISK		0x0006	/* ramdisk address and size */
					/* (struct mem_info) */
#define BI_COMMAND_LINE		0x0007	/* kernel command line parameters */
					/* (string) */

#define MACH_X68000		14

#define CPUB_68030		1
#define CPUB_68040		2
#define CPUB_68060		3

#define CPU_68030		(1 << CPUB_68030)
#define CPU_68040		(1 << CPUB_68040)
#define CPU_68060		(1 << CPUB_68060)

#define MMUB_68030		1	/* Internal MMU */
#define MMUB_68040		2	/* Internal MMU */
#define MMUB_68060		3	/* Internal MMU */

#define MMU_68030		(1 << MMUB_68030)
#define MMU_68040		(1 << MMUB_68040)
#define MMU_68060		(1 << MMUB_68060)

#define BI(_tag, _size)			\
	do {					\
		bi = bi_ptr;			\
		bi->tag = _tag;			\
		bi->size = _size;		\
		bi_ptr += 6 + size;		\
	} while(0)

unsigned long build_bootinfo(void *kernel, unsigned long size, char *args)
{
	void *bi_ptr = kernel + size;
	struct bi_record *bi;
	char *cmdline;
	BI(BI_MACHTYPE, 4);
	*(unsigned int *)bi->data[0] = MACH_X68000;
	BI(BI_FPUTYPE, 4);
	*(unsigned int *)bi->data[0] = 0;
	BI(BI_MMUTYPE, 4);
	*(unsigned int *)bi->data[0] = MMU_68030;
	BI(BI_CPUTYPE, 4);
	*(unsigned int *)bi->data[0] = CPU_68030;
	bi = bi_ptr;
	bi->tag = BI_MEMCHUNK;
	bi->size = 8;
	bi->data[0] = 0x00000000;
	bi->data[1] = 0x00c00000;
	bi = bi_ptr + 14;
	bi->tag = BI_COMMAND_LINE;
	bi->size = 4;
	cmdline = bi_ptr + 10 + 6;
	bi->data[0] = (unsigned long)cmdline;
	bi = bi_ptr + 10;
	BI(BI_LAST, 0);
	strcpy(cmdline, args);
	return bi_ptr - kernel + 256;
}

int main(int argc, char *argv[])
{
	char args[512];
	int i;
	int video = 0;
	int cpu;
	int sp;

	if (argc < 2) {
		printf("loadlin <kernel> <bootargs> ...\n");
		exit(1);
	}
	kernel = malloc(KERNEL_SIZE);
	if (kernel == NULL) {
		fprintf(stderr, "malloc failed\n");
		exit(1);
	}
	fprintf(stderr, "Uncompressing kernel");
	decompress_kernel(argv[1], kernel);
	fprintf(stderr, "\nKernel size=%d\n", raw_size);
	memset(args, 0, sizeof(args));
	for (i = 2; i < argc; i++) {
		if (strncmp(argv[i], "video", 5) == 0)
			video = 1;
		strcat(args, argv[i]);
		strcat(args, " ");
	}
	fprintf(stderr, "Kernel args=%s", args);
	if (video) {
		fprintf(stderr, "\x1b[>5h");
		_iocs_crtmod(0x10);
		_iocs_g_clr_on();
	}
	sp = _dos_super(0);
	*(volatile char *)0xe8801d = 0x00;
	*(volatile char *)0xe98001 = 9;
	*(volatile char *)0xe98001 = 0x40;
	cpu = *(volatile char *)0xcbc;
	switch (cpu) {
	case 0:
		start_kernel_000(kernel, raw_size, args);
		break;
	case 3:
		raw_size += build_bootinfo(kernel, raw_size, args);
		start_kernel_030(kernel, raw_size);
		break;
	default:
		fprintf(stderr, "Unsupported CPU\n");
		_dos_super(sp);
	}
	return 0;
}
