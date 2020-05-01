#include <iostream>
#include <stdint.h>

#include <unistd.h>
#include <errno.h>
#include <sys/timerfd.h>


// g++ -Wall test_timerfd.cc
int main() {
    int ret;

    int timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC /* | TFD_NONBLOCK */);
    if (timerfd == -1) {
        std::cout << "timerfd create error, ret: " << timerfd << ", errno: " << errno << std::endl;
        return 0;
    }

    struct itimerspec spec = {
        .it_interval = {0, 0},  /* non zero for repeated timer */
        .it_value = { .tv_sec = 0, .tv_nsec = 900000000 },
    };
    ret = timerfd_settime(timerfd, 0, &spec, 0);
    if (ret == -1) {
        std::cout << "timerfd settime error, ret: " << ret << ", errno: " << errno << std::endl;
        return 0;
    }

    int64_t count;

    std::cout << "before 1st read: " << time(0) << std::endl;
    ret = read(timerfd, &count, sizeof(count));
    std::cout << "after 1st read: " << time(0) << std::endl;

    std::cout << "before 2nd read: " << time(0) << std::endl;
    ret = read(timerfd, &count, sizeof(count));
    std::cout << "after 2nd read: " << time(0) << std::endl;  // never reach

    close(timerfd);
    return 0;
}