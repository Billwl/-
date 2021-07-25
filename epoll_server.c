#include <stdio.h> //tcp服务器
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h> //定义了sockaddr_in
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/poll.h>
#include <sys/epoll.h>

#include "tpool.h"

#define PORT 8888 //端口号不要小于1024
#define MYADDR "192.168.12.13"

void *read_msg(void *arg)
{
    int c_fd = *((int *)arg);
    char buffer[1024] = {0};

    memset(buffer, 0, sizeof(buffer));
    if (read(c_fd, buffer, sizeof(buffer)) == 0)
    {
        printf("%s is exit!\n", "客户端");
        //exit(1);
        //pthread_exit(NULL);
        close(c_fd);
        return NULL;
    }
    printf("接收到来自客户端的数据：%s\n", buffer);
}

void *write_msg(void *arg)
{
    int c_fd = *((int *)arg);
    char buffer[1024] = {0};
    while (1)
    {
        printf("请输入你要对客户端说的话：");
        memset(buffer, 0, sizeof(buffer));
        scanf("%s", buffer);
        write(c_fd, buffer, sizeof(buffer));
    }
}



int main()
{
    signal(SIGPIPE, SIG_IGN); //忽略管道信号；

    int s_fd;
    pid_t pid;
    pthread_t r_id;
    pthread_t w_id;

    //套接字描述符
    s_fd = socket(AF_INET, SOCK_STREAM, 0); //调用套接字函数，配置协议族和子通信方式
    if (s_fd == -1)
    {
        perror("socekt:");
        exit(1);
    }

    struct sockaddr_in s_addr; //服务器的ip端口结构体
    int s_addrlen = sizeof(struct sockaddr_in);
    memset(&s_addr, 0, s_addrlen);
    s_addr.sin_family = AF_INET;                //绑定协议族
    s_addr.sin_port = htons(PORT);              //将小端本地数据转换成大端网络数据
    s_addr.sin_addr.s_addr = inet_addr(MYADDR); //inet_addr函数：将字符串类型的ip地址转换成32位无符号整型

    int opt = 1;
    setsockopt(s_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //实现端口复用

    int ret = bind(s_fd, (struct sockaddr *)&s_addr, s_addrlen); //bind函数：绑定ip地址和端口
    if (ret == -1)
    {
        perror("bind");
        exit(0);
    }
    printf("ip和端口绑定成功！\n");

    ret = listen(s_fd, 3); //将文件描述符由主动化为被动
    if (ret == -1)
    {
        perror("listen");
        exit(2);
    }
    printf("监听中......\n");



    tpool_create(20);

    int fd = epoll_create(512);

    int num = 0;
    int temp = 0;
    int *arg;
    struct pollfd fds[1024];

    for(int i = 0; i < 1024; i++)
    {
        fds[i].fd = -1;
    }

    fds[num].fd = s_fd;
    fds[num].events = POLLIN;

    num++;

    int c_fd;                  //客户端描述符
    struct sockaddr_in c_addr; //客户端的ip端口结构体
    int c_addrlen = sizeof(struct sockaddr_in);
    memset(&c_addr, 0, c_addrlen);

    int *arg;
    int epfd;
    struct epoll_event event;
    struct epoll_event events[512];
    event.events = EPOLLIN;
    event.data.fd = s_fd;

    epoll_ctl(epfd,EPOLL_CTL_ADD,s_fd,&event);

    //fork
    while (1)
    {
        int fd_num = epoll_wait(epfd,events, sizeof(event)/sizeof(events[0]), -1);
        for(int i = 0;i < fd_num ; i++)
        {
            if(events[i].events == EPOLLIN)
            {
                if(events[i].data.fd == s_fd)
                {
                    c_fd = accept(s_fd, (struct sockaddr *)&c_addr, &c_addrlen); //accept函数是阻塞的
                    if (c_fd == -1)
                    {
                        printf("accept");
                        exit(3);
                    }
                    printf("客户端:ip:%s,端口:%d\n", inet_ntoa(c_addr.sin_addr), ntohs(c_addr.sin_port));
                    event.events = EPOLLIN;
                    event.data.fd = c_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, c_fd, &event);
                }
                else
                {
                    arg = (int *)malloc(sizeof(int));
                    *arg = events[i].data.fd;
                    tpool_add_work(read_msg, (void *)arg);
                    usleep(3);
                }
            }
        }


        int fd_num = poll(fds, num, -1);

        temp = num;

        for (int i = 0; i < 1024; i++)
        {
            if(fds[i].fd == -1)
            {
                continue;
            }

            if (fds[i].events == fds[i].revents)
            {
                if (fds[i].fd == s_fd)
                {
                    c_fd = accept(s_fd, (struct sockaddr *)&c_addr, &c_addrlen); //accept函数是阻塞的
                    if (c_fd == -1)
                    {
                        printf("accept");
                        exit(3);
                    }
                    printf("客户端:ip:%s,端口:%d\n", inet_ntoa(c_addr.sin_addr), ntohs(c_addr.sin_port));

                    //fds[num].fd = c_fd;
                    //fds[num].events = POLLIN;
                    //num++;
                    for(int i = 0; i < 1024; i++)
                    {
                        if(fds[i].fd == -1)
                        {
                            fds[i].fd = c_fd;
                            fds[i].events = POLLIN;
                            num++;
                            break;
                        }
                    }

                    if (num == 1024)
                    {
                        printf("the max fd error!\n");
                        exit(1);
                    }

                    --fd_num;
                }
                else
                {
                    arg = (int *)malloc(sizeof(int));
                    *arg = fds[i].fd;
                    tpool_add_work(read_msg, (void *)arg);
                    usleep(3);

                    --fd_num;
                }
                if(fd_num == 0)
                {
                    break;
                }
            }
        }
    }
    return 0;
}
