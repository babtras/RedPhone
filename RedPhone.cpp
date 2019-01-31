/* RedPhone.cpp

"BAD DAY HELP PHONE" Project

Copyright 2019 Cory Whitesell
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include "bcm2835.h"

#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

// GPIO Pins used to detect that the handset is off the hook (HOOK_PIN), dialling in progress, and the dialing pulse
#define HOOK_PIN	23
#define DIAL_PIN	17
#define PULSE_PIN	22

#define CLOCK_TYPE	CLOCK_PROCESS_CPUTIME_ID

// defines a maximum number of filename strings to allocate for audio files
#define FILES_PER_DIRECTORY		500

// minimum number of microseconds between pulses while dialling (to suppress noise)
#define PULSE_INTERVAL			95000

#define PLAYER_CMD			"omxplayer -o local --no-osd --no-keys"

bool Terminated;

// Handler for signals from OS so that the application can exit gracefully if terminated
void INTHandler(int sig)
{
	char  c;

	signal(sig, SIG_IGN);

	// ctrl-c
	if (sig==SIGINT)
	{
		printf("Process terminated by user.\n");
		Terminated=true;
		return;
	}

	// system shutdown or session terminated
	if (sig==SIGTERM)
	{
		printf("Process terminated by system.\n");
		Terminated=true;
		return;
	}

	// process killed with pskill
	if (sig==SIGKILL||sig==SIGHUP)
	{
		printf("Process killed.\n");
		Terminated=true;
		return;
	}

	// Program caused a segmentation fault. Best to catch it than to inexplicably terminate
	if (sig==SIGSEGV)
	{
		printf("Segmentation Fault.\n");
		Terminated=true;
		return;
	}
}


int main()
{
	bool OnHook=true;
	bool Dialling=false;
	bool Pulse=false;
	int pulsecount;
	uint8_t hl,dl,pl;
	struct timespec start, pulsetime,now;
	int i,t,randsound;
	char audiofile[10][FILES_PER_DIRECTORY][256];
	int afcount[10];
	DIR *dp;
	struct dirent *ep;
	char directory[10]={0};
	char cmd[500]={0};

	Terminated=false;

	// fork the process to spawn a daemon (service)
	pid_t dpid=fork();
	pid_t session_id;

	// if the daemon fails to fork
	if (dpid < 0)
	{
		printf("Unable to initialize Daemon\n");
		exit(1);
	}

	if (dpid > 0) exit(0); // terminate original process indicating the daemon is started

	umask(0);

	session_id=setsid();	// give the daemon a session ID

	if (session_id <0) exit(1);

	// set signal handlers
	signal(SIGINT,INTHandler);
	signal(SIGTERM,INTHandler);
	signal(SIGKILL,INTHandler);
	signal(SIGSEGV,INTHandler);

	if (!bcm2835_init())
		exit(1);

	// get a list of objects in each of 10 directories
	for (i = 0; i < 10; i++)
	{
		afcount[i]=0;
		snprintf(directory,9,"./%d/",i);
		dp=opendir(directory);

		if (dp != NULL)
		{
			while (ep=readdir(dp))
			{
				// only play compatible formats
				if (strstr(ep->d_name,".wav") != NULL || strstr(ep->d_name,".mp3") != NULL)
				{
					strncpy(audiofile[i][afcount[i]],ep->d_name,255);
					afcount[i]++;
				}
			}

			closedir(dp);
		}
	}

	srand(time(NULL));

	// set the pins to input
	bcm2835_gpio_fsel(HOOK_PIN, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(DIAL_PIN, BCM2835_GPIO_FSEL_INPT);
	bcm2835_gpio_fsel(PULSE_PIN, BCM2835_GPIO_FSEL_INPT);

	while (!Terminated)
	{
		hl=bcm2835_gpio_lev(HOOK_PIN);
		dl=bcm2835_gpio_lev(DIAL_PIN);
		pl=bcm2835_gpio_lev(PULSE_PIN);

		// watch for pin state changes
		if (OnHook==false && hl==LOW)
		{
			pulsecount=0;
			OnHook=false;
		}

		if (hl == HIGH && OnHook==true)
		{
			OnHook=false;
		}

		if (!Dialling && dl == HIGH && !OnHook)
		{
			clock_gettime(CLOCK_TYPE,&start);
			Dialling=true;
			pulsecount=0;
			Pulse=false;
		}

		if (Dialling)
		{
			if (dl==LOW)
			{
				Dialling=false;
				if (pulsecount<=10&&pulsecount>0)
				{
					if (pulsecount==10) pulsecount=0;

					if (afcount[pulsecount]>0)
					{
						// select sound at random from the appropriate folder
						randsound=rand()%afcount[pulsecount];
						// play sound
						snprintf(cmd,499,"%s \"./%d/%s\" > /dev/null &",PLAYER_CMD,pulsecount,audiofile[pulsecount][randsound]);
						system(cmd);
                    }
				}
			}
			else
			{
				if (Pulse == false && pl == LOW)
				{
					if (pulsecount==0)
						Pulse=true;
					else
					{
						// ignore if less than 90000 microseconds has elapsed since last pulse
						clock_gettime(CLOCK_TYPE,&now);
						t=(now.tv_sec-pulsetime.tv_sec)*1000000+(now.tv_nsec-pulsetime.tv_nsec)/1000;
						if (t>PULSE_INTERVAL)
							Pulse=true;
					}
				}
				else if (Pulse == true && pl == HIGH)
				{
					clock_gettime(CLOCK_TYPE,&pulsetime);
					pulsecount++;
					Pulse=false;
				}
			}
		}
	}

	return 0;
}
