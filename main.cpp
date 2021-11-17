#include "config/config.h"
#include "webSvr.h"



int main(int argc, char *argv[]){
    //需要修改的数据库信息,登录名,密码,库名
    string user = "111";
    string passwd = "111";
    string databasename = "111";

    config config;
    config.parse_arg(argc, argv);

    webSvr server;

    server.init(config.PORT,config.TRIGMode, config.actor_model,user,passwd,databasename, config.sql_num, config.close_log ,config.LOGWrite, config.log_dirname, config.thread_num);
    server.log_write();
    server.init_thread_pool();
    server.sql_pool();
    server.trigmode_ET();
    server.eventListen();
    server.eventLoop();

    return 0;
}