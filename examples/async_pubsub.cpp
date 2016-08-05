#include <string>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/format.hpp>
#include <boost/asio/deadline_timer.hpp>

#include <redisclient/redisasyncclient.h>

static const std::string channelName = "unique-redis-channel-name-example";
static const boost::posix_time::seconds timeout(1);

class Client
{
public:
    Client(boost::asio::io_service &ioService,
           const boost::asio::ip::address &address,
           unsigned short port)
        : ioService(ioService), publishTimer(ioService),
          connectSubscriberTimer(ioService), connectPublisherTimer(ioService),
          address(address), port(port),
          publisher(ioService), subscriber(ioService)
    {
        publisher.installErrorHandler(boost::bind(&Client::connectPublisher, this));
        subscriber.installErrorHandler(boost::bind(&Client::connectSubscriber, this));
    }

    void publish(const std::string &str)
    {
        publisher.publish(channelName, str);
    }

    void start()
    {
        connectPublisher();
        connectSubscriber();
    }

protected:
    void errorPubProxy(const std::string &)
    {
        publishTimer.cancel();
        connectPublisher();
    }

    void errorSubProxy(const std::string &)
    {
        connectSubscriber();
    }

    void connectPublisher()
    {
        std::cerr << "connectPublisher\n";

        if( publisher.isConnected() )
        {
            std::cerr << "disconnectPublisher\n";

            publisher.disconnect();
            publishTimer.cancel();
        }

        publisher.connect(address, port,
                          boost::bind(&Client::onPublisherConnected, this, _1, _2));
    }

    void connectSubscriber()
    {
        std::cerr << "connectSubscriber\n";

        if( subscriber.isConnected() )
        {
            std::cerr << "disconnectSubscriber\n";
            subscriber.disconnect();
        }

        subscriber.connect(address, port,
                           boost::bind(&Client::onSubscriberConnected, this, _1, _2));
    }

    void callLater(boost::asio::deadline_timer &timer,
                   void(Client::*callback)())
    {
        std::cerr << "callLater\n";
        timer.expires_from_now(timeout);
        timer.async_wait([callback, this](const boost::system::error_code &ec) {
            if( !ec )
            {
                (this->*callback)();
            }
        });
    }

    void onPublishTimeout()
    {
        static size_t counter = 0;
        std::string msg = str(boost::format("message %1%")  % counter++);

        if( publisher.isConnected() )
        {
            std::cerr << "pub " << msg << "\n";
            publish(msg);
        }

        callLater(publishTimer, &Client::onPublishTimeout);
    }

    void onPublisherConnected(bool status, const std::string &error)
    {
        if( !status )
        {
            std::cerr << "onPublisherConnected: can't connect to redis: " << error << "\n";
            callLater(connectPublisherTimer, &Client::connectPublisher);
        }
        else
        {
            std::cerr << "onPublisherConnected ok\n";

            callLater(publishTimer, &Client::onPublishTimeout);
        }
    }

    void onSubscriberConnected(bool status, const std::string &error)
    {
        if( !status )
        {
            std::cerr << "onSubscriberConnected: can't connect to redis: " << error << "\n";
            callLater(connectSubscriberTimer, &Client::connectSubscriber);
        }
        else
        {
            std::cerr << "onSubscriberConnected ok\n";
            subscriber.subscribe(channelName,
                                 boost::bind(&Client::onMessage, this, _1));
        }
    }

    void onMessage(const std::vector<char> &buf)
    {
        std::string s(buf.begin(), buf.end());
        std::cout << "onMessage: " << s << "\n";
    }

private:
    boost::asio::io_service &ioService;
    boost::asio::deadline_timer publishTimer;
    boost::asio::deadline_timer connectSubscriberTimer;
    boost::asio::deadline_timer connectPublisherTimer;
    const boost::asio::ip::address address;
    const unsigned short port;

    redisclient::RedisAsyncClient publisher;
    redisclient::RedisAsyncClient subscriber;
};

int main(int, char **)
{
    boost::asio::ip::address address = boost::asio::ip::address::from_string("127.0.0.1");
    const unsigned short port = 6379;

    boost::asio::io_service ioService;

    Client client(ioService, address, port);

    client.start();
    ioService.run();

    std::cerr << "done\n";

    return 0;
}
