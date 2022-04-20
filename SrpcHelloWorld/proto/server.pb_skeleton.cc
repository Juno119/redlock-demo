
#include "helloworld.srpc.h"
#include "workflow/WFFacilities.h"

using namespace srpc;

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	wait_group.done();
}

class GreeterServiceImpl : public ::helloworld::Greeter::Service
{
public:

	void SayHello(::helloworld::HelloRequest *request, ::helloworld::HelloReply *response, srpc::RPCContext *ctx) override
	{}
};

int main()
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	unsigned short port = 1412;
	SRPCServer server;

	GreeterServiceImpl greeter_impl;
	server.add_service(&greeter_impl);

	server.start(port);
	wait_group.wait();
	server.stop();
	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}
