#include <dev/chrdev.h>
#include <lib/printk.h>
#include <sys/errno.h>
#include <lib/string.h>
#include <sys/spinlock.h>

static chrdev_entry_t chrdev_table[CHRDEV_MAX_MAJOR];
static spinlock_t chrdev_lock;

void chrdev_init(void)
{
	spin_init(&chrdev_lock);
	for (int i = 0; i < CHRDEV_MAX_MAJOR; i++) {
		chrdev_table[i].name = NULL;
		chrdev_table[i].fops = NULL;
	}
	log("chrdev: initialised (max major %d)\n", CHRDEV_MAX_MAJOR - 1);
}

int chrdev_register(uint32_t major, const char *name, file_ops_t *fops)
{
	if (major == 0 || major >= CHRDEV_MAX_MAJOR)
		return -EINVAL;
	if (!name || !fops)
		return -EINVAL;

	unsigned long flags = spin_lock_irqsave(&chrdev_lock);

	if (chrdev_table[major].fops != NULL) {
		spin_unlock_irqrestore(&chrdev_lock, flags);
		log("chrdev: major %u already registered as '%s'\n",
		    major,
		    chrdev_table[major].name);
		return -EBUSY;
	}

	chrdev_table[major].name = name;
	chrdev_table[major].fops = fops;

	spin_unlock_irqrestore(&chrdev_lock, flags);
	log("chrdev: registered major %u as '%s'\n", major, name);
	return 0;
}

int chrdev_unregister(uint32_t major)
{
	if (major == 0 || major >= CHRDEV_MAX_MAJOR)
		return -EINVAL;

	unsigned long flags = spin_lock_irqsave(&chrdev_lock);

	if (chrdev_table[major].fops == NULL) {
		spin_unlock_irqrestore(&chrdev_lock, flags);
		return -ENODEV;
	}

	log("chrdev: unregistered major %u ('%s')\n", major, chrdev_table[major].name);
	chrdev_table[major].name = NULL;
	chrdev_table[major].fops = NULL;
	spin_unlock_irqrestore(&chrdev_lock, flags);
	return 0;
}

file_ops_t *chrdev_lookup(uint32_t major)
{
	if (major == 0 || major >= CHRDEV_MAX_MAJOR)
		return NULL;
	unsigned long flags = spin_lock_irqsave(&chrdev_lock);
	file_ops_t *fops = chrdev_table[major].fops;
	spin_unlock_irqrestore(&chrdev_lock, flags);
	return fops;
}