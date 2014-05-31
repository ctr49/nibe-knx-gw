/* nibe-rcu.c

   Read/write data from Nibe Heat Pump

   This program is distributed under the GPL v2(see LICENSE file)
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <ftdi.h>
#include <time.h>
#include "common.h"

static int exitRequested = 0;
/*
 * sigintHandler --
 *
 *    SIGINT handler, so we can gracefully exit when the user hits ctrl-C.
 */
static void
sigintHandler(int signum)
{
    exitRequested = 1;
}

	char* itoa(int value, char* result, int base) {
		// check that the base if valid
		if (base < 2 || base > 36) { *result = '\0'; return result; }
	
		char* ptr = result, *ptr1 = result, tmp_char;
		int tmp_value;
	
		do {
			tmp_value = value;
			value /= base;
			*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
		} while ( value );
	
		// Apply negative sign
		if (tmp_value < 0) *ptr++ = '-';
		*ptr-- = '\0';
		while(ptr1 < ptr) {
			tmp_char = *ptr;
			*ptr--= *ptr1;
			*ptr1++ = tmp_char;
		}
		return result;
	}

int main(int argc, char **argv)
{
    FILE *REGCONF;
    struct ftdi_context *ftdiglob;
    unsigned char buf[1];
    unsigned char message[256];
    char *filename;
    int f = 0, i, j;
    int f1 = 0;
    int vid = 0x403;
    int pid = 0x6010;
    int baudrate = 19200;
    int interface = 1;
    int do_write = 0;
    int newmsg = 0;
    int endmsg = 0;
    int msglen = 0;
    int pllen = 0;
    int maxlen = 0;
    unsigned int pattern = 0xffff;
    int retval = EXIT_FAILURE;
    int verbose = 0;
    int debug = 0;
    int exp = 0;
    int mant = 0;
    int sign = 0;
    unsigned char ack[1];
    unsigned char val4[256];
    ack[0] = 0x06;
    unsigned short reg[256];
    unsigned short regold[256];
    unsigned int eibmessage;
    struct tm *newtime;
    time_t long_time;
    uchar byte1, byte2;
    float factor[256];
    unsigned short val1, monreg[256];
    float val2;
    unsigned long val3, DPT[256];
    unsigned short monregmax = 0;
    eibaddr_t gaddr[256];
    char buffernew[256], bufferold[256], bufferdiff[256];

    for (i = 0; i < 256; i++)
    {
        reg[i] = 65535;
        regold[i] = 65535;
	val4[i] = 0;
	monreg[i] = 0;
	DPT[i] = 0;
	gaddr[i] = 0;
	buffernew[i] = 0;
	bufferold[i] = 0;
	bufferdiff[i] = 0;
    }

    while ((i = getopt(argc, argv, "i:v:p:b:w:f::")) != -1)
    {
        switch (i)
        {
            case 'i': // 0=ANY, 1=A, 2=B, 3=C, 4=D
                interface = strtoul(optarg, NULL, 0);
                break;
            case 'v':
                vid = strtoul(optarg, NULL, 0);
                break;
            case 'p':
                pid = strtoul(optarg, NULL, 0);
                break;
            case 'b':
                baudrate = strtoul(optarg, NULL, 0);
                break;
            case 'w':
                do_write = 1;
                break;
	    case 'f':
		filename = optarg;
		break;
            default:
                fprintf(stderr, "usage: %s [-i interface] [-v vid] [-p pid] [-b baudrate] [-w]\n", *argv);
                exit(-1);
        }
    }


    REGCONF = fopen(filename, "r");
    if (NULL == REGCONF)
    {
	fprintf(stderr, "Error opening config file: %s\n", filename);
	return retval;
    }
    else
    {
	if (debug) printf("opening config file\n");
	while((fscanf(REGCONF,"%d,%f,%d,%s\n", &val1, &val2, &val3, val4)) != EOF )
	{
	  if (verbose) printf("reading line %d: val1: %d val2: %f val3: %d val4 %s\n", monregmax, val1, val2, val3, val4);
	  monreg[monregmax] = val1;
	  factor[monregmax] = val2;
	  DPT[monregmax] = val3;  
	  gaddr[monregmax] = readgaddr(val4);
	  if (verbose) printf("Monitor register: %d with factor %f and data type %d for GA %s (%d)\n", monreg[monregmax], factor[monregmax], DPT[monregmax], val4, gaddr[monregmax]);
	  monregmax++;
	}
    }

    // Init
    //for (i = 0; i < 256; i++)
    //{
//	reg[i] = 65535;
//	regold[i] = 65535;
    //}
    if ((ftdiglob = ftdi_new()) == 0)
    {
        fprintf(stderr, "ftdi_new failed\n");
        return retval;
    }

    if (!vid && !pid && (interface == INTERFACE_ANY))
    {
        ftdi_set_interface(ftdiglob, INTERFACE_ANY);
        struct ftdi_device_list *devlist;
        int res;
        if ((res = ftdi_usb_find_all(ftdiglob, &devlist, 0, 0)) < 0)
        {
            fprintf(stderr, "No FTDI with default VID/PID found\n");
            goto do_deinit;
        }
        if (res == 1)
        {
            f = ftdi_usb_open_dev(ftdiglob,  devlist[0].dev);
            if (f<0)
            {
                fprintf(stderr, "Unable to open device %d: (%s)",
                        i, ftdi_get_error_string(ftdiglob));
            }
        }
        ftdi_list_free(&devlist);
        if (res > 1)
        {
            fprintf(stderr, "%d Devices found, please select Device with VID/PID\n", res);
            /* TODO: List Devices*/
            goto do_deinit;
        }
        if (res == 0)
        {
            fprintf(stderr, "No Devices found with default VID/PID\n");
            goto do_deinit;
        }
    }
    else
    {
        // Select interface
        ftdi_set_interface(ftdiglob, interface);
        
        // Open device
        f = ftdi_usb_open(ftdiglob, vid, pid);
    }
    if (f < 0)
    {
        fprintf(stderr, "unable to open ftdi device: %d (%s)\n", f, ftdi_get_error_string(ftdiglob));
        exit(-1);
    }

    // Set baudrate
    f = ftdi_set_baudrate(ftdiglob, baudrate);
    if (f < 0)
    {
        fprintf(stderr, "unable to set baudrate: %d (%s)\n", f, ftdi_get_error_string(ftdiglob));
        exit(-1);
    }
    
    f = ftdi_set_line_property(ftdiglob, 8, STOP_BIT_1, SPACE);
    if (f < 0)
    {
        fprintf(stderr, "unable to set line parameters: %d (%s)\n", f, ftdi_get_error_string(ftdiglob));
        exit(-1);
    }
    else
    {
	if (verbose) fprintf(stderr, "successfully set line parameters: %d (%s)\n", f, ftdi_get_error_string(ftdiglob));
    }
    
    signal(SIGINT, sigintHandler);
    while (!exitRequested)
    {
	time( &long_time );
	newtime = localtime( &long_time ); 
        f = ftdi_read_data(ftdiglob, buf, sizeof(buf));
        if (f<0)
            usleep(1 * 1000000);
        else if(f> 0)
        {
	    if (debug) fprintf(stdout, "%02x ", buf[0]);
	    if (newmsg == 1)
	    {
		message[msglen] = buf[0];
		msglen++;
		if (debug) fprintf(stdout, "Entering newmsg loop, byte: %02x, msglength: %d\n", buf[0], msglen);
		if ((msglen == 3) && (message[0] == 0x03) && (message[1] == 0x00) && (message[2] == 0x14))
                {
                    if (do_write)
                    {
                        f = ftdi_write_data(ftdiglob, ack, 1);
                                //(baudrate/512 >sizeof(buf))?sizeof(buf):
                                //(baudrate/512)?baudrate/512:1);
                        if (debug) fprintf(stdout, "\nreceived RCU challenge (0x00 0x14), ACK'ed (0x06) with result: %d (%s)\n", f, ftdi_get_error_string(ftdiglob));
                     }
                     else
                        if (debug) fprintf(stderr, "\nreceived RCU challenge (0x00 0x14), but we are not in write mode, this will result in comms error!\n");
                    msglen = 0;
                    newmsg = 1;
                }
		else if ((msglen == 4) && (buf[0] == 0x06))  // NOOP
		{
		     if (debug > 8) fprintf(stdout, "NOOP found, starting over\n");
		     for (i = 0; i < msglen; i++)
	 		message[i] = 0x00;
		     msglen = 0;
		     newmsg = 1;
		}
                else if ((msglen == 4) && (buf[0] == 0x05))  // NOOP
                {
                     if (debug > 8) fprintf(stdout, "NOOP found, starting over\n");
                     for (i = 0; i < msglen; i++)
                        message[i] = 0x00;
                     msglen = 0;
                     newmsg = 0;
                }
                else if ((msglen == 4) && (((message[1] == 0x00) && (message[2] == 0x00)) || (message[1] != 0x00)))  // Bogus?
                {
                     if (debug > 8) fprintf(stdout, "bogus message found, starting over\n");
                     for (i = 0; i < msglen; i++)
                        message[i] = 0x00;
                     msglen = 0;
                     newmsg = 0;
                }
		else if (msglen == 4)
		{
		    maxlen = buf[0] + 5; 
		    if (debug > 8) fprintf(stdout, "Message length defined as: %02x (%d)\n", buf[0], buf[0]);
		}
		else if (msglen == maxlen) // End of message buf = checksum
		{
		    unsigned char checksum = 0;
		    for(i = 0; i < maxlen - 1; i++)
			checksum ^= message[i];
		    if (debug > 8)
		    {
			fprintf(stdout, "calc checksum %02x\n", checksum);
			fprintf(stdout, "recv checksum %02x\n", buf[0]);
		    }

		    if (checksum != buf[0])
		    {
			fprintf(stderr, "checksum error\n");
			if (debug)
			{
			    fprintf(stdout, "CMD: %02x, sender address: %02x %02x, payload: ", message[0], message[1], message[2]);
                            for (i = 4; i < maxlen - 1; i++)
                        	fprintf(stdout, "%02x ", message[i]);
                            fprintf(stdout, ", CRC NOK (%02x)\n", checksum);
			}
			newmsg = 0;
			for (i = 0; i < msglen; i++)
                            message[i] = 0x00;
                     	msglen = 0;
		    }
		    else
		    {
			if ((debug) && (message[0] != 0xc0))
			{
			     for (i = 4; i < maxlen - 1; i++)
				fprintf(stdout, "%02x ", message[i]);
			     if ((message[0] > 0x50) && (message[0] < 0x55))
			     {
				fprintf(stdout, ", CRC OK (%02x) - ", checksum);
				for (i = 4; i < maxlen - 1; i++)
				     fprintf(stdout, "%c", message[i]);
				fprintf(stdout, "\n");
			     }
			     else
				fprintf(stdout, ", CRC OK (%02x)\n", checksum);
			}
			else if (message[0] == 0xc0)
			{
			   if (verbose)
			   {
			      fprintf(stdout, "CMD: %02x, sender address: %02x %02x, payload: ", message[0], message[1], message[2]);
			      for (i = 4; i < maxlen - 1; i++)
			      {
				fprintf(stdout, "%02x ", message[i]);
			      }
			      fprintf(stdout, ", CRC OK (%02x)\n", checksum);	
			   }
                           for (i = 4; i < maxlen - 3; i++)
			   {
                             	    if (((message[i] == 0x00) && (message[i+1] != 0x00)) || ((i == 4) && (message[4] == 0x00) && (message[5] == 0x00)))
                             	    {
					if (((message[i+4] == 0x00) && (i + 2 < maxlen)) || ((i + 6 == maxlen) && (i + 2 < maxlen)))
					     {
						reg[message[i+1]] = (message[i+2] << 8) | message[i+3];
						if (debug) fprintf(stdout, "update register %02x: %02x %02x loop long\n", message[i+1], message[i+2], message[i+3]);
						i=i+2;
					     }
				        else if (i + 2 < maxlen)
				        {
						reg[message[i+1]] = message[i+2];
						if (debug) fprintf(stdout, "update register %02x: %02x loop short\n", message[i+1], message[i+2]);
						i=i+1;
				        }
				    }
			   }
			   for (i = 0; i < 256; i++)
                           {
				if (reg[i] != regold[i])
				{
				  for (j = 0; j < monregmax - 1; j++)
				  {
				    if (i == monreg[j])
				    {
					if (DPT[j] == 9001)
					{
					    exp = 0;
					    if (reg[i] <0)
						sign = 0x8000;
					    mant = (int)(reg[i] * factor[j] * 100.0);
					    while (abs(mant) > 2047)
					    {
						mant = mant >> 1;
						exp++;
					    }
					    eibmessage = sign | (exp << 11) | (mant & 0x07ff);
					    byte1 = eibmessage >> 8;
					    byte2 = eibmessage & 0xff;
					}
					else 
					  if ( reg[i] < 0x100 )
					  {
					    byte1 = 0x00;
					    byte2 = reg[i];
					  }
					  else
				  	  {
					    byte1 = reg[i] >> 8;
					    byte2 = reg[i] & 0xff;
					  }
					EIBConnection *con;
					uchar eibbuf[4] = { 0, 0x80, byte1, byte2 };

					con = EIBSocketURL ("local:/tmp/eib");
					if (!con)
					   die ("Open failed");

					if ((EIBOpenT_Group (con, gaddr[j], 1)) == -1)
					    die ("Connect failed");

					printf("Sending: %02x %02x to GA ", eibbuf[2], eibbuf[3]);
					printGroup(gaddr[j]);
					printf(" (%d) \n", gaddr[j]);

					if (!(EIBSendAPDU (con, 4, eibbuf)))
					    die ("Request failed");
					else
					    if (debug) printf ("Sent request\n");

					EIBClose (con);
				    }
				  }
				  itoa (regold[i],bufferold,2);
				  itoa (reg[i],buffernew,2);
				  itoa (regold[i] - reg[i],bufferdiff,2);
				  if ((i < 64) && (regold[i] != 0xffff))
				  {
				      fprintf(stdout, "%02d:%02d:%02d - register %02x / %d changed:\n", newtime->tm_hour, newtime->tm_min, newtime->tm_sec, i, i);
				      fprintf(stdout, "from: hex %02x \t/ dec %d \t/ bin %s\n", regold[i], regold[i], bufferold);
				      fprintf(stdout, "  to: hex %02x \t/ dec %d \t/ bin %s\n", reg[i], reg[i], buffernew);
				      fprintf(stdout, "diff: hex %02x \t/ dec %d \t/ bin %s\n", regold[i] - reg[i], regold[i] -  reg[i], bufferdiff);
				  }
				  regold[i] = reg[i];
				}
                           }
			 fflush(stderr);
        	         fflush(stdout);
			}
			for (i = 0; i < msglen; i++)
                            message[i] = 0x00;
                     	msglen = 0;
                     	newmsg = 0;
		    }
		}
	    }
            else if (newmsg == 0)
            {
                if (buf[0] == 0x06) // possible ACK, a new message may start in the next byte
                {
                    newmsg = 1;
                    msglen = 0;
                    pllen = 0;
                    maxlen = 0;
                    if (debug) fprintf(stdout, "ACK found, looking for new message to follow...\n");
                }
		else if (buf[0] == 0x05) // another NOOP?
		    if (debug) fprintf(stdout, "special ACK found: %02x\n", buf[0]);
		else
		    if (debug > 4) fprintf(stdout, "no ACK found, out of message byte; %02x\n", buf[0]);
            }
        }
    }
    signal(SIGINT, SIG_DFL);
    retval =  EXIT_SUCCESS;
            
    ftdi_usb_close(ftdiglob);
    do_deinit:
    ftdi_free(ftdiglob);

    return retval;
}
