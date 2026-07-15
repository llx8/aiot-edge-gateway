/**
 * uart_loopback_test.c — UART 物理回环验证工具
 *
 * 用途：验证 RK3588 UART 硬件通路是否正常。
 * 需要将 TX 和 RX 物理短接（回环），然后自发自收。
 *
 * 编译（板端）：
 *   gcc -std=c11 uart_loopback_test.c -o uart_loopback_test
 *
 * 用法：
 *   ./uart_loopback_test /dev/ttyS0 115200
 *
 * 测试流程：
 *   1. 用杜邦线短接 TX 和 RX
 *   2. 运行本工具
 *   3. 发送测试数据，验证接收到的数据完全一致
 *   4. 自动测试 3 种波特率：9600, 115200, 921600
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <stdint.h>

static int configure_uart(const char* device, speed_t speed) {
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;        // 无校验
    tty.c_cflag &= ~CSTOPB;        // 1 停止位
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 数据位
    tty.c_cflag &= ~CRTSCTS;       // 无硬件流控

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // 原始模式
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);           // 无软件流控
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;            // 至少读 1 字节
    tty.c_cc[VTIME] = 5;           // 0.5 秒超时

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    // 清空缓冲区
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static int test_loopback(int fd, int baud_rate) {
    static const char* test_patterns[] = {
        "Hello RK3588 UART!",
        "\x00\x01\x02\x03\xFF\xFE\xFD\xFC",
        "Modbus: 01 03 00 00 00 0A C5 CD",
        "0123456789ABCDEF0123456789ABCDEF",
    };
    static const int num_patterns = 4;

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < num_patterns; i++) {
        size_t len = strlen(test_patterns[i]);
        if (i == 1) len = 8;  // 二进制模式

        // 发送
        ssize_t written = write(fd, test_patterns[i], len);
        if (written != (ssize_t)len) {
            printf("  [FAIL] write: %zd/%zu bytes\n", written, len);
            failed++;
            continue;
        }

        // 接收（等待最多 1 秒）
        uint8_t buf[256] = {0};
        size_t total = 0;
        int retries = 10;
        while (total < len && retries-- > 0) {
            ssize_t n = read(fd, buf + total, len - total);
            if (n > 0) total += n;
            else if (n == 0) break;
            else if (errno != EAGAIN) break;
        }

        if (total != len) {
            printf("  [FAIL] read: %zu/%zu bytes\n", total, len);
            failed++;
            continue;
        }

        if (memcmp(test_patterns[i], buf, len) == 0) {
            printf("  [PASS] pattern %d: %zu bytes matched\n", i + 1, len);
            passed++;
        } else {
            printf("  [FAIL] pattern %d: data mismatch\n", i + 1);
            printf("    sent: ");
            for (size_t j = 0; j < len; j++) printf("%02X ", (uint8_t)test_patterns[i][j]);
            printf("\n    recv: ");
            for (size_t j = 0; j < len; j++) printf("%02X ", buf[j]);
            printf("\n");
            failed++;
        }
    }

    printf("  Result: %d passed, %d failed\n", passed, failed);
    return failed;
}

int main(int argc, char* argv[]) {
    const char* device = (argc >= 2) ? argv[1] : "/dev/ttyS0";
    int baud_arg = (argc >= 3) ? atoi(argv[2]) : 115200;

    printf("=== UART 物理回环验证工具 ===\n");
    printf("Device: %s\n", device);
    printf("请确保 TX 和 RX 已用杜邦线短接！\n\n");

    if (baud_arg > 0) {
        // 单波特率测试
        speed_t speed = B115200;
        switch (baud_arg) {
            case 9600:   speed = B9600;   break;
            case 19200:  speed = B19200;  break;
            case 38400:  speed = B38400;  break;
            case 57600:  speed = B57600;  break;
            case 115200: speed = B115200; break;
            case 921600: speed = B921600; break;
            default:
                fprintf(stderr, "Unsupported baud rate: %d\n", baud_arg);
                return 1;
        }

        printf("测试波特率: %d\n", baud_arg);
        int fd = configure_uart(device, speed);
        if (fd < 0) return 1;
        int fails = test_loopback(fd, baud_arg);
        close(fd);
        return fails ? 1 : 0;
    }

    // 自动测试多种波特率
    static const struct { int baud; speed_t speed; } tests[] = {
        {9600,   B9600},
        {115200, B115200},
        {921600, B921600},
    };

    int total_fails = 0;
    for (size_t i = 0; i < 3; i++) {
        printf("--- 测试波特率: %d ---\n", tests[i].baud);
        int fd = configure_uart(device, tests[i].speed);
        if (fd < 0) {
            total_fails++;
            continue;
        }
        total_fails += test_loopback(fd, tests[i].baud);
        close(fd);
        printf("\n");
    }

    printf("=== 总计: %d 失败 ===\n", total_fails);
    return total_fails ? 1 : 0;
}