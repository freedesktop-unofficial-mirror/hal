/***************************************************************************
 * CVSID: $Id$
 *
 * addon-hid-ups.c : Detect UPS'es using the USB HID interface and
 *                   add and maintain battery.* properties
 *
 * Copyright (C) 2005 David Zeuthen, <david@fubar.dk>
 *
 * Based on hid-ups.c: Copyright (c) 2001 Vojtech Pavlik
 *                     <vojtech@ucw.cz>, Copyright (c) 2001 Paul
 *                     Stewart <hiddev@wetlogic.net>, Tweaked by Kern
 *                     Sibbald <kern@sibbald.com> to learn about USB
 *                     UPSes.  hid-ups.c is GPLv2 and available from
 *                     examples directory of version 3.10.16 of the
 *                     acpupsd project; see http://www.apcupsd.com.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* asm/types.h required for __s32 in linux/hiddev.h */
#include <asm/types.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hiddev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "libhal/libhal.h"

#include "../../util_helper.h"
#include "../../logger.h"

#define UPS_USAGE		0x840000
#define UPS_SERIAL		0x8400fe
#define UPS_CHEMISTRY		0x850089
#define UPS_CAPACITY_MODE	0x85002c
#define UPS_BATTERY_VOLTAGE	0x840030
#define UPS_BELOW_RCL		0x840042
#define UPS_SHUTDOWN_IMMINENT	0x840069
#define UPS_PRODUCT		0x8400fe
#define UPS_SERIALNUMBER	0x8400ff
#define UPS_CHARGING		0x850044
#define UPS_DISCHARGING 	0x850045
#define UPS_REMAINING_CAPACITY	0x850066
#define UPS_RUNTIME_TO_EMPTY	0x850068
#define UPS_AC_PRESENT		0x8500d0
#define UPS_BATTERYPRESENT	0x8500d1
#define UPS_DESIGNCAPACITY	0x850083
#define UPS_DEVICENAME		0x850088
#define UPS_DEVICECHEMISTRY	0x850089
#define UPS_RECHARGEABLE	0x85008b
#define UPS_OEMINFORMATION	0x85008f

#define STATE_NORMAL 0		      /* unit powered */
#define STATE_DEBOUNCE 1	      /* power failure */
#define STATE_BATTERY 2 	      /* power failure confirmed */

static dbus_bool_t
is_ups (int fd)
{
	unsigned int i;
	dbus_bool_t ret;
	struct hiddev_devinfo device_info;

	ret = FALSE;

	if (ioctl (fd, HIDIOCGDEVINFO, &device_info) < 0)
		goto out;

	for (i = 0; i < device_info.num_applications; i++) {
		if ((ioctl(fd, HIDIOCAPPLICATION, i) & 0xff0000) == UPS_USAGE) {
			ret = TRUE;
			goto out;
		}			
	}

out:
	return ret;
}

static char *
ups_get_string (int fd, int sindex)
{
	static struct hiddev_string_descriptor sdesc;
	
	if (sindex == 0) {
		return "";
	}
	sdesc.index = sindex;
	if (ioctl (fd, HIDIOCGSTRING, &sdesc) < 0) {
		return "";
	}
	HAL_DEBUG (("foo: '%s'", sdesc.value));
	return sdesc.value;
}


static dbus_bool_t
ups_get_static (LibHalContext *ctx, const char *udi, int fd)
{
	int ret;
	struct hiddev_report_info rinfo;
	struct hiddev_field_info finfo;
	struct hiddev_usage_ref uref;
	int rtype;
	unsigned int i, j;
	DBusError error;

	/* set to failure */
	ret = FALSE;

	/* first check that we are an UPS */
	if (!is_ups (fd))
		goto out;

	for (rtype = HID_REPORT_TYPE_MIN; rtype <= HID_REPORT_TYPE_MAX; rtype++) {
		rinfo.report_type = rtype;
		rinfo.report_id = HID_REPORT_ID_FIRST;
		while (ioctl (fd, HIDIOCGREPORTINFO, &rinfo) >= 0) {
			for (i = 0; i < rinfo.num_fields; i++) { 
				memset (&finfo, 0, sizeof (finfo));
				finfo.report_type = rinfo.report_type;
				finfo.report_id = rinfo.report_id;
				finfo.field_index = i;
				ioctl (fd, HIDIOCGFIELDINFO, &finfo);
				
				memset (&uref, 0, sizeof (uref));
				for (j = 0; j < finfo.maxusage; j++) {
					uref.report_type = finfo.report_type;
					uref.report_id = finfo.report_id;
					uref.field_index = i;
					uref.usage_index = j;
					ioctl (fd, HIDIOCGUCODE, &uref);
					ioctl (fd, HIDIOCGUSAGE, &uref);

					dbus_error_init (&error);

					switch (uref.usage_code) {

					case UPS_REMAINING_CAPACITY:
						libhal_device_set_property_int (
							ctx, udi, "battery.charge_level.current", uref.value, &error);
						libhal_device_set_property_int (
							ctx, udi, "battery.charge_level.percentage", uref.value, &error);
						libhal_device_set_property_string (
							ctx, udi, "battery.charge_level.unit", "percent", &error);
						libhal_device_set_property_int (
							ctx, udi, "battery.reporting.current", uref.value, &error);
						libhal_device_set_property_int (
							ctx, udi, "battery.reporting.percentage", uref.value, &error);
						libhal_device_set_property_string (
							ctx, udi, "battery.reporting.unit", "percent", &error);
						break;

					case UPS_RUNTIME_TO_EMPTY:
						libhal_device_set_property_int (
							ctx, udi, "battery.remaining_time", uref.value, &error);
						break;

					case UPS_CHARGING:
						libhal_device_set_property_bool (
							ctx, udi, "battery.rechargeable.is_charging", uref.value != 0, &error);
						break;

					case UPS_DISCHARGING:
						libhal_device_set_property_bool (
							ctx, udi, "battery.rechargeable.is_discharging", uref.value != 0, &error);
						break;

					case UPS_BATTERYPRESENT:
						libhal_device_set_property_bool (
							ctx, udi, "battery.present", uref.value != 0, &error);
						break;

					case UPS_DEVICENAME:
						libhal_device_set_property_string (
							ctx, udi, "foo", 
							ups_get_string (fd, uref.value), &error);
						break;

					case UPS_CHEMISTRY:
						libhal_device_set_property_string (
							ctx, udi, "battery.technology", 
							ups_get_string (fd, uref.value), &error);
						break;

					case UPS_RECHARGEABLE:
						libhal_device_set_property_bool (
							ctx, udi, "battery.is_rechargeable", uref.value != 0, &error);
						break;

					case UPS_OEMINFORMATION:
						libhal_device_set_property_string (
							ctx, udi, "battery.vendor", 
							ups_get_string (fd, uref.value), &error);
						break;

					case UPS_PRODUCT:
						libhal_device_set_property_string (
							ctx, udi, "battery.model", 
							ups_get_string (fd, uref.value), &error);
						break;

					case UPS_SERIALNUMBER:
						libhal_device_set_property_string (
							ctx, udi, "battery.serial", 
							ups_get_string (fd, uref.value), &error);
						break;

					case UPS_DESIGNCAPACITY:
						libhal_device_set_property_int (
							ctx, udi, "battery.charge_level.design", uref.value, &error);
						libhal_device_set_property_int (
							ctx, udi, "battery.charge_level.last_full", uref.value, &error);
						libhal_device_set_property_int (
							ctx, udi, "battery.reporting.design", uref.value, &error);
						libhal_device_set_property_int (
							ctx, udi, "battery.reporting.last_full", uref.value, &error);
						break;

					default:
						break;
					}
				}
			}
			rinfo.report_id |= HID_REPORT_ID_NEXT;
		}
	}

	libhal_device_set_property_bool (ctx, udi, "battery.present", TRUE, &error);
	libhal_device_set_property_string (ctx, udi, "battery.type", "ups", &error);
	libhal_device_add_capability (ctx, udi, "battery", &error);

	ret = TRUE;

out:
	return ret;
}

int
main (int argc, char *argv[])
{
	int fd;
	char *udi;
	char *device_file;
	LibHalContext *ctx = NULL;
	DBusError error;
	unsigned int i;
	fd_set fdset;
	struct hiddev_event ev[64];
	int rd;

	hal_set_proc_title_init (argc, argv);

	setup_logger ();	

	udi = getenv ("UDI");
	if (udi == NULL)
		goto out;

	dbus_error_init (&error);
	if ((ctx = libhal_ctx_init_direct (&error)) == NULL)
		goto out;

	device_file = getenv ("HAL_PROP_HIDDEV_DEVICE");
	if (device_file == NULL)
		goto out;

	fd = open (device_file, O_RDONLY);
	if (fd < 0)
		goto out;

	if (!ups_get_static (ctx, udi, fd))
		goto out;

	hal_set_proc_title ("hald-addon-hid-ups: listening on %s", device_file);

	FD_ZERO(&fdset);
	while (1) {
		FD_SET(fd, &fdset);
		rd = select(fd+1, &fdset, NULL, NULL, NULL);
		
		if (rd > 0) {
			rd = read(fd, ev, sizeof(ev));
			if (rd < (int) sizeof(ev[0])) {
				close(fd);
				goto out;
			}

			for (i = 0; i < rd / sizeof(ev[0]); i++) {
				DBusError error;
				
				dbus_error_init (&error);
				switch (ev[i].hid) {
				case UPS_REMAINING_CAPACITY:
					libhal_device_set_property_int (
						ctx, udi, "battery.charge_level.current", ev[i].value, &error);
					libhal_device_set_property_int (
						ctx, udi, "battery.charge_level.percentage", ev[i].value, &error);
					libhal_device_set_property_int (
						ctx, udi, "battery.reporting.current", ev[i].value, &error);
					libhal_device_set_property_int (
						ctx, udi, "battery.reporting.percentage", ev[i].value, &error);
					break;
					
				case UPS_RUNTIME_TO_EMPTY:
					libhal_device_set_property_int (
						ctx, udi, "battery.remaining_time", ev[i].value, &error);
					break;
					
				case UPS_CHARGING:
					libhal_device_set_property_bool (
						ctx, udi, "battery.rechargeable.is_charging", ev[i].value != 0, &error);
					break;
					
				case UPS_DISCHARGING:
					libhal_device_set_property_bool (
						ctx, udi, "battery.rechargeable.is_discharging", ev[i].value != 0, &error);
					break;
					
				default:
					break;
				}
			}
		}
	}

out:
	return 0;
}