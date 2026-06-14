#include "limine.h"
#include <boot/limine.h>

__attribute__((used, section(".limine_requests"))) volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((
    used,
    section(".limine_requests_start"))) static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;
/* LIMINE REQUESTS*/

__attribute__((
    used, section(".limine_requests"))) volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

__attribute__((
    used, section(".limine_requests"))) volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

__attribute__((used,
               section(".limine_requests"))) volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0};

__attribute__((used, section(".limine_requests"))) volatile struct limine_executable_address_request
    kaddr_request = {.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 0};

__attribute__((used,
               section(".limine_requests"))) volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0};

__attribute__((used, section(".limine_requests"))) volatile struct limine_executable_cmdline_request
    cmdline_request = {.id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, .revision = 0};

/* LIMINE REQUESTS END */
__attribute__((
    used, section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;
