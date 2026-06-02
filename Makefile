.SUFFIXES:
.DELETE_ON_ERROR:

override IMAGE_NAME := moose
override KERNEL_NAME := moose-kernel

MAKEFLAGS += --no-print-directory

V ?= 0
Q := @
ifeq ($(V),1)
Q :=
endif

QEMUFLAGS := -m 2G -serial stdio

HOST_CC := cc
HOST_CFLAGS := -g -O2 -pipe
HOST_CPPFLAGS :=
HOST_LDFLAGS :=
HOST_LIBS :=

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: all-hdd
all-hdd: $(IMAGE_NAME).hdd

.PHONY: run
run: $(IMAGE_NAME).iso
	@printf "  %-7s %s\n" QEMU "$(IMAGE_NAME).iso"
	$(Q)qemu-system-x86_64 \
		-M q35 \
		-cdrom $(IMAGE_NAME).iso \
		-boot d \
		$(QEMUFLAGS)

.PHONY: run-uefi
run-uefi: edk2-ovmf-bins $(IMAGE_NAME).iso
	@printf "  %-7s %s\n" QEMU "$(IMAGE_NAME).iso"
	$(Q)qemu-system-x86_64 \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		-boot d \
		$(QEMUFLAGS)

.PHONY: run-hdd
run-hdd: $(IMAGE_NAME).hdd
	@printf "  %-7s %s\n" QEMU "$(IMAGE_NAME).hdd"
	$(Q)qemu-system-x86_64 \
		-M q35 \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: run-hdd-uefi
run-hdd-uefi: edk2-ovmf-bins $(IMAGE_NAME).hdd
	@printf "  %-7s %s\n" QEMU "$(IMAGE_NAME).hdd"
	$(Q)qemu-system-x86_64 \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

edk2-ovmf-bins:
	@printf "  %-7s %s\n" FETCH "edk2-ovmf-bins"
	$(Q)curl -fsSL https://github.com/osdev0/edk2-ovmf-stable-bins/releases/latest/download/edk2-ovmf-bins.tar.gz | gunzip | tar -xf -

limine-binary/limine:
	@printf "  %-7s %s\n" FETCH "limine-binary"
	$(Q)rm -rf limine-binary
	$(Q)curl -fsSL https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz | gunzip | tar -xf -
	@printf "  %-7s %s\n" MAKE "limine-binary"
	$(Q)$(MAKE) -C limine-binary \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)" >.limine-build.log 2>&1 || { cat .limine-build.log; rm -f .limine-build.log; exit 1; }
	$(Q)rm -f .limine-build.log

kernel/.deps-obtained:
	@printf "  %-7s %s\n" DEPS "kernel"
	$(Q)./kernel/get-deps

.PHONY: kernel
kernel: kernel/.deps-obtained
	$(Q)$(MAKE) -C kernel

$(IMAGE_NAME).iso: limine-binary/limine limine.conf kernel rootfs
	@printf "  %-7s %s\n" GEN "iso_root"
	$(Q)rm -rf iso_root
	$(Q)mkdir -p iso_root/boot
	$(Q)mkdir -p iso_root/boot/limine
	$(Q)mkdir -p iso_root/EFI/BOOT

	@printf "  %-7s %s\n" CP "kernel/bin/$(KERNEL_NAME)"
	$(Q)cp kernel/bin/$(KERNEL_NAME) iso_root/boot/

	@printf "  %-7s %s\n" CP "rootfs/"
	$(Q)cp -r rootfs/* iso_root/
	
	@printf "  %-7s %s\n" CP "limine files"
	$(Q)cp limine.conf \
		limine-binary/limine-bios.sys \
		limine-binary/limine-bios-cd.bin \
		limine-binary/limine-uefi-cd.bin \
		iso_root/boot/limine/

	@printf "  %-7s %s\n" CP "EFI files"
	$(Q)cp limine-binary/BOOTX64.EFI iso_root/EFI/BOOT/
	$(Q)cp limine-binary/BOOTIA32.EFI iso_root/EFI/BOOT/

	@printf "  %-7s %s\n" MKISO "$(IMAGE_NAME).iso"
	$(Q)xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso >.xorriso.log 2>&1 || { cat .xorriso.log; rm -f .xorriso.log; exit 1; }
	$(Q)rm -f .xorriso.log

	@printf "  %-7s %s\n" LIMINE "$(IMAGE_NAME).iso"
	$(Q)./limine-binary/limine bios-install $(IMAGE_NAME).iso >.limine-iso.log 2>&1 || { cat .limine-iso.log; rm -f .limine-iso.log; exit 1; }
	$(Q)rm -f .limine-iso.log

	@printf "  %-7s %s\n" CLEAN "iso_root"
	$(Q)rm -rf iso_root

# todo: make hdd copy over rootfs (idk mtools that good)
$(IMAGE_NAME).hdd: limine-binary/limine limine.conf kernel 
	@printf "  %-7s %s\n" HDD "$(IMAGE_NAME).hdd"
	$(Q)rm -f $(IMAGE_NAME).hdd
	$(Q)dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME).hdd status=none

	@printf "  %-7s %s\n" SGDISK "$(IMAGE_NAME).hdd"
	$(Q)PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00 -m 1 >/dev/null

	@printf "  %-7s %s\n" LIMINE "$(IMAGE_NAME).hdd"
	$(Q)./limine-binary/limine bios-install $(IMAGE_NAME).hdd >.limine-hdd.log 2>&1 || { cat .limine-hdd.log; rm -f .limine-hdd.log; exit 1; }
	$(Q)rm -f .limine-hdd.log

	@printf "  %-7s %s\n" MFORMAT "$(IMAGE_NAME).hdd"
	$(Q)mformat -i $(IMAGE_NAME).hdd@@1M

	@printf "  %-7s %s\n" MMD "$(IMAGE_NAME).hdd"
	$(Q)mmd -i $(IMAGE_NAME).hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine

	@printf "  %-7s %s\n" MCOPY "kernel/bin/$(KERNEL_NAME)"
	$(Q)mcopy -i $(IMAGE_NAME).hdd@@1M kernel/bin/$(KERNEL_NAME) ::/boot

	@printf "  %-7s %s\n" MCOPY "limine files"
	$(Q)mcopy -i $(IMAGE_NAME).hdd@@1M limine.conf limine-binary/limine-bios.sys ::/boot/limine

	@printf "  %-7s %s\n" MCOPY "EFI files"
	$(Q)mcopy -i $(IMAGE_NAME).hdd@@1M limine-binary/BOOTX64.EFI ::/EFI/BOOT
	$(Q)mcopy -i $(IMAGE_NAME).hdd@@1M limine-binary/BOOTIA32.EFI ::/EFI/BOOT

.PHONY: clean
clean:
	$(Q)$(MAKE) -C kernel clean
	@printf "  %-7s %s\n" CLEAN "root"
	$(Q)rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd
	$(Q)rm -f .xorriso.log .limine-build.log .limine-iso.log .limine-hdd.log

.PHONY: distclean
distclean:
	$(Q)$(MAKE) -C kernel distclean
	@printf "  %-7s %s\n" CLEAN "root"
	$(Q)rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd
	$(Q)rm -f .xorriso.log .limine-build.log .limine-iso.log .limine-hdd.log
	@printf "  %-7s %s\n" DIST "root"
	$(Q)rm -rf limine-binary edk2-ovmf-bins