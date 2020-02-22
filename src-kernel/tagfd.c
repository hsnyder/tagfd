/*	Copyright (C) 2018, 2020 Harris M. Snyder

	This file is part of Tagfd.

	Tagfd is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Foobar is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
*/

/*

	tagfd: A Linux kernel module that adds a new IPC mechanism, 
	designed to facilitate process control. 
	
	Harris M. Snyder, 2018

*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/poll.h>


#include "../include/tagfd-shared.h"

#define NAME "tagfd"
#define MASTERNAME "tagfd.master"
#define PREFIX "tagfd!"

// -----------------------------------------
// Module parameter(s)
// -----------------------------------------


// These parameters will appear in /sys/module/tagfd/parameters/, but are read-only there.
// They can be set when the module is loaded, as a command line args to insmod.
// If we wanted to make values changeable at runtime (and we wanted to know about it) through sysfs,
// we could do what is shown here: https://stackoverflow.com/questions/34957016/signal-on-kernel-parameter-change

// This parameter stores the number of data tags that the system will allocate. 
static int max_tags = 64;
module_param(max_tags, int, 0444 );




// -----------------------------------------
// Module types and globals
// -----------------------------------------

struct tag_ctx
{
	tag_t             tag;
	struct mutex      mtx;
	struct cdev       cdev;
	char              name[TAG_NAME_LENGTH];
	wait_queue_head_t wqh;
};

struct tag_watcher
{
	struct tag_ctx * e_ctx;
	timestamp_t         ts_lastRead;
};

static dev_t gl_dev; // First device number. 
static struct class * gl_tagfdClass = NULL;

static int gl_nEntities = 0;

static struct tag_ctx  * gl_tags = NULL; // Our list of tags.

// The master device (used for configuration) - can be written to by only one process at a time.
static atomic_t          gl_masterAvailable  = ATOMIC_INIT(1);
static struct cdev       gl_masterCdev;
static int               gl_masterStatus = 0;

static char  gl_configBuffer[sizeof(struct tag_config)];
static char  gl_newNameBuffer[sizeof(struct tag_config) + 100];
static const char * validTagNameChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_";

// -----------------------------------------
// Misc functions. 
// -----------------------------------------

// This function is used by our device class to set the permissions of the devices that it creates. 
static char *
tagfd_devnode(struct device *dev, umode_t *mode)
{
	if(!mode) return NULL;
    if(dev->devt == MKDEV(MAJOR(gl_dev), 0))
	{
		*mode = 0200;
	}
	else
	{
		*mode = 0666;
	}
	
    return NULL;
}


static int 
tagfd_isNameTaken(const char * name, size_t namelen)
{
	int i ;
	
	if(namelen > TAG_STRING_VALUE_LENGTH) namelen = TAG_STRING_VALUE_LENGTH;
	
	for(i = 0 ; i < gl_nEntities; i++)
	{
		if (0 == strncmp(gl_tags[i].name, name, namelen))
		{
			return 1;
		}
	}
	return 0;
}



// -----------------------------------------
// tag_ctx file ops
// -----------------------------------------


static int 
tagfd_open(struct inode * inode, struct file * filp)
{
	
	struct tag_watcher * watcher = kmalloc(sizeof(struct tag_watcher), GFP_KERNEL);
	if(watcher == NULL)
	{
		return -ENOMEM;
	}
	
	watcher->ts_lastRead = 0;
	watcher->e_ctx = container_of(inode->i_cdev, struct tag_ctx, cdev);
	
	filp->private_data = watcher;
	
	return 0;
}

static int
tagfd_release(struct inode * inode, struct file * filp)
{
	kfree(filp->private_data);
	return 0;
}


static ssize_t
tagfd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct tag_watcher * watcher = filp->private_data;
	
	if(count < sizeof(tag_t))
		return -EINVAL;
	

	// acquire lock on mutex. 
	if(mutex_lock_interruptible(&watcher->e_ctx->mtx))
		return -ERESTARTSYS;
	
	
	// while no new value
	while (watcher->ts_lastRead == watcher->e_ctx->tag.timestamp)
	{ 
		// release the lock.
		mutex_unlock(&watcher->e_ctx->mtx);
		
		// if we're in non-blocking mode, don't block. 
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		
		// if we can block, do so. 
		if(wait_event_interruptible(watcher->e_ctx->wqh, (watcher->ts_lastRead != watcher->e_ctx->tag.timestamp)))
			return -ERESTARTSYS;
		
		// reaquire lock for while condition check.
		if(mutex_lock_interruptible(&watcher->e_ctx->mtx))
			return -ERESTARTSYS;
	}
	
	// ok, data is available. 
	if(copy_to_user(buf, &watcher->e_ctx->tag ,sizeof(tag_t)))
	{
		mutex_unlock(&watcher->e_ctx->mtx);
		return -EFAULT;
	}
	watcher->ts_lastRead = watcher->e_ctx->tag.timestamp;

	mutex_unlock(&watcher->e_ctx->mtx);
	return sizeof(tag_t);
}

static ssize_t
tagfd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	tag_t tmp;
	struct tag_watcher * watcher = filp->private_data;
	
	if(count < sizeof(tag_t))
		return -EINVAL;

	// acquire lock on mutex. 
	if(mutex_lock_interruptible(&watcher->e_ctx->mtx))
		return -ERESTARTSYS;
	
	// copy data
	if(copy_from_user(&tmp,buf,sizeof(tag_t)))
	{
		mutex_unlock(&watcher->e_ctx->mtx);
		return -EFAULT;
	}
	
	// permission check
	// if they try to change the data type, deny permission
	if(watcher->e_ctx->tag.dtype != tmp.dtype)
	{
		mutex_unlock(&watcher->e_ctx->mtx);
		return -EPERM;
	}
	// if they don't update the timesatmp, the request is invalid
	if(watcher->e_ctx->tag.timestamp >= tmp.timestamp)
	{
		mutex_unlock(&watcher->e_ctx->mtx);
		return -EINVAL;
	}
	
	// copy into place. 
	memcpy(&watcher->e_ctx->tag, &tmp, sizeof(tag_t));
	
	// unlock
	mutex_unlock(&watcher->e_ctx->mtx);
	
	// wake anybody waiting
	wake_up_interruptible(&watcher->e_ctx->wqh);
	
	return sizeof(tag_t);
}


static unsigned int 
tagfd_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct tag_watcher * watcher = filp->private_data;
	
	//lock
	if(mutex_lock_interruptible(&watcher->e_ctx->mtx))
		return -ERESTARTSYS;
	// poll wait
	poll_wait(filp, &watcher->e_ctx->wqh,  wait);
	// readable
	if (watcher->ts_lastRead != watcher->e_ctx->tag.timestamp)
		mask |= POLLIN | POLLRDNORM;	
	// always writable
	mask |= POLLOUT | POLLWRNORM;
	//unlock
	mutex_unlock(&watcher->e_ctx->mtx);
	return mask;
}


struct file_operations tagfd_tag_ctx_fops = {
	.owner = THIS_MODULE,
	.open = tagfd_open,
	.release = tagfd_release,
	.read = tagfd_read,
	.write = tagfd_write,
	.poll = tagfd_poll,
};


// -----------------------------------------
// Constructor and destructor for struct tag_ctx
// -----------------------------------------


// constructor 

static int 
tagfd_construct_tag(struct tag_ctx * ectx, int minor, struct class * class, tag_t ent, const char * name)
{
	int err = 0;
	dev_t devno = MKDEV(MAJOR(gl_dev),minor);
	struct device * device = NULL;
	
	ectx->tag = ent;
	strncpy(ectx->name, name, TAG_NAME_LENGTH-1);
	
	// Rest of context initialization
	mutex_init(&ectx->mtx);
	cdev_init(&ectx->cdev, &tagfd_tag_ctx_fops);
	ectx->cdev.owner = THIS_MODULE;
	init_waitqueue_head(&ectx->wqh);
	
	err = cdev_add(&ectx->cdev, devno, 1);
	if(err)
	{
		printk(KERN_WARNING "tagfd: Error %d while trying to add device %s\n", err, name);
		mutex_destroy(&ectx->mtx);
		return err;
	}
	
	device = device_create(class, NULL, devno, NULL, name);
	if(IS_ERR(device))
	{
		err = PTR_ERR(device);
		printk(KERN_WARNING "tagfd: Error %d while trying to create %s\n", err, name);
		mutex_destroy(&ectx->mtx);
		cdev_del(&ectx->cdev);
		return err;
	}
	
	return 0;
}


// destructor
static void
tagfd_destruct_tag(struct tag_ctx * ectx, int minor, struct class * class)
{
	device_destroy(class, MKDEV(MAJOR(gl_dev), minor));
	cdev_del(&ectx->cdev);
	mutex_destroy(&ectx->mtx);
	// wait queue?
}




// -----------------------------------------
// Master device file ops 
// -----------------------------------------


static int 
tagfd_masterOpen(struct inode * inode, struct file * filp)
{
	if(! atomic_dec_and_test(&gl_masterAvailable))
	{
		atomic_inc(&gl_masterAvailable);
		return -EBUSY;
	}
	filp->private_data = &gl_masterCdev;
	return 0;
}

static int
tagfd_masterRelease(struct inode * inode, struct file * filp)
{
	atomic_inc(&gl_masterAvailable);
	return 0;
}


static ssize_t
tagfd_masterWrite(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int result, err, i, namelen;
	tag_t ent;
	struct timespec ts;
	struct tag_config * econf = (struct tag_config*) gl_configBuffer;
	
	// set up tag
	memset(&ent,0,sizeof(tag_t));
	getnstimeofday(&ts);
	
	ent.timestamp = ts.tv_sec;
	ent.timestamp *= 1000;
	ent.timestamp += ts.tv_nsec/1000000;
	ent.quality = QUALITY_UNCERTAIN;
	
	// Make sure their write request was big enough to be valid. 
	if(count < sizeof(struct tag_config))
	{
		printk(KERN_WARNING "tagfd.master: Received write request with invalid count.\n");
		return -EINVAL;
	}
	
	// fetch the data from the user and make sure that the parameters they supplied are actually right. 
	memset (gl_configBuffer, 0, sizeof(struct tag_config));
	result = copy_from_user(gl_configBuffer, buf, sizeof(struct tag_config));
	if(result)
	{
		printk(KERN_WARNING "tagfd.master: Could not fetch data from userspace.\n");
		return -EFAULT;
	}
	
	// check action
	if(econf->action != '+')
	{
		printk(KERN_WARNING "tagfd.master: Received request with invalid action.\n");
		return -EINVAL;
	}
	
	// make sure there is space for us to add a new tag
	if(gl_nEntities == max_tags)
	{
		printk(KERN_WARNING "tagfd.master: Received tag creation request, but already at maximum number of tags.\n");
		return -ENOMEM;
	}
	
	
	// check data type
	switch(econf->dtype)
	{
		case DT_INT8 :
		case DT_UINT8 :
		case DT_INT16 :
		case DT_UINT16 :
		case DT_INT32 :
		case DT_UINT32 :
		case DT_INT64 :
		case DT_UINT64 :
		case DT_REAL32 :
		case DT_REAL64 :
		case DT_TIMESTAMP :
		case DT_STRING :
			break;
		default:
			printk(KERN_WARNING "tagfd.master: Received tag creation request with invalid datatype.\n");
			return -EINVAL;
			
	}
	// assign data type. 
	ent.dtype = econf->dtype;

	
	// check that the name is null terminated. 
	if(econf->name[TAG_NAME_LENGTH-1] != 0)
	{
		printk(KERN_WARNING "tagfd.master: Received tag creation request with invalid name (not null-terminated).\n");
		//print_hex_dump_bytes("", DUMP_PREFIX_NONE, econf, sizeof(struct tag_config));
		return -EINVAL;
	}

	// check if the name is empty.
	namelen = strlen(econf->name);
	if(namelen == 0)
	{
		printk(KERN_WARNING "tagfd.master: Received tag creation request with empty name.\n");
		return -EINVAL;
	}
	
	// check if the name contains only allowed characters. 
	for(i=0; i < namelen;i++)
	{
		if(!strchr(validTagNameChars,econf->name[i]))
		{
			printk(KERN_WARNING "tagfd.master: Received tag creation request with invalid name: %s\n",econf->name);
			return -EINVAL;
		}			
	}
	
	// check if the name is already taken.
	if(tagfd_isNameTaken(econf->name, namelen))
	{
		printk(KERN_WARNING "tagfd.master: Received tag creation request but name already exists: %s\n",econf->name);
		return -EEXIST ;
	}
	
	// good to go!
	memset(gl_newNameBuffer,0,sizeof(gl_newNameBuffer));
	result = snprintf(gl_newNameBuffer, sizeof(gl_newNameBuffer), "%s%s", PREFIX, econf->name);
	if(result < 0 || result == sizeof(gl_newNameBuffer))
	{
		printk(KERN_WARNING "tagfd.master: Failed to snprintf device path for: %s\n",econf->name);
		return -ENOTRECOVERABLE ;
	}
	
	err = tagfd_construct_tag(&gl_tags[gl_nEntities], gl_nEntities+1, gl_tagfdClass ,ent, gl_newNameBuffer);
	if(err)
	{
		printk(KERN_WARNING "tagfd.master: Failed to create tag at: %s\n",gl_newNameBuffer);
		return err ;
	}
	gl_nEntities++;
	
	return sizeof(struct tag_config);
}	


struct file_operations tagfd_masterFOps = {
	.owner = THIS_MODULE,
	.write = tagfd_masterWrite,
	.open = tagfd_masterOpen,
	.release = tagfd_masterRelease,
};




// -----------------------------------------
// Module initialization and exit
// -----------------------------------------

static void 
tagfd_cleanup(void)
{
	int i;
	
	// Destruct our tags.
	if(gl_tags)
	{
		for(i = 0; i < gl_nEntities; i++)
		{
			// remember, minor number zero is the master device, so always pass i+1.
			tagfd_destruct_tag(&gl_tags[i], i+1, gl_tagfdClass);
		}
		kfree(gl_tags);
	}
	
	// Remove our master device.
	if(gl_masterStatus > 1)
		device_destroy(gl_tagfdClass, MKDEV(MAJOR(gl_dev),0));
	if(gl_masterStatus > 0)
		cdev_del(&gl_masterCdev);
	
	
	// Destroy our device class.
	if(gl_tagfdClass)
		class_destroy(gl_tagfdClass);
	
	
	
	// Unregister our character devices. 
	// Note that this doesn't get called if alloc_chrdev_region fails. 
	unregister_chrdev_region(gl_dev, max_tags+1);
	
	
}


// Initialization function
static int __init // "__init" (optional) tells the kernel that this function is only needed at init time. 
tagfd_init(void)
{	
	int err;
	struct device *masterDev = NULL;
	
	// Make sure max_tags is valid
	if (max_tags < 1)
	{
		printk(KERN_WARNING "tagfd: %d is not a valid value for max_tags. Must be positive. \n", max_tags);
		return -EINVAL; // we can't goto fail yet, don't change this. 
	}
	
	// Allocate our range of char devices.
	// Use module parameter "max_tags" to determine how many device minor numbers to register. 
	// Device major number is acquired dynamically though alloc_chardev_region.
	err = alloc_chrdev_region(&gl_dev, 0, max_tags+1, NAME);
	if(err < 0)
	{
		printk(KERN_WARNING "tagfd: failed to allocate chardev region, errror %d.\n", err);
		return err; // we can't goto fail yet, don't change this. 
	}
	
	
	
	// Create a device class (for tags)
	gl_tagfdClass = class_create(THIS_MODULE, NAME);
	if(IS_ERR(gl_tagfdClass))
	{
		printk(KERN_WARNING "tagfd: failed to create device class\n");
		err = PTR_ERR(gl_tagfdClass);
		goto fail;
	}
	gl_tagfdClass->devnode = tagfd_devnode;
	
	// Allocate memory for our actual data storage. 
	gl_tags = kmalloc(max_tags * sizeof(struct tag_ctx),GFP_KERNEL);
	if(gl_tags == NULL)
	{
		printk(KERN_WARNING "tagfd: failed to allocate tags.\n");
		err = -ENOMEM;
		goto fail;
	}
	memset(gl_tags,0,sizeof(struct tag_ctx) * max_tags);
	
	// Create our master device
	cdev_init(&gl_masterCdev, &tagfd_masterFOps);
	gl_masterCdev.owner = THIS_MODULE;
	err = cdev_add(&gl_masterCdev, MKDEV(MAJOR(gl_dev),0), 1);
	if(err)
	{
		printk(KERN_WARNING "tagfd: failed to add master device.\n");
		goto fail;
	}
	gl_masterStatus++;
	
	// Add the master device to the filesystem
	masterDev = device_create(gl_tagfdClass, NULL, // no parent device
	                          MKDEV(MAJOR(gl_dev),0), NULL, // no additional data
							  "tagfd.master");
	if(IS_ERR(masterDev))
	{
		err = PTR_ERR(masterDev);
		printk(KERN_WARNING "tagfd: failed to add master device to the filesystem: %d\n", err);
		goto fail;
	}
	gl_masterStatus++;
	
	printk(KERN_WARNING "tagfd: loaded.\n");
	return 0;
	
	fail:
	
	tagfd_cleanup();
	return err;
}

// Cleanup function
static void __exit // "__exit" (optional) tells the kernel that this function is only needed at cleanup time.
tagfd_exit(void)
{
	tagfd_cleanup();
	printk(KERN_WARNING "tagfd: unloaded.\n");
}

// tell the kernel what functions to call on load and unload.
module_init(tagfd_init);
module_exit(tagfd_exit);

// -----------------------------------------
// Module info
// -----------------------------------------

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harris M. Snyder");
MODULE_DESCRIPTION("An IPC mechanism for process control.");

