#include <iostream>
#include <list>
#include <SFML/Network.hpp>


struct TcpSocketClient
{
	sf::TcpSocket* socket;
	float heartbeatTimer = 0.f;
};

int main()
{
	bool isPlaying = true;
	bool isServer = true;
	bool isConnected = false;
	float heartbeatTimer = 0.f;

	std::string messageLog = "";
	std::string messageSent = "";
	std::string username = "";

	sf::IpAddress serverIp = "";

	int limitUsers = 0;
	unsigned const short port = 54123;
	std::list<TcpSocketClient*> sockets;
	sockets.clear();

	sf::TcpSocket *socket = new sf::TcpSocket;
	sf::TcpListener listener;
	sf::SocketSelector selector;
	
	sf::Mutex mutex;
	sf::Thread *threadSend = nullptr, *threadReceive = nullptr, *threadStandBy = nullptr, *threadHeartbeat = nullptr;

	auto log = [&](std::string value)
	{
		if (value.size() > 0)
		{
			if (value[0] == '!')
				value.erase(0, 1);
			system("cls");
			messageLog += value + "\n";
			std::cout << messageLog;
		}
	};

	auto sendMessageQuick = [&](std::string text)
	{
		std::string messageValue = "!" + username + text;
		sf::Packet packetSend;
		packetSend << messageValue;

		if (isServer)
		{
			for (auto& socketIndex : sockets)
				socketIndex->socket->send(packetSend);
		}
		else
			socket->send(packetSend);
	};

	auto sendMessageRaw = [&](std::string text)
	{
		std::string messageValue = text;
		sf::Packet packetSend;
		packetSend << messageValue;

		if (isServer)
		{
			for (auto& socketIndex : sockets)
				socketIndex->socket->send(packetSend);
		}
		else
			socket->send(packetSend);
	};

	auto sendMessageQuickAll = [&](std::string text)
	{
		text = "!" + text;
		log(text);
		sendMessageQuick(text);
	};

	auto protocolFilter = [&](std::string message, TcpSocketClient* socketClient, sf::TcpSocket *socketIndex)
	{
		if (message != "" && message.size() > 0)
		{
			char protocol = message[0];
			message.erase(0, 1);
			if (protocol == '!')
			{
				std::string messageValue = "";

				if (isServer)
				{
					messageValue = "![" + socketClient->socket->getRemoteAddress().toString() + "] " + message;
					sf::Packet packetSend;
					packetSend << messageValue;

					for (auto& socketIndex : sockets)
						socketIndex->socket->send(packetSend);
				}
				else
					messageValue = "!" + message;

				log(messageValue);
			}		
			else if (protocol == '#')
			{
				if (isServer)
				{
					socketClient->heartbeatTimer = 0.f;
				}
				else
				{
					heartbeatTimer = 0.f;
					sendMessageRaw("#");
				}
			}
		}
	};

	auto deleteClientSocket = [&](TcpSocketClient* socketClient)
	{

		socketClient->socket->disconnect();
		selector.clear();
		sockets.remove(socketClient);
		delete socketClient->socket;
		delete socketClient;

		selector.add(listener);
		for (auto& socketIndex : sockets)
			selector.add(*socketIndex->socket);
	};

	auto sendCommand = [&](std::string command) 
	{
		sf::Packet packetSend;
		packetSend << command;
		if (isServer)
			for (auto& socketIndex : sockets)
				socketIndex->socket->send(packetSend);
		else
			socket->send(packetSend);
	};

	auto sendMessage = [&]()
	{
		while (isPlaying)
		{
			if (messageSent != "")
			{

				sf::Packet packetSend;

				mutex.lock();
				messageSent = username + " said: " + messageSent;
				packetSend << "!" + messageSent;
				if (isServer) 
					log("[Server] " + messageSent);
				messageSent = "";
				mutex.unlock();

				if (isServer)
				{
					for (auto& socketIndex : sockets)
						socketIndex->socket->send(packetSend);
				}
				else
					socket->send(packetSend);		
			}
		}
		
	};

	auto receiveMessage = [&]()
	{
		while (isPlaying && !isServer)
		{
			std::string messageReceived = "";
			sf::Packet packetReceive;

			socket->receive(packetReceive);
			packetReceive >> messageReceived;
				
			if (messageReceived == "")
				continue;

			protocolFilter(messageReceived, nullptr, socket);
		}
	};

	auto serverConnect = [&]()
	{
		listener.listen(port);
		selector.add(listener);

		while (isPlaying)
		{
			if (selector.wait())
			{
				if (selector.isReady(listener))
				{
					TcpSocketClient *socketClient = new TcpSocketClient();
					socketClient->socket = new sf::TcpSocket();
					socketClient->heartbeatTimer = 0.f;
					if (listener.accept(*socketClient->socket) == sf::Socket::Done)
					{
						if ((limitUsers > sockets.size() || limitUsers == 0))
						{
							sockets.push_back( socketClient );
							selector.add(*socketClient->socket);
							log("New client connected: " + socketClient->socket->getRemoteAddress().toString());
						}
						else
						{
							std::string messageValue = "!New client rejected (users limit reached): " + socketClient->socket->getRemoteAddress().toString();
							std::cout << messageValue;

							sf::Packet packetSend;
							packetSend << messageValue;

							for (auto& socketIndex : sockets)
								socketIndex->socket->send(packetSend);

							log(messageValue);

							messageValue = "!Sorry, but the chat room limit has been reached! Goodbye!";
							sf::Packet packetSendReject;
							packetSendReject << messageValue;
							socketClient->socket->send(packetSendReject);

							delete socketClient->socket;
							delete socketClient;
						}
						
					}
					else
					{
						delete socketClient->socket;
						delete socketClient;
					}
				}
				else
				{
					mutex.lock();
					for (auto& socketIndex : sockets)
					{
						if (selector.isReady(*socketIndex->socket))
						{
							std::string messageReceived = "";
							sf::Packet packetReceive;

							socketIndex->socket->receive(packetReceive);
							packetReceive >> messageReceived;

							if (messageReceived == "")
								continue;

							protocolFilter(messageReceived, socketIndex, nullptr);
						}
					}
					mutex.unlock();
				}
			}		
		}	
	};

	auto clientConnect = [&]()
	{
		if (socket->connect(serverIp, port) == sf::Socket::Done)
		{
			log("Connected to the chat room, welcome!");
			sendMessageQuick(" entered the chat!");
		}
	};

	auto heartbeatProtocol = [&]()
	{
		sf::Clock clock;
		float TIMEOUT = 10.f, TIMESEND = 5.f, timeElapsed = 0.f;
		while (isPlaying)
		{
			float timer = clock.getElapsedTime().asSeconds();
			timeElapsed += timer;
			clock.restart();

			if (isServer)
			{
				if (timeElapsed >= TIMESEND)
				{
					timeElapsed = 0.f;
					sendMessageRaw("#");
				}

				std::list<TcpSocketClient*> removeList;
				removeList.clear();

				for (auto&& socketIndex : sockets)
				{
					socketIndex->heartbeatTimer += timer;
					if (socketIndex->heartbeatTimer >= TIMEOUT)
					{
						socketIndex->heartbeatTimer = -1.f;
						sendMessageQuickAll(socketIndex->socket->getRemoteAddress().toString() + " took too long to respond and got disconnected!");
						removeList.push_back(socketIndex);
					}
				}

				for (auto&& socketIndex : removeList)
					deleteClientSocket(socketIndex);

				removeList.clear();
			}
			else
			{
				heartbeatTimer += timer;

				if (heartbeatTimer >= TIMEOUT)
				{
					log("Server took too long to respond, connection closed!");
					isPlaying = false;
				}
			}

		}
	};

	auto close = [&]()
	{
		mutex.lock();

		for (auto&& socketIndex : sockets)
		{
			socketIndex->socket->disconnect();
			delete socketIndex;
		}

		sockets.clear();
		socket->disconnect();
		delete socket;
		mutex.unlock();

		listener.close();

		if (threadSend)
		{
			threadSend->wait();
			delete threadSend;
		}

		if (threadReceive)
		{
			threadReceive->wait();
			delete threadReceive;
		}

		if (threadStandBy)
		{
			threadStandBy->wait();
			delete threadStandBy;
		}

		if (threadHeartbeat)
		{
			threadHeartbeat->wait();
			delete threadHeartbeat;
		}
	};

	std::cout << "Nickname: ";
	std::cin >> username;

	std::cout << "Type \"0\" to start a new chat room or the chat room IP to enter: ";
	std::cin >> messageSent;

	isServer = messageSent == "0";
	
	if (!isServer)
		serverIp = messageSent;
	else
	{
		std::cout << "Users limit (type \"0\" for limitless): ";
		std::cin >> limitUsers;
	}

	messageSent = "";

	std::cout << "Waiting connection...\n";
	if (isServer)
		threadStandBy = new sf::Thread(serverConnect);
	else
		threadStandBy = new sf::Thread(clientConnect);	
		
	threadStandBy->launch();

	threadSend = new sf::Thread(sendMessage);
	threadSend->launch();
	threadReceive = new sf::Thread(receiveMessage);
	threadReceive->launch();
	threadHeartbeat = new sf::Thread(heartbeatProtocol);
	threadHeartbeat->launch();

	log("Chat started! Type \"stop\" to close");

	while (isPlaying)
	{
		std::string command = "";
		std::getline(std::cin, command);
		
		mutex.lock();
		if (command == "stop")
		{
			isPlaying = false;
			if (isServer)
				sendMessageQuick(" [server] left the chat! CHAT ROOM CLOSED!");
			else
				sendMessageQuick(" left the chat!");
		}
		else
			messageSent = command;
		mutex.unlock();
	}

	close();

	return 0;
}
