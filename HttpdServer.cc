#include "HttpdServer.hpp"

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

	HttpdServer *serp = new HttpdServer(atoi(argv[1]));		
	serp->InitServer();				//初始化服务器
	serp->Start();					//启动服务器

	delete serp;

	return 0;
}
