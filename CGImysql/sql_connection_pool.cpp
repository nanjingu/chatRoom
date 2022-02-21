#include<mysql/mysql.h>
#include<stdio.h>
#include<string.h>
#include<string>
#include<stdlib.h>
#include<list>
#include<pthread.h>
#include<iostream>
#include"sql_connection_pool.h"

using namespace std;

MYSQL* connection_pool::GetConnection()
{
    MYSQL *con = nullptr;
    if(connList.size() == 0)
        return nullptr;
    reserve.wait();
    lock.lock();
    con = connList.front();
    connList.pop_front();
    --freeConn;
    ++curConn;
    lock.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL *conn)
{
    if(conn == nullptr)
        return false;
    lock.lock();
    connList.push_back(conn);
    ++freeConn;
    --curConn;
    lock.unlock();
    reserve.post();
    return true;
}

int connection_pool::GetFreeConn()
{
    return this->freeConn;
}

void connection_pool::DestroyPool()
{
    lock.lock();
    if(connList.size() > 0)
    {
        list<MYSQL*>::iterator it;
        for(it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            mysql_close(con);
        }
        freeConn = 0;
        curConn = 0;
        connList.clear();
        lock.unlock();
    }
    lock.unlock();
}

connection_pool *connection_pool::GetInstence()
{
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(string url, string user, string password, string databasename, int port, int maxConn)
{
    this->url = url;
    this->user = user;
    this->password = password;
    this->databasename = databasename;
    this->port = port;

    lock.lock();
    for(int i = 0; i < maxConn; ++i)
    {
        MYSQL *con = nullptr;
        con = mysql_init(con);
        if(con == nullptr)
        {
            cout<<"Error: "<<mysql_error(con);
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(), databasename.c_str(), port, nullptr, 0);
        if(con == nullptr)
        {
            cout<<"Error: "<<mysql_error(con);
            exit(1);
        }
        connList.push_back(con);
        ++freeConn;
    }
    reserve = sem(freeConn);
    this->maxConn = freeConn;
    lock.unlock();
}

connection_pool::connection_pool()
{
    this->freeConn = 0;
    this->curConn = 0;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **conn, connection_pool *connPool)
{
    *conn = connPool->GetConnection();
    connRAII = *conn;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(connRAII);
}