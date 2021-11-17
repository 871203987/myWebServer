#include <sys/uio.h>

#include "http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int http_conn::m_epollfd=-1;
int http_conn::m_client_count=0;

map<string,string> http_conn::users;
mysql_pool* http_conn::connpool=NULL;
//-------------------------------------------------------------------------------------------------------------构造、析构函数
http_conn::http_conn(){};
http_conn::~http_conn(){};
//-------------------------------------------------------------------------------------------------------------初始化、关闭连接
void http_conn::init_coon(int sockfd, const sockaddr_in &c_addr,int connectET_enable,char* doc_root){
    m_sockfd=sockfd;
    m_addr=c_addr;
    m_connectET_enable=connectET_enable;
    m_doc_root=doc_root;

    utils::addfd(m_epollfd,m_sockfd,true,m_connectET_enable);
    m_client_count++;

    // strcpy(sql_user, user.c_str());
    // strcpy(sql_passwd, passwd.c_str());
    // strcpy(sql_name, sqlname.c_str());

    init();
};

void http_conn::init(){

    m_state=0;

    m_readIndex=0;         //缓冲区中m_read_buf中数据的最后一个字节的下一个位置,即数据大小
    m_checkIndex=0;         //m_read_buf读取的位置
    m_start_line=0;

    m_cgi=0;

    m_check_state=CHECK_STATE_REQUESTLINE;

    m_url=NULL;
    m_method=GET;
    m_version=NULL;

    m_host=NULL;
    m_linger=false;
    m_contentLength=0;

    m_postInfo=NULL;

    m_writeIndex=0;

    isdeal=0;              //请求是否被处理，被工作线程修改
    istimeout=0;

    // m_fileAddress=NULL;
    // m_fileStatus.st_size=0;

    // m_iv[0].iov_base=NULL;
    // m_iv[0].iov_len=0;
    // m_iv[1].iov_base=NULL;
    // m_iv[1].iov_len=0;
    // m_ivCount=0;

    m_bytesToSend=0;
    m_bytesHaveSend=0;

    memset(m_readBuff, '\0', READ_BUFFER_SIZE);
    memset(m_writeBuff, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_url, '\0', FILENAME_LEN);
    memset(m_contentType, '\0', 200);

    m_mysql=NULL;
}

void http_conn::close_conn(bool real_close){
    if(real_close && m_sockfd!=-1){
        utils::removefd(m_epollfd,m_sockfd);
        close(m_sockfd);
        m_sockfd=-1;
        m_client_count--;
    }
};

void http_conn::init_sql(mysql_pool *sql_pool){
    connpool= sql_pool;
    connectionRAII(&m_mysql, sql_pool);

    if (mysql_query(m_mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(m_mysql));
    }
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(m_mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}
//-------------------------------------------------------------------------------------------------------------从内核缓存区读到用户缓存区
bool http_conn::read_once(){
    if(m_readIndex>=READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;

    //LT模式读数据
    if(m_connectET_enable==0){
        bytes_read =recv (m_sockfd, m_readBuff+m_readIndex, READ_BUFFER_SIZE-m_readIndex,0);
        
        if(bytes_read<=0){
            return false;
        }
        m_readIndex+=bytes_read;
        return true;
    }
    //ET模式读数据
    else if(m_connectET_enable==1){
        while(true){
            bytes_read =recv (m_sockfd, m_readBuff+m_readIndex, READ_BUFFER_SIZE-m_readIndex,0);
            if(bytes_read==-1){
                if(errno == EAGAIN || errno == EWOULDBLOCK)break;
                return false;
            }
            else if(bytes_read==0){
                return false;
            }
            m_readIndex+=bytes_read;
        }
        
        return true;
    }
};
//-------------------------------------------------------------------------------------------------------------解析用户缓存区的请求
http_conn::HTTP_CODE http_conn::parse_requestLine(char *text){//主状态机状态函数-处理请求行

    //解析URL
    m_url=strpbrk(text, " \t"); //请求行中最先含有空格和\t任一字符的位置并返回
    if(!m_url){
        return BAD_REQUEST;
    }
    *m_url++='\0';//将该位置改为\0，用于将前面数据取出
    //m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    m_url+=strspn(m_url," \t");//将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符

    //解析方法
    char* method=text;
    if(strcasecmp(method, "GET") == 0){
        m_method=GET;
    }
    else if(strcasecmp(method, "POST") == 0){
        m_method=POST;
        m_cgi=1;
    }
    else{
        return BAD_REQUEST;
    }

    //解析版本
    m_version=strpbrk(m_url, " \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++='\0';
    m_version+=strspn(m_version," \t");
    
    if (strcasecmp(m_version,"HTTP/1.1")!=0)
    {
        return BAD_REQUEST;
    }
    
    //进一步解析URL
    //对请求资源前7个字符进行判断
    if(strncasecmp(m_url,"http://",7)==0){ //这里主要是有些报文的请求资源中会带有http://，这里需要对这种情况进行单独处理
        m_url+=7;
        m_url=strchr(m_url,'/');
    }
    if(strncasecmp(m_url,"https://",8)==0){ //这里主要是有些报文的请求资源中会带有https://，这里需要对这种情况进行单独处理
        m_url+=8;
        m_url=strchr(m_url,'/');
    }
    if(!m_url&&m_url[0]!='/'){
        return BAD_REQUEST;
    }
    if(strlen(m_url)==1){
        strcat(m_url,"login.html");
    }

    char* contentType=strrchr(m_url, '.');
    if(contentType!=NULL&&strncmp(contentType,".html",6)==0){
        strcpy(m_contentType,"text/html;charset=utf-8");
    }
    else {
        memset(m_contentType, '\0', 200);
    }

    m_check_state=CHECK_STATE_HEADER;
    return NO_REQUEST;
};
http_conn::HTTP_CODE http_conn::parse_header(char *text){//主状态机状态函数-处理头部信息
    //判断是空行还是请求头
    if(text[0]=='\0'){
        //判断是GET还是POST请求
        if(m_contentLength!=0){
            //POST需要跳转到消息体处理状态
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    //解析请求头部连接字段
    else if(strncasecmp(text,"Host:",5)==0){
        text+=5;
        text+=strspn(text," \n");
        m_host=text;
    }
    else if(strncasecmp(text,"Connection:",11)==0){
        text+=11;
        text+=strspn(text," \n");//跳过空格和\t字符
        if(strcasecmp(text,"keep-alive")==0){//如果是长连接，则将linger标志设置为true
            m_linger=true;
        }
    }
    else if(strncasecmp(text,"Content-length:",15)==0){
        text+=15;
        text+=strspn(text," \n");
        m_contentLength=atol(text);
    }
    else{
        // LOG_INFO("oop!unknow header: %s", text);//--------------------------------
    }

    return NO_REQUEST;
};
http_conn::HTTP_CODE http_conn::parse_content(char *text){//主状态机状态函数-处理数据内容,POST
    //判断buffer中是否读取了消息体
    if(m_readIndex>=m_checkIndex+m_contentLength){
        text[m_contentLength]='\0';
        m_postInfo=text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
};

http_conn::LINE_STATE http_conn::parse_line(){//从状态机状态函数-处理一行
    char tem;
    for(;m_checkIndex<m_readIndex;++m_checkIndex){
        tem = m_readBuff[m_checkIndex];
        if(tem=='\r'){
            if((m_checkIndex+1)==m_readIndex){
                return LINE_OPEN;
            }
            if(m_readBuff[m_checkIndex+1]=='\n'){


                m_readBuff[m_checkIndex++]='\0';
                m_readBuff[m_checkIndex++]='\0';

                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(tem=='\n'){
            if(m_checkIndex>1&&m_readBuff[m_checkIndex-1]=='\r'){
                m_readBuff[m_checkIndex-1]='\0';
                m_readBuff[m_checkIndex++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
};

http_conn::HTTP_CODE http_conn::process_read(){     //主状态机嵌套从状态机

    m_line_state=LINE_OK;
    HTTP_CODE ret = NO_RESOURCE;
    char *text=0;
    while((m_check_state==CHECK_STATE_CONTENT&&m_line_state==LINE_OK)||parse_line()==LINE_OK){
        
        text= m_readBuff + m_start_line;               //此时从状态机已提前将一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
        m_start_line=m_checkIndex;                     //m_checkIndex经过parse_line()已经增加到下一行要读的位置
        LOG_INFO("%s", text);

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:{
            ret = parse_requestLine(text);            //包含状态转移
            if(ret==BAD_REQUEST){
                return BAD_REQUEST;
            }
            break;
        }   
        case CHECK_STATE_HEADER:{
            ret = parse_header(text);

            if(ret==BAD_REQUEST){
                return BAD_REQUEST;
            }
            else if(ret==GET_REQUEST){
               return do_request();//完整解析GET请求后，跳转到报文响应函数
            }
            break;
        }
        case CHECK_STATE_CONTENT:{ 

            ret = parse_content(text);

            if (ret == GET_REQUEST){//完整解析POST请求后，跳转到报文响应函数
                return do_request();
            }

            m_line_state = LINE_OPEN;//解析完消息体即完成报文解析，避免再次进入循环，更新m_line_state---------------

            break;
        }
        default:
            return INTERNAL_ERROR;
        }

    }
    return NO_REQUEST;

};
//-------------------------------------------------------------------------------------------------------------处理请求，主要是处理URL
http_conn::HTTP_CODE http_conn::do_request(){
    if(m_cgi==1&&(strncmp(m_url, "/logining",9)==0||strncmp(m_url, "/registering",12)==0)&&m_contentLength!=0){//登录或者注册
        //提取密码
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_postInfo[i] != '&'; ++i)
            name[i - 5] = m_postInfo[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_postInfo[i] != '\0'; ++i, ++j)
            password[j] = m_postInfo[i];
        password[j] = '\0';

        if(strncmp(m_url, "/logining",9)==0){//如果是登录
            if (users.find(name) != users.end() && users[name] == password){
                strcpy(m_url, "/home.html");

            }
        
            else
                strcpy(m_url, "/loginError.html");
        }
        else if(strncmp(m_url, "/registering",12)==0){//如果是注册
            //先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            if(users.find(name) == users.end()){
                //数据库插入语句
                char *sql_insert = (char *)malloc(sizeof(char) * 200);
                strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
                strcat(sql_insert, "'");
                strcat(sql_insert, name);
                strcat(sql_insert, "', '");
                strcat(sql_insert, password);
                strcat(sql_insert, "')");
                
                connectionRAII(&m_mysql, connpool);

                m_locker.lock();
                int res = mysql_query(m_mysql, sql_insert);//插入数据库
                if(!res)users.insert(pair<string, string>(name, password));//插入本地map
                m_locker.unlock();
                if (!res)
                    strcpy(m_url, "/registerSuccess.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else{

                strcpy(m_url, "/registerError.html");
            }
        }

    }
    //处理正常的资源请求
    strcpy(m_real_url, m_doc_root);
    strncpy(m_real_url+strlen(m_doc_root), m_url, strlen(m_url));
    //通过stat获取请求资源文件信息，成功则将信息更新到m_fileStatus结构体
    if(stat(m_real_url,&m_fileStatus)<0){//失败返回NO_RESOURCE状态，表示资源不存在
        return NO_RESOURCE;
    }
    if(!(m_fileStatus.st_mode & S_IROTH)){//判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_fileStatus.st_mode)){//判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
        return BAD_REQUEST;
    }
    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_url, O_RDONLY);
    m_fileAddress = (char *)mmap(0, m_fileStatus.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
    
}
//-------------------------------------------------------------------------------------------------------------生成响应并写入用户写缓存区（根据url）
bool http_conn::add_line(const char *format, ...){ //往写buf里添加行，与parse_line对应
    if(m_writeIndex>=WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;//定义可变参数列表
    va_start(arg_list,format);//将变量arg_list初始化为传入参数
    int len=vsnprintf(m_writeBuff+ m_writeIndex, WRITE_BUFFER_SIZE - 1 - m_writeIndex, format, arg_list);//该函数在#include <stdio.h>中
    if(len>=WRITE_BUFFER_SIZE - 1 - m_writeIndex){//如果写入的数据长度超过缓冲区剩余空间，则报错
        va_end(arg_list);
        return false;
    }
    m_writeIndex += len;//更新m_write_idx位置
    va_end(arg_list);//清空可变参列表

    return true;
}

bool http_conn::add_stausLine(int status, const char *title){
    return add_line("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_linger(){
    return add_line("Connection:%s\r\n",(m_linger==true)?"keep-alive":"close");
}
bool http_conn::add_contentType(){
    return add_line("Content-Type:%s\r\n", m_contentType);
}
bool http_conn::add_contentLength(int contentLength){
    return add_line("Content-Length:%d\r\n", contentLength);
}
bool http_conn::add_blankLine(){
    return add_line("%s", "\r\n");
}
bool http_conn::add_header(int contentLength){
    return add_linger()&&add_contentType()&&add_contentLength( contentLength)&&add_blankLine();
}

bool http_conn::add_content(const char *content){
    return add_line("%s", content);
}

bool http_conn::process_write(HTTP_CODE read_res){
    switch (read_res)
    {
    case INTERNAL_ERROR:{//内部错误，500
        add_stausLine(500, error_500_title);
        add_header(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:{//报文语法有误，400
        add_stausLine(400, error_400_title);
        add_header(strlen(error_400_form));
        if (!add_content(error_400_form))
            return false;
        break;
    }
    case NO_RESOURCE:{//没找到该资源，404
        add_stausLine(404, error_404_title);
        add_header(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:{ //资源没有访问权限，403
        add_stausLine(403, error_403_title);
        add_header(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:{//文件存在，200
        add_stausLine(200,ok_200_title);
        if(m_fileStatus.st_size!=0){//如果请求的资源存在
            add_header(m_fileStatus.st_size);
            m_iv[0].iov_base=m_writeBuff;//第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_len=m_writeIndex;//m_writeIndex随着add函数改变了位置
            m_iv[1].iov_base=m_fileAddress;//第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_len=m_fileStatus.st_size;//m_writeIndex随着add函数改变了位置
            m_ivCount = 2;
            m_bytesToSend = m_writeIndex + m_fileStatus.st_size;//发送的全部数据为响应报文头部信息和文件大小

            return true;
        }
        else{
            const char* ok_file = "<html><body></body></html>";
            add_header(strlen(ok_file));
            if(!add_content(ok_file)){
                return false;
            }
        }
        
    }
    default:
        return false;
    }
    m_iv[0].iov_base=m_writeBuff;
    m_iv[0].iov_len=m_writeIndex;//m_writeIndex随着add函数改变了位置
    m_ivCount = 1;
    m_bytesToSend = m_writeIndex;

    return true;
};
//-------------------------------------------------------------------------------------------------------------从用户缓存区写入内核缓存区
void http_conn::unmap()//取消文件与内存块的映射
{
    if (m_fileAddress)
    {
        munmap(m_fileAddress, m_fileStatus.st_size);
        m_fileAddress = 0;
    }
}

bool http_conn::write(){
    int tem=0;
    int newadd=0;

    if(m_bytesToSend==0){//若要发送的数据长度为0,表示响应报文为空，一般不会出现这种情况
        utils::modifyfd(m_epollfd,m_sockfd,EPOLLIN,m_connectET_enable);
        init();
        return false;
    }

    while(true){
        tem=writev(m_sockfd,m_iv,m_ivCount);//将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        if(tem<0){
            if(errno==EAGAIN){

                utils::modifyfd(m_epollfd,m_sockfd,EPOLLOUT,m_connectET_enable);
                return true;
            }
            unmap();
            return false;
        }
        m_bytesHaveSend+=tem;
        m_bytesToSend-=tem;
        if (m_bytesHaveSend >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_fileAddress + (m_bytesHaveSend - m_writeIndex);
            m_iv[1].iov_len = m_bytesToSend;
        }
        else
        {
            m_iv[0].iov_base = m_writeBuff + m_bytesHaveSend;
            m_iv[0].iov_len = m_iv[0].iov_len - m_bytesHaveSend;
        }
        if (m_bytesToSend <= 0)
        {
            unmap();
            utils::modifyfd(m_epollfd, m_sockfd, EPOLLIN, m_connectET_enable);//在epoll树上重置EPOLLONESHOT事件
            
            if (m_linger)//浏览器的请求为长连接
            {
                init();//重新初始化HTTP对象
                return true;
            }
            else//短连接则关闭连接
            {
                return false;
            }
        }
              
    }

}

//-------------------------------------------------------------------------------------------------------------综合处理过程
void http_conn::process(){
    HTTP_CODE read_res = process_read();
    if(read_res==NO_REQUEST){
        utils::modifyfd(m_epollfd, m_sockfd,EPOLLIN,m_connectET_enable);
        return;
    }
    bool write_res = process_write(read_res);

    if(!write_res){
        close_conn();
    }

    utils::modifyfd(m_epollfd, m_sockfd,EPOLLOUT,m_connectET_enable);
};

sockaddr_in* http_conn::get_address()
    {
        return &m_addr;
    }

int http_conn::get_sockfd()
    {
        return m_sockfd;
    }