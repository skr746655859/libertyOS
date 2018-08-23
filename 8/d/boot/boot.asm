;===================================================
;　boot.asm
; 编译方法: nasm boot.asm -o boot.bin
;===================================================

; 常量------------------------------------------------------
ButtomOfStack	equ	7c00h		; 栈底地址

%include	"load.inc"
;-----------------------------------------------------------
	
	org	7c00h
	jmp short LABEL_START		; Start to boot.
	nop				; 这个 nop 不可少

%include	"fat12hdr.inc"

LABEL_START:
	mov	ax, cs
	mov	ds, ax
	mov	ss, ax
	mov	sp, ButtomOfStack

	mov	ax, LoaderSeg
	mov	es, ax

	; 清屏
	mov	ax, 0600h
	mov	bh, 07h
	mov	cx, 0
	mov	dx, 3250h
	int	10h

	mov	al, 2			; 'Booting  '
	mov	dh, 0			;　行号 0
	call	DispMsg

	; 0 号软驱(A盘)复位
	xor	ah, ah
	xor	dl, dl
	int	13h

	; 读 A 盘的根目录区(共14个扇区)至缓冲区
	push	BaseOfRootBuf
	push	RootDirSecCnt
	push	RootDirSecNo
	call	ReadSector
	add	sp, 6

	; 遍历根目录区的条目, 匹配文件名
	; 外层循环遍历根目录区的条目
	; 内层循环遍历字符串 DIR_Name

	cld
	mov	si, BaseOfRootBuf
.strcmp:
	mov	di, LoaderFileName
	mov	cx, DirNameLen		; 内层循环次数
.continue:
	lodsb				; AL <- (DS:SI), 并令 SI++
	cmp	al, byte [di]
	jnz	.next	
	inc	di
	loop	.continue
.found:
	sub	si, DirNameLen		; 使 SI 指向当前条目开头
	add	si, OffsetFstClus	; 使 SI 指向 DIR_FstClus
	mov	ax, [si]
	mov	[LoaderClusNo], ax	; 读取 DIR_FstClus 并保存

	call	LoadLoader		; 加载 LOADER.BIN
	mov	al, 0			; 'Ready    '
	mov	dh, 1			; 行号 1
	call	DispMsg
	jmp	LoaderSeg:LoaderOff	; 转交控制权, MBR 的使命结束
.next:
	dec	byte [LoopCnt]
	cmp	byte [LoopCnt], 0
	jz	.notfound
	; 令 SI 指向下一个条目开头
	and	si, 0FFF0h		; 将 SI 低 4 位清零, 使其指向当前条目开头
	add	si, 20h
	jmp	.strcmp
.notfound:
	mov	al, 1			; 'No loader!'
	mov	dh, 1			; 行号 1
	call	DispMsg
	jmp	$

;-----------------------------------------------------------
; ReadSector(SecNo, SecCnt, pBuf)
; 功能: 读磁盘扇区至缓冲区
; 参数:
; 起始扇区号:	SecNo, 16 bits
; 要读的扇区数:	SecCnt, 16 bits (lower 8 bits avaliable)
; 缓冲区地址:	pBuf, 16 bits (段地址在 es 中)
;-----------------------------------------------------------
ReadSector:
	push	bp
	mov	bp, sp

	mov	ax, [bp+4]		; SecNo
	mov	bl, [BPB_SecPerTrk]	; 每磁道扇区数
	div	bl			; 16 bits 被除数 AX, 8 bits 除数 BL
					; AH = 余数, AL = 商
	mov	ch, al
	shr	ch, 1			; CH = 柱面(磁道)号 = (AL >> 1)
	mov	dh, al
	and	dh, 1			; DH = 磁头号 = (AL & 1)
	mov	cl, ah
	inc	cl			; CL = 起始扇区号 = (AH + 1)
	mov	dl, 0			; DL = 驱动器号 (0号软驱, A盘)
	mov	al, byte [bp+6]		; SecCnt
	mov	bx, [bp+8]		; pBuf
	mov	ah, 02h			; AH = 功能号 (02h, 读磁盘扇区)
	int	13h

	pop	bp
	ret
;-----------------------------------------------------------


;-----------------------------------------------------------
; LoadLoader 加载 LOADER.BIN
;-----------------------------------------------------------
LoadLoader:
	mov	word [ppDst], LoaderOff

	; 加载 LOADER.BIN 的第 1 个簇
	push	word [ppDst]		; pDst
	push	word [LoaderClusNo]	; ClusNo
	call	CopyCode
	add	sp, 4
	add	word [ppDst], 200h	; BPB_BytsPerSec * BPB_SecPerClus = 200h

	; 如果 LOADER.BIN 跨越多个簇, 需要通过 FAT 表查找之

	; 读取 FAT1 到 BaseOfBuffer
	push	BaseOfFatBuf
	push	word [BPB_FATSz16]
	push	Fat1SecNo
	call	ReadSector
	add	sp, 6

.begin:
	mov	ah, 0Eh			;　每读一个簇(这里, 1 个簇包含 1 个扇区)就打印一个 '.',
	mov	al, '.'			; 形成如下效果:
	mov	bx, 0007h		; 	Booting  .....
	int	10h			;

	; 判断簇号的奇偶性
	mov	ax, [LoaderClusNo]
	push	ax			; save ax
	mov	bl, 2
	div	bl			; AH = 余数, AL = 商
	mov	[Reminder], ah
	pop	ax			; resume ax
	mov	bl, 2
	div	bl
	mov	bl, 3
	mul	bl			; AX = AL * BL
	add	ax, BaseOfFatBuf
	push	ax			; save ax
	cmp	byte [Reminder], 0
	jnz	.odd
.even: ; 簇号为偶数
	inc	ax
	mov	si, ax
	mov	al, byte [si]
	and	ax, 0Fh
	shl	ax, 8
	pop	bx			; resume ax in bx
	mov	bl, byte [bx]
	and	bx, 0FFh
	or	ax, bx
	cmp	ax, 0FFFh		; 结束标志 0FFFh
	jz	.end
	mov	[LoaderClusNo], ax	; 保存下一个簇号

	; 加载 LOADER.BIN 的下一个簇
	push	word [ppDst]		; pDst
	push	ax			; ClusNo
	call	CopyCode
	add	sp, 4
	add	word [ppDst], 200h	; BPB_BytsPerSec * BPB_SecPerClus = 200h

	jmp	.begin

.odd: ; 簇号为奇数
	add	ax, 2
	mov	si, ax
	mov	al, byte [si]
	and	ax, 00FFh
	shl	ax, 4
	pop	bx
	inc	bx
	mov	bl, byte [bx]
	and	bx, 00FFh
	shr	bx, 4
	or	ax, bx
	cmp	ax, 0FFFh
	jz	.end
	mov	[LoaderClusNo], ax	; 保存下一个簇号

	push	word [ppDst]		; pDst
	push	ax			; Clus
	call	CopyCode
	add	sp, 4
	add	word [ppDst], 200h	; BPB_BytsPerSec * BPB_SecPerClus = 200h

	jmp	.begin

.end:
	ret

;-----------------------------------------------------------


;-----------------------------------------------------------
; CopyCode (ClusNo, es:pDst)
; ClusNo	簇号
; es:pDst	目标地址
;-----------------------------------------------------------
CopyCode:
	push	bp
	mov	bp, sp

	; 将 ClusNo 对应扇区的数据读到缓冲区
	; SecNo = (ClusNo - 2) + DataSecNo = ClusNo + 31

	mov	ax, [bp+4]		; ClusNo
	add	ax, 31			; SecNo
	xor	cx, cx
	mov	cl, [BPB_SecPerClus]

	push	word [bp+6]		; pBuf
	push	cx			; SecCnt
	push	ax			; SecNo
	call	ReadSector
	add	sp, 6

	pop	bp
	ret	
;-----------------------------------------------------------


;-----------------------------------------------------------
; DispMsg 打印字符串
; al = 字符串在 MsgBase 里的索引
; dh = row
;-----------------------------------------------------------
DispMsg:
	mov	bl, MsgLen
	mul	bl			; AX = AL * BL
	add	ax, MsgBase
	mov	bp, ax			; ES:BP = 串地址
	mov	cx, MsgLen		; CX = 串长度
	mov	ax, 01301h		; AH = 13h  AL = 01h
	mov	bx, 0007h		; 页号为0(BH = 0) 黑底灰字(BL = 07h,高亮)
	mov	dl, 0			; CL = column
	int	10h
	ret
;-----------------------------------------------------------

; 变量------------------------------------------------------
LoaderFileName:	db 'LOADER  BIN'
LoaderClusNo:	dw 0
Reminder:	db 0
ppDst:		dw 0
LoopCnt:	db RootDirItemCnt

MsgBase:	; 以下字符串固定长度为 13
MsgLen		equ	9
Msg0:		db 'Ready    '
Msg1:		db 'No loader'
Msg2:		db 'Booting  '
;-----------------------------------------------------------

times 	510-($-$$)	db	0	; 填充剩下的空间，使生成的二进制代码恰好为512字节
dw 	0xaa55				; 结束标志
