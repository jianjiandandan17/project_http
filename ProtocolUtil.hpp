#ifndef __PROTOCOL_UTIL_HPP__
#define __PROTOCOL_UTIL_HPP__

#include <iostream>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <sstream>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unordered_map>
#include "Log.hpp"

#define OK 200
#define BAD_REQUEST 400
#define NOT_FOUND 404
#define SERVER_ERROR 500

#define WEB_ROOT "wwwroot"
#define HOME_PAGE "index.html"
#define HTTP_VERSION "HTTP/1.0" 
#define PAGE_404 "404.html"

std::unordered_map<std::string, std::string> stuffix_map{
	{".html", "text/html"},
	{".htm", "text/html"},
	{".js", "application/x-javascript"},
	{".css", "text/css"}
};

class ProtocolUtil{
		public:
			static void MakeKV(std::unordered_map<std::string, std::string> &kv_, std::string &str_) {
				std::size_t pos = str_.find(": ");
				if(std::string::npos == pos) {
					return;
				}

				std::string k_ = str_.substr(0, pos);
				std::string v_ = str_.substr(pos + 2);
				
				kv_.insert(make_pair(k_, v_));
			}
			static std::string IntToString(int code)
			{
				std::stringstream ss;
				ss << code;
				return ss.str();
			}
			static std::string CodeToDesc(int code)
			{
				switch(code) {
					case 200:
						return "OK";
					case 400:
						return "Bad Request";
					case 404:
						return "NOT FOUND";
					case 500:
						return "Internal Server Error";
					default:
						return "UNKNOW";
				}
			}

			static std::string SuffixToType(const std::string &suffix_)
			{
				return stuffix_map[suffix_];
			}
};

class Request{
	public:
		std::string rq_line;
		std::string rq_head;
		std::string blank;
		std::string rq_text;
	private:
		std::string method;
		std::string uri;
		std::string version;

		std::string path;
		std::string param;
		
		bool cgi;				//无论GET/POST方法 带参cgi为true 不带参cgi为false
		int resource_size;
		int content_length;
		std::string resource_suffix;
		std::unordered_map<std::string, std::string> head_kv;

	public:
		Request():blank("\n"),cgi(false),path(WEB_ROOT), resource_size(0), content_length(-1), resource_suffix(".html")
		{}

		void RequestLineParse()			//利用stringstream将第一行拆分为方法（GET/POST） uri 版本号（HTTP/1.0）
		{
			std::stringstream ss(rq_line);
			ss >> method >> uri >> version;
		}

		bool IsMethodLegal()	//判断是否是GET/POST方法
		{
			if(strcasecmp(method.c_str(), "GET") == 0 || (cgi = (strcasecmp(method.c_str(), "POST") == 0))) {	//strcasecmp忽略大小写比较是否相同
				
				return true;
			}
			return false;
		}

		void UriParse()		//处理路径，如果是GET方法，看是否带参
		{
			if(strcasecmp(method.c_str(), "GET") == 0) {
				std::size_t pos_ = uri.find('?');
				if(std::string::npos != pos_) {		//找到? 并且为GET方法
					cgi = true;
					path += uri.substr(0, pos_);		//path加上uri问号前面的路径
					param = uri.substr(pos_ + 1);	//param(参数)为问号后面的	
				}
				else {								//没找到?
					path += uri;					//path加上uri路径
				}
			}
			else {
				path += uri;
			}
			if(path[path.size() - 1] == '/') {	//如果path最后一个符号为/ 则表示目录，加上默认首页
				path += HOME_PAGE;
			}
		}

		bool IsPathLegal()						//判断路径是否合法
		{
			struct stat st;						
			if(stat(path.c_str(), &st) < 0) {		//通过path.c_str获取文件信息，如小于0，则说明没找到该文件
				LOG(WARNING, "path not found!");
				return false;
			}
			if(S_ISDIR(st.st_mode)) {			//利用S_ISDIR这个宏和st_mode判断文件是否是目录，是，就加上默认首页
				path += "/";
				path += HOME_PAGE;
			}
			else {
				if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)) {	//判断路径是否具有可执行权限(拥有者，所属组，所有人)
					cgi = true;
				}
			}

			resource_size = st.st_size;			//文件大小储存到resource_size中
			std::size_t pos = path.rfind(".");
			if(std::string::npos != pos) {
				resource_suffix = path.substr(pos);
			}
			return true;
		}

		bool RequestHeadParse()					//处理请求报头，建立kv模型
		{
			int start = 0;
			while(start < rq_head.size()) {
				std::size_t pos = rq_head.find('\n', start);	//从start之后查找\n
				if(std::string::npos == pos) {
					break;
				}
				std::string sub_string_ = rq_head.substr(start, pos - start);	//将start位置到pos-start之间的字符拷贝到sub_string_中
				if(!sub_string_.empty()) {										//如果sub_string_非空
					LOG(INFO, "substr is not empty!");
					ProtocolUtil::MakeKV(head_kv, sub_string_);					//将sub_string_数据放入kv模型中
				}
				else {
					LOG(INFO, "substr is empty!");
					break;
				}
				start = pos + 1;												//下一次开始位置(start)是\n的下一个位置

			}
			return true;
		}
		
		int GetContentLength()													//获取ContentLength这个参数，该参数表明正文长度
		{
			std::string cl_ = head_kv["Content-Length"];					
			if(!cl_.empty()) {
				std::stringstream ss(cl_);
				ss >> content_length;
			}
			return content_length;
		}

		bool IsNeedRecvText()		//判断是否是POST方法
		{
			if(strcasecmp(method.c_str(), "POST") == 0){
				return true;
			}
			return false;
		}

		std::string &GetParam()		//获取参数
		{	
			return param;
		}

		int GetResourceSize()
		{
			return resource_size;
		}

		void SetResourceSize(int rs_)
		{
			resource_size = rs_;
		}

		std::string &GetSuffix()
		{
			return resource_suffix;
		}

		void SetSuffix(std::string suffix_)
		{
			resource_suffix = suffix_;
		}

		std::string &GetPath()
		{
			return path;
		}

		void SetPath(std::string &path_)
		{
			path = path_;
		}

		bool IsCgi()
		{
			return cgi;
		}

		~Request()
		{}

};

class Response{
	public:
		std::string rsp_line;
		std::string rsp_head;
		std::string blank;
		std::string rsp_text;
		int fd;
		int code;
	public:
		Response():blank("\n"), code(OK), fd(-1)
		{}

		void MakeStatusLine()		//获取状态行
		{
			rsp_line = HTTP_VERSION;		//版本
			rsp_line += " ";
			rsp_line += ProtocolUtil::IntToString(code);	//状态码
			rsp_line += " ";
			rsp_line += ProtocolUtil::CodeToDesc(code);		//状态码描述
			rsp_line += "\n";
		}
		
		void MakeResponseHead(Request *&rq_)	//建立响应报头
		{
			rsp_head = "Content-Length: ";
			rsp_head += ProtocolUtil::IntToString(rq_->GetResourceSize());	//获取响应正文长度
			rsp_head += "\n";
			rsp_head += "Content-Type: ";
			std::string suffix_ = rq_->GetSuffix();				//获取后缀
			rsp_head += ProtocolUtil::SuffixToType(suffix_);	//添加到参数中
			rsp_head += "\n";
		}

		void OpenResource(Request *&rq_) 		//以只读方式打开文件
		{
			std::string path_ = rq_->GetPath();	//获取文件路径
			fd = open(path_.c_str(), O_RDONLY);	//以只读方式打开文件
		}

		~Response()
		{
			if(fd >= 0) {
				close(fd);
			}
				
		}
};

class Connect{
	private:
		int sock;
	public:
		Connect(int sock_):sock(sock_)
		{}

		int RecvOneLine(std::string &line_)
		{
			char c = 'X';								//初始化字符c
			while(c != '\n') {
				ssize_t s = recv(sock, &c, 1, 0);		//每次接收一个字符
				if(s > 0) {								
					if(c == '\r') {						//处理\r情况
						recv(sock, &c, 1, MSG_PEEK);	//利用recv窥探技术看下一个字符是否是\n
						if(c == '\n') {
							recv(sock, &c, 1, 0);		//如果是，将\n接收，字符c会被置为\n
						}
						else {
							c = '\n';					//如果不是，字符c置为\n
						}
					}
					line_.push_back(c);					//将c插入line_中
				}
				else {
					break;								//recv接收失败，说明没有字符可以接收，跳出循环
				}
			}
			return line_.size();						//返回该行大小
		}

		void RecvRequestHead(std::string &head_)		//将参数整合成一列string字符串
		{
			head_ = "";
			std::string line_;
			while(line_ != "\n") {
				line_ = "";
				RecvOneLine(line_);
				head_ += line_;
			}
		}

		void RecvRequestText(std::string &text_, int len_, std::string &param_)		//处理POST方法，参数在请求正文中
		{
			char c_;
			int i_ = 0;
			while(i_ < len_) {
					recv(sock, &c_, 1, 0);
					text_.push_back(c_);
					i_++;
			}
			param_ = text_;
		}
		

		void SendResponse(Response *&rsp_,Request *&rq_, bool cgi_)		//发送回应
		{
				std::string &rsp_line_ = rsp_->rsp_line;
				std::string &rsp_head_ = rsp_->rsp_head;
				std::string &blank_ = rsp_->blank;

				send(sock, rsp_line_.c_str(), rsp_line_.size(), 0);		//发送
				send(sock, rsp_head_.c_str(), rsp_head_.size(), 0);
				send(sock, blank_.c_str(), blank_.size(), 0);

				if(cgi_) {
					std::string &rsp_text_ = rsp_->rsp_text;
					send(sock, rsp_text_.c_str(), rsp_text_.size(), 0);
				}else {
					int &fd = rsp_->fd;
					sendfile(sock, fd, NULL, rq_->GetResourceSize());
				}
		}

		~Connect()
		{
			if(sock >= 0) {
				close(sock);
			}
				
		}
};

class Entry{
	public:
		static void ProcessNonCgi(Connect *&conn_, Request *&rq_, Response *&rsp_)	//不是cgi情况
		{
			int &code_ = rsp_->code;
			rsp_->MakeStatusLine();
			rsp_->MakeResponseHead(rq_);
			rsp_->OpenResource(rq_);
			conn_->SendResponse(rsp_, rq_, false);	
		}

		static void ProcessCgi(Connect *&conn_, Request *&rq_, Response *&rsp_)
		{
			int &code_ = rsp_->code;
			int input[2];
			int output[2];
			std::string &param_ = rq_->GetParam();
			std::string &rsp_text_ = rsp_->rsp_text;
			
			pipe(input);
			pipe(output);

			pid_t id = fork();
			if(id < 0) {
				code_ = SERVER_ERROR;
				return;
			}
			else if(id == 0) {
				close(input[1]);
				close(output[0]);

				const std::string &path_ = rq_->GetPath();
				std::string cl_env_ = "Content-Length=";
				cl_env_ += ProtocolUtil::IntToString(param_.size());
				putenv((char*)cl_env_.c_str());

				dup2(input[0], 0);
				dup2(output[1], 1);
				execl(path_.c_str(), path_.c_str(), NULL);
				exit(1);
			}
			else {
				close(input[0]);
				close(output[1]);

				size_t size_ = param_.size();
				size_t total_ = 0;
				size_t curr_ = 0;
				const char *p_ = param_.c_str();
				while(total_ < size_ && (curr_ = write(input[1], p_ + total_, size_ - total_)) > 0) {
					total_ += curr_;
				}
				
				char c;
				while(read(output[0], &c, 1) > 0) {
					rsp_text_.push_back(c);
				}
				waitpid(id, NULL, 0);

				close(input[1]);
				close(output[0]);

				rsp_->MakeStatusLine();
				rq_->SetResourceSize(rsp_text_.size());
				rsp_->MakeResponseHead(rq_);

				conn_->SendResponse(rsp_, rq_, true);
			}
		}

		static int PorcessResponse(Connect *&conn_, Request *&rq_, Response *&rsp_)		//处理cgi和不是cgi的两种情况
		{
			if(rq_->IsCgi()) {
				ProcessCgi(conn_, rq_, rsp_);
			}
			else {
				ProcessNonCgi(conn_, rq_, rsp_);
			}
		}

		static void HandlerError(Connect *&conn_, Request *&rq_, Response *&rsp_)
		{
			int &code_ = rsp_->code;
			switch(code_) {
				case 400:
					break;
				case 404:
					Process404(conn_, rq_, rsp_);
					break;
				case 500:
					break;
				case 503:
					break;
			}
		}

		static void Process404(Connect *&conn_, Request *&rq_, Response *&rsp_)
		{
			std::string path_ = WEB_ROOT;
			path_ += "/";
			path_ += PAGE_404;
			struct stat st;
			stat(path_.c_str(), &st);

			rq_->SetResourceSize(st.st_size);
			rq_->SetSuffix(".html");
			rq_->SetPath(path_);

			ProcessNonCgi(conn_, rq_, rsp_);
		}	

		static int HandlerRequest(int sock_)
		{
			Connect *conn_ = new Connect(sock_);
			Request *rq_ = new Request();
			Response *rsp_ = new Response();

			int &code_ = rsp_->code;

			conn_->RecvOneLine(rq_->rq_line);		//接收第一行
			rq_->RequestLineParse();				//处理请求行，将第一行拆分成方法 URI 版本
			if(!rq_->IsMethodLegal() ) {			//判断方法是否合法(是否是GET/POST方法)
				conn_->RecvRequestHead(rq_->rq_head);
				code_ = BAD_REQUEST;					//不合法，将code置为NOT_FOUND
				goto end;
			}
			rq_->UriParse();						//处理路径
			if(!rq_->IsPathLegal()) {				//判断路径是否合法
				conn_->RecvRequestHead(rq_->rq_head);
				code_ = NOT_FOUND;					//不合法，将code置为NOT_FOUND
				goto end;
			}

			LOG(INFO, "request path is OK!");
			
			conn_->RecvRequestHead(rq_->rq_head);	//将所有参数整合成string字符串
			if(rq_->RequestHeadParse()) {			//处理请求报头并建立kv模型
				LOG(INFO, "parse head done");
			}
			else {
				code_ = BAD_REQUEST;
				goto end;
			}

			if(rq_->IsNeedRecvText()){				//判断是否是POST方法，是否需要在请求正文中获取参数
				conn_->RecvRequestText(rq_->rq_text, rq_->GetContentLength(), rq_->GetParam());		//在请求正文中获取参数
			}

			PorcessResponse(conn_, rq_, rsp_);

end:
			if(code_ != OK) {
				HandlerError(conn_, rq_, rsp_);
			}
			delete conn_;
			delete rq_;
			delete rsp_;
			return code_;
		}
	private:

};



#endif
