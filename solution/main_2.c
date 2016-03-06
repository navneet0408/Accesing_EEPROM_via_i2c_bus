#include <stdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define FLASHGETS 1001
#define FLASHGETP 2002
#define FLASHSETP 3003
#define FLASHERASE 4004

int main(int argc, char* argv)
{
	// Variables	
	int fd = 0;
	int i,j,pointer;
	unsigned char write_buf[64*600];
	unsigned char read_buf[64*600];
	unsigned char read_buf_2[64*600];
	int ret = 0;
	
	for(i=0; i<(64*600); ++i)
	{
		read_buf[i] = (unsigned char) 0x00;
		read_buf_2[i] = (unsigned char) 0x00;
		write_buf[i] = (unsigned char) (i%256);
	}
	
	printf("Opening\n");
	fd = open("/dev/i2c_flash", O_RDWR);
	if(!fd)
		printf("Cannot open... :(\n");
	
	printf("Erasing\n");		
	ret = ioctl(fd, FLASHERASE, 0);
	if(ret<0)
		printf("Erase failed... \n");
	while (ioctl(fd, FLASHGETS, 0) !=0);	
	
	printf("\n\n");
	printf("!!!!!	Writing 300 Pages starting from address 0	!!!!!\n");	
	printf("Setting pointer\n");	
	ret = ioctl(fd, FLASHSETP, 0);
	if(ret<0)
		printf("IOCTL 1 failed \n");	
	while (ioctl(fd, FLASHGETS, 0) !=0);
	printf("Pointer Set\n");	
	printf("Writing.....\n");	
	ret = write (fd, write_buf, 300);
	if(ret<0)
		printf("Write 1 failed.... \n");
	printf("Returned after writing.....\n");	
	if (ioctl(fd, FLASHGETS, 0) !=0)
	{
		printf("Trying another write...Should Fail..\n");
		ret = write (fd, write_buf, 300);		
		if(ret<0)
			printf("Write failed as expected... :)\n");
		printf("Trying another read...Should Fail..\n");
		ret = read (fd, read_buf, 300);		
		if(ret<0)
		{
			if(errno == EBUSY)			
				printf("Read failed as expected... :)\n");
		}
		
		printf("Waiting for write to finish.....\n");
		while(ioctl(fd, FLASHGETS, 0) !=0);
	}
	printf ("write finished.. \n");
	
	printf("\n\n");

	printf("!!!!!!	Reading the pointer...Should be 300	!!!!!\n");
	if (ioctl(fd, FLASHGETP, &pointer) !=0)
		printf("Reading pointer failed.....\n");	
	else
		printf("Address: %d\n", pointer);

	printf("\n\n");
	
	printf("!!!!!	Reading 300 Pages starting from address 0	!!!!!\n");	
	printf("Setting pointer\n");	
	ret = ioctl(fd, FLASHSETP, 0);
	if(ret<0)
		printf("IOCTL 3 failed... \n");
	while (ioctl(fd, FLASHGETS, 0) !=0);
	printf("Pointer Set\n");	
	printf("Reading 1st time.....\n");
	ret = read (fd, read_buf, 300);
	if(ret<0)
	{
		if(errno == EBUSY)			
			printf("Read failed... :)\n");
	}
	printf("Returned after reading.....\n");	
	if (ioctl(fd, FLASHGETS, 0) !=0)
	{
		printf("Trying another write...Should Fail..\n");
		ret = write (fd, write_buf, 300);		
		if(ret<0)
			printf("Write failed as expected... :)\n");
		printf("Trying another read...Should Fail..\n");
		ret = read (fd, read_buf, 300);		
		if(ret<0)
		{
			if(errno == EBUSY)			
				printf("Read failed as expected... :)\n");
		}
		printf("Waiting for read to finish.....\n");
		while(ioctl(fd, FLASHGETS, 0) !=0);
	}		
	printf("Read Finished... \n");
	
	printf("Setting Pointer\n");
	ret = ioctl(fd, FLASHSETP, 0);
	if(ret<0)
		printf("IOCTL 3 failed... \n");
	while (ioctl(fd, FLASHGETS, 0) !=0);
	
	printf("Pointer Set\n");	
	printf("Reading 2nd time.....\n");
	
	ret = read (fd, read_buf, 300);
	if(ret<0)
	{
		if(errno == EBUSY)			
			printf("Read failed... :)\n");
	}
	printf("Returned after reading.....\n");	
	if (ioctl(fd, FLASHGETS, 0) !=0)
	{
		printf("Waiting for read to finish.....\n");
		while(ioctl(fd, FLASHGETS, 0) !=0);
	}	
	printf("Read Finished... \n");
	
	for (i=0; i<300; ++i)
	{	
		printf("W Page: %02d :", i);
		for(j=0; j<64; ++j)		
			printf("%02x ", write_buf[i*64+j]);
		printf("\n");
		printf("R Page: %02d :", i);
		for(j=0; j<64; ++j)		
			printf("%02x ", read_buf[i*64+j]);
		printf("\n");
		printf("\n");
	}
	
	
	printf("\n\n");
	printf("!!!!!	Writing 100 Pages starting from address 400	!!!!!\n");	
	printf("Setting pointer\n");	
	ret = ioctl(fd, FLASHSETP, 400);
	if(ret<0)
		printf("IOCTL 1 failed \n");	
	while (ioctl(fd, FLASHGETS, 0) !=0);
	printf("Pointer Set\n");	
	printf("Writing.....\n");	
	ret = write (fd, &write_buf[400*64], 100);
	if(ret<0)
		printf("Write 1 failed.... \n");
	printf("Returned after writing.....\n");	
	if (ioctl(fd, FLASHGETS, 0) !=0)
	{
		printf("Waiting for write to finish.....\n");
		while(ioctl(fd, FLASHGETS, 0) !=0);
	}
	printf ("write finished.. \n");
	
	printf("\n\n");
	printf("!!!!!	Reading 512 Pages starting from address 0	!!!!!\n");	
	printf("Setting pointer\n");	
	ret = ioctl(fd, FLASHSETP, 0);
	if(ret<0)
		printf("IOCTL 3 failed... \n");
	while (ioctl(fd, FLASHGETS, 0) !=0);
	printf("Pointer Set\n");	
	printf("Reading 1st time.....\n");
	ret = read (fd, read_buf_2, 512);
	if(ret<0)
	{
		if(errno == EBUSY)			
			printf("Read failed... :)\n");
	}
	printf("Returned after reading.....\n");	
	if (ioctl(fd, FLASHGETS, 0) !=0)
	{
		printf("Waiting for read to finish.....\n");
		while(ioctl(fd, FLASHGETS, 0) !=0);
	}		
	printf("Read Finished... \n");
	printf("Setting Pointer\n");
	ret = ioctl(fd, FLASHSETP, 0);
	if(ret<0)
		printf("IOCTL 3 failed... \n");
	while (ioctl(fd, FLASHGETS, 0) !=0);
	
	printf("Pointer Set\n");	
	printf("Reading 2nd time.....\n");
	
	ret = read (fd, read_buf_2, 512);
	if(ret<0)
	{
		if(errno == EBUSY)			
			printf("Read failed... :)\n");
	}
	
	printf("Returned after reading.....\n");	
	if (ioctl(fd, FLASHGETS, 0) !=0)
	{
		printf("Waiting for read to finish.....\n");
		while(ioctl(fd, FLASHGETS, 0) !=0);
	}	
	printf("Read Finished... \n");
	
	printf("Printing final EEPROM data:... \n");
	for (i=0; i<512; ++i)
	{	
		printf("R Page: %02d :", i);
		for(j=0; j<64; ++j)		
			printf("%02x ", read_buf_2[i*64+j]);
		printf("\n");
	}
	
	close(fd);
	return 0;
}
