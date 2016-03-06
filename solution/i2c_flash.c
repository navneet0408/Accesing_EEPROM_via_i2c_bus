//Include all the required files

#include <linux/module.h>  
#include <linux/kernel.h>  
#include <linux/fs.h>	   
#include <linux/cdev.h>    
#include <linux/types.h>
#include <linux/slab.h>	   
#include <asm/uaccess.h>   
#include <linux/string.h>
#include <linux/device.h>  
#include <linux/i2c.h>     
#include <linux/i2c-dev.h>

#include <linux/gpio.h>
#include <asm/gpio.h>
#include <linux/semaphore.h>

#include <linux/init.h>
#include <linux/moduleparam.h> 

#include <linux/workqueue.h>
#include <linux/errno.h>

// The #defines
#define DEVICE_NAME "i2c_flash"  
#define DEVICE_ADDR 0x54
#define I2CMUX 29 
#define LED 26
#define FLASHGETS 1001
#define FLASHGETP 2002
#define FLASHSETP 3003
#define FLASHERASE 4004

#define NUM_PAGES 512
#define NUM_BYTES_PER_PAGE 64

//Device Structure
struct flash_dev 
{
	struct cdev cdev;               
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	unsigned char low;
	unsigned char high;
	struct workqueue_struct *write_wq;
	int eeprom_busy;
	int eeprom_data_ready;
	char *eeprom_data;
	struct semaphore sem;
} *flash_devp;

//Work Queue - write Structure
typedef struct 
{
	struct work_struct write_work;
	struct file *file;
	const char *buf;
	size_t count;
	loff_t *ppos;
} write_work_t;

//Work Queue - read Structure
typedef struct 
{
	struct work_struct read_work;
	struct file *file;
	char *buf;
	size_t count;
	loff_t *ppos;
} read_work_t;

//Device structures and variables.....
static dev_t flash_dev_number;     
struct class *flash_dev_class;          
static struct device *flash_dev_device;

void wq_queued_write(struct work_struct *work);
ssize_t wq_fops_write(struct file *file, const char *buf, size_t count, loff_t *ppos);
int flash_driver_open(struct inode *inode, struct file *file);
int flash_driver_release(struct inode *inode, struct file *file);
ssize_t flash_driver_write(struct file *file, const char *buf, size_t count, loff_t *ppos);
ssize_t flash_driver_read(struct file *file, char *ubuf, size_t count, loff_t *ppos);
long flash_driver_ioctl (struct file *file, unsigned int cmd, unsigned long arg);

//Work Queue write function.... Writes data to EEPROM
void wq_queued_write(struct work_struct *work)
{
	ssize_t ret;	
	write_work_t *write_work = (write_work_t*)work;
	struct flash_dev *flash_devp = (struct flash_dev *)write_work->file->private_data;	
	
	ret = flash_driver_write(write_work->file, write_work->buf, write_work->count, write_work->ppos);
	if(ret < 0)
	{
		printk("Error: Write failed wq_queued_write.\n");
		return;
	}
	
	down (&flash_devp->sem);	
	flash_devp->eeprom_busy=0;
	up (&flash_devp->sem);
	kfree(work);
	return;
}

//Work Queue read function - reads data from EEPROM
void wq_queued_read(struct work_struct *work)
{
	read_work_t *read_work = (read_work_t*)work;
	struct flash_dev *flash_devp = (struct flash_dev *) read_work->file->private_data;
	char *buffer = kmalloc(read_work->count * NUM_BYTES_PER_PAGE, GFP_KERNEL);
	ssize_t ret = flash_driver_read(read_work->file, buffer, read_work->count, read_work->ppos);
	if(ret < 0)
	{
		printk("Error: Read failed wq_queued_read.\n");
		return;
	}
	
	down (&flash_devp->sem);	
	flash_devp->eeprom_busy=0;
	flash_devp->eeprom_data_ready=1;
	flash_devp->eeprom_data = kmalloc(read_work->count * NUM_BYTES_PER_PAGE, GFP_KERNEL);
	memcpy(flash_devp->eeprom_data, buffer, (read_work->count)*NUM_BYTES_PER_PAGE);
	up (&flash_devp->sem);
	kfree(work);
	kfree(buffer);
	return;
}

//Write function.... Launches work queue or returns busy
ssize_t wq_fops_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	int ret;
	write_work_t *work;
	struct flash_dev *flash_devp = file->private_data;
	down (&flash_devp->sem);	
	if(flash_devp->eeprom_busy == 1)
	{
		up (&flash_devp->sem);	
		return -EBUSY;
	}
	flash_devp->eeprom_busy = 1;
	up (&flash_devp->sem);	
		
	work = (write_work_t*)kmalloc(sizeof(write_work_t), GFP_KERNEL);
	INIT_WORK((struct work_struct*)work, wq_queued_write);
	work->file = file;
	work->buf=buf;
	work->count=count;
	work->ppos = ppos;
	ret = queue_work(flash_devp->write_wq, (struct work_struct *)work);
	return 0;
}

//Read function - Launches work queue and returns -EAGAIN, returns -EBUSY or returns data read by a previous read if available..
ssize_t wq_fops_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int ret;
	read_work_t *work;
	struct flash_dev *flash_devp = file->private_data;
	down (&flash_devp->sem);	
	if(flash_devp->eeprom_data_ready == 1)
	{
		ret = copy_to_user (buf, flash_devp->eeprom_data, count*NUM_BYTES_PER_PAGE);
		if(ret < 0)
		{
			printk("Error: Copy to user failed in fops read.\n");
			up (&flash_devp->sem);				
			return -1;
		}
		
		flash_devp->eeprom_data_ready = 0;		
		up (&flash_devp->sem);
		return 0;
	}
	up (&flash_devp->sem);		
	
	down (&flash_devp->sem);	
	if(flash_devp->eeprom_busy == 1)
	{
		up (&flash_devp->sem);	
		return -EBUSY;
	}
	flash_devp->eeprom_busy = 1;
	up (&flash_devp->sem);	
		
	work = (read_work_t*)kmalloc(sizeof(read_work_t), GFP_KERNEL);
	INIT_WORK((struct work_struct*)work, wq_queued_read);
	work->file = file;
	work->buf=buf;
	work->count=count;
	work->ppos = ppos;
	ret = queue_work(flash_devp->write_wq, (struct work_struct *)work);
	return -EAGAIN;
}

// Open the driver
int flash_driver_open(struct inode *inode, struct file *file)
{
	struct flash_dev *flash_devp;
	flash_devp = container_of(inode->i_cdev, struct flash_dev, cdev);
	file->private_data = flash_devp;
	return 0;
}

//Release the driver
int flash_driver_release(struct inode *inode, struct file *file)
{
	return 0;
}

//Blocking function for write
ssize_t flash_driver_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	int i,j, address, ret;	
	struct flash_dev *flash_devp = file->private_data;
	unsigned char *kbuf = kmalloc(NUM_BYTES_PER_PAGE*count, GFP_KERNEL);
	unsigned char sendBuf[NUM_BYTES_PER_PAGE+2];
	gpio_set_value_cansleep(LED, 1);	
	
	//ret = copy_from_user(kbuf, buf, count*NUM_BYTES_PER_PAGE);
	//if(ret < 0)
	//{
	//	printk("Error: Copy from user failed in write.\n");
	//	return -1;
	//}
	
	memcpy(kbuf, buf, count*NUM_BYTES_PER_PAGE);
	
	for(i=0; i<count; ++i)
	{	
		sendBuf[0] = flash_devp->high;
		sendBuf[1] = flash_devp->low;
		do	
		{
			ret = i2c_master_send(flash_devp->client, sendBuf, 2);
		} while(ret <=0);
		
		for(j=0; j<NUM_BYTES_PER_PAGE; ++j)
			sendBuf[j+2] = kbuf[i*NUM_BYTES_PER_PAGE +j];
		
		
		ret = i2c_master_send(flash_devp->client, sendBuf, NUM_BYTES_PER_PAGE+2);
		if(ret < 0)
		{
			printk("Error: i2c_master_send failed in write.\n");
			return -1;
		}		
		address = (flash_devp->high * 256) + flash_devp->low;
		address = (address + NUM_BYTES_PER_PAGE) % (NUM_PAGES*NUM_BYTES_PER_PAGE);		
		flash_devp->high = address / 256;
		flash_devp->low = address % 256;
		
	}
	
	do	
	{
		ret = i2c_master_send(flash_devp->client, sendBuf, 2);
	} while(ret <=0);
	
	gpio_set_value_cansleep(LED, 0);
	kfree(kbuf);
	return count;
}

//Blocking function for read
ssize_t flash_driver_read(struct file *file, char *ubuf, size_t count, loff_t *ppos)
{
	int ret, address;//,i,j;	
	struct flash_dev *flash_devp = file->private_data;
	unsigned char *buf = kmalloc(count*NUM_BYTES_PER_PAGE, GFP_KERNEL);
	
	gpio_set_value_cansleep(LED, 1);
	ret = i2c_master_recv(flash_devp->client, buf, count*NUM_BYTES_PER_PAGE);
	if(ret < 0)
	{
		printk("Error: i2c_master_recv failed in read.\n");
		return -1;
	}
		
	//ret = copy_to_user(ubuf, buf, count*NUM_BYTES_PER_PAGE);
	//if(ret < 0)
	//{
	//	printk("Error: copy_to_user failed in read.\n");
	//	return -1;
	//}
			
	memcpy(ubuf, buf, count*NUM_BYTES_PER_PAGE);
	address = (flash_devp->high * 256) + flash_devp->low;
	address = (address + NUM_BYTES_PER_PAGE*count) % (NUM_PAGES*NUM_BYTES_PER_PAGE);		
	flash_devp->high = address / 256;
	flash_devp->low = address % 256;
	gpio_set_value_cansleep(LED, 0);
	kfree(buf);
	return count;

}

// ioctl implementation... 
long flash_driver_ioctl (struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned char buf[2];
	unsigned char sendBuf[NUM_BYTES_PER_PAGE+2];	
	struct flash_dev *flash_devp = file->private_data;
	int ret, address, *x, i, j;	
	switch (cmd)
	{
	case FLASHGETS:
		return flash_devp->eeprom_busy;	
	break;

	case FLASHGETP:
		if(flash_devp->eeprom_busy == 1)
		{
			return -EBUSY;
		}		
		address = (flash_devp->high * 256) + flash_devp->low;
		address = address / 64;		
		x = (int *) arg;
		ret = copy_to_user(x, &address, sizeof(int));
		return 0;
			
	break;

	case FLASHSETP:
		down (&flash_devp->sem);	
		if(flash_devp->eeprom_busy == 1)
		{
			up (&flash_devp->sem);	
			return -EBUSY;
		}
		flash_devp->eeprom_busy = 1;
		up (&flash_devp->sem);	
		
		buf[1] = flash_devp->low=(unsigned char) ((arg*NUM_BYTES_PER_PAGE) & 0x00FF);
		buf[0] = flash_devp->high=(unsigned char) (((arg*NUM_BYTES_PER_PAGE) & 0xFF00) >> 8);
	
		ret = i2c_master_send(flash_devp->client, buf, 2);
		if(ret < 0)
		{
			printk("Error: could not set ptr setp.\n");
			return -1;
		}
		
		down (&flash_devp->sem);	
		flash_devp->eeprom_busy=0;
		up (&flash_devp->sem);	
		
	break;

	case FLASHERASE:
		down (&flash_devp->sem);	
		if(flash_devp->eeprom_busy == 1)
		{
			up (&flash_devp->sem);	
			return -EBUSY;
		}
		flash_devp->eeprom_busy = 1;
		up (&flash_devp->sem);	
		
		gpio_set_value_cansleep(LED, 1);
		
		flash_devp->high=0;
		flash_devp->low=0;		
		for(i=0; i<NUM_PAGES; ++i)
		{	
			sendBuf[0] = flash_devp->high;
			sendBuf[1] = flash_devp->low;
		
			do	
			{
				ret = i2c_master_send(flash_devp->client, sendBuf, 2);
			} while(ret <=0);
			
			for(j=0; j<NUM_BYTES_PER_PAGE; ++j)
				sendBuf[j+2] = 1;
		
			ret = i2c_master_send(flash_devp->client, sendBuf, NUM_BYTES_PER_PAGE+2);
			if(ret < 0)
			{
				printk("Error: could not send in erase.\n");
				return -1;
			}	
			address = (flash_devp->high * 256) + flash_devp->low;
			address = (address + 64) % (512*64);		
			flash_devp->high = address / 256;
			flash_devp->low = address % 256;
		}
		do	
		{
			ret = i2c_master_send(flash_devp->client, sendBuf, 2);
		}while(ret <=0);
		
		down (&flash_devp->sem);	
		flash_devp->eeprom_busy=0;
		up (&flash_devp->sem);		
		gpio_set_value_cansleep(LED, 0);
		
		return 0;
	break;
	}
	return 0;
}

//file operations structure
static struct file_operations flash_fops = {
    .owner		= THIS_MODULE,           
    .open		= flash_driver_open,        
    .release	= flash_driver_release,    
    .write		= wq_fops_write,       
    .read		= wq_fops_read,        
	.unlocked_ioctl = flash_driver_ioctl,
};

//Initializations of the driver
int __init flash_driver_init(void)
{
	int ret;
	
	if (alloc_chrdev_region(&flash_dev_number, 0, 1, DEVICE_NAME) < 0) 
	{
		printk(KERN_DEBUG "Can't register device\n"); return -1;
	}

	flash_dev_class = class_create(THIS_MODULE, DEVICE_NAME);

	flash_devp = kmalloc(sizeof(struct flash_dev), GFP_KERNEL);
	if (!flash_devp) 
	{
		printk("Bad Kmalloc\n"); 
		return -ENOMEM;
	}

	cdev_init(&flash_devp->cdev, &flash_fops);
	flash_devp->cdev.owner = THIS_MODULE;

	ret = cdev_add(&flash_devp->cdev, (flash_dev_number), 1);
	if (ret) 
	{
		printk("Bad cdev\n");
		return ret;
	}

	flash_dev_device = device_create(flash_dev_class, NULL, MKDEV(MAJOR(flash_dev_number), 0), NULL, DEVICE_NAME);	

	ret = gpio_request(I2CMUX, "I2CMUX");
	if(ret)
	{
		printk("GPIO %d is not requested.\n", I2CMUX);
	}

	ret = gpio_direction_output(I2CMUX, 0);
	if(ret)
	{
		printk("GPIO %d is not set as output.\n", I2CMUX);
	}

	gpio_set_value_cansleep(I2CMUX, 0); 

	ret = gpio_request(LED, "LED");
	if(ret)
	{
		printk("GPIO %d is not requested.\n", LED);
	}

	ret = gpio_direction_output(LED, 0);
	if(ret)
	{
		printk("GPIO %d is not set as output.\n", LED);
	}

	gpio_set_value_cansleep(LED, 0); 
	
	flash_devp->adapter = i2c_get_adapter(0);
	if(flash_devp->adapter == NULL)
	{
		printk("Could not acquire i2c adapter.\n");
		return -1;
	}

	flash_devp->client = (struct i2c_client*) kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	flash_devp->client->addr = DEVICE_ADDR; 
	snprintf(flash_devp->client->name, I2C_NAME_SIZE, DEVICE_NAME);
	flash_devp->client->adapter = flash_devp->adapter;
	flash_devp->write_wq = create_workqueue("write_queue");
	flash_devp->eeprom_busy=0;	
	flash_devp->eeprom_data_ready=0;	
	sema_init (&flash_devp->sem, 1);
	return 0;
}

//cleanup... :)
void __exit flash_driver_exit(void)
{

	flush_workqueue(flash_devp->write_wq);
	destroy_workqueue(flash_devp->write_wq);
	
	i2c_put_adapter(flash_devp->adapter);
	kfree(flash_devp->client);

	unregister_chrdev_region((flash_dev_number), 1);

	device_destroy (flash_dev_class, MKDEV(MAJOR(flash_dev_number), 0));
	cdev_del(&flash_devp->cdev);
	kfree(flash_devp);
	
	class_destroy(flash_dev_class);
	
	gpio_free(I2CMUX);
	gpio_set_value_cansleep(LED, 0);	
	gpio_free(LED);
}

module_init(flash_driver_init);
module_exit(flash_driver_exit);
MODULE_LICENSE("GPL v2");
