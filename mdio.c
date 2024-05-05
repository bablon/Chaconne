#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/mii.h>
#include <linux/mdio.h>
#include <linux/if.h>
#include <linux/sockios.h>

#include "cli-term.h"

struct mdio_sock {
	int fd;
	const char *ifname;
};

static int mdio_sock_read(struct mdio_sock *sock, int phy, int reg,
			  uint16_t *val)
{
	struct ifreq rq;
	struct mii_ioctl_data *mii;

	snprintf(rq.ifr_name, sizeof(rq.ifr_name), "%s", sock->ifname);
	mii = (struct mii_ioctl_data *)&rq.ifr_ifru;

	mii->phy_id = phy;
	mii->reg_num = reg;

	if (ioctl(sock->fd, SIOCGMIIREG, &rq) == -1) {
		fprintf(stderr, "%s: siocgmiireg %x.%x: %s\n", sock->ifname,
			phy, reg, strerror(errno));
		return -1;
	}

	*val = mii->val_out;

	return 0;
}

static int mdio_sock_write(struct mdio_sock *sock, int phy, int reg,
			   uint16_t val)
{
	struct ifreq rq;
	struct mii_ioctl_data *mii;

	snprintf(rq.ifr_name, sizeof(rq.ifr_name), "%s", sock->ifname);
	mii = (struct mii_ioctl_data *)&rq.ifr_ifru;

	mii->phy_id = phy;
	mii->reg_num = reg;
	mii->val_in = val;

	if (ioctl(sock->fd, SIOCSMIIREG, &rq) == -1) {
		fprintf(stderr, "%s: siocsmiireg %x.%x %x: %s\n", sock->ifname,
			phy, reg, val, strerror(errno));
		return -1;
	}

	return 0;
}

static struct mdio_sock *mdio_sock_create(const char *ifname)
{
	struct mdio_sock *sock;

	sock = malloc(sizeof(*sock));
	if (sock == NULL) {
		printf("%s: malloc: %s\n", ifname, strerror(errno));
		return NULL;
	}

	sock->ifname = ifname;

	sock->fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock->fd == -1) {
		fprintf(stderr, "%s: socket: %s\n", ifname, strerror(errno));
		free(sock);
		return NULL;
	}

	return sock;
}

static void mdio_sock_destroy(struct mdio_sock *sock)
{
	if (sock) {
		if (sock->fd != -1)
			close(sock->fd);
		free(sock);
	}
}

struct reg_info {
	int is_c22;
	int is_write;

	int phy;
	int page;
	int dev;
	int reg;
	int bit_h;
	int bit_l;
	int data;
};

static int get_number(char *s, char *end, int base, int *data,
		      const char *key, int min, int max)
{
	unsigned long val;
	char *endptr;

	errno = 0;
	val = strtoul(s, &endptr, base);
	if (errno != 0) {
		printf("%s: invalid %s\n", key, s);
		return -1;
	} else if (endptr == s) {
		printf("%s: missing digits\n", key);
		return -1;
	} else if (endptr != end) {
		printf("%s: invalid char in %s\n", key, s);
		return -1;
	}

	if (val < min || val > max) {
		printf("%s: %s out of range [%x, %x]\n", key, s, min, max);
		return -1;
	}

	*data = val;

	return 0;
}

static int parse_reg(char *regstr, struct reg_info *ri)
{
	char *base = regstr;
	char *ptr;

	ptr = strchr(base, '.');
	if (ptr == NULL) {
		printf("invalid register %s\n", regstr);
		return -1;
	}

	if (get_number(base, ptr, 16, &ri->phy, "phy address", 0, 0x1f))
		return -1;

	base = ptr + 1;
	ptr = strpbrk(base, "@.");
	if (ptr == NULL)
		ptr = base + strlen(base);

	if (get_number(base, ptr, 10, &ri->reg, "register", 0, 31))
		return -1;

	if (*ptr == '\0')
		return 0;

	if (*ptr == '@') {
		base = ptr + 1;
		ptr = strchr(base, '.');
		if (ptr == NULL)
			ptr = base + strlen(base);
		if (get_number(base, ptr, 10, &ri->page, "page", 0, 255))
			return -1;
		if (*ptr == '\0')
			return 0;
	}

	base = ptr + 1;
	ptr = strchr(base, ':');
	if (ptr == NULL)
		ptr = base + strlen(base);
	if (get_number(base, ptr, 10, &ri->bit_h, "bit_h", 0, 31))
		return -1;
	if (*ptr == '\0') {
		ri->bit_l = ri->bit_h;
		return 0;
	}
	base = ptr + 1;
	ptr = base + strlen(base);
	if (get_number(base, ptr, 10, &ri->bit_l, "bit_l", 0, 31))
		return -1;

	if (ri->bit_h < ri->bit_l) {
		int tmp = ri->bit_h;

		ri->bit_h = ri->bit_l;
		ri->bit_l = tmp;
	}

	return 0;
}

static void init_reg_info(struct reg_info *ri)
{
	if (ri) {
		memset(ri, 0, sizeof(*ri));

		ri->page = -1;
		ri->bit_h = ri->bit_l = -1;
	}
}

static int parse_reg_info(int argc, char *argv[], struct reg_info *ri)
{
	init_reg_info(ri);

	if (strcmp(argv[0], "c22") == 0)
		ri->is_c22 = 1;

	if (strcmp(argv[1], "write") == 0) {
		char *base = argv[4];
		char *ptr = base + strlen(base);

		ri->is_write = 1;
		if (get_number(base, ptr, 0, &ri->data, "data", 0, 0xffff))
			return -1;
	}

	return parse_reg(argv[3], ri);
}

static int get_space_index(int hi, int li, uint8_t *idx)
{
	int width = hi - li + 1;

	if (width <= 4) {
		return 0;
	} else if (width <= 8) {
		idx[0] = li + 4;
		return 1;
	} else if (width <= 12) {
		idx[0] = li + 8;
		idx[1] = li + 4;
		return 2;
	} else {
		idx[0] = li + 12;
		idx[1] = 8;
		idx[2] = 4;
		return 3;
	}
}

static void bin2str(uint16_t val, char *buf, int hi, int li)
{
	int i, sc, sci = 0;
	char *p = buf;
	uint8_t idx[4];

	sc = get_space_index(hi, li, idx);

	for (i = hi; i >= li; i--) {
		*p++ = (val & (1 << i)) ? '1' : '0';

		if (sc) {
			if (i == idx[sci]) {
				*p++ = ' ';
				sci++;
				sc--;
			}
		}
	}

	*p = '\0';
}

#define BITS_PER_LONG (sizeof(long) << 3)
#define GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

COMMAND(cmd_mdio, NULL,
	"mdio (c22|c45) (read|write) IFNAME REGSTR .DATA",
	"mdio register utility\n"
	"mdio c22 access\n"
	"mdio c45 access\n"
	"read operation\n"
	"write operation\n"
	"ethernet interface name\n"
	"register format phy.reg[@(page|dev)][.bit_h:bit_l]\n"
	"register data to write\n")
{
	int ret = 0;
	struct mdio_sock *mdio;
	struct reg_info ri;

	if (!strcmp(opt->argv[1], "write") && opt->argc != 5) {
		term_print(term, "missing DATA for write\r\n");
		return 1;
	}

	if (parse_reg_info(opt->argc, opt->argv, &ri))
		return 1;

	mdio = mdio_sock_create(opt->argv[2]);
	if (mdio == NULL) {
		term_print(term, "failed to create socket\n");
		return 1;
	}

	if (ri.page >= 0) {
		if (mdio_sock_write(mdio, ri.phy, 22, ri.page) != 0) {
			ret = 3;
			goto out;
		}
	}

	if (ri.is_write) {
		int phy;
		uint16_t data, mask;

		phy = ri.is_c22 ? ri.phy : mdio_phy_id_c45(ri.phy, ri.dev);

		data = ri.data;
		if (ri.bit_h >= 0) {
			if (mdio_sock_read(mdio, phy, ri.reg, &data) != 0) {
				ret = 2;
				goto out;
			}

			mask = GENMASK(ri.bit_h, ri.bit_l);
			data &= ~mask;
			data |= (ri.data << ri.bit_l) & mask;
		}

		if (mdio_sock_write(mdio, phy, ri.reg, data) != 0)
			ret = 3;
	} else {
		int phy;
		uint16_t data;
		char buf[32];

		phy = ri.is_c22 ? ri.phy : mdio_phy_id_c45(ri.phy, ri.dev);

		if (mdio_sock_read(mdio, phy, ri.reg, &data) != 0)
			ret = 2;
		else {
			if (ri.bit_h >= 0) {
				uint16_t part, mask;

				mask = GENMASK(ri.bit_h, ri.bit_l);
				part = (data & mask) >> ri.bit_l;
				bin2str(data, buf, ri.bit_h, ri.bit_l);
				printf("%04x \"%s\"\n", part, buf);
			} else {
				bin2str(data, buf, 15, 0);
				printf("%04x \"%s\"\n", data, buf);
			}
		}
	}

out:
	mdio_sock_destroy(mdio);

	return ret;
}
