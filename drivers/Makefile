drivers-y :=
drivers-m :=

drivers-y += ../drivers/bus/pci.c
drivers-y += ../drivers/bus/acpi_platform.c
drivers-$(CONFIG_DRIVER_I8042_KBD) += ../drivers/input/keyboard/i8042_kbd.c

DRIVER_CFILES := $(drivers-y)
