#include <stddef.h>
#include <stdint.h>

int hci_drv_read_sync(uint8_t *dst, size_t len);
int hci_drv_write_sync(uint8_t *src, size_t len);
