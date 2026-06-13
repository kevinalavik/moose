#ifndef DEV_DEBUGCON_H
#define DEV_DEBUGCON_H

#define DEBUGCON_IO_PORT 0xE9

/* QEMU/bochs debugcon driver */
void dbg_write(const char *str); /* writes NULL terminated string to DEBUGCON_IO_PORT */

#endif // DEV_DEBUGCON_H