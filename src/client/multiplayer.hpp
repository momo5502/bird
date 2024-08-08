#pragma once
#include <network/manager.hpp>

inline network::address get_master_server()
{
	return {"server.momo5502.com:20810"};
}

class multiplayer
{
public:
	multiplayer(network::address server = get_master_server())
		: server_(std::move(server))
	{
	}

private:
	network::address server_{};
	network::manager manager_{};
};
