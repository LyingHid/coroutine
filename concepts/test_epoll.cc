#include <iostream>
#include <thread>
#include <stdint.h>

#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>

#include <sys/eventfd.h>


#define EPOLL_SIZE 1024


void notify(int event_fd) {
    uint64_t count = 1;

    std::this_thread::sleep_for(std::chrono::seconds(2));

    ssize_t ret = write(event_fd, &count, sizeof(count));
    if (ret != sizeof(count)) {
        std::cout << "notify failed" << std::endl;
    }
}


// g++ -Wall test_epoll.cc -lpthread
int main(int argc, char *argv[]) {
    int epoll_fd = epoll_create(EPOLL_SIZE);
    if (epoll_fd == -1) {
        std::cout << "epoll create error, ret: " << epoll_fd << ", errno: " << errno << std::endl;
        return 0;
    }

    int event_fd = eventfd(0, EFD_NONBLOCK);
    if (event_fd == -1) {
        std::cout << "event fd create error, ret: " << event_fd << ", errno: " << errno << std::endl;
        return 0;
    }

    struct epoll_event read_event;
    read_event.events = EPOLLIN | EPOLLET;
    read_event.data.fd = event_fd;
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &read_event);
    if (ret == -1)
    {
        std::cout << "epoll add error, ret: " << ret << ", errno: " << errno << std::endl;
        return 0;
    }

    std::thread notify_thread(notify, event_fd);

    struct epoll_event happened_events[EPOLL_SIZE];
    int happend = epoll_wait(epoll_fd, happened_events, EPOLL_SIZE, -1);
    for (int i = 0; i < happend; i++) {
        const auto &event = happened_events[i];
        if (event.data.fd == event_fd) {
            uint64_t count;
            ssize_t read_bytes = read(event_fd, &count, sizeof(count));
            std::cout << "received notify: " << (event.events & EPOLLIN) << std::endl;
            std::cout << "event count: " << count << ", length: " << read_bytes << std::endl;
        }
    }
    notify_thread.join();

    close(event_fd);
    close(epoll_fd);
    return 0;
}