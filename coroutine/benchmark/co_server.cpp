#include <boost/thread.hpp>
#include <sys/resource.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include "coroutine.h"

static const char* g_ip = "127.0.0.1";
static const uint16_t g_port = 43333;
int thread_count = 1;

void echo_server()
{
    int ret;
    int accept_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(accept_fd >= 0);

    int v = 1;
    ret = setsockopt(accept_fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(v));
    assert(ret == 0);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    addr.sin_addr.s_addr = inet_addr(g_ip);
    ret = bind(accept_fd, (sockaddr*)&addr, sizeof(addr));
    assert(ret == 0);

    ret = listen(accept_fd, 100);
    assert(ret == 0);

    printf("Coroutine server startup, thread:%d, listen %s:%d\n", thread_count, g_ip, g_port);
    std::atomic<int> session_count{0};
    for (;;) {
        socklen_t addr_len = sizeof(addr);
        int sock_fd = accept(accept_fd, (sockaddr*)&addr, &addr_len);
        if (sock_fd < 0) {
            perror("accept error:");
            continue;
        }

        go [sock_fd, &session_count]{
            ++session_count;
            printf("connected(%d).\n", (int)session_count);
            co_chan<bool> err;

            go [err, sock_fd] {
                int buflen = 4096;
                char *buf = new char[buflen];
                std::unique_ptr<char[]> _ep(buf);

                int noyield_for_c = 0;
                for (;;++noyield_for_c) {
                    auto n = read(sock_fd, buf, buflen);
                    if (n <= 0) break;

                    n = write(sock_fd, buf, n);
                    if (n <= 0) break;

                    if ((noyield_for_c & 0xff) == 0)
                        yield;
                }
                err << true;
            };

            err >> nullptr;
            --session_count;
            printf("disconnected(%d).\n", (int)session_count);
        };
    }
}

int main(int argc, char **argv)
{
    if (argc > 1)
        thread_count = atoi(argv[1]);

    rlimit of = {65536, 65536};
    if (-1 == setrlimit(RLIMIT_NOFILE, &of)) {
        perror("setrlimit");
        exit(1);
    }

    go echo_server;

    boost::thread_group tg;
    for (int i = 0; i < thread_count; ++i)
        tg.create_thread([] { g_Scheduler.RunUntilNoTask(); });
    tg.join_all();
    return 0;
}
