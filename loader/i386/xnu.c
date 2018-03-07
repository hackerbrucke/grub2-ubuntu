/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2009  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/env.h>
#include <grub/file.h>
#include <grub/disk.h>
#include <grub/xnu.h>
#include <grub/cpu/xnu.h>
#include <grub/mm.h>
#include <grub/loader.h>
#include <grub/autoefi.h>
#include <grub/i386/tsc.h>
#include <grub/efi/api.h>
#include <grub/i386/pit.h>
#include <grub/misc.h>
#include <grub/charset.h>
#include <grub/term.h>
#include <grub/command.h>
#include <grub/gzio.h>

char grub_xnu_cmdline[1024];

/* Aliases set for some tables. */
struct tbl_alias
{
  grub_efi_guid_t guid;
  char *name;
};

struct tbl_alias table_aliases[] =
  {
    {GRUB_EFI_ACPI_20_TABLE_GUID, "ACPI_20"},
    {GRUB_EFI_ACPI_TABLE_GUID, "ACPI"},
  };

struct grub_xnu_devprop_device_descriptor
{
  struct grub_xnu_devprop_device_descriptor *next;
  struct property_descriptor *properties;
  struct grub_efi_device_path *path;
  int pathlen;
};

/* The following function is used to be able to debug xnu loader
   with grub-emu. */
#ifdef GRUB_UTIL
static grub_err_t
grub_xnu_launch (void)
{
  grub_printf ("Fake launch %x:%p:%p", grub_xnu_entry_point, grub_xnu_arg1,
	       grub_xnu_stack);
  grub_getkey ();
  return 0;
}
#else
static void (*grub_xnu_launch) (void) = 0;
#endif

static int
utf16_strlen (grub_uint16_t *in)
{
  int i;
  for (i = 0; in[i]; i++);
  return i;
}

/* Read frequency from a string in MHz and return it in Hz. */
static grub_uint64_t
readfrequency (const char *str)
{
  grub_uint64_t num = 0;
  int mul = 1000000;
  int found = 0;

  while (*str)
    {
      unsigned long digit;

      digit = grub_tolower (*str) - '0';
      if (digit > 9)
	break;

      found = 1;

      num = num * 10 + digit;
      str++;
    }
  num *= 1000000;
  if (*str == '.')
    {
      str++;
      while (*str)
	{
	  unsigned long digit;

	  digit = grub_tolower (*str) - '0';
	  if (digit > 9)
	    break;

	  found = 1;

	  mul /= 10;
	  num = num + mul * digit;
	  str++;
	}
    }
  if (! found)
    return 0;

  return num;
}

/* Thanks to Kabyl for precious information about Intel architecture. */
static grub_uint64_t
guessfsb (void)
{
  const grub_uint64_t sane_value = 100000000;
  grub_uint32_t manufacturer[3], max_cpuid, capabilities, msrlow;
  grub_uint64_t start_tsc;
  grub_uint64_t end_tsc;
  grub_uint64_t tsc_ticks_per_ms;

  if (! grub_cpu_is_cpuid_supported ())
    return sane_value;

#ifdef APPLE_CC
  asm volatile ("movl $0, %%eax\n"
#ifdef __x86_64__
		"push %%rbx\n"
#else
		"push %%ebx\n"
#endif
		"cpuid\n"
#ifdef __x86_64__
		"pop %%rbx\n"
#else
		"pop %%ebx\n"
#endif
		: "=a" (max_cpuid),
		  "=d" (manufacturer[1]), "=c" (manufacturer[2]));

  /* Only Intel for now is done. */
  if (grub_memcmp (manufacturer + 1, "ineIntel", 12) != 0)
    return sane_value;

#else
  asm volatile ("movl $0, %%eax\n"
		"cpuid"
		: "=a" (max_cpuid), "=b" (manufacturer[0]),
		  "=d" (manufacturer[1]), "=c" (manufacturer[2]));

  /* Only Intel for now is done. */
  if (grub_memcmp (manufacturer, "GenuineIntel", 12) != 0)
    return sane_value;
#endif

  /* Check Speedstep. */
  if (max_cpuid < 1)
    return sane_value;

#ifdef APPLE_CC
  asm volatile ("movl $1, %%eax\n"
#ifdef __x86_64__
		"push %%rbx\n"
#else
		"push %%ebx\n"
#endif
		"cpuid\n"
#ifdef __x86_64__
		"pop %%rbx\n"
#else
		"pop %%ebx\n"
#endif
		: "=c" (capabilities):
		: "%rax", "%rdx");
#else
  asm volatile ("movl $1, %%eax\n"
		"cpuid"
		: "=c" (capabilities):
		: "%rax", "%rbx", "%rdx");
#endif

  if (! (capabilities & (1 << 7)))
    return sane_value;

  /* Calibrate the TSC rate. */

  start_tsc = grub_get_tsc ();
  grub_pit_wait (0xffff);
  end_tsc = grub_get_tsc ();

  tsc_ticks_per_ms = grub_divmod64 (end_tsc - start_tsc, 55, 0);

  /* Read the multiplier. */
  asm volatile ("movl $0x198, %%ecx\n"
		"rdmsr"
		: "=d" (msrlow)
		:
		: "%ecx", "%eax");

  return grub_divmod64 (2000 * tsc_ticks_per_ms,
			((msrlow >> 7) & 0x3e) + ((msrlow >> 14) & 1), 0);
}

struct property_descriptor
{
  struct property_descriptor *next;
  grub_uint8_t *name;
  grub_uint16_t *name16;
  int name16len;
  int length;
  void *data;
};

struct grub_xnu_devprop_device_descriptor *devices = 0;

grub_err_t
grub_xnu_devprop_remove_property (struct grub_xnu_devprop_device_descriptor *dev,
				  char *name)
{
  struct property_descriptor *prop;
  prop = grub_named_list_find (GRUB_AS_NAMED_LIST_P (&dev->properties), name);
  if (!prop)
    return GRUB_ERR_NONE;

  grub_free (prop->name);
  grub_free (prop->name16);
  grub_free (prop->data);

  grub_list_remove (GRUB_AS_LIST_P (&dev->properties), GRUB_AS_LIST (prop));

  return GRUB_ERR_NONE;
}

grub_err_t
grub_xnu_devprop_remove_device (struct grub_xnu_devprop_device_descriptor *dev)
{
  void *t;
  struct property_descriptor *prop;

  grub_list_remove (GRUB_AS_LIST_P (&devices), GRUB_AS_LIST (dev));

  for (prop = dev->properties; prop; )
    {
      grub_free (prop->name);
      grub_free (prop->name16);
      grub_free (prop->data);
      t = prop;
      prop = prop->next;
      grub_free (t);
    }

  grub_free (dev->path);
  grub_free (dev);

  return GRUB_ERR_NONE;
}

struct grub_xnu_devprop_device_descriptor *
grub_xnu_devprop_add_device (struct grub_efi_device_path *path, int length)
{
  struct grub_xnu_devprop_device_descriptor *ret;

  ret = grub_zalloc (sizeof (*ret));
  if (!ret)
    return 0;

  ret->path = grub_malloc (length);
  if (!ret->path)
    {
      grub_free (ret);
      return 0;
    }
  ret->pathlen = length;
  grub_memcpy (ret->path, path, length);

  grub_list_push (GRUB_AS_LIST_P (&devices), GRUB_AS_LIST (ret));

  return ret;
}

static grub_err_t
grub_xnu_devprop_add_property (struct grub_xnu_devprop_device_descriptor *dev,
			       grub_uint8_t *utf8, grub_uint16_t *utf16,
			       int utf16len, void *data, int datalen)
{
  struct property_descriptor *prop;

  prop = grub_malloc (sizeof (*prop));
  if (!prop)
    return grub_errno;

  prop->name = utf8;
  prop->name16 = utf16;
  prop->name16len = utf16len;

  prop->length = datalen;
  prop->data = grub_malloc (prop->length);
  if (!prop->data)
    {
      grub_free (prop);
      grub_free (prop->name);
      grub_free (prop->name16);
      return grub_errno;
    }
  grub_memcpy (prop->data, data, prop->length);
  grub_list_push (GRUB_AS_LIST_P (&dev->properties),
		  GRUB_AS_LIST (prop));
  return GRUB_ERR_NONE;
}

grub_err_t
grub_xnu_devprop_add_property_utf8 (struct grub_xnu_devprop_device_descriptor *dev,
				    char *name, void *data, int datalen)
{
  grub_uint8_t *utf8;
  grub_uint16_t *utf16;
  int len, utf16len;
  grub_err_t err;

  utf8 = (grub_uint8_t *) grub_strdup (name);
  if (!utf8)
    return grub_errno;

  len = grub_strlen (name);
  utf16 = grub_malloc (sizeof (grub_uint16_t) * len);
  if (!utf16)
    {
      grub_free (utf8);
      return grub_errno;
    }

  utf16len = grub_utf8_to_utf16 (utf16, len, utf8, len, NULL);
  if (utf16len < 0)
    {
      grub_free (utf8);
      grub_free (utf16);
      return grub_errno;
    }

  err = grub_xnu_devprop_add_property (dev, utf8, utf16,
				       utf16len, data, datalen);
  if (err)
    {
      grub_free (utf8);
      grub_free (utf16);
      return err;
    }

  return GRUB_ERR_NONE;
}

grub_err_t
grub_xnu_devprop_add_property_utf16 (struct grub_xnu_devprop_device_descriptor *dev,
				     grub_uint16_t *name, int namelen,
				     void *data, int datalen)
{
  grub_uint8_t *utf8;
  grub_uint16_t *utf16;
  grub_err_t err;

  utf16 = grub_malloc (sizeof (grub_uint16_t) * namelen);
  if (!utf16)
    return grub_errno;
  grub_memcpy (utf16, name, sizeof (grub_uint16_t) * namelen);

  utf8 = grub_malloc (namelen * 4 + 1);
  if (!utf8)
    {
      grub_free (utf8);
      return grub_errno;
    }

  *grub_utf16_to_utf8 ((grub_uint8_t *) utf8, name, namelen) = '\0';

  err = grub_xnu_devprop_add_property (dev, utf8, utf16,
				       namelen, data, datalen);
  if (err)
    {
      grub_free (utf8);
      grub_free (utf16);
      return err;
    }

  return GRUB_ERR_NONE;
}

static inline int
hextoval (char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z')
    return c - 'A' + 10;
  return 0;
}

void
grub_cpu_xnu_unload (void)
{
  struct grub_xnu_devprop_device_descriptor *dev1, *dev2;

  for (dev1 = devices; dev1; )
    {
      dev2 = dev1->next;
      grub_xnu_devprop_remove_device (dev1);
      dev1 = dev2;
    }
}

static grub_err_t
grub_cpu_xnu_fill_devprop (void)
{
  struct grub_xnu_devtree_key *efikey;
  int total_length = sizeof (struct grub_xnu_devprop_header);
  struct grub_xnu_devtree_key *devprop;
  struct grub_xnu_devprop_device_descriptor *device;
  void *ptr;
  struct grub_xnu_devprop_header *head;
  void *t;
  int numdevs = 0;

  /* The key "efi". */
  efikey = grub_xnu_create_key (&grub_xnu_devtree_root, "efi");
  if (! efikey)
    return grub_errno;

  for (device = devices; device; device = device->next)
    {
      struct property_descriptor *propdesc;
      total_length += sizeof (struct grub_xnu_devprop_device_header);
      total_length += device->pathlen;

      for (propdesc = device->properties; propdesc; propdesc = propdesc->next)
	{
	  total_length += sizeof (grub_uint32_t);
	  total_length += sizeof (grub_uint16_t)
	    * (propdesc->name16len + 1);
	  total_length += sizeof (grub_uint32_t);
	  total_length += propdesc->length;
	}
      numdevs++;
    }

  devprop = grub_xnu_create_value (&(efikey->first_child), "device-properties");
  if (devprop)
    {
      devprop->data = grub_malloc (total_length);
      devprop->datasize = total_length;
    }

  ptr = devprop->data;
  head = ptr;
  ptr = head + 1;
  head->length = total_length;
  head->alwaysone = 1;
  head->num_devices = numdevs;
  for (device = devices; device; )
    {
      struct grub_xnu_devprop_device_header *devhead;
      struct property_descriptor *propdesc;
      devhead = ptr;
      devhead->num_values = 0;
      ptr = devhead + 1;

      grub_memcpy (ptr, device->path, device->pathlen);
      ptr = (char *) ptr + device->pathlen;

      for (propdesc = device->properties; propdesc; )
	{
	  grub_uint32_t *len;
	  grub_uint16_t *name;
	  void *data;

	  len = ptr;
	  *len = 2 * propdesc->name16len + sizeof (grub_uint16_t)
	    + sizeof (grub_uint32_t);
	  ptr = len + 1;

	  name = ptr;
	  grub_memcpy (name, propdesc->name16, 2 * propdesc->name16len);
	  name += propdesc->name16len;

	  /* NUL terminator.  */
	  *name = 0;
	  ptr = name + 1;

	  len = ptr;
	  *len = propdesc->length + sizeof (grub_uint32_t);
	  data = len + 1;
	  ptr = data;
	  grub_memcpy (ptr, propdesc->data, propdesc->length);
	  ptr = (char *) ptr + propdesc->length;

	  grub_free (propdesc->name);
	  grub_free (propdesc->name16);
	  grub_free (propdesc->data);
	  t = propdesc;
	  propdesc = propdesc->next;
	  grub_free (t);
	  devhead->num_values++;
	}

      devhead->length = (char *) ptr - (char *) devhead;
      t = device;
      device = device->next;
      grub_free (t);
    }

  devices = 0;

  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_devprop_load (grub_command_t cmd __attribute__ ((unused)),
		       int argc, char *args[])
{
  grub_file_t file;
  void *buf, *bufstart, *bufend;
  struct grub_xnu_devprop_header *head;
  grub_size_t size;
  unsigned i, j;

  if (argc != 1)
    return grub_error (GRUB_ERR_BAD_ARGUMENT, "File name required. ");

  file = grub_gzfile_open (args[0], 1);
  if (! file)
    return grub_error (GRUB_ERR_FILE_NOT_FOUND,
		       "Couldn't load device-propertie dump. ");
  size = grub_file_size (file);
  buf = grub_malloc (size);
  if (!buf)
    {
      grub_file_close (file);
      return grub_errno;
    }
  if (grub_file_read (file, buf, size) != (grub_ssize_t) size)
    {
      grub_file_close (file);
      return grub_errno;
    }
  grub_file_close (file);

  bufstart = buf;
  bufend = (char *) buf + size;
  head = buf;
  buf = head + 1;
  for (i = 0; i < grub_le_to_cpu32 (head->num_devices) && buf < bufend; i++)
    {
      struct grub_efi_device_path *dp, *dpstart;
      struct grub_xnu_devprop_device_descriptor *dev;
      struct grub_xnu_devprop_device_header *devhead;

      devhead = buf;
      buf = devhead + 1;
      dpstart = buf;

      do
	{
	  dp = buf;
	  buf = (char *) buf + GRUB_EFI_DEVICE_PATH_LENGTH (dp);
	}
      while (!GRUB_EFI_END_ENTIRE_DEVICE_PATH (dp) && buf < bufend);

      dev = grub_xnu_devprop_add_device (dpstart, (char *) buf
					 - (char *) dpstart);

      for (j = 0; j < grub_le_to_cpu32 (devhead->num_values) && buf < bufend;
	   j++)
	{
	  grub_uint32_t *namelen;
	  grub_uint32_t *datalen;
	  grub_uint16_t *utf16;
	  void *data;
	  grub_err_t err;

	  namelen = buf;
	  buf = namelen + 1;
	  if (buf >= bufend)
	    break;

	  utf16 = buf;
	  buf = (char *) buf + *namelen - sizeof (grub_uint32_t);
	  if (buf >= bufend)
	    break;

	  datalen = buf;
	  buf = datalen + 1;
	  if (buf >= bufend)
	    break;

	  data = buf;
	  buf = (char *) buf + *datalen - sizeof (grub_uint32_t);
	  if (buf >= bufend)
	    break;
	  err = grub_xnu_devprop_add_property_utf16
	    (dev, utf16, (*namelen - sizeof (grub_uint32_t)
			  - sizeof (grub_uint16_t)) / sizeof (grub_uint16_t),
	     data, *datalen - sizeof (grub_uint32_t));
	  if (err)
	    {
	      grub_free (bufstart);
	      return err;
	    }
	}
    }

  grub_free (bufstart);
  return GRUB_ERR_NONE;
}

/* Fill device tree. */
/* FIXME: some entries may be platform-agnostic. Move them to loader/xnu.c. */
grub_err_t
grub_cpu_xnu_fill_devicetree (void)
{
  struct grub_xnu_devtree_key *efikey;
  struct grub_xnu_devtree_key *cfgtablekey;
  struct grub_xnu_devtree_key *curval;
  struct grub_xnu_devtree_key *runtimesrvkey;
  struct grub_xnu_devtree_key *platformkey;
  unsigned i, j;

  /* The value "model". */
  /* FIXME: may this value be sometimes different? */
  curval = grub_xnu_create_value (&grub_xnu_devtree_root, "model");
  if (! curval)
    return grub_errno;
  curval->datasize = sizeof ("ACPI");
  curval->data = grub_strdup ("ACPI");
  curval = grub_xnu_create_value (&grub_xnu_devtree_root, "compatible");
  if (! curval)
    return grub_errno;
  curval->datasize = sizeof ("ACPI");
  curval->data = grub_strdup ("ACPI");

  /* The key "efi". */
  efikey = grub_xnu_create_key (&grub_xnu_devtree_root, "efi");
  if (! efikey)
    return grub_errno;

  /* Information about firmware. */
  curval = grub_xnu_create_value (&(efikey->first_child), "firmware-revision");
  if (! curval)
    return grub_errno;
  curval->datasize = (SYSTEM_TABLE_SIZEOF (firmware_revision));
  curval->data = grub_malloc (curval->datasize);
  if (! curval->data)
    return grub_errno;
  grub_memcpy (curval->data, (SYSTEM_TABLE_VAR(firmware_revision)),
	       curval->datasize);

  curval = grub_xnu_create_value (&(efikey->first_child), "firmware-vendor");
  if (! curval)
    return grub_errno;
  curval->datasize =
    2 * (utf16_strlen (SYSTEM_TABLE_PTR (firmware_vendor)) + 1);
  curval->data = grub_malloc (curval->datasize);
  if (! curval->data)
    return grub_errno;
  grub_memcpy (curval->data, SYSTEM_TABLE_PTR (firmware_vendor),
	       curval->datasize);

  curval = grub_xnu_create_value (&(efikey->first_child), "firmware-abi");
  if (! curval)
    return grub_errno;
  curval->datasize = sizeof ("EFI32");
  curval->data = grub_malloc (curval->datasize);
  if (! curval->data)
    return grub_errno;
  if (SIZEOF_OF_UINTN == 4)
    grub_memcpy (curval->data, "EFI32", curval->datasize);
  else
    grub_memcpy (curval->data, "EFI64", curval->datasize);

  /* The key "platform". */
  platformkey = grub_xnu_create_key (&(efikey->first_child),
				     "platform");
  if (! platformkey)
    return grub_errno;

  /* Pass FSB frequency to the kernel. */
  curval = grub_xnu_create_value (&(platformkey->first_child), "FSBFrequency");
  if (! curval)
    return grub_errno;
  curval->datasize = sizeof (grub_uint64_t);
  curval->data = grub_malloc (curval->datasize);
  if (!curval->data)
    return grub_errno;

  /* First see if user supplies the value. */
  char *fsbvar = grub_env_get ("fsb");
  if (! fsbvar)
    *((grub_uint64_t *) curval->data) = 0;
  else
    *((grub_uint64_t *) curval->data) = readfrequency (fsbvar);
  /* Try autodetect. */
  if (! *((grub_uint64_t *) curval->data))
    *((grub_uint64_t *) curval->data) = guessfsb ();
  grub_dprintf ("xnu", "fsb autodetected as %llu\n",
		(unsigned long long) *((grub_uint64_t *) curval->data));

  cfgtablekey = grub_xnu_create_key (&(efikey->first_child),
				     "configuration-table");
  if (!cfgtablekey)
    return grub_errno;

  /* Fill "configuration-table" key. */
  for (i = 0; i < SYSTEM_TABLE (num_table_entries); i++)
    {
      void *ptr;
      struct grub_xnu_devtree_key *curkey;
      grub_efi_guid_t guid;
      char guidbuf[64];

      /* Retrieve current key. */
#ifdef GRUB_MACHINE_EFI
      {
	ptr = (void *)
	  grub_efi_system_table->configuration_table[i].vendor_table;
	guid = grub_efi_system_table->configuration_table[i].vendor_guid;
      }
#else
      if (SIZEOF_OF_UINTN == 4)
	{
	  ptr = UINT_TO_PTR (((grub_efiemu_configuration_table32_t *)
			      SYSTEM_TABLE_PTR (configuration_table))[i]
			     .vendor_table);
	  guid =
	    ((grub_efiemu_configuration_table32_t *)
	     SYSTEM_TABLE_PTR (configuration_table))[i].vendor_guid;
	}
      else
	{
	  ptr = UINT_TO_PTR (((grub_efiemu_configuration_table64_t *)
			      SYSTEM_TABLE_PTR (configuration_table))[i]
			     .vendor_table);
	  guid =
	    ((grub_efiemu_configuration_table64_t *)
	     SYSTEM_TABLE_PTR (configuration_table))[i].vendor_guid;
	}
#endif

      /* The name of key for new table. */
      grub_sprintf (guidbuf, "%08x-%04x-%04x-%02x%02x-",
		    guid.data1, guid.data2, guid.data3, guid.data4[0],
		    guid.data4[1]);
      for (j = 2; j < 8; j++)
	grub_sprintf (guidbuf + grub_strlen (guidbuf), "%02x", guid.data4[j]);
      /* For some reason GUID has to be in uppercase. */
      for (j = 0; guidbuf[j] ; j++)
	if (guidbuf[j] >= 'a' && guidbuf[j] <= 'f')
	  guidbuf[j] += 'A' - 'a';
      curkey = grub_xnu_create_key (&(cfgtablekey->first_child), guidbuf);
      if (! curkey)
	return grub_errno;

      curval = grub_xnu_create_value (&(curkey->first_child), "guid");
      if (! curval)
	return grub_errno;
      curval->datasize = sizeof (guid);
      curval->data = grub_malloc (curval->datasize);
      if (! curval->data)
	return grub_errno;
      grub_memcpy (curval->data, &guid, curval->datasize);

      /* The value "table". */
      curval = grub_xnu_create_value (&(curkey->first_child), "table");
      if (! curval)
	return grub_errno;
      curval->datasize = SIZEOF_OF_UINTN;
      curval->data = grub_malloc (curval->datasize);
      if (! curval->data)
	return grub_errno;
      if (SIZEOF_OF_UINTN == 4)
	*((grub_uint32_t *)curval->data) = PTR_TO_UINT32 (ptr);
      else
	*((grub_uint64_t *)curval->data) = PTR_TO_UINT64 (ptr);

      /* Create alias. */
      for (j = 0; j < sizeof (table_aliases) / sizeof (table_aliases[0]); j++)
	if (grub_memcmp (&table_aliases[j].guid, &guid, sizeof (guid)) == 0)
	  break;
      if (j != sizeof (table_aliases) / sizeof (table_aliases[0]))
	{
	  curval = grub_xnu_create_value (&(curkey->first_child), "alias");
	  if (!curval)
	    return grub_errno;
	  curval->datasize = grub_strlen (table_aliases[j].name) + 1;
	  curval->data = grub_malloc (curval->datasize);
	  if (!curval->data)
	    return grub_errno;
	  grub_memcpy (curval->data, table_aliases[j].name, curval->datasize);
	}
    }

  /* Create and fill "runtime-services" key. */
  runtimesrvkey = grub_xnu_create_key (&(efikey->first_child),
				       "runtime-services");
  if (! runtimesrvkey)
    return grub_errno;
  curval = grub_xnu_create_value (&(runtimesrvkey->first_child), "table");
  if (! curval)
    return grub_errno;
  curval->datasize = SIZEOF_OF_UINTN;
  curval->data = grub_malloc (curval->datasize);
  if (! curval->data)
    return grub_errno;
  if (SIZEOF_OF_UINTN == 4)
    *((grub_uint32_t *) curval->data)
      = PTR_TO_UINT32 (SYSTEM_TABLE_PTR (runtime_services));
  else
    *((grub_uint64_t *) curval->data)
      = PTR_TO_UINT64 (SYSTEM_TABLE_PTR (runtime_services));

  return GRUB_ERR_NONE;
}

/* Boot xnu. */
grub_err_t
grub_xnu_boot (void)
{
  struct grub_xnu_boot_params *bootparams_relloc;
  grub_off_t bootparams_relloc_off;
  grub_off_t mmap_relloc_off;
  grub_err_t err;
  grub_efi_uintn_t memory_map_size = 0;
  grub_efi_memory_descriptor_t *memory_map;
  grub_efi_uintn_t map_key = 0;
  grub_efi_uintn_t descriptor_size = 0;
  grub_efi_uint32_t descriptor_version = 0;
  grub_uint64_t firstruntimepage, lastruntimepage;
  grub_uint64_t curruntimepage;
  void *devtree;
  grub_size_t devtreelen;
  int i;

  err = grub_autoefi_prepare ();
  if (err)
    return err;

  err = grub_cpu_xnu_fill_devprop ();
  if (err)
    return err;

  err = grub_cpu_xnu_fill_devicetree ();
  if (err)
    return err;

  err = grub_xnu_fill_devicetree ();
  if (err)
    return err;

  /* Page-align to avoid following parts to be inadvertently freed. */
  err = grub_xnu_align_heap (GRUB_XNU_PAGESIZE);
  if (err)
    return err;

  /* Pass memory map to kernel. */
  memory_map_size = 0;
  memory_map = 0;
  map_key = 0;
  descriptor_size = 0;
  descriptor_version = 0;

  grub_dprintf ("xnu", "eip=%x\n", grub_xnu_entry_point);

  const char *debug = grub_env_get ("debug");

  if (debug && (grub_strword (debug, "all") || grub_strword (debug, "xnu")))
    {
      grub_printf ("Press any key to launch xnu\n");
      grub_getkey ();
    }

  /* Relocate the boot parameters to heap. */
  bootparams_relloc = grub_xnu_heap_malloc (sizeof (*bootparams_relloc));
  if (! bootparams_relloc)
    return grub_errno;
  bootparams_relloc_off = (grub_uint8_t *) bootparams_relloc
    - (grub_uint8_t *) grub_xnu_heap_start;

  /* Set video. */
  err = grub_xnu_set_video (bootparams_relloc);
  if (err != GRUB_ERR_NONE)
    {
      grub_print_error ();
      grub_errno = GRUB_ERR_NONE;
      grub_printf ("Booting in blind mode\n");

      bootparams_relloc->lfb_mode = 0;
      bootparams_relloc->lfb_width = 0;
      bootparams_relloc->lfb_height = 0;
      bootparams_relloc->lfb_depth = 0;
      bootparams_relloc->lfb_line_len = 0;
      bootparams_relloc->lfb_base = 0;
    }

  if (grub_autoefi_get_memory_map (&memory_map_size, memory_map,
				   &map_key, &descriptor_size,
				   &descriptor_version) < 0)
    return grub_errno;

  /* We will do few allocations later. Reserve some space for possible
     memory map growth.  */
  memory_map_size += 20 * descriptor_size;
  memory_map = grub_xnu_heap_malloc (memory_map_size);
  if (! memory_map)
    return grub_errno;
  mmap_relloc_off = (grub_uint8_t *) memory_map
    - (grub_uint8_t *) grub_xnu_heap_start;

  err = grub_xnu_writetree_toheap (&devtree, &devtreelen);
  if (err)
    return err;
  bootparams_relloc = (struct grub_xnu_boot_params *)
    (bootparams_relloc_off + (grub_uint8_t *) grub_xnu_heap_start);

  grub_memcpy (bootparams_relloc->cmdline, grub_xnu_cmdline,
	       sizeof (bootparams_relloc->cmdline));

  bootparams_relloc->devtree = ((char *) devtree - grub_xnu_heap_start)
    + grub_xnu_heap_will_be_at;
  bootparams_relloc->devtreelen = devtreelen;

  memory_map = (grub_efi_memory_descriptor_t *)
    ((grub_uint8_t *) grub_xnu_heap_start + mmap_relloc_off);

  if (grub_autoefi_get_memory_map (&memory_map_size, memory_map,
				   &map_key, &descriptor_size,
				   &descriptor_version) <= 0)
    return grub_errno;

  bootparams_relloc->efi_system_table
    = PTR_TO_UINT32 (grub_autoefi_system_table);

  firstruntimepage = (((grub_addr_t) grub_xnu_heap_will_be_at
		       + grub_xnu_heap_size + GRUB_XNU_PAGESIZE - 1)
		      / GRUB_XNU_PAGESIZE) + 20;
  curruntimepage = firstruntimepage;

  for (i = 0; (unsigned) i < memory_map_size / descriptor_size; i++)
    {
      grub_efi_memory_descriptor_t *curdesc = (grub_efi_memory_descriptor_t *)
	((char *) memory_map + descriptor_size * i);

      curdesc->virtual_start = curdesc->physical_start;

      if (curdesc->type == GRUB_EFI_RUNTIME_SERVICES_DATA
	  || curdesc->type == GRUB_EFI_RUNTIME_SERVICES_CODE)
	{
	  curdesc->virtual_start = curruntimepage << 12;
	  curruntimepage += curdesc->num_pages;
	  if (curdesc->physical_start
	      <= PTR_TO_UINT64 (grub_autoefi_system_table)
	      && curdesc->physical_start + (curdesc->num_pages << 12)
	      > PTR_TO_UINT64 (grub_autoefi_system_table))
	    bootparams_relloc->efi_system_table
	      = PTR_TO_UINT64 (grub_autoefi_system_table)
	      - curdesc->physical_start + curdesc->virtual_start;
	  if (SIZEOF_OF_UINTN == 8 && grub_xnu_is_64bit)
	    curdesc->virtual_start |= 0xffffff8000000000ULL;
	}
    }

  lastruntimepage = curruntimepage;

  bootparams_relloc->efi_mmap = grub_xnu_heap_will_be_at + mmap_relloc_off;
  bootparams_relloc->efi_mmap_size = memory_map_size;
  bootparams_relloc->efi_mem_desc_size = descriptor_size;
  bootparams_relloc->efi_mem_desc_version = descriptor_version;

  bootparams_relloc->heap_start = grub_xnu_heap_will_be_at;
  bootparams_relloc->heap_size = grub_xnu_heap_size;
  bootparams_relloc->efi_runtime_first_page = firstruntimepage;

  bootparams_relloc->efi_runtime_npages = lastruntimepage - firstruntimepage;
  bootparams_relloc->efi_uintnbits = SIZEOF_OF_UINTN * 8;

  bootparams_relloc->verminor = GRUB_XNU_BOOTARGS_VERMINOR;
  bootparams_relloc->vermajor = GRUB_XNU_BOOTARGS_VERMAJOR;

  /* Parameters for asm helper. */
  grub_xnu_stack = bootparams_relloc->heap_start
    + bootparams_relloc->heap_size + GRUB_XNU_PAGESIZE;
  grub_xnu_arg1 = bootparams_relloc_off + grub_xnu_heap_will_be_at;
#ifndef GRUB_UTIL
  grub_xnu_launch = (void (*) (void))
    (grub_xnu_heap_start + grub_xnu_heap_size);
#endif

  grub_memcpy (grub_xnu_heap_start + grub_xnu_heap_size,
	       grub_xnu_launcher_start,
	       grub_xnu_launcher_end - grub_xnu_launcher_start);


  if (! grub_autoefi_exit_boot_services (map_key))
    return grub_error (GRUB_ERR_IO, "can't exit boot services");

  grub_autoefi_set_virtual_address_map (memory_map_size, descriptor_size,
					descriptor_version,memory_map);

  grub_xnu_launch ();

  /* Never reaches here. */
  return 0;
}

static grub_command_t cmd_devprop_load;

void
grub_cpu_xnu_init (void)
{
  cmd_devprop_load = grub_register_command ("xnu_devprop_load",
					    grub_cmd_devprop_load,
					    0, "Load device-properties dump.");
}

void
grub_cpu_xnu_fini (void)
{
  grub_unregister_command (cmd_devprop_load);
}
