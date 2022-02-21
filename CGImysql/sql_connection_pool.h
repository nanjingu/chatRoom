#ifndef __CONNECTION_POOL_
#define __CONNECTION_POOL_

#include<list>
#include<stdio.h>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>
#include"../lock/locker.h"

using namespace std;

class connection_pool
{
public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *conn);
    int GetFreeConn();
    void DestroyPool();

    static connection_pool *GetInstence();

    void init(string url, string user, string password, string databasename, int port, int maxConn);

    connection_pool();
    ~connection_pool();

private:
    unsigned int maxConn;
    unsigned int freeConn;
    unsigned int curConn;

    locker lock;
    list<MYSQL*> connList;
    sem reserve;

    string url;
    string port;
    string user;
    string password;
    string databasename;
};

class connectionRAII{
public:
    connectionRAII(MYSQL **conn, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *connRAII;
    connection_pool *poolRAII;
};

#endif