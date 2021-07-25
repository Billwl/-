#include <stdio.h> //tcp服务器
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h> //定义了sockaddr_in
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sqlite3.h>

#include "network_chat.h"

#define PORT 5555 //端口号不要小于1024
#define MYADDR "127.0.0.1"

int s_fd;
char buffer[1024];

//id--fd结构体类型
typedef struct
{
    int client_fd;
    char client_id[4];
} client_id_to_fd;

//将用户帐号和占用的文件描述符一一对应起来，
//方便后续一对一通信
client_id_to_fd id_to_fd[CLIENT_MAX];

//数据库的连接指针
static sqlite3 *db = NULL;

//数据库初始化工作
//连接数据库，创建表格
void DataBase_init(void)
{
    // 打开zhuce.db的数据库，如果数据库不存在，则创建并打开
    sqlite3_open_v2("zhuce.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (db == NULL)
    {
        perror("sqlite3_open_v2 失败!\n");
        exit(-1);
    }

    //在数据库中创建表格
    //ID 昵称 密码，ID为主键
    char *errmsg = NULL;
    char *sql = "CREATE TABLE if not exists id_sheet(id text primary key,name text not null,passwd text not null);";
    sqlite3_exec(db, sql, NULL, NULL, &errmsg);
}

/*
 *功能：验证客户端传来的ID信息
 *输入：服务器accept后产生的套接字
 *输出：验证状态，1登录成功，2注册成功，其他失败
 */
int check_recv_id(int fd)
{

    int ret;
    int i;
    people_t client_ID_info; //帐号信息结构体ID_info
    char recv_ID_info[17];   //客户端传来的帐号信息其登录/注册选择
    char ack_ID_info[4];
    char ack_ok[3];
    char *errmsg = NULL;
    char sql[128];

    int nrow = 0;
    int ncolumn = 0;
    char **azResult = NULL;
    char status[128];

    //接收ID_info
    memset(recv_ID_info, 0, 17);
    memset(ack_ID_info, 0, 4);
    memset(sql, 0, 128);
    memset(status, 0, 16);

    ret = recv(fd, recv_ID_info, 17, 0);
    if (-1 == ret)
    {
        perror("recv 错误！");
        return -1;
    }

    //打印接收到的信息
    for (i = 0; i < 17; i++)
    {
        if (recv_ID_info[i] == '/')
            recv_ID_info[i] = '\0';
    }
    memcpy(client_ID_info.id, recv_ID_info + 1, 4);
    memcpy(client_ID_info.name, recv_ID_info + 5, 4);
    memcpy(client_ID_info.passwd, recv_ID_info + 9, 8);

    //登录,验证输入的ID和passwd是否正确
    if (recv_ID_info[0] == '1')
    {
        sprintf(sql, "select * from hwy_id_sheet where id = '%s' and passwd = '%s';",
                client_ID_info.id, client_ID_info.passwd);
        sqlite3_get_table(db, sql, &azResult, &nrow, &ncolumn, &errmsg);
        if (nrow == 0)
        {
            //没有匹配项，登录验证失败
            strcpy(status, "登录验证失败!");
            send(fd, status, strlen(status), 0);
            return -1;
        }
        else
        {
            //登录验证成功
            memset(status, 0, 16);
            strcpy(status, "登录验证成功!");
            send(fd, status, strlen(status), 0);
            recv(fd, ack_ok, 3, 0);

            //在这里绑定client_fd---client_id
            for (i = 0; i < CLIENT_MAX; i++)
            {
                if (id_to_fd[i].client_fd == fd)
                {
                    memcpy(id_to_fd[i].client_id, client_ID_info.id, 4);
                    break;
                }
            }

            //发送用户昵称
            strcpy(ack_ID_info, azResult[4]);
            send(fd, ack_ID_info, strlen(ack_ID_info), 0);
            return 1;
        }
    }

    //注册，根据昵称和密码注册、记录帐号信息，并返回帐号
    else
    {
        int j = 100;
        char *sql1 = "select * from id_sheet;";
        sqlite3_get_table(db, sql1, &azResult, &nrow, &ncolumn, &errmsg);
        j = j + nrow;
        memset(ack_ID_info, 0, 4);
        sprintf(ack_ID_info, "%d", j); //---itoa
        memcpy(client_ID_info.id, ack_ID_info, 4);

        sprintf(sql, "insert into id_sheet values('%s','%s','%s'); ", client_ID_info.id,
                client_ID_info.name, client_ID_info.passwd);
        ret = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
        if (ret == SQLITE_OK)
        {
            printf("注册成功\n");
            memset(status, 0, 16);
            strcpy(status, "sign up");
            send(fd, status, strlen(status), 0);
            recv(fd, ack_ok, 3, 0);

            //发送用户帐号信息
            send(fd, ack_ID_info, strlen(ack_ID_info), 0);
            return 2;
        }
        else
        {
            printf("注册失败\n");
            memset(status, 0, 16);
            strcpy(status, "sign up error");
            send(fd, status, strlen(status), 0);
            return -1;
        }
    }
}
/*
 *功能：把服务器接收的信息发给所有人
 *输入：聊天信息具体内容
 *输出：无
 * */
void SendMsgToAll(char *msg)
{
  int i;
  for(i=0;i<CLIENT_MAX;i++){
    if(id_to_fd[i].client_fd != 0){
      printf("发送给%s\n",id_to_fd[i].client_id);
      printf("%s\n",msg);
      send(id_to_fd[i].client_fd,msg,strlen(msg),0);
    }
  }
}

/*
 * 功能：把服务器接收的消息发给指定的人
 * 输入：目标帐号所绑定的fd，具体聊天内容
 * 输出：无
 */
void SendMsgToSb(int destfd,char *msg)
{
  int i;
  for(i=0;i<CLIENT_MAX;i++){
    if(id_to_fd[i].client_fd == destfd ){
  		printf("发送给%s\n",id_to_fd[i].client_id);
  		printf("%s\n",msg);
  		send(destfd,msg,strlen(msg),0);
		break;
  	}
  }
}


void *read_msg(void *arg)
{
    int c_fd = *((int *)arg);
    char buffer[1024] = {0};

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        if (read(c_fd, buffer, sizeof(buffer)) == 0)
        {
            printf("%s is exit!\n", "客户端");
            //exit(1);
            pthread_exit(NULL);
        }
        printf("接收到来自客户端的数据：%s\n", buffer);
    }
}

void *write_msg(void *arg)
{
    int c_fd = *((int *)arg);
    char buffer[1024];
    while (1)
    {
        //printf("请输入你要对客户端说的话：");
        //memset(buffer, 0, sizeof(buffer));
        //scanf("%s", buffer);
        write(c_fd, buffer, sizeof(buffer));
        memset(buffer, 0, sizeof(buffer));
    }
}

//每接收一个客户端的连接，便创建一个线程
void *thread_func(void *arg)
{
    int fd = *(int *)arg;
    int ret;
    int i;
    send_msg_t C_SendMsg;
    printf("pthread = %d\n", fd);

    char recv_buffer[CHAT_STRUCT_SIZE];

    //验证登录/注册信息
    while (1)
    {
        ret = check_recv_id(fd);
        printf("check_recv_id = %d\n", ret);
        if (ret == 1)
        {
            //成功登录
            printf("登录成功\n");
            break;
        }
        else if (ret == 2)
        {
            //注册成功,需要重新登录
            continue;
        }
        else
        {
            //验证失败,服务器不退出
            continue;
        }
    }

    //登录成功，处理正常聊天的信息--接收与转发
    while (1)
    {
        memset(recv_buffer, 0, CHAT_STRUCT_SIZE);
        ret = recv(fd, recv_buffer, CHAT_STRUCT_SIZE, 0);
        if (-1 == ret)
        {
            perror("recv error!");
            return 0;
        }
        else if (ret > 0)
        {
            printf("接收到的内容为：%s\n", recv_buffer);

            if (memcmp(recv_buffer + POSITION_DESTID, "999", 3) == 0)
                SendMsgToAll(recv_buffer);
            else
            {
                for (i = 0; i < CLIENT_MAX; i++)
                {
                    if (memcmp(id_to_fd[i].client_id, recv_buffer + POSITION_DESTID, 3) == 0)
                    {
                        SendMsgToSb(id_to_fd[i].client_fd, recv_buffer);
                        break;
                    }
                }
            }
        }
    }
}
int bindport(int port) //端口绑定，创建套接字，并绑定到指定端口，返回套接字函数
{

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
    s_addr.sin_port = htons(port);              //将小端本地数据转换成大端网络数据
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
    return s_fd;
}

int main(int argc, char argv[])
{
    signal(SIGPIPE, SIG_IGN); //忽略管道信号；

    //int s_fd;
    pid_t pid, ppid;
    pthread_t r_id;
    pthread_t w_id;

    //套接字描述符
    int s_fd = bindport(PORT);

    int c_fd;                  //客户端描述符
    struct sockaddr_in c_addr; //客户端的ip端口结构体
    int c_addrlen = sizeof(struct sockaddr_in);
    memset(&c_addr, 0, c_addrlen);

    //fork
    DataBase_init(); //创建注册用户表格

    pid = fork();
    if (pid < 0)
    {
        perror("create pid error!\n");
        exit(1);
    }
    if (pid == 0) //创建子进程
    {
        ppid = fork();
        if (ppid < 0)
        {
            perror("create ppid error!\n");
            exit(1);
        }
        if (ppid == 0) //创建子子进程:遍历链表，查询密码是否正确、是否登录，返回登录信息
        {
#if 0
            while (1)
            {
                c_fd = accept(s_fd, (struct sockaddr *)&c_addr, &c_addrlen); //accept函数是阻塞的
                if (c_fd == -1)
                {
                    printf("accept");
                    exit(3);
                }
                printf("客户端:ip:%s,端口:%d\n", inet_ntoa(c_addr.sin_addr), ntohs(c_addr.sin_port));

                pthread_create(&r_id, NULL, read_msg, (void *)&c_fd);

                if (buffer == "yes")
                {
                    memset(buffer, 0, sizeof(buffer));
                    write(buffer, "1", 4);
                }
                else if (buffer == "no")
                {
                    memset(buffer, 0, sizeof(buffer));
                    write(buffer, "0", 4);
                }
                pthread_create(&w_id, NULL, write_msg, (void *)&c_fd);

                c_fd = accept(s_fd, (struct sockaddr *)&c_addr, &c_addrlen); //接收账号名
#endif
            while (1)
            {
                s_fd = accept(c_fd, (struct sockaddr *)&c_addr, &c_addrlen);
                pthread_create(&r_id, 0, thread_func, &c_fd);
            }
        }
    }

    else if (ppid > 0) //创建子父进程:接发客户端的聊天请求
    {
    }
    else if (pid > 0) //创建父进程:创建数据库保存登录注册信息，创建在线用户链表用于查询登录情况
    {

        // pthread_detach(r_id);
        // pthread_detach(w_id);
    }
    //将32位无符号整型ip地址数据转成字符串类型，将大端端口号数据转成小端

    // char buffer[1024] = {0};

    return 0;
}