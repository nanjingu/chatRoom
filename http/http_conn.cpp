#include "http_conn.h"
#include <mysql/mysql.h>

 const char* ok_200_title = "OK";
 const char* error_400_title = "Bad Request";
 const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
 const char* error_403_title = "Forbidden";
 const char* error_403_form = "You do not have permission to get file from this server.\n";
 const char* error_404_title = "Not Found";
 const char* error_404_form = "The requested file was not found on this server.\n";
 const char* error_500_title = "Internal Error";
 const char* error_500_form = "There was an unusual problem serving the requested file.\n";
 const char* doc_root = "/home/user/programe/myWebServer/html";

 locker m_lock;
 map<string, string> users;

 void http_conn::initmysql_result(connection_pool *connPool)
 {
     MYSQL *mysql = nullptr;
     connectionRAII mysqlcon(&mysql, connPool);
     if(mysql_query(mysql, "SELECT username, passwd FROM user"))
     {
         cout<<"SELECT ERROR : "<< mysql_error(mysql)<<endl;
     }
     MYSQL_RES *result = mysql_store_result(mysql);
     MYSQL_FIELD *field = mysql_fetch_fields(result);
     while(MYSQL_ROW row = mysql_fetch_row(result))
     {
         string tmp1(row[0]);
         string tmp2(row[1]);
         users[tmp1] = tmp2;cout<<row[0]<<' '<<row[1]<<endl;
     }
 }

 int setnonblocking(int fd)
 {
     int old_option = fcntl(fd, F_GETFL);
     int new_option = old_option | O_NONBLOCK;
     fcntl(fd, F_SETFL, new_option);
     return old_option;
 }

 void addfd(int epollfd, int fd, bool oneshot)
 {
     epoll_event event;
     event.data.fd = fd;
     event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
     if(oneshot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
 }

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    mysql = nullptr;
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

void http_conn::init()
{
    verifyOk = false;
    cookie = "";
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_check_idx < m_read_idx; ++m_check_idx)
    {
        temp = m_read_buf[m_check_idx];
        if(temp == '\r')
        {
            if((m_check_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if(m_read_buf[m_check_idx + 1] == '\n')
            {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n')
        {
            if(m_check_idx > 1 && m_read_buf[m_check_idx - 1] == '\r')
            {
                m_read_buf[m_check_idx - 1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int byte_read = 0;
    while(true)
    {
        byte_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(byte_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if(byte_read == 0)
            return false;
        m_read_idx += byte_read;
    }
    printf("\n%s", m_read_buf);
    return true;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    //printf("%s\n", text);
    m_url = strpbrk(text, " \t");
    //printf("m_url:%s\n", m_url);
    //printf("text:%s\n", text);
    if(!m_url)
        return BAD_REQUEST;
    *m_url++ = '\0';
    char* method = text;
    if(strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if(strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    if(strlen(m_url) == 1)
        strcat(m_url, "hello.html");
    m_check_state = CHECK_STATE_HEADER;
    //printf("%s\n", m_url);
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    if(text[0] == '\0')
    {
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } 
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Cookie:", 7) == 0)
    {
        text += 7;
        text += strspn(text, " \t");
        verifyOk = true;
        cookie = text;
    }
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        //printf("unknow header %s\n", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_check_idx))
    {
        text[m_content_length] = '\0';
        m_string = text;
        //printf("parse content m_string %s\n", m_string);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_check_idx;
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if(ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if(ret == GET_REQUEST)
            {
                return do_request();
            }                
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if(ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }        
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn:: HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char* p = strrchr(m_url, '/');
    //printf("second m_url:%s\n", m_url);
    if(cgi == 1 && (*(p + 1) == '3' || *(p + 1) == '4'))//log or register
    {
        char flag = m_url[1];
        //char* m_url_real = (char*)malloc(sizeof(char) * 200);
        //strcpy(m_url_real, "/");
        //strcat(m_url_real, m_url + 2);
        //strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        //free(m_url_real);
        char name[100], password[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        if(*(p + 1) == '3')
        {
            char *sql_insert = (char*)malloc(sizeof(char)*200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            if(users.find(name) == users.end())
            {
                m_name = name;//cout<<m_name<<" " << name<<endl;
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();
                if(!res) strcpy(m_url, "/log.html");
                else strcpy(m_url, "/registerError.html");
            }
            else strcpy(m_url, "/registerError.html");
            //strcpy(m_url, "/log.html"); 
        }
        else if(*(p + 1) == '4')
        {
            if(users.find(name) != users.end() && users[name] == password)
            {
                m_name = name;
                strcpy(m_url, "/welcome.html");
                verifyOk = true;cout<<verifyOk<<" hhhhhhhh" <<endl;
            }
            else
                strcpy(m_url, "/logError.html");
            //strcpy(m_url, "/welcome.html");
        }
    }
    if(cgi == 1 && (*(p + 1) == '6'))//
    {
        strcpy(m_url, "/user_txt.html");
        time_t cur = time(NULL);
        char say[100];
        int i;
        for(i = 4; m_string[i] != '\0'; ++i)
            say[i - 4] = m_string[i];
        say[i - 4] = '\0';
        m_lock.lock();
        fstream file1("./html/user_txt.txt", fstream::out | fstream::app);
        if (!file1) {
            cout << "file1 open error! " << endl;
        }
        else{
            //cout<<"cout file : "<<m_name<<endl;
            file1 << m_name<<" : " <<cur<<" : "<<endl;
                file1 << "  " << say<<endl;
            file1.close();
        }
        m_lock.unlock();
    }
    //printf("url %s\n", m_url);
    if(*(p + 1) == 'm' || *(p + 1) == '7')
    {
        char* m_url_real = (char*)malloc(sizeof(char)*200);
        if(!verifyOk)strcpy(m_url_real, "/log.html");
        else strcpy(m_url_real, "/user_txt.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } 
    else if(*(p + 1) == 't')
    {
        char* m_url_real = (char*)malloc(sizeof(char)*200);
        if(!verifyOk)strcpy(m_url_real, "/log.html");
        else strcpy(m_url_real, "/word.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);cout<<verifyOk<<" hhhhhhhh" <<endl;
    }
    else if(*(p + 1) == 'p')
    {
        char* m_url_real = (char*)malloc(sizeof(char)*200);
        if(!verifyOk)strcpy(m_url_real, "/log.html");
        else strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } 
    else if(*(p + 1) == 'v')
    {
        char* m_url_real = (char*)malloc(sizeof(char)*200);
        if(!verifyOk)strcpy(m_url_real, "/log.html");
        else strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '0')
    {
        char* m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '1')
    {
        char* m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else
    {
        //printf("0: m_real_file %s\n", m_real_file);
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
        //printf("1: m_real_file %s\n", m_real_file);
    }
    if(stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    } 
         
    if(!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    //cout<<"m_real_file = " << m_real_file << endl;
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0, i = 0;
    if(bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while(1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);//cout<<i++<<" now write file length is : "<< temp<<endl;
        if(temp < 0)
        {
            if(errno == EAGAIN)
            { 
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if(m_linger)
            {
                init();
                return true;
            }
            else
                return false;
        }
    }
}

bool http_conn::add_response(const char* format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
        return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    if(verifyOk) add_cookie();
    add_blank_line();
}

bool http_conn:: add_cookie()
{
    return add_response("Set-Cookie:cookie=ok\r\n");
}
bool http_conn:: add_content_length(int content_len)
{
    return add_response("Content-length:%d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    { 
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if(m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;//cout<<m_write_buf<<endl;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;//cout<<m_file_address<<endl;
            m_iv[1].iov_len = m_file_stat.st_size;//cout<<"file length : " << m_file_stat.st_size<<endl;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char* ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if(!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret)
        close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
