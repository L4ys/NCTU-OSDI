# Makefile for the simple kernel.
CC	=gcc
AS	=as
LD	=ld
OBJCOPY = objcopy
OBJDUMP = objdump
NM = nm

LDFLAGS = -m elf_i386
CFLAGS = -m32 -Wall -O -fstrength-reduce -fomit-frame-pointer -finline-functions -nostdinc -fno-builtin -fno-stack-protector

# Add debug symbol
CFLAGS += -g
CFLAGS += -I.

OBJDIR = .

.DEFAULT_GOAL := all

include boot/Makefile
include kernel/Makefile

all: boot/boot kernel/system
	dd if=/dev/zero of=$(OBJDIR)/kernel.img count=10000 2>/dev/null
	dd if=$(OBJDIR)/boot/boot of=$(OBJDIR)/kernel.img conv=notrunc 2>/dev/null
	dd if=$(OBJDIR)/kernel/system of=$(OBJDIR)/kernel.img seek=1 conv=notrunc 2>/dev/null

run: all
	qemu-system-i386 -hda kernel.img -monitor stdio

run-ssh: all
	qemu-system-i386 -hda kernel.img -monitor stdio -curses

clean:
	rm -rf $(OBJDIR)/boot/*.o $(OBJDIR)/boot/boot.out $(OBJDIR)/boot/boot $(OBJDIR)/boot/boot.asm
	rm -rf $(OBJDIR)/kernel/*.o $(OBJDIR)/kernel/system* kernel.*
	rm -rf $(OBJDIR)/lib/*.o
