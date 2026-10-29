#ifndef __LINKLIST_H__
#define __LINKLIST_H__

typedef char* Elemtype; //数据类型

//数据结点
typedef struct node
{
    Elemtype data;//数据域
    struct node *prev; //保存前驱结点地址
    struct node *next; //保存后继结点地址
}Node;

//头结点
typedef struct linklist
{
    int count; //保存数据结点的个数
    Node *first; //指向数据节点中第一个数据节点
    Node *last; //指向数据节点中最后一个数据节点
}Linklist;

/*
    create_linklist: 创建一个空链表（创建头节点）
    返回值:
        Linklist *
*/
Linklist *create_linklist();

/*
    create_node: 为数据创建一个新的数据节点
    @data: 待创建节点的数据
    返回值:
        Node *
*/
Node *create_node(Elemtype data);

/*
    tail_insert: 尾插法往双向循环链表中插入新的数据节点
    @head: 链表的头节点的地址
    @pnew: 待插入的新节点的地址
    返回值:
        void
*/
void tail_insert(Linklist *head, Node *pnew);

void print_linklist(Linklist *head);

/*
    delete_linklist: 删除双向循环链表
    @head: 链表的头结点的地址
    返回值:
        void *
*/
void *delete_linklist(Linklist *head);

/*
    save_bmp_path: 保存bmp图片路径
    返回值:
        Linklist *
*/
int save_path(Linklist *head, char *dir_path);
Linklist *save_bmp_path(void);

#endif