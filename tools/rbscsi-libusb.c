/*
 * rbscsi backend over libusb, for systems where Rockbox's rbscsi has none
 * (macOS). It implements the rbscsi.h API by speaking USB mass storage
 * Bulk-Only Transport directly: a 31-byte command block wrapper (CBW)
 * carrying the SCSI CDB, the data phase, then a 13-byte status wrapper
 * (CSW). The kernel's mass storage driver has to let go of the device
 * first, which on macOS needs root.
 *
 * Device path: "VVVV:PPPP" (USB vendor:product, hex); empty or "-" selects
 * the Philips GoGear Raga, 0471:20e3.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include "rbscsi.h"

#define DEFAULT_VID 0x0471
#define DEFAULT_PID 0x20e3

#define CBW_SIGNATURE 0x43425355u
#define CSW_SIGNATURE 0x53425355u
#define CBW_SIZE 31
#define CSW_SIZE 13
#define CBW_FLAG_IN 0x80

#define MIN_TIMEOUT_MS 10000 /* firmware writes can be slow */

struct rb_scsi_device_t
{
    libusb_context *ctx;
    libusb_device_handle *handle;
    int iface;
    unsigned char ep_in, ep_out;
    uint32_t tag;
    unsigned flags;
    void *user;
    rb_scsi_printf_t printf;
};

#define dbg(dev, ...) \
    do { if((dev)->flags & RB_SCSI_DEBUG) (dev)->printf((dev)->user, __VA_ARGS__); } while(0)

static void std_printf(void *user, const char *fmt, ...)
{
    (void) user;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static uint32_t get32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Finds the mass storage Bulk-Only interface and its two bulk endpoints. */
static int find_interface(struct rb_scsi_device_t *dev)
{
    struct libusb_config_descriptor *cfg;
    if(libusb_get_active_config_descriptor(libusb_get_device(dev->handle), &cfg))
        return -1;
    int found = -1;
    for(int i = 0; i < cfg->bNumInterfaces && found < 0; i++)
    {
        const struct libusb_interface_descriptor *alt = &cfg->interface[i].altsetting[0];
        if(alt->bInterfaceClass != LIBUSB_CLASS_MASS_STORAGE || alt->bInterfaceProtocol != 0x50)
            continue;
        dev->ep_in = dev->ep_out = 0;
        for(int e = 0; e < alt->bNumEndpoints; e++)
        {
            const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
            if((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
                continue;
            if(ep->bEndpointAddress & LIBUSB_ENDPOINT_IN)
                dev->ep_in = ep->bEndpointAddress;
            else
                dev->ep_out = ep->bEndpointAddress;
        }
        if(dev->ep_in && dev->ep_out)
            found = alt->bInterfaceNumber;
    }
    libusb_free_config_descriptor(cfg);
    return found;
}

rb_scsi_device_t rb_scsi_open(const char *path, unsigned flags, void *user,
    rb_scsi_printf_t printf)
{
    struct rb_scsi_device_t *dev = calloc(1, sizeof(*dev));
    unsigned vid = DEFAULT_VID, pid = DEFAULT_PID;
    int ret;

    dev->flags = flags;
    dev->user = user;
    dev->printf = printf ? printf : std_printf;
    if(path && *path && strcmp(path, "-") && sscanf(path, "%x:%x", &vid, &pid) != 2)
    {
        dev->printf(user, "rb_scsi: device must be given as VVVV:PPPP, not '%s'\n", path);
        goto fail;
    }
    if(libusb_init(&dev->ctx))
        goto fail;
    dev->handle = libusb_open_device_with_vid_pid(dev->ctx, vid, pid);
    if(dev->handle == NULL)
    {
        dev->printf(user, "rb_scsi: no USB device %04x:%04x (or no permission: use sudo)\n", vid, pid);
        goto fail;
    }
    dev->iface = find_interface(dev);
    if(dev->iface < 0)
    {
        dev->printf(user, "rb_scsi: %04x:%04x has no mass storage bulk-only interface\n", vid, pid);
        goto fail;
    }
    libusb_set_auto_detach_kernel_driver(dev->handle, 1);
    ret = libusb_claim_interface(dev->handle, dev->iface);
    if(ret)
    {
        dev->printf(user, "rb_scsi: cannot claim the interface: %s%s\n", libusb_strerror(ret),
            ret == LIBUSB_ERROR_ACCESS || ret == LIBUSB_ERROR_BUSY ? " (run with sudo)" : "");
        goto fail;
    }
    dbg(dev, "rb_scsi: %04x:%04x interface %d, endpoints in %02x out %02x\n",
        vid, pid, dev->iface, dev->ep_in, dev->ep_out);
    return dev;

fail:
    if(dev->handle)
        libusb_close(dev->handle);
    if(dev->ctx)
        libusb_exit(dev->ctx);
    free(dev);
    return NULL;
}

/* Bulk transfer that clears a stalled endpoint once and retries. */
static int bulk(struct rb_scsi_device_t *dev, unsigned char ep, unsigned char *buf,
    int len, int *done, unsigned tmo)
{
    int ret = libusb_bulk_transfer(dev->handle, ep, buf, len, done, tmo);
    if(ret == LIBUSB_ERROR_PIPE)
    {
        libusb_clear_halt(dev->handle, ep);
        ret = libusb_bulk_transfer(dev->handle, ep, buf, len, done, tmo);
    }
    return ret;
}

/* Bulk-Only Mass Storage Reset, after a phase error or a broken transfer. */
static void reset_recovery(struct rb_scsi_device_t *dev)
{
    libusb_control_transfer(dev->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE, 0xff, 0, dev->iface,
        NULL, 0, 1000);
    libusb_clear_halt(dev->handle, dev->ep_in);
    libusb_clear_halt(dev->handle, dev->ep_out);
}

/* One command through the CBW / data / CSW cycle. *len is updated with the
 * bytes actually transferred; returns the CSW status (0 good, 1 failed,
 * 2 phase error) or a negative libusb error. */
static int bot_command(struct rb_scsi_device_t *dev, int dir, const void *cdb, int cdb_len,
    void *buf, int *len, unsigned tmo)
{
    unsigned char cbw[CBW_SIZE] = {0}, csw[CSW_SIZE];
    uint32_t tag = ++dev->tag;
    int done, ret;

    if(cdb_len < 1 || cdb_len > 16)
        return LIBUSB_ERROR_INVALID_PARAM;
    put32(cbw, CBW_SIGNATURE);
    put32(cbw + 4, tag);
    put32(cbw + 8, dir == RB_SCSI_NONE ? 0 : *len);
    cbw[12] = dir == RB_SCSI_READ ? CBW_FLAG_IN : 0;
    cbw[13] = 0; /* LUN */
    cbw[14] = cdb_len;
    memcpy(cbw + 15, cdb, cdb_len);

    ret = bulk(dev, dev->ep_out, cbw, CBW_SIZE, &done, tmo);
    if(ret || done != CBW_SIZE)
        goto broken;

    if(dir != RB_SCSI_NONE && *len > 0)
    {
        unsigned char ep = dir == RB_SCSI_READ ? dev->ep_in : dev->ep_out;
        done = 0;
        ret = libusb_bulk_transfer(dev->handle, ep, buf, *len, &done, tmo);
        /* A stall ends the data phase early (e.g. the device has less data
         * than asked for): clear it and go on to the CSW. No retry here, a
         * retried read would swallow the CSW. */
        if(ret == LIBUSB_ERROR_PIPE)
            libusb_clear_halt(dev->handle, ep);
        else if(ret)
            goto broken;
        *len = done;
    }
    else
        *len = 0;

    ret = bulk(dev, dev->ep_in, csw, CSW_SIZE, &done, tmo);
    if(ret || done != CSW_SIZE || get32(csw) != CSW_SIGNATURE || get32(csw + 4) != tag)
    {
        dbg(dev, "rb_scsi: bad status wrapper (%s)\n", ret ? libusb_strerror(ret) : "contents");
        reset_recovery(dev);
        return ret ? ret : LIBUSB_ERROR_IO;
    }
    if(csw[12] == 2)
        reset_recovery(dev);
    return csw[12];

broken:
    dbg(dev, "rb_scsi: transfer failed: %s\n", libusb_strerror(ret ? ret : LIBUSB_ERROR_IO));
    reset_recovery(dev);
    return ret ? ret : LIBUSB_ERROR_IO;
}

int rb_scsi_raw_xfer(rb_scsi_device_t dev, struct rb_scsi_raw_cmd_t *raw)
{
    unsigned tmo = raw->tmo * 1000 < MIN_TIMEOUT_MS ? MIN_TIMEOUT_MS : raw->tmo * 1000;
    int len = raw->buf_len;
    int ret = bot_command(dev, raw->dir, raw->cdb, raw->cdb_len, raw->buf, &len, tmo);

    raw->buf_len = len;
    if(ret < 0)
    {
        raw->status = ret;
        raw->sense_len = 0;
        return RB_SCSI_ERROR;
    }
    if(ret == 0)
    {
        raw->status = RB_SCSI_GOOD;
        raw->sense_len = 0;
        return RB_SCSI_OK;
    }
    if(ret == 2)
    {
        raw->status = ret;
        raw->sense_len = 0;
        return RB_SCSI_ERROR;
    }

    /* command failed: fetch the sense data, as an OS SCSI layer would */
    raw->status = RB_SCSI_CHECK_CONDITION;
    if(raw->sense == NULL || raw->sense_len <= 0)
        return RB_SCSI_STATUS;
    unsigned char rs[6] = {0x03, 0, 0, 0, raw->sense_len > 252 ? 252 : raw->sense_len, 0};
    int sense_len = rs[4];
    if(bot_command(dev, RB_SCSI_READ, rs, sizeof(rs), raw->sense, &sense_len, tmo) != 0)
    {
        raw->sense_len = 0;
        return RB_SCSI_STATUS;
    }
    raw->sense_len = sense_len;
    return RB_SCSI_SENSE;
}

void rb_scsi_decode_sense(rb_scsi_device_t dev, void *_sense, int sense_len)
{
    static const char *const keys[] = {
        "no sense", "recovered error", "not ready", "medium error", "hardware error",
        "illegal request", "unit attention", "data protect", "blank check",
        "vendor specific", "copy aborted", "aborted command",
    };
    unsigned char *sense = _sense;

    if(sense_len < 3)
        return;
    switch(sense[0] & 0x7f)
    {
        case 0x70: case 0x73: dbg(dev, "Current sense: "); break;
        case 0x71: case 0x72: dbg(dev, "Previous sense: "); break;
        default: dbg(dev, "Unknown sense\n"); return;
    }
    unsigned key = sense[2] & 0xf;
    dbg(dev, "%s\n", key < sizeof(keys) / sizeof(keys[0]) ? keys[key] : "unknown key");
}

void rb_scsi_close(rb_scsi_device_t dev)
{
    if(dev == NULL)
        return;
    libusb_release_interface(dev->handle, dev->iface); /* hands it back to the kernel */
    libusb_close(dev->handle);
    libusb_exit(dev->ctx);
    free(dev);
}

/* Listing is not used by scsitool; the device is chosen by USB id instead. */
struct rb_scsi_devent_t *rb_scsi_list(void)
{
    struct rb_scsi_devent_t *list = calloc(1, sizeof(*list));
    return list;
}

void rb_scsi_free_list(struct rb_scsi_devent_t *list)
{
    free(list);
}
