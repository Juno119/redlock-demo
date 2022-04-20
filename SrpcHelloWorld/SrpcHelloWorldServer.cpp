// SrpcHelloWorld.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <signal.h>
#include "glog/logging.h"
#include "gflags/gflags.h"
#include "proto/helloworld.srpc.h"
#include "workflow/WFFacilities.h"
#include "sw/redis++/redis++.h"
#include "redlock/redlock.h"

using namespace srpc;

#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib, "ws2_32.lib")

DEFINE_string(rpc_host, "0.0.0.0", "rpc_host");
DEFINE_int32(rpc_port, 1412, "rpc_port");
DEFINE_string(http_host, "0.0.0.0", "http_host");
DEFINE_int32(http_port, 8811, "http_port");

DEFINE_string(redis_host, "124.223.10.33", "redis_host");
DEFINE_int32(redis_port, 6379, "redis_port");

static WFFacilities::WaitGroup wait_group(1);
static std::shared_ptr<sw::redis::Redis> pRedis = nullptr;

void sig_handler(int signo)
{
	wait_group.done();
}

class GreeterServiceImpl : public helloworld::Greeter::Service
{
public:

	void SayHello(helloworld::HelloRequest* request, helloworld::HelloReply* response, srpc::RPCContext* ctx) override
	{
		response->set_request_id(request->request_id());
		response->set_message("Hi, " + request->name());
		LOG(INFO) << " get_req:" << request->DebugString() << "set_resp:" << response->DebugString();

		// redis process
		if (!pRedis) {
			return;
		}

		const static std::string key = "ticket";
		const static std::string lockkey = "lock";
		sw::redis::RedLockMutex mtx(*pRedis , lockkey);

		// Not locked.
		sw::redis::RedLock<sw::redis::RedLockMutex> lock(mtx, std::defer_lock);

		// Try to get the lock, and keep 30 seconds.
		// It returns the validity time of the lock, i.e. the lock is only
		// valid in *validity_time*, after that the lock might be acquired by others.
		// If failed to acquire the lock, throw an exception of Error type.
		auto validity_time = lock.try_lock(std::chrono::seconds(30));

		if (!validity_time) {
			LOG(INFO) << "get lock failed validity_time=" << validity_time;
			return;
		}

		// Extend the lock before the lock expired.
		//validity_time = lock.extend_lock(std::chrono::seconds(10));

		sw::redis::OptionalString ticket = pRedis->get(key);
		if (ticket) {
			LOG(INFO) << "get ticket=" << *ticket;
			if (std::stoi(*ticket)  > 0) {
				auto value = pRedis->decr(key);
				if (value == std::stoi(*ticket)) {
					LOG(ERROR) << "decr error value="<< value;
				}
			}
			else {
				LOG(INFO) << "ticket sold out";
			}
		}
		LOG(INFO) << "unlock";
		// You can unlock explicitly.
		lock.unlock();
	}
};

static void InitGlog(char** argv) {
	google::InitGoogleLogging(argv[0]);
	google::SetLogDestination(google::GLOG_INFO, ("log/" + std::string(argv[0]) + "_").c_str());
	google::SetStderrLogging(google::GLOG_INFO);
	google::SetLogFilenameExtension(".log");
	FLAGS_colorlogtostderr = true;
	FLAGS_logbufsecs = 0;
	FLAGS_max_log_size = 10;
	FLAGS_stop_logging_if_full_disk = true;
}

static void DestroyGlog() {
	google::ShutDownCommandLineFlags();
}

static void InitRedis(const std::string& redisHost = "127.0.0.1",
	int redisPort = 6379, const std::string& auth = "", int db = 1) {
	try {
		sw::redis::ConnectionOptions connection_options;
		connection_options.host = redisHost;// "124.223.10.33";  // Required.
		connection_options.port = redisPort;// 6379; // Optional. The default port is 6379.
		connection_options.password = auth;   // Optional. No password by default.
		connection_options.db = db;  // Optional. Use the 0th database by default.

		// Optional. Timeout before we successfully send request to or receive response from redis.
		// By default, the timeout is 0ms, i.e. never timeout and block until we send or receive successfuly.
		// NOTE: if any command is timed out, we throw a TimeoutError exception.
		//connection_options.socket_timeout = std::chrono::milliseconds(200);

		sw::redis::ConnectionPoolOptions pool_options;
		pool_options.size = 5;  // Pool size, i.e. max number of connections.

		// Optional. Max time to wait for a connection. 0ms by default, which means wait forever.
		// Say, the pool size is 3, while 4 threds try to fetch the connection, one of them will be blocked.
		//pool_options.wait_timeout = std::chrono::milliseconds(100);

		// Optional. Max lifetime of a connection. 0ms by default, which means never expire the connection.
		// If the connection has been created for a long time, i.e. more than `connection_lifetime`,
		// it will be expired and reconnected.
		//pool_options.connection_lifetime = std::chrono::minutes(10);

		// Connect to Redis server with a connection pool.
		pRedis = std::make_shared<sw::redis::Redis>(connection_options, pool_options);
		sw::redis::OptionalString ticket = pRedis->get("ticket");
		if (ticket) {
			LOG(INFO) << "init ticket=" << *ticket;
		}
	}
	catch (const sw::redis::Error& e) {
		LOG(ERROR) << "init redis failed: " << e.what() << std::endl;
	}
}

int main(int argc, char** argv)
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	google::ParseCommandLineFlags(&argc, &argv, true);
	InitGlog(argv);
	InitRedis(FLAGS_redis_host, FLAGS_redis_port);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	SRPCServer server_tcp;
	SRPCHttpServer server_http;

	GreeterServiceImpl imp;
	server_tcp.add_service(&imp);
	server_http.add_service(&imp);

	server_tcp.start(FLAGS_rpc_host.c_str(), FLAGS_rpc_port);
	server_http.start(FLAGS_http_host.c_str(), FLAGS_http_port);
	LOG(INFO) << "start rpc server on " << FLAGS_rpc_host << ":" << FLAGS_rpc_port;
	LOG(INFO) << "start http server on " << FLAGS_http_host << ":" << FLAGS_http_port;
	LOG(INFO) << "connect to redis server on " << FLAGS_redis_host << ":" << FLAGS_redis_port;
	wait_group.wait();
	server_http.stop();
	server_tcp.stop();
	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
