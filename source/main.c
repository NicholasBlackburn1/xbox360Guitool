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
#include "video_init.h"
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


void dumpana() {
	int i;
	for (i = 0; i < 0x100; ++i)
	{
		uint32_t v;
		xenon_smc_ana_read(i, &v);
		printf("0x%08x, ", (unsigned int)v);
		if ((i&0x7)==0x7)
			printf(" // %02x\n", (unsigned int)(i &~0x7));
	}
}

char FUSES[350]; /* this string stores the ascii dump of the fuses */

unsigned char stacks[6][0x10000];

void reset_timebase_task()
{
	mtspr(284,0); // TBLW
	mtspr(285,0); // TBUW
	mtspr(284,0);
}

void DumpFuses(){
   printf(" * FUSES - write them down and keep them safe:\n");
	char *fusestr = FUSES;
	for (int i=0; i<12; ++i){
		u64 line;
		unsigned int hi,lo;

		line=xenon_secotp_read_line(i);
		hi=line>>32;
		lo=line&0xffffffff;

		fusestr += sprintf(fusestr, "fuseset %02d: %08x%08x\n", i, hi, lo);
	}
	printf(FUSES);

	print_cpu_dvd_keys();
	network_print_config();
}

void synchronize_timebases()
{
	xenon_thread_startup();
	
	std((void*)0x200611a0,0); // stop timebase
	
	int i;
	for(i=1;i<6;++i){
		xenon_run_thread_task(i,&stacks[i][0xff00],(void *)reset_timebase_task);
		while(xenon_is_thread_task_running(i));
	}
	
	reset_timebase_task(); // don't forget thread 0
			
	std((void*)0x200611a0,0x1ff); // restart timebase
}
	

static void extrnal_Storage_Setup(){
   printf("Found Devices!\n");
   printf("Mouting Devices!\n");
   mount_all_devices();
}

static void inline play_buffer(void)
{
	int i;
	for(i=0;i<buffer_size/4;++i) ((int*)buffer)[i]=bswap_32(((int*)buffer)[i]);
	
	//printf("%8d %8d\n",xenon_sound_get_free()
   xenon_sound_submit(buffer, buffer_size);
}

static void inline copy_to_buffer(int *buffer, int *stream, unsigned int length, unsigned int stream_length)
{
   //	printf("c2b %p %p %d %d\n",buffer,stream,length,stream_length);
   // NOTE: length is in samples (stereo (2) shorts)
   int di;
   double si;
   for (di = 0, si = 0.0f; di < length; ++di, si += freq_ratio)
   {
#if 1
      // Linear interpolation between current and next sample
      double t = si - floor(si);
      short *osample = (short *)(buffer + di);
      short *isample1 = (short *)(stream + (int)si);
      short *isample2 = (short *)(stream + (int)ceil(si));

      // Left and right
      osample[0] = (1.0f - t) * isample1[0] + t * isample2[0];
      osample[1] = (1.0f - t) * isample1[1] + t * isample2[1];
#else
      // Quick and dirty resampling: skip over or repeat samples
      buffer[di] = stream[(int)si];
#endif
   }
}

static s16 prevLastSample[2] = {0, 0};
// resamples pStereoSamples (taken from http://pcsx2.googlecode.com/svn/trunk/plugins/zerospu2/zerospu2.cpp)
void ResampleLinear(s16 *pStereoSamples, s32 oldsamples, s16 *pNewSamples, s32 newsamples)
{
   s32 newsampL, newsampR;
   s32 i;

   for (i = 0; i < newsamples; ++i)
   {
      s32 io = i * oldsamples;
      s32 old = io / newsamples;
      s32 rem = io - old * newsamples;

      old *= 2;
      //printf("%d %d\n",old,oldsamples);
      if (old == 0)
      {
         newsampL = prevLastSample[0] * (newsamples - rem) + pStereoSamples[0] * rem;
         newsampR = prevLastSample[1] * (newsamples - rem) + pStereoSamples[1] * rem;
      }
      else
      {
         newsampL = pStereoSamples[old - 2] * (newsamples - rem) + pStereoSamples[old] * rem;
         newsampR = pStereoSamples[old - 1] * (newsamples - rem) + pStereoSamples[old + 1] * rem;
      }
      pNewSamples[2 * i] = newsampL / newsamples;
      pNewSamples[2 * i + 1] = newsampR / newsamples;
   }

   prevLastSample[0] = pStereoSamples[oldsamples * 2 - 2];
   prevLastSample[1] = pStereoSamples[oldsamples * 2 - 1];
}

static void inline add_to_buffer(void *stream, unsigned int length)
{
   unsigned int lengthLeft = length >> 2;
   unsigned int rlengthLeft = ceil(lengthLeft / freq_ratio);

#if 1
   //copy_to_buffer((int *)buffer, stream , rlengthLeft, lengthLeft);
   ResampleLinear((s16 *)stream, lengthLeft, (s16 *)buffer, rlengthLeft);
   buffer_size = rlengthLeft << 2;
   play_buffer();
#else
   static unsigned int buffer_offset = 0;
   // This shouldn't lose any data and works for any size
   unsigned int stream_offset = 0;
   // Length calculations are in samples (stereo (short) sample pairs)
   unsigned int lengthi, rlengthi;
   while (1)
   {
      rlengthi = (buffer_offset + (rlengthLeft << 2) <= buffer_size) ? rlengthLeft : ((buffer_size - buffer_offset) >> 2);
      lengthi = rlengthi * freq_ratio;

      //copy_to_buffer((int *)(buffer + buffer_offset), stream + stream_offset, rlengthi, lengthi);
      ResampleLinear((s16 *)(stream + stream_offset), lengthi, (s16 *)(buffer + buffer_offset), rlengthi);

      if (buffer_offset + (rlengthLeft << 2) < buffer_size)
      {
         buffer_offset += rlengthi << 2;
         return;
      }
      lengthLeft -= lengthi;
      stream_offset += lengthi << 2;
      rlengthLeft -= rlengthi;
      play_buffer();
#ifdef AIDUMP
      if (AIdump)
         fwrite(&buffer[which_buffer][0], 1, buffer_size, AIdump);
#endif
      buffer_offset = 0;

      //		buffer_size = is_60Hz ? BUFFER_SIZE_48_60 : BUFFER_SIZE_48_50;
   }
#endif
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
   printf("press b to Get FUSE DATA!\n");
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
   
   audio= fopen("sda:/test.raw","wb");

   
   while (1)
   {

      /* gets the Temp Sensor Data via buffer*/
      memset(buf, 0, 16);
      buf[0] = 0x07;

      xenon_smc_send_message(buf);
      xenon_smc_receive_response(buf);

      struct controller_data_s c;
      if (get_controller_data(&c, 0)){

      if ((c.a)&& (!oldc.a)){
         printf("\n");
         printf("Set Fan speed to 100%!\n");
         xenon_set_cpu_fan_speed(100);
         printf("\n");
      }
         else if ((c.b)&&(!oldc.b))
         {
            printf("\n");
            printf("Print dvd key and cpu key! \n");
         }

         else if ((c.x)&&(!oldc.x))
         {
            printf("\n");
            printf("exiting..\n");
            xenon_smc_power_reboot();
         }
         else if ((c.y)&&(!oldc.y))
         {

            add_to_buffer(fgetc(audio),20);
            play_buffer();
         }

         else if ((c.start) && (!oldc.start))
         {
            printf("\n");
            printf("Crashes Xbox 360!\n");
            console_clrscr();
            stack_trace(100);
         }

         else if ((c.back)&&(!oldc.back))
         {
            console_clrscr();
            printf("goes back to main\n");
            main();
         }

         else if ((c.up)&&(!oldc.up))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_RED);
            main();
         }
         else if ((c.down)&&(!oldc.down))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_GREEN);
            main();
         }

         else if ((c.right)&&(!oldc.right))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_BLUE);
            main();
         }

         else if ((c.left)&&(!oldc.left))
         {
            console_clrscr();
            console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_YELLOW);
            main();
         }
         else if((c.rb)&&(!oldc.rb)){
            console_clrscr();
            dumpana();
            main();
         }
         else if((c.lb)&&(!oldc.lb)){
            console_clrscr();
            DumpFuses();
            delay(5);
            main();
         }
         oldc=c;
      }
      usb_do_poll();
   }
}