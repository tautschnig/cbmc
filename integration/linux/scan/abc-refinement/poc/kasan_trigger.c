#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
int main(void) {
    int fd = open("/dev/kasan_poc", O_WRONLY);
    if (fd < 0) { perror("open /dev/kasan_poc"); return 1; }
    char buf[256];
    memset(buf, 'X', sizeof(buf));
    ssize_t n = write(fd, buf, sizeof(buf));
    printf("[kasan_poc] write(%d) = %zd\n", (int)sizeof(buf), n);
    close(fd);
    return 0;
}
