	.cpu	68030
RELOC_DEST	.equ	0xbf0000
kernel_dest	.equ	0x1000
	.text
	.globl	_start_kernel_030
_start_kernel_030:
	move.w	sr,d0
	or	#0x0700,d0
	move.w	d0,sr
	lea	reloc_main(pc), a1
	move.l	#RELOC_DEST, a2
	move.l	#reloc_size, d0
	lsr.l	#2, d0
	subq.l	#1, d0
1:	move.l	(a1)+,(a2)+
	dbra	d0,1b
	jmp	RELOC_DEST

reloc_main:
	move.l	4(sp),d0
	move.l	d0,a0
	move.l	8(sp),d0
	lsr.l	#2,d0
	subq.l	#1,d0
	move.l	#kernel_dest,a1
1:	move.l	(a0)+,(a1)+
	subq.l	#1,d0
	bne	1b
	jmp	kernel_dest
reloc_size:	.equ	$ - reloc_main

	.end
