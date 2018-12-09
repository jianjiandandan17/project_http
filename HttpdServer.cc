#include "HttpdServer.hpp"
#include <signal.h>
#include <unistd.h>

static void *Usage(std::string proc_)
{
	std::cout << "Usage " << proc_ << " port" << std::endl;
}

int main(int argc, char *argv[])
{
	if(argc != 2) {
		Usage(argv[0]);			//打印输入格式
		exit(1);
	}	

	signal(SIGPIPE, SIG_IGN);
	HttpdServer *serp = new HttpdServer(atoi(argv[1]));		
	serp->InitServer();				//初始化服务器
	//daemon(1, 0);					//将进程变成守护进程
	serp->Start();					//启动服务器

	delete serp;

	return 0;
}
