
#include <byteswap.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <console/console.h>
#include <xenos/xenos.h>
#include <xenos/xe.h>
#include <xenon_smc/xenon_smc.h>
#include <input/input.h>
// Xenon includes
#include <libfat/fat.h>
#include <xenos/xenos.h>
#include <console/console.h>
#include <time/time.h>
#include <ppc/timebase.h>
#include <usb/usbmain.h>
#include <sys/iosupport.h>
#include <ppc/register.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xenon_nand/xenon_config.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_soc/xenon_io.h>
#include <xenon_sound/sound.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_smc/xenon_gpio.h>
#include <xb360/xb360.h>
#include <debug.h>
#include "asciiart.h"
#include "ogg.h"

void dumpana()
{
   int i;
   for (i = 0; i < 0x100; ++i)
   {
      uint32_t v;
      xenon_smc_ana_read(i, &v);
      printf("0x%08x, ", (unsigned int)v);
      if ((i & 0x7) == 0x7)
         printf(" // %02x\n", (unsigned int)(i & ~0x7));
   }
}

char FUSES[350]; /* this string stores the ascii dump of the fuses */

unsigned char stacks[6][0x10000];

void reset_timebase_task()
{
   mtspr(284, 0); // TBLW
   mtspr(285, 0); // TBUW
   mtspr(284, 0);
}

void DumpFuses()
{
   printf(" * FUSES - write them down and keep them safe:\n");
   char *fusestr = FUSES;
   for (int i = 0; i < 12; ++i)
   {
      u64 line;
      unsigned int hi, lo;

      line = xenon_secotp_read_line(i);
      hi = line >> 32;
      lo = line & 0xffffffff;

      fusestr += sprintf(fusestr, "fuseset %02d: %08x%08x\n", i, hi, lo);
   }
   printf(FUSES);

   print_cpu_dvd_keys();
   network_print_config();
}

void synchronize_timebases()
{
   xenon_thread_startup();

   std((void *)0x200611a0, 0); // stop timebase

   int i;
   for (i = 1; i < 6; ++i)
   {
      xenon_run_thread_task(i, &stacks[i][0xff00], (void *)reset_timebase_task);
      while (xenon_is_thread_task_running(i))
         ;
   }

   reset_timebase_task(); // don't forget thread 0

   std((void *)0x200611a0, 0x1ff); // restart timebase
}

static void extrnal_Storage_Setup()
{
   printf("Found Devices!\n");
   printf("Mouting Devices!\n");
   mount_all_devices();
}

/**
 * Controls cpu fan speed
 */
void xenon_set_cpu_fan_speed(unsigned val)
{

   unsigned char msg[16] = {0x94, (val & 0x7F) | 0x80};

   xenon_smc_send_message(msg);
}

/**
 * sets gpu fan speed
 */
void xenon_set_gpu_fan_speed(unsigned val)
{

   unsigned char msg[16] = {0x89, (val & 0x7F) | 0x80};

   xenon_smc_send_message(msg);
}

void do_asciiart()
{
   char *p = asciiart;
   while (*p)
      console_putch(*p++);
}

void playMusic()
{
}