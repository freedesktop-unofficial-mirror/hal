/***************************************************************************
 * CVSID: $Id$
 *
 * Generic methods for class devices
 *
 * Copyright (C) 2003 David Zeuthen, <david@fubar.dk>
 *
 * Licensed under the Academic Free License version 2.0
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>

#include <linux/input.h>	/* for EV_* etc. */

#include "../logger.h"
#include "../device_store.h"
#include "../hald.h"

#include "common.h"
#include "class_device.h"

/**
 * @defgroup HalDaemonLinuxClass Generic methods for class devices
 * @ingroup HalDaemonLinux
 * @brief Generic methods for class devices
 * @{
 */

typedef struct {
	HalDevice *device;
	ClassDeviceHandler *handler;
} AsyncInfo;

static void
got_sysdevice_cb (HalDeviceStore *store, HalDevice *device,
		  gpointer user_data)
{
	AsyncInfo *ai = user_data;

	class_device_got_sysdevice (device, ai->device, ai->handler);

	g_free (ai);
}

static void
got_parent_cb (HalDeviceStore *store, HalDevice *device,
	       gpointer user_data)
{
	AsyncInfo *ai = user_data;

	class_device_got_parent_device (device, ai->device, ai->handler);

	g_free (ai);
}


/** Generic visitor method for class devices.
 *
 *  This function parses the attributes present and merges more information
 *  into the HAL device this class device points to
 *
 *  @param  path                Sysfs-path for class device
 *  @param  class_device        Libsysfs object for device
 */
void
class_device_visit (ClassDeviceHandler *self,
		    const char *path,
		    struct sysfs_class_device *class_device,
		    dbus_bool_t is_probing)
{
	HalDevice *d;
	char dev_file_prop_name[SYSFS_PATH_MAX];

	/* only care about given sysfs class name */
	if (strcmp (class_device->classname, self->sysfs_class_name) != 0)
		return;

	if (class_device->sysdevice == NULL) {
		HAL_WARNING (("class device %s doesn't have sysdevice", path));
		return;
	}

	/* Construct a new device and add to temporary device list */
	d = hal_device_new ();
	hal_device_store_add (hald_get_tdl (), d);
	if (!self->merge_or_add) {
		hal_device_property_set_string (d, "info.bus", self->hal_class_name);
		hal_device_property_set_string (d, "linux.sysfs_path", path);
		hal_device_property_set_string (d, "linux.sysfs_path_device", 
					class_device->sysdevice->path);
	} 

	if (self->require_device_file) {
		/* temporary property used for _udev_event() */
		hal_device_property_set_string (d, ".udev.sysfs_path", path);
		hal_device_property_set_string (d, ".udev.class_name", self->sysfs_class_name);

		/* Find the property name we should store the device file in */
		self->get_device_file_target (self, d, path, class_device,
					      dev_file_prop_name, SYSFS_PATH_MAX);
		hal_device_property_set_string (d, ".target_dev", dev_file_prop_name);
	}

	/* Ask udev about the device file if we are probing */
	if (self->require_device_file && is_probing) {
		char dev_file[SYSFS_PATH_MAX];

		if (!class_device_get_device_file (path, dev_file, 
						   SYSFS_PATH_MAX)) {
			HAL_WARNING (("Couldn't get device file for sysfs "
				      "path %s", path));
			return;
		}

		/* If we are not probing this function will be called upon
		 * receiving a dbus event */
		self->udev_event (self, d, dev_file);
	}

	/* Now find the physical device; this happens asynchronously as it
	 * might be added later. */
	if (self->merge_or_add) {
#if 0
		ds_device_async_find_by_key_value_string
			("linux.sysfs_path_device", 
			 class_device->sysdevice->path,
			 TRUE, class_device_got_sysdevice, 
			 (void *) d, (void *) self,
			 is_probing ? 0 : HAL_LINUX_HOTPLUG_TIMEOUT);
#elif 0
		class_device_got_sysdevice (
			hal_device_store_match_key_value_string (hald_get_gdl (),
								 "linux.sysfs_path_device",
								 class_device->sysdevice->path),
			d, self);
#else
		{
			AsyncInfo *ai = g_new0 (AsyncInfo, 1);
			ai->device = d;
			ai->handler = self;

			hal_device_store_match_key_value_string_async (
				hald_get_gdl (),
				"linux.sysfs_path_device",
				class_device->sysdevice->path,
				got_sysdevice_cb, ai,
				is_probing ? 0 : HAL_LINUX_HOTPLUG_TIMEOUT);
		}
#endif
	} else {
		char *parent_sysfs_path;

		parent_sysfs_path = get_parent_sysfs_path (class_device->sysdevice->path);

#if 0
		ds_device_async_find_by_key_value_string
			("linux.sysfs_path_device", 
			 parent_sysfs_path,
			 TRUE, class_device_got_parent_device, 
			 (void *) d, (void *) self,
			 is_probing ? 0 : HAL_LINUX_HOTPLUG_TIMEOUT);
#elif 0
		class_device_got_parent_device (
			hal_device_store_match_key_value_string (hald_get_gdl (),
								 "linux.sysfs_path_device",
								 parent_sysfs_path),
			d, self);
#else
		{
			AsyncInfo *ai = g_new0 (AsyncInfo, 1);
			ai->device = d;
			ai->handler = self;

			hal_device_store_match_key_value_string_async (
				hald_get_gdl (),
				"linux.sysfs_path_device",
				parent_sysfs_path,
				got_parent_cb, ai,
				is_probing ? 0 : HAL_LINUX_HOTPLUG_TIMEOUT);
		}
#endif
	}
}

/** Called when the class device instance have been removed
 *
 *  @param  self               Pointer to class members
 *  @param  sysfs_path         The path in sysfs (including mount point) of
 *                             the class device in sysfs
 *  @param  d                  The HalDevice object of the instance of
 *                             this device class
 */
void
class_device_removed (ClassDeviceHandler* self, 
		      const char *sysfs_path, 
		      HalDevice *d)
{
	HAL_INFO (("entering : sysfs_path = '%s'", sysfs_path));
}

/** Called when a device file (e.g. a file in /dev) have been created by udev 
 *
 *  @param  self               Pointer to class members
 *  @param  d                   The HalDevice object of the instance of
 *                              this device class
 *  @param  dev_file            device file, e.g. /dev/input/event4
 */
void
class_device_udev_event (ClassDeviceHandler *self,
			 HalDevice *d, char *dev_file)
{
	const char *target_dev;
	char *target_dev_copy;

	/* merge the device file name into the name determined above */
	target_dev = hal_device_property_get_string (d, ".target_dev");
	assert (target_dev != NULL);

	/* hmm, have to make a copy because somewhere asynchronously we
	 * remove .target_dev */
	target_dev_copy = strdup (target_dev);
	assert (target_dev_copy != NULL);

	/* this will invoke _got_device_file per the _async_wait_for_  below */
	hal_device_property_set_string (d, target_dev_copy, dev_file);

	free (target_dev_copy);
}

typedef struct {
	char *target_dev;
	ClassDeviceHandler *handler;
	guint signal_id;
} PropInfo;

static void
prop_changed_cb (HalDevice *device, const char *key,
		 gboolean removed, gboolean added, gpointer user_data)
{
	PropInfo *pi = user_data;

	if (strcmp (key, pi->target_dev) != 0)
		return;

	/* the property is no longer there */
	if (removed)
		goto cleanup;


	class_device_got_device_file (device, device, pi->handler);

cleanup:
	g_signal_handler_disconnect (device, pi->signal_id);

	g_free (pi->target_dev);
	g_free (pi);
}

/** Callback when the parent device is found or if timeout for search occurs.
 *  (only applicable if self->merge_or_add==FALSE)
 *
 *  @param  sysdevice           Async Return value from the find call or NULL
 *                              if timeout
 *  @param  data1               User data (or own device in this case)
 *  @param  data2               User data
 */
void
class_device_got_parent_device (HalDevice *parent, void *data1, void *data2)
{
	const char *target_dev = NULL;
	const char *sysfs_path = NULL;
	char *new_udi = NULL;
	HalDevice *new_d = NULL;
	HalDevice *d = (HalDevice *) data1;
	ClassDeviceHandler *self = (ClassDeviceHandler *) data2;
	struct sysfs_class_device *class_device;

	if (parent == NULL) {
		HAL_WARNING (("No parent for class device at sysfs path %s",
			      d->udi));
		/* get rid of temporary device */
		hal_device_store_remove (hald_get_tdl (), d);
		g_object_unref (d);
		return;
	}

	/* set parent */
	hal_device_property_set_string (d, "info.parent", parent->udi);

	/* get more information about the device from the specialised 
	 * function */
	sysfs_path = hal_device_property_get_string (d, "linux.sysfs_path");
	assert (sysfs_path != NULL);
	class_device = sysfs_open_class_device (sysfs_path);
	if (class_device == NULL)
		DIE (("Coulnd't get sysfs class device object for path %s", 
		      sysfs_path));
	self->post_process (self, d, sysfs_path, class_device);
	sysfs_close_class_device (class_device);

	/** @todo handle merge_or_add==FALSE && require_device_file==TRUE */
	if (self->require_device_file) {
		target_dev = hal_device_property_get_string (d, ".target_dev");
		assert (target_dev != NULL);
#if 0
		ds_device_async_wait_for_property (
			d, target_dev, 
			class_device_got_device_file,
			(void *) d, (void *) self,
			is_probing ? 0 : HAL_LINUX_HOTPLUG_TIMEOUT);
#else
		if (hal_device_has_property (d, target_dev))
			class_device_got_device_file (d, d, self);
		else {
			PropInfo *pi;

			pi = g_new0 (PropInfo, 1);
			pi->target_dev = g_strdup (target_dev);
			pi->handler = self;
			pi->signal_id = g_signal_connect (d,
							  "property_changed",
							  prop_changed_cb, pi);
		}
#endif
	} else {
		/* Compute a proper UDI (unique device id) and try to locate a 
		 * persistent unplugged device or simple add this new device...
		 */
		new_udi = rename_and_merge (d, self->compute_udi, self->hal_class_name);
		if (new_udi != NULL) {
			new_d = hal_device_store_find (hald_get_gdl (),
						       new_udi);
			hal_device_store_add (hald_get_gdl (),
					      new_d != NULL ? new_d : d);
		}
		hal_device_store_remove (hald_get_tdl (), d);
		g_object_unref (d);
	}
}


/** Callback when the sysdevice is found or if timeout for search occurs.
 *  (only applicable if self->merge_or_add==TRUE)
 *
 *  @param  sysdevice           Async Return value from the find call or NULL
 *                              if timeout
 *  @param  data1               User data (or own device in this case)
 *  @param  data2               User data
 */
void
class_device_got_sysdevice (HalDevice *sysdevice, void *data1, void *data2)
{
	const char *target_dev;
	const char *parent_udi;
	HalDevice *parent_device;
	HalDevice *d = (HalDevice *) data1;
	ClassDeviceHandler *self = (ClassDeviceHandler *) data2;

	HAL_INFO (("Entering d=0x%0x, sysdevice=0x%0x!", d, sysdevice));

	if (sysdevice == NULL) {
		HAL_WARNING (("Sysdevice for a class device never appeared!"));
		/* get rid of temporary device */
		hal_device_store_remove (hald_get_tdl (), d);
		g_object_unref (d);
		return;
	}

	/* if the sysdevice is virtual, ascent into the closest non-
	 * virtual device */
	while (hal_device_has_property (sysdevice, "info.virtual") &&
	       hal_device_property_get_bool (sysdevice, "info.virtual") &&
	       (parent_udi = 
		hal_device_property_get_string (sysdevice, "info.parent")) != NULL ) {
		parent_device = hal_device_store_find (hald_get_gdl (),
						       parent_udi);
		if (parent_device != NULL) {
			sysdevice = parent_device;
		}
	}
	
	/* store the name of the sysdevice in a temporary property */
	hal_device_property_set_string (d, ".sysdevice", sysdevice->udi);

	/* wait for the appropriate property for the device file */
	if (self->require_device_file ) {
		target_dev = hal_device_property_get_string (d, ".target_dev");
		assert (target_dev != NULL);
#if 0
		ds_device_async_wait_for_property (
			d, target_dev, 
			class_device_got_device_file,
			(void *) d, (void *) self,
			is_probing ? 0 : HAL_LINUX_HOTPLUG_TIMEOUT);
#else
		if (hal_device_has_property (d, target_dev))
			class_device_got_device_file (d, d, self);
		else {
			PropInfo *pi;

			pi = g_new0 (PropInfo, 1);
			pi->target_dev = g_strdup (target_dev);
			pi->handler = self;
			pi->signal_id = g_signal_connect (d,
							  "property_changed",
							  prop_changed_cb, pi);
		}
#endif
	} else {
		/** @todo FIXME */
		DIE (("fix me here"));
	}
}

/** Callback when we got a device file found or on timeout on search occurs.
 *
 *  @param  sysdevice           Async Return value from the find call or NULL
 *                              if timeout
 *  @param  data1               User data (our own device in this case)
 *  @param  data2               User data
 */
void
class_device_got_device_file (HalDevice *dm, void *data1, void *data2)
{
	HalDevice *d = (HalDevice *) data1;
	HalDevice *sysdevice;
	ClassDeviceHandler *self = (ClassDeviceHandler *) data2;

	if (dm == NULL) {
		HAL_WARNING (("Never got device file for class device at %s", 
			      hal_device_property_get_string (d, ".udev.sysfs_path")));
		hal_device_store_remove (hald_get_tdl (), d);
		g_object_unref (d);
		return;
	}

	/* remove the property for the device file name target */
	hal_device_property_remove (d, ".target_dev");

	/* remove properties for udev reception */
	hal_device_property_remove (d, ".udev.sysfs_path");
	hal_device_property_remove (d, ".udev.class_name");

	if (self->merge_or_add) {
		const char *sysdevice_udi;
		/* get the sysdevice from the temporary cookie */
		sysdevice_udi = hal_device_property_get_string (d, ".sysdevice");
		assert (sysdevice_udi != NULL);
		sysdevice = hal_device_store_find (hald_get_gdl (),
						   sysdevice_udi);
		assert (sysdevice != NULL);
		hal_device_property_remove (d, ".sysdevice");
		
		/* now, do some post-processing */
		self->post_process (self, d, 
				    hal_device_property_get_string (d, ".udev.sysfs_path"),
				    NULL /** @todo FIXME */ );

		/* merge information from temporary device onto the physical
		 * device */
		hal_device_merge (sysdevice, d);

		/* get rid of temporary device */
		hal_device_store_remove (hald_get_tdl (), d);
		g_object_unref (d);

	} else {
		char *new_udi;
		HalDevice *new_d;
		/* Compute a proper UDI (unique device id) and try to locate a 
		 * persistent unplugged device or simply add this new device...
		 */
		new_udi = rename_and_merge (d, self->compute_udi, self->hal_class_name);
		if (new_udi != NULL) {
			new_d = hal_device_store_find (hald_get_gdl (),
						       new_udi);
			hal_device_store_add (hald_get_gdl (),
					      new_d != NULL ? new_d : d);
		}
		hal_device_store_remove (hald_get_tdl (), d);
		g_object_unref (d);
	}
}


/** Init function for block device handling. Does nothing.
 *
 */
void
class_device_init (ClassDeviceHandler *self)
{
}

/** This function is called when all device detection on startup is done
 *  in order to perform optional batch processing on devices. Does nothing.
 *
 */
void
class_device_detection_done (ClassDeviceHandler *self)
{
}

/** Shutdown function for block device handling. Does nothing.
 *
 */
void
class_device_shutdown (ClassDeviceHandler *self)
{
}


/** This method is called just before the device is either merged
 *  onto the sysdevice or added to the GDL (cf. merge_or_add). 
 *  This is useful for extracting more information about the device
 *  through e.g. ioctl's using the device file property and also
 *  for setting info.category|capability.
 *
 *  @param  self          Pointer to class members
 *  @param  d             The HalDevice object of the instance of
 *                        this device class
 *  @param  sysfs_path    The path in sysfs (including mount point) of
 *                        the class device in sysfs
 *  @param  class_device  Libsysfs object representing class device
 *                        instance
 */
void 
class_device_post_process (ClassDeviceHandler *self,
			   HalDevice *d,
			   const char *sysfs_path,
			   struct sysfs_class_device *class_device)
{
	/* this function is left intentionally blank */
}

/** Called regulary (every two seconds) for polling / monitoring on devices
 *  of this class
 *
 *  @param  self          Pointer to class members
 */
void
class_device_tick (ClassDeviceHandler *self)
{
}

/** Get the name of that the property that the device file should
 *  be put in. This generic implementation just uses sysfs_class_name
 *  appended with '.device'.
 *
 *  @param  self          Pointer to class members
 *  @param  d             The HalDevice object of the instance of
 *                        this device class
 *  @param  sysfs_path    The path in sysfs (including mount point) of
 *                        the class device in sysfs
 *  @param  class_device  Libsysfs object representing class device
 *                        instance
 *  @param  dev_file_prop Device file property name (out)
 *  @param  dev_file_prop_len  Maximum length of string
 */
void
class_device_get_device_file_target (ClassDeviceHandler *self,
				     HalDevice *d,
				     const char *sysfs_path,
				     struct sysfs_class_device *class_device,
				     char* dev_file_prop,
				     int dev_file_prop_len)
{
	snprintf (dev_file_prop, dev_file_prop_len, 
		  "%s.device", self->hal_class_name);
}


/** @} */
