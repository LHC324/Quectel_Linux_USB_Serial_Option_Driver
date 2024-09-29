/*
  Option Card (PCMCIA to) USB to Serial Driver

  Copyright (C) 2005  Matthias Urlichs <smurf@smurf.noris.de>

  This driver is free software; you can redistribute it and/or modify
  it under the terms of Version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  Portions copied from the Keyspan driver by Hugh Blemings <hugh@blemings.org>

  History:

  2005-05-19  v0.1   Initial version, based on incomplete docs
                     and analysis of misbehavior with the standard driver
  2005-05-20  v0.2   Extended the input buffer to avoid losing
                     random 64-byte chunks of data
  2005-05-21  v0.3   implemented chars_in_buffer()
                     turned on low_latency
                     simplified the code somewhat
  2005-05-24  v0.4   option_write() sometimes deadlocked under heavy load
                     removed some dead code
                     added sponsor notice
                     coding style clean-up
  2005-06-20  v0.4.1 add missing braces :-/
                     killed end-of-line whitespace
  2005-07-15  v0.4.2 rename WLAN product to FUSION, add FUSION2
  2005-09-10  v0.4.3 added HUAWEI E600 card and Audiovox AirCard
  2005-09-20  v0.4.4 increased recv buffer size: the card sometimes
                     wants to send >2000 bytes.
  2006-04-10  v0.4.2 fixed two array overrun errors :-/

  Work sponsored by: Sigos GmbH, Germany <info@sigos.de>

  Based on version modification, the author is Quectel <fae-support@quectel.com>

*/

#define DRIVER_VERSION "v0.4"
#define DRIVER_AUTHOR "Matthias Urlichs <smurf@smurf.noris.de>"
#define DRIVER_DESC "Option Card (PC-Card to) USB to Serial Driver"

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "usb-serial.h"

/* Function prototypes */
static int  option_open(struct usb_serial_port *port, struct file *filp);
static void option_close(struct usb_serial_port *port, struct file *filp);
static int  option_startup(struct usb_serial *serial);
static void option_shutdown(struct usb_serial *serial);
static void option_rx_throttle(struct usb_serial_port *port);
static void option_rx_unthrottle(struct usb_serial_port *port);
static int  option_write_room(struct usb_serial_port *port);

static void option_instat_callback(struct urb *urb, struct pt_regs *regs);

static int option_write(struct usb_serial_port *port,
			const unsigned char *buf, int count);

static int  option_chars_in_buffer(struct usb_serial_port *port);
static int  option_ioctl(struct usb_serial_port *port, struct file *file,
			unsigned int cmd, unsigned long arg);
static void option_set_termios(struct usb_serial_port *port,
				struct termios *old);
static void option_break_ctl(struct usb_serial_port *port, int break_state);
static int  option_tiocmget(struct usb_serial_port *port, struct file *file);
static int  option_tiocmset(struct usb_serial_port *port, struct file *file,
				unsigned int set, unsigned int clear);
static int  option_send_setup(struct usb_serial_port *port);

/* Vendor and product IDs */
#define OPTION_VENDOR_ID			0x0AF0
#define HUAWEI_VENDOR_ID			0x12D1
#define AUDIOVOX_VENDOR_ID			0x0F3D

#define OPTION_PRODUCT_OLD		0x5000
#define OPTION_PRODUCT_FUSION	0x6000
#define OPTION_PRODUCT_FUSION2	0x6300
#define HUAWEI_PRODUCT_E600     0x1001
#define AUDIOVOX_PRODUCT_AIRCARD 0x0112

static struct usb_device_id option_ids[] = {
#if 1 //Added by Quectel
	{ USB_DEVICE(0x05C6, 0x9090) }, /* Quectel UC15 */
	{ USB_DEVICE(0x05C6, 0x9003) }, /* Quectel UC20 */
	{ USB_DEVICE(0x05C6, 0x9215) }, /* Quectel EC20(MDM9215) */
    { USB_DEVICE(0x05C6, 0x9091) }, /* Quectel QCM6125 */
	{ USB_DEVICE(0x05C6, 0x90DB) }, /* Quectel QCM6490 */
	{ USB_DEVICE(0x2C7C, 0x0125) }, /* Quectel EC20(MDM9x07)/EC25/EG25 */
	{ USB_DEVICE(0x2C7C, 0x0121) }, /* Quectel EC21 */
    { USB_DEVICE(0x2C7C, 0x030E) }, /* Quectel EM05G */
	{ USB_DEVICE(0x2C7C, 0x0191) }, /* Quectel EG91 */
	{ USB_DEVICE(0x2C7C, 0x0195) }, /* Quectel EG95 */
	{ USB_DEVICE(0x2C7C, 0x0306) }, /* Quectel EG06/EP06/EM06 */
	{ USB_DEVICE(0x2C7C, 0x030B) }, /* Quectel EG065K/EG060K */
	{ USB_DEVICE(0x2C7C, 0x0514) }, /* Quectel BL EG060K RNDIS Only */
	{ USB_DEVICE(0x2C7C, 0x0512) }, /* Quectel EG12/EP12/EM12/EG16/EG18 */
	{ USB_DEVICE(0x2C7C, 0x0296) }, /* Quectel BG96 */
	{ USB_DEVICE(0x2C7C, 0x0700) }, /* Quectel BG95/BG77/BG600L-M3/BC69 */
	{ USB_DEVICE(0x2C7C, 0x0435) }, /* Quectel AG35 */
	{ USB_DEVICE(0x2C7C, 0x0415) }, /* Quectel AG15 */
	{ USB_DEVICE(0x2C7C, 0x0452) }, /* Quectel AG520 */
	{ USB_DEVICE(0x2C7C, 0x0455) }, /* Quectel AG550 */
	{ USB_DEVICE(0x2C7C, 0x0620) }, /* Quectel EG20 */
	{ USB_DEVICE(0x2C7C, 0x0800) }, /* Quectel RG500/RM500/RG510/RM510 */
	{ USB_DEVICE(0x2C7C, 0x0801) }, /* Quectel RG520/RM520/SG520 */
    { USB_DEVICE(0x2C7C, 0x0122) }, /* Quectel RG650 SDX7X */
    { USB_DEVICE(0x2C7C, 0x0316) }, /* Quectel RG255 SDX35 */
	{ USB_DEVICE(0x2C7C, 0x6026) }, /* Quectel EC200 */
	{ USB_DEVICE(0x2C7C, 0x6120) }, /* Quectel UC200 */
	{ USB_DEVICE(0x2C7C, 0x6000) }, /* Quectel EC200/UC200 */
	{ USB_DEVICE(0x3763, 0x3C93) }, /* Quectel GW */
	{ USB_DEVICE(0x3C93, 0xFFFF) }, /* Quectel GW */
	{ .match_flags = USB_DEVICE_ID_MATCH_VENDOR, .idVendor = 0x2C7C }, /* Match All Quectel Modules */
#endif
	{ USB_DEVICE(OPTION_VENDOR_ID, OPTION_PRODUCT_OLD) },
	{ USB_DEVICE(OPTION_VENDOR_ID, OPTION_PRODUCT_FUSION) },
	{ USB_DEVICE(OPTION_VENDOR_ID, OPTION_PRODUCT_FUSION2) },
	{ USB_DEVICE(HUAWEI_VENDOR_ID, HUAWEI_PRODUCT_E600) },
	{ USB_DEVICE(AUDIOVOX_VENDOR_ID, AUDIOVOX_PRODUCT_AIRCARD) },
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, option_ids);

static struct usb_driver option_driver = {
	.name       = "option",
	.probe      = usb_serial_probe,
	.disconnect = usb_serial_disconnect,
	.id_table   = option_ids,
	.no_dynamic_id = 	1,
};

/* The card has three separate interfaces, which the serial driver
 * recognizes separately, thus num_port=1.
 */
static struct usb_serial_driver option_3port_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"option",
	},
	.description       = "Option 3G data card",
	.id_table          = option_ids,
	.num_interrupt_in  = NUM_DONT_CARE,
	.num_bulk_in       = NUM_DONT_CARE,
	.num_bulk_out      = NUM_DONT_CARE,
	.num_ports         = 1, /* 3, but the card reports its ports separately */
	.open              = option_open,
	.close             = option_close,
	.write             = option_write,
	.write_room        = option_write_room,
	.chars_in_buffer   = option_chars_in_buffer,
	.throttle          = option_rx_throttle,
	.unthrottle        = option_rx_unthrottle,
	.ioctl             = option_ioctl,
	.set_termios       = option_set_termios,
	.break_ctl         = option_break_ctl,
	.tiocmget          = option_tiocmget,
	.tiocmset          = option_tiocmset,
	.attach            = option_startup,
	.shutdown          = option_shutdown,
	.read_int_callback = option_instat_callback,
};

#ifdef CONFIG_USB_DEBUG
static int debug;
#else
#define debug 0
#endif

/* per port private data */

#define N_IN_URB 4
#define N_OUT_URB 4
#define IN_BUFLEN 4096
#define OUT_BUFLEN 2048

struct option_port_private {
	/* Input endpoints and buffer for this port */
	struct urb *in_urbs[N_IN_URB];
	char in_buffer[N_IN_URB][IN_BUFLEN];
	/* Output endpoints and buffer for this port */
	struct urb *out_urbs[N_OUT_URB];
	char out_buffer[N_OUT_URB][OUT_BUFLEN];

	/* Settings for the port */
	int rts_state;	/* Handshaking pins (outputs) */
	int dtr_state;
	int cts_state;	/* Handshaking pins (inputs) */
	int dsr_state;
	int dcd_state;
	int ri_state;

	unsigned long tx_start_time[N_OUT_URB];
};

/* Functions used by new usb-serial code. */
static int __init option_init(void)
{
	int retval;
	retval = usb_serial_register(&option_3port_device);
	if (retval)
		goto failed_3port_device_register;
	retval = usb_register(&option_driver);
	if (retval)
		goto failed_driver_register;

	info(DRIVER_DESC ": " DRIVER_VERSION);

	return 0;

failed_driver_register:
	usb_serial_deregister (&option_3port_device);
failed_3port_device_register:
	return retval;
}

static void __exit option_exit(void)
{
	usb_deregister (&option_driver);
	usb_serial_deregister (&option_3port_device);
}

module_init(option_init);
module_exit(option_exit);

static void option_rx_throttle(struct usb_serial_port *port)
{
	dbg("%s", __FUNCTION__);
}

static void option_rx_unthrottle(struct usb_serial_port *port)
{
	dbg("%s", __FUNCTION__);
}

static void option_break_ctl(struct usb_serial_port *port, int break_state)
{
	/* Unfortunately, I don't know how to send a break */
	dbg("%s", __FUNCTION__);
}

static void option_set_termios(struct usb_serial_port *port,
			struct termios *old_termios)
{
	dbg("%s", __FUNCTION__);

	option_send_setup(port);
}

static int option_tiocmget(struct usb_serial_port *port, struct file *file)
{
	unsigned int value;
	struct option_port_private *portdata;

	portdata = usb_get_serial_port_data(port);

	value = ((portdata->rts_state) ? TIOCM_RTS : 0) |
		((portdata->dtr_state) ? TIOCM_DTR : 0) |
		((portdata->cts_state) ? TIOCM_CTS : 0) |
		((portdata->dsr_state) ? TIOCM_DSR : 0) |
		((portdata->dcd_state) ? TIOCM_CAR : 0) |
		((portdata->ri_state) ? TIOCM_RNG : 0);

	return value;
}

static int option_tiocmset(struct usb_serial_port *port, struct file *file,
			unsigned int set, unsigned int clear)
{
	struct option_port_private *portdata;

	portdata = usb_get_serial_port_data(port);

	if (set & TIOCM_RTS)
		portdata->rts_state = 1;
	if (set & TIOCM_DTR)
		portdata->dtr_state = 1;

	if (clear & TIOCM_RTS)
		portdata->rts_state = 0;
	if (clear & TIOCM_DTR)
		portdata->dtr_state = 0;
	return option_send_setup(port);
}

static int option_ioctl(struct usb_serial_port *port, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

/* Write */
static int option_write(struct usb_serial_port *port,
			const unsigned char *buf, int count)
{
	struct option_port_private *portdata;
	int i;
	int left, todo;
	struct urb *this_urb = NULL; /* spurious */
	int err;

	portdata = usb_get_serial_port_data(port);

	dbg("%s: write (%d chars)", __FUNCTION__, count);

	i = 0;
	left = count;
	for (i=0; left > 0 && i < N_OUT_URB; i++) {
		todo = left;
		if (todo > OUT_BUFLEN)
			todo = OUT_BUFLEN;

		this_urb = portdata->out_urbs[i];
		if (this_urb->status == -EINPROGRESS) {
			if (time_before(jiffies,
					portdata->tx_start_time[i] + 10 * HZ))
				continue;
			usb_unlink_urb(this_urb);
			continue;
		}
		if (this_urb->status != 0)
			dbg("usb_write %p failed (err=%d)",
				this_urb, this_urb->status);

		dbg("%s: endpoint %d buf %d", __FUNCTION__,
			usb_pipeendpoint(this_urb->pipe), i);

		/* send the data */
		memcpy (this_urb->transfer_buffer, buf, todo);
		this_urb->transfer_buffer_length = todo;

		this_urb->dev = port->serial->dev;
		err = usb_submit_urb(this_urb, GFP_ATOMIC);
		if (err) {
			dbg("usb_submit_urb %p (write bulk) failed "
				"(%d, has %d)", this_urb,
				err, this_urb->status);
			continue;
		}
		portdata->tx_start_time[i] = jiffies;
		buf += todo;
		left -= todo;
	}

	count -= left;
	dbg("%s: wrote (did %d)", __FUNCTION__, count);
	return count;
}

static void option_indat_callback(struct urb *urb, struct pt_regs *regs)
{
	int err;
	int endpoint;
	struct usb_serial_port *port;
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;

	dbg("%s: %p", __FUNCTION__, urb);

	endpoint = usb_pipeendpoint(urb->pipe);
	port = (struct usb_serial_port *) urb->context;

	if (urb->status) {
		dbg("%s: nonzero status: %d on endpoint %02x.",
		    __FUNCTION__, urb->status, endpoint);
	} else {
		tty = port->tty;
		if (urb->actual_length) {
			tty_buffer_request_room(tty, urb->actual_length);
			tty_insert_flip_string(tty, data, urb->actual_length);
			tty_flip_buffer_push(tty);
		} else {
			dbg("%s: empty read urb received", __FUNCTION__);
		}

		/* Resubmit urb so we continue receiving */
		if (port->open_count && urb->status != -ESHUTDOWN) {
			err = usb_submit_urb(urb, GFP_ATOMIC);
			if (err)
				printk(KERN_ERR "%s: resubmit read urb failed. "
					"(%d)", __FUNCTION__, err);
		}
	}
	return;
}

static void option_outdat_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port;

	dbg("%s", __FUNCTION__);

	port = (struct usb_serial_port *) urb->context;

	if (port->open_count)
		schedule_work(&port->work);
}

static void option_instat_callback(struct urb *urb, struct pt_regs *regs)
{
	int err;
	struct usb_serial_port *port = (struct usb_serial_port *) urb->context;
	struct option_port_private *portdata = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;

	dbg("%s", __FUNCTION__);
	dbg("%s: urb %p port %p has data %p", __FUNCTION__,urb,port,portdata);

	if (urb->status == 0) {
		struct usb_ctrlrequest *req_pkt =
				(struct usb_ctrlrequest *)urb->transfer_buffer;

		if (!req_pkt) {
			dbg("%s: NULL req_pkt\n", __FUNCTION__);
			return;
		}
		if ((req_pkt->bRequestType == 0xA1) &&
				(req_pkt->bRequest == 0x20)) {
			int old_dcd_state;
			unsigned char signals = *((unsigned char *)
					urb->transfer_buffer +
					sizeof(struct usb_ctrlrequest));

			dbg("%s: signal x%x", __FUNCTION__, signals);

			old_dcd_state = portdata->dcd_state;
			portdata->cts_state = 1;
			portdata->dcd_state = ((signals & 0x01) ? 1 : 0);
			portdata->dsr_state = ((signals & 0x02) ? 1 : 0);
			portdata->ri_state = ((signals & 0x08) ? 1 : 0);

			if (port->tty && !C_CLOCAL(port->tty) &&
					old_dcd_state && !portdata->dcd_state)
				tty_hangup(port->tty);
		} else {
			dbg("%s: type %x req %x", __FUNCTION__,
				req_pkt->bRequestType,req_pkt->bRequest);
		}
	} else
		dbg("%s: error %d", __FUNCTION__, urb->status);

	/* Resubmit urb so we continue receiving IRQ data */
	if (urb->status != -ESHUTDOWN) {
		urb->dev = serial->dev;
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err)
			dbg("%s: resubmit intr urb failed. (%d)",
				__FUNCTION__, err);
	}
}

static int option_write_room(struct usb_serial_port *port)
{
	struct option_port_private *portdata;
	int i;
	int data_len = 0;
	struct urb *this_urb;

	portdata = usb_get_serial_port_data(port);

	for (i=0; i < N_OUT_URB; i++) {
		this_urb = portdata->out_urbs[i];
		if (this_urb && this_urb->status != -EINPROGRESS)
			data_len += OUT_BUFLEN;
	}

	dbg("%s: %d", __FUNCTION__, data_len);
	return data_len;
}

static int option_chars_in_buffer(struct usb_serial_port *port)
{
	struct option_port_private *portdata;
	int i;
	int data_len = 0;
	struct urb *this_urb;

	portdata = usb_get_serial_port_data(port);

	for (i=0; i < N_OUT_URB; i++) {
		this_urb = portdata->out_urbs[i];
		if (this_urb && this_urb->status == -EINPROGRESS)
			data_len += this_urb->transfer_buffer_length;
	}
	dbg("%s: %d", __FUNCTION__, data_len);
	return data_len;
}

static int option_open(struct usb_serial_port *port, struct file *filp)
{
	struct option_port_private *portdata;
	struct usb_serial *serial = port->serial;
	int i, err;
	struct urb *urb;

	portdata = usb_get_serial_port_data(port);

	dbg("%s", __FUNCTION__);

	/* Set some sane defaults */
	portdata->rts_state = 1;
	portdata->dtr_state = 1;

	/* Reset low level data toggle and start reading from endpoints */
	for (i = 0; i < N_IN_URB; i++) {
		urb = portdata->in_urbs[i];
		if (! urb)
			continue;
		if (urb->dev != serial->dev) {
			dbg("%s: dev %p != %p", __FUNCTION__,
				urb->dev, serial->dev);
			continue;
		}

		/*
		 * make sure endpoint data toggle is synchronized with the
		 * device
		 */
		usb_clear_halt(urb->dev, urb->pipe);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			dbg("%s: submit urb %d failed (%d) %d",
				__FUNCTION__, i, err,
				urb->transfer_buffer_length);
		}
	}

	/* Reset low level data toggle on out endpoints */
	for (i = 0; i < N_OUT_URB; i++) {
		urb = portdata->out_urbs[i];
		if (! urb)
			continue;
		urb->dev = serial->dev;
		/* usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
				usb_pipeout(urb->pipe), 0); */
	}

	port->tty->low_latency = 1;

	option_send_setup(port);

	return (0);
}

static inline void stop_urb(struct urb *urb)
{
	if (urb && urb->status == -EINPROGRESS)
		usb_kill_urb(urb);
}

static void option_close(struct usb_serial_port *port, struct file *filp)
{
	int i;
	struct usb_serial *serial = port->serial;
	struct option_port_private *portdata;

	dbg("%s", __FUNCTION__);
	portdata = usb_get_serial_port_data(port);

	portdata->rts_state = 0;
	portdata->dtr_state = 0;

	if (serial->dev) {
		option_send_setup(port);

		/* Stop reading/writing urbs */
		for (i = 0; i < N_IN_URB; i++)
			stop_urb(portdata->in_urbs[i]);
		for (i = 0; i < N_OUT_URB; i++)
			stop_urb(portdata->out_urbs[i]);
	}
	port->tty = NULL;
}

/* Helper functions used by option_setup_urbs */
static struct urb *option_setup_urb(struct usb_serial *serial, int endpoint,
		int dir, void *ctx, char *buf, int len,
		void (*callback)(struct urb *, struct pt_regs *regs))
{
	struct urb *urb;

	if (endpoint == -1)
		return NULL;		/* endpoint not needed */

	urb = usb_alloc_urb(0, GFP_KERNEL);		/* No ISO */
	if (urb == NULL) {
		dbg("%s: alloc for endpoint %d failed.", __FUNCTION__, endpoint);
		return NULL;
	}

		/* Fill URB using supplied data. */
	usb_fill_bulk_urb(urb, serial->dev,
		      usb_sndbulkpipe(serial->dev, endpoint) | dir,
		      buf, len, callback, ctx);

#if 1 //Added by Quectel for Zero Packet
	if (dir == USB_DIR_OUT) {
		if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && serial->dev->descriptor.idProduct == cpu_to_le16(0x9090))
			urb->transfer_flags |= URB_ZERO_PACKET;
		if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && serial->dev->descriptor.idProduct == cpu_to_le16(0x9003))
			urb->transfer_flags |= URB_ZERO_PACKET;
		if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && serial->dev->descriptor.idProduct == cpu_to_le16(0x9215))
			urb->transfer_flags |= URB_ZERO_PACKET;
        if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && serial->dev->descriptor.idProduct == cpu_to_le16(0x9091))
			urb->transfer_flags |= URB_ZERO_PACKET;
        if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && serial->dev->descriptor.idProduct == cpu_to_le16(0x90DB))
			urb->transfer_flags |= URB_ZERO_PACKET;
		if (serial->dev->descriptor.idVendor == cpu_to_le16(0x2C7C))
			urb->transfer_flags |= URB_ZERO_PACKET;
	}
#endif

	return urb;
}

/* Setup urbs */
static void option_setup_urbs(struct usb_serial *serial)
{
	int j;
	struct usb_serial_port *port;
	struct option_port_private *portdata;

	dbg("%s", __FUNCTION__);

	port = serial->port[0];
	portdata = usb_get_serial_port_data(port);

	/* Do indat endpoints first */
	for (j = 0; j < N_IN_URB; ++j) {
		portdata->in_urbs[j] = option_setup_urb (serial,
                  port->bulk_in_endpointAddress, USB_DIR_IN, port,
                  portdata->in_buffer[j], IN_BUFLEN, option_indat_callback);
	}

	/* outdat endpoints */
	for (j = 0; j < N_OUT_URB; ++j) {
		portdata->out_urbs[j] = option_setup_urb (serial,
                  port->bulk_out_endpointAddress, USB_DIR_OUT, port,
                  portdata->out_buffer[j], OUT_BUFLEN, option_outdat_callback);
	}
}

static int option_send_setup(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct option_port_private *portdata;

	dbg("%s", __FUNCTION__);

	portdata = usb_get_serial_port_data(port);

	if (port->tty) {
		int val = 0;
		if (portdata->dtr_state)
			val |= 0x01;
		if (portdata->rts_state)
			val |= 0x02;

		return usb_control_msg(serial->dev,
				usb_rcvctrlpipe(serial->dev, 0),
				0x22,0x21,val,0,NULL,0,USB_CTRL_SET_TIMEOUT);
	}

	return 0;
}

static int option_startup(struct usb_serial *serial)
{
	int i, err;
	struct usb_serial_port *port;
	struct option_port_private *portdata;

	dbg("%s", __FUNCTION__);

#if 1 //Added by Quectel
	//Quectel UC20's interface 4 can be used as USB Network device
	if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && serial->dev->descriptor.idProduct == cpu_to_le16(0x9003)
		&& serial->interface->cur_altsetting->desc.bInterfaceNumber >= 4)
		return -ENODEV;

	//Quectel EC20(MDM9215)'s interface 4 can be used as USB Network device
	if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && serial->dev->descriptor.idProduct == cpu_to_le16(0x9215)
		&& serial->interface->cur_altsetting->desc.bInterfaceNumber >= 4)
		return -ENODEV;
        
    //Quectel QCM6125 & QCM6490's interface 2 can be used as USB Network device
	if (serial->dev->descriptor.idVendor == cpu_to_le16(0x05C6) && 
		(serial->dev->descriptor.idProduct == cpu_to_le16(0x9091) || serial->dev->descriptor.idProduct == cpu_to_le16(0x90DB))
		&& serial->interface->cur_altsetting->desc.bInterfaceNumber >= 2)
		return -ENODEV;

	if (serial->dev->descriptor.idVendor == cpu_to_le16(0x2C7C)) {
		__u16 idProduct = le16_to_cpu(serial->dev->descriptor.idProduct);
		struct usb_interface_descriptor *intf = &serial->interface->cur_altsetting->desc;

		if (intf->bInterfaceClass != 0xFF || intf->bInterfaceSubClass == 0x42) {
			//ECM, RNDIS, NCM, MBIM, ACM, UAC, ADB
			return -ENODEV;
		}

		if ((idProduct&0xF000) == 0x0000) {
			//MDM interface 4 is QMI
            if (idProduct == 0x0316 && intf->bInterfaceNumber == 3)   //SDX35
				return -ENODEV;
            
			if (intf->bInterfaceNumber == 4 && intf->bNumEndpoints == 3
				&& intf->bInterfaceSubClass == 0xFF && intf->bInterfaceProtocol == 0xFF)
				return -ENODEV;
		}
	}
#endif

	/* Now setup per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		portdata = kzalloc(sizeof(*portdata), GFP_KERNEL);
		if (!portdata) {
			dbg("%s: kmalloc for option_port_private (%d) failed!.",
					__FUNCTION__, i);
			return (1);
		}

		usb_set_serial_port_data(port, portdata);

		if (! port->interrupt_in_urb)
			continue;
		err = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (err)
			dbg("%s: submit irq_in urb failed %d",
				__FUNCTION__, err);
	}

	option_setup_urbs(serial);

	return (0);
}

static void option_shutdown(struct usb_serial *serial)
{
	int i, j;
	struct usb_serial_port *port;
	struct option_port_private *portdata;

	dbg("%s", __FUNCTION__);

	/* Stop reading/writing urbs */
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		portdata = usb_get_serial_port_data(port);
		for (j = 0; j < N_IN_URB; j++)
			stop_urb(portdata->in_urbs[j]);
		for (j = 0; j < N_OUT_URB; j++)
			stop_urb(portdata->out_urbs[j]);
	}

	/* Now free them */
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		portdata = usb_get_serial_port_data(port);

		for (j = 0; j < N_IN_URB; j++) {
			if (portdata->in_urbs[j]) {
				usb_free_urb(portdata->in_urbs[j]);
				portdata->in_urbs[j] = NULL;
			}
		}
		for (j = 0; j < N_OUT_URB; j++) {
			if (portdata->out_urbs[j]) {
				usb_free_urb(portdata->out_urbs[j]);
				portdata->out_urbs[j] = NULL;
			}
		}
	}

	/* Now free per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		kfree(usb_get_serial_port_data(port));
	}
}

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

#ifdef CONFIG_USB_DEBUG
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug messages");
#endif

