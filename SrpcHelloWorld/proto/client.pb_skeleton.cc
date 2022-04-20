
#include "helloworld.srpc.h"
#include "workflow/WFFacilities.h"

using namespace srpc;

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	wait_group.done();
}

static void sayhello_done(::helloworld::HelloReply *response, srpc::RPCContext *context)
{
}

int main()
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	const char *ip = "127.0.0.1";
	unsigned short port = 1412;

	::helloworld::Greeter::SRPCClient client(ip, port);

	// example for RPC method call
	::helloworld::HelloRequest sayhello_req;
	//sayhello_req.set_message("Hello, srpc!");
	client.SayHello(&sayhello_req, sayhello_done);

	wait_group.wait();
	google::protobuf::ShutdownProtobufLibrary();
	return 0;
}
