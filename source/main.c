/**
 * 
 * Learning xbox 360 homebrew dev in c 
 * By Nicholas Blackburn & docker!
 */
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
#include "ogg.h"
#include "con.h"

#define MAX_UNPLAYED 32768
#define BUFFER_SIZE 65536
static char buffer[BUFFER_SIZE];
static unsigned int freq;
static unsigned int real_freq;
static double freq_ratio;
static int is_60Hz;
// NOTE: 32khz actually uses ~2136 bytes/frame @ 60hz
static enum { BUFFER_SIZE_32_60 = 2112,
              BUFFER_SIZE_48_60 = 3200,
              BUFFER_SIZE_32_50 = 2560,
              BUFFER_SIZE_48_50 = 3840 } buffer_size = 1024;

static unsigned char thread_stack[0x10000];
static unsigned int thread_lock __attribute__((aligned(128))) = 0;
static volatile void *thread_buffer = NULL;
static volatile int thread_bufsize = 0;
static int thread_bufmaxsize = 0;
static volatile int thread_terminate = 0;

char FUSES[350]; /* this string stores the ascii dump of the fuses */

unsigned char stacks[6][0x10000];

/**
 * Main function initss and sets up xbox control
 */

int main()
{

   // Over Drives Xbox
   xenon_make_it_faster(XENON_SPEED_FULL);

   xenon_sound_init();
   xenos_init(VIDEO_MODE_AUTO);
   console_init();

   usb_init();
   usb_do_poll();
   do_asciiart();

   printf("\n");
   printf("press a to See temp\n");
   printf("\n");
   printf("press b IS UNUSED RN\n");
   printf("\n");
   printf(" press x to close program\n");
   printf("\n");
   printf(" press y to play sound! (Does not work!!)\n");
   printf("\n");
   printf(" press back to go to main menu\n");
   printf("\n");
   printf(" press start to crash xbox!\n");
   printf("\n");
   printf(" press dpad to switch text colors to red,green,blue, yellow\n");
   printf("\n");
   printf("Press rb to dump nand!\n");
   printf("\n");
   printf("press lb to dump fuses!\n");

   unsigned int audiobuf[32];
   uint8_t buf[16];
   float CPU_TMP = 0, GPU_TMP = 0, MEM_TMP = 0, MOBO_TMP = 0;
   struct controller_data_s oldc;
   FILE *audio;


   while (1)
   {

      /* gets the Temp Sensor Data via buffer*/
      memset(buf, 0, 16);
      buf[0] = 0x07;

      xenon_smc_send_message(buf);
      xenon_smc_receive_response(buf);

      struct controller_data_s c;
      if (get_controller_data(&c, 0))
      {

         if ((c.a) && (!oldc.a))
         {
            printf("\n");
            printf("Set Fan speed to 100%!\n");
            xenon_set_cpu_fan_speed(100);
            printf("\n");
         }
         else if ((c.b) && (!oldc.b))
         {
            printf("\n");
            printf("DOes nothing rn \n");
         }

         else if ((c.x) && (!oldc.x))
         {
            printf("\n");
            printf("exiting..\n");
            xenon_smc_power_reboot();
         }
         else if ((c.y) && (!oldc.y))
         {
         }

         else if ((c.start) && (!oldc.start))
         {
            printf("\n");
            printf("Crashes Xbox 360!\n");
            console_clrscr();
            stack_trace(100);
         }

         else if ((c.back) && (!oldc.back))
         {
            console_clrscr();
            printf("goes back to main\n");
            main();
         }

         else if ((c.up) && (!oldc.up))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_RED);
            main();
         }
         else if ((c.down) && (!oldc.down))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_GREEN);
            main();
         }

         else if ((c.right) && (!oldc.right))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_BLUE);
            main();
         }

         else if ((c.left) && (!oldc.left))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_YELLOW);
            main();
         }
         else if ((c.rb) && (!oldc.rb))
         {
            console_clrscr();
            printf("Dumping nand to txt output!\n will not save nand\n");
            dumpana();
            delay(5);
            main();
         }
         else if ((c.lb) && (!oldc.lb))
         {
            console_clrscr();
            DumpFuses();
            delay(5);
            main();
         }
         oldc = c;
      }
      usb_do_poll();
   }
}