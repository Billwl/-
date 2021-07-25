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
#include "network_chat.h"

#include "tpool.h"

#define PORT 5555 //端口号不要小于1024
#define MYADDR "127.0.0.1"

//登录者帐号信息
static people_t client_id;

int c_fd;
char buffer[1024];
void *write_msg(char *buffer)
{
    memset(buffer, 0, sizeof(buffer));
    scanf("%s", buffer);
    write(c_fd, buffer, sizeof(buffer));
    memset(buffer, 0, sizeof(buffer));
}

/*
 *功能：验证登录或注册帐号信息
 *输入：客户端套接字，登录/注册选择
 *输出：登录成功1，注册成功2，任意失败-1
 * */
int check_id(int sockfd, int choice)
{
    char ID_info[17]; //存储帐号信息以及登录/注册选择
    int i;
    char status[16]; //存储登录/注册状态
    int ret;

    printf("check ID information!\n");
    memset(ID_info, 0, 17);
    //登录，携带ID 和密码
    if (1 == choice)
    {
        ID_info[0] = '1';
        memcpy(ID_info + 1, client_id.id, 4);
        memcpy(ID_info + 9, client_id.passwd, 8);
    }

    //注册，携带昵称和密码
    else
    {
        ID_info[0] = '2';
        memcpy(ID_info + 5, client_id.name, 4);
        memcpy(ID_info + 9, client_id.passwd, 8);
    }

    //发送帐号信息给服务器端进行验证
    for (i = 0; i < 16; i++)
    {
        if (ID_info[i] == '\0')
        {
            ID_info[i] = '/';
        }
    }
    ID_info[i] = '\0';
    ret = send(sockfd, ID_info, strlen(ID_info), 0);
    if (-1 == ret)
    {
        perror("send id_info error!");
        return -1;
    }

    //接收帐号验证信息
    memset(status, 0, 16);
    ret = recv(sockfd, status, 16, 0);
    if (-1 == ret)
    {
        perror("recv id_info error!");
        return -1;
    }

    if (memcmp(status, "successfully!", 13) == 0)
    {
        //登录成功
        //printf("login successfully!\n");
        send(sockfd, "ok", 3, 0);
        ret = recv(sockfd, client_id.name, 4, 0);
        if (-1 == ret)
        {
            perror("recv ack_id_info error");
            return -1;
        }
        return 1;
    }
    else if (memcmp(status, "sign up", 7) == 0)
    {
        //注册成功
        printf("注册成功!\n");
        send(sockfd, "ok", 3, 0);
        ret = recv(sockfd, client_id.id, 4, 0);
        if (-1 == ret)
        {
            perror("recv ack_id_info error");
            return -1;
        }
        return 2;
    }

    else
    {
        printf("login or sign up error!\n");
        return -1;
    }
}

int main(int argc, char argv[])
{
    int choice;        //1--代表登录，2代表注册
    int login_status;  //登录状态，1成功，-1失败
    int signup_status; //注册状态，-1代表失败，2代表成功

    pthread_t r_id;

    pid_t pid;
    c_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c_fd == -1)
    {
        perror("socket:");
        exit(1);
    }

    struct sockaddr_in c_addr;
    int c_addrlen = sizeof(struct sockaddr_in);
    memset(&c_addr, 0, c_addrlen);

    c_addr.sin_family = AF_INET;
    c_addr.sin_port = htons(PORT);
    c_addr.sin_addr.s_addr = inet_addr(MYADDR);

    int ret = connect(c_fd, (struct sockaddr *)&c_addr, c_addrlen);
    if (ret == 0)
    {
        printf("欢迎使用-----聊天室\n");
        while (1)
        {
            printf("登录请输入1\n注册请输入2\n");
            scanf("%d", &choice);
            if (1 == choice)
            {
                printf("请输入账号名");
                scanf("%s", client_id.id);
                printf("请输入密码：");
                scanf("%s", client_id.passwd);
                login_status = check_id(c_fd, choice);
                if (login_status == 1)
                {
                    //登录成功,进入聊天室
                    printf("欢迎登录聊天室～%s\n", client_id.name);
                    break;
                }
                else
                    continue;
            }
            else if (choice == 2)
            {
                //注册，输入昵称和密码
                printf("请输入昵称:");
                scanf("%s", client_id.name);
                printf("请输入密码:");
                scanf("%s", client_id.passwd);
                signup_status = check_id(c_fd, choice);
                if (2 == signup_status)
                {
                    //注册成功，返回登录界面
                    printf("注册成功\n");
                    printf("你的帐号为：%s\n请重新登录\n", client_id.id);
                    continue;
                }
                else
                {
                    //注册失败
                    return -1;
                }
            }
            else
            {
                printf("错误！请输入正确数值！\n");
                continue;
            }
        }
    }
    else 
    {
        printf("连接失败!\n");
    }
}
