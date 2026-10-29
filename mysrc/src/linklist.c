#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include "linklist.h"

/*
    create_linklist: 创建一个空链表（创建头节点）
    返回值:
        Linklist *
*/
Linklist *create_linklist()
{
    Linklist *head = (Linklist *)malloc(sizeof(Linklist));
    head->count = 0;
    head->first = head->last = NULL;

    return head;
}

/*
    create_node: 为数据创建一个新的数据节点
    @data: 待创建节点的数据
    返回值:
        Node *
*/
Node *create_node(Elemtype data)
{
    Node * pnew = (Node *)malloc(sizeof(Node));
    pnew->data = (Elemtype)malloc(strlen(data) + 1); //为\0预留空间
    memset(pnew->data, 0, strlen(data) + 1); //清0
    strcpy(pnew->data , data);
    pnew->prev = NULL;
    pnew->next = NULL;

    return pnew;
}

/*
    tail_insert: 尾插法往双向循环链表中插入新的数据节点
    @head: 链表的头节点的地址
    @pnew: 待插入的新节点的地址
    返回值:
        void
*/
void tail_insert(Linklist *head, Node *pnew)
{
    if(head == NULL || pnew == NULL)
    {
        return ;
    }

    if(head->first == NULL) //pnew是第一个插入到链表的数据节点
    {
        head->first = pnew;
        head->last = pnew;
    }
    else //pnew是第二个及往后的数据节点
    {
        head->last->next = pnew;
        pnew->prev = head->last;
        head->last = pnew;
    }
    head->first->prev = head->last;
    head->last->next = head->first;
    head->count++;
}

void print_linklist(Linklist *head)
{
    if(head == NULL || head->first == NULL)
    {
        return ;
    }

    Node *p = head->first;
    while(p)
    {
        printf("%s\n", p->data);
        p = p->next;
        if(p == head->first) //已经完整遍历一遍
        {
            break;
        }
    }
}

/*
    delete_linklist: 删除双向循环链表
    @head: 链表的头结点的地址
    返回值:
        void *
*/
void *delete_linklist(Linklist *head)
{
    if(head == NULL)
    {
        return NULL;
    }

    //断开环，删除双向链表一样进行删除(头删)
    if(head->first != NULL)
    {
        head->first->prev = NULL;
        head->last->next = NULL;
    }
    Node *pdel = head->first;
    while(head->first)
    {
        pdel = head->first; //pdel指针指向待删除节点
        head->first = head->first->next; //原来的第二个数据节点称为新的第一个
        pdel->next = NULL;
        if(head->first)
        {
            head->first->prev = NULL;
        }
        free(pdel->data);
        free(pdel);
    }
    head->first = head->last = NULL;
    head->count = 0;
    free(head);
    return NULL;
}

/*
    save_bmp_path: 保存bmp图片路径
    返回值:
        Linklist *
*/
Linklist *save_bmp_path(void)
{
    //创建一张空链表
    Linklist *head = create_linklist();

    printf("请输入要保存的BMP图片的路径，结束输入按下#\n");
    //不断从键盘获取数据，数据为其创建数节点并插入链表中
    while(1)
    {
        char data[52] = {0};
        scanf("%s", data);
        getchar(); //吸收换行符
        if(data[0] == '#')
        {
            break;
        }

        Node *pnew = create_node(data);

        tail_insert(head, pnew);
    }

    return head;
}

int save_path(Linklist *head, char *dir_path)
{
	if(head == NULL || dir_path == NULL)
    {
        return -1;
    }

    //1.打开dir_path目录
    DIR *drip = opendir(dir_path);
    if(NULL == drip)
    {
        perror("opendir fail");
        return -1;
    }
    
    //2.循环不断readdir读取目录项
    while(1)
    {
        struct dirent *dt = readdir(drip);
        if(dt == NULL) //读完了 or 出错了
        {
            break;
        }
        
        //保存文件属性信息
        struct stat st; 
        char new_path[512] = {0};
        sprintf(new_path, "%s/%s", dir_path, dt->d_name);
        if(-1 == lstat(new_path, &st))
        {
            perror("lstat fail");
            return -1;
        }

        if(S_ISREG(st.st_mode) && (strncmp(dt->d_name + strlen(dt->d_name) - 4, ".mp4", 4) == 0 || strncmp(dt->d_name + strlen(dt->d_name) - 4, ".avi", 4) == 0))//文件为一个普通文件且以.bmp结尾
        {
            //5.创建一个数据节点并插入链表
            Node *pnew = create_node(new_path);
            tail_insert(head, pnew);
        }  
        else if(S_ISDIR(st.st_mode) && strcmp(dt->d_name, ".") != 0 && strcmp(dt->d_name, "..") != 0) //是目录文件且不是 . 或 ..
        {
            save_path(head, new_path);
        }
    }
    
    //6.关闭目录
    closedir(drip);
    return 0;
}