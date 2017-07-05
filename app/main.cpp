#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <cctype>

#include "mqtt/async_client.h"

#include "helper/arg_parser.hpp"

const std::string SERVER_ADDRESS { "tcp://106.109.130.18:1883" };
const std::string CLIENT_ID { "sync_consume_cpp" };
const std::string TOPIC { "devices/lora" };

const int QOS = 1;
const char NUM_RETRY_ATTEMPTS = 5;

// Callbacks for the success or failures of requested actions.

class ActionListener : public virtual mqtt::iaction_listener
{
    public:
        ActionListener(const std::string &name) :
                _name(name) {
        }

    private:
        std::string _name;

        void on_failure(const mqtt::token &tok) override {
            std::cout << _name << " failure";
            if (tok.get_message_id() != 0)
                std::cout << " for token: [" << tok.get_message_id() << "]"
                        << std::endl << std::endl;
        }

        void on_success(const mqtt::token &tok) override {
            std::cout << _name << " success";
            if (tok.get_message_id() != 0)
                std::cout << " for token: [" << tok.get_message_id() << "]"
                        << std::endl;
            auto top = tok.get_topics();
            if (top && !top->empty())
                std::cout << "\ttoken topic: '" << (*top)[0] << "', ...";
            std::cout << std::endl << std::endl;
        }
};

class Callback : public virtual mqtt::callback,
        public virtual mqtt::iaction_listener
{
    public:
        Callback(mqtt::async_client &cli_cl, mqtt::connect_options &connOpts) :
                nretry(0), cli(cli_cl), connOpts(connOpts),
                subListener("Subscription") {
        }

    private:
        // Counter for the number of connection retries
        int nretry;
        // Options to use if we need to reconnect
        mqtt::async_client &cli;
        // An action listener to display the result of actions.
        mqtt::connect_options connOpts;
        ActionListener subListener;

        // This deomonstrates manually reconnecting to the broker by calling
        // connect() again. This is a possibility for an application that keeps
        // a copy of it's original connect_options, or if the app wants to
        // reconnect with different options.
        // Another way this can be done manually, if using the same options, is
        // to just call the async_client::reconnect() method.
        void reconnect() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));
            try {
                cli.connect(connOpts, nullptr, *this);
            } catch (const mqtt::exception &exc) {
                std::cerr << "Error: " << exc.what() << std::endl;
                exit(1);
            }
        }

        // Re-connection failure
        void on_failure(const mqtt::token& tok) override {
            std::cout << "Connection failed" << std::endl;
            if (++nretry > NUM_RETRY_ATTEMPTS)
                exit(1);
            reconnect();
        }

        // Re-connection success
        void on_success(const mqtt::token& tok) override {
            std::cout << "Connection success" << std::endl;
            std::cout << "Subscribing to topic '" << TOPIC << "'\n"
                    << "\tfor client " << CLIENT_ID << " using QoS" << QOS
                    << "\n" << "\nPress Q<Enter> to quit\n" << std::endl;

            cli.subscribe(TOPIC, QOS, nullptr, subListener);
        }

        // Callback for when the connection is lost.
        // This will initiate the attempt to manually reconnect.
        void connection_lost(const std::string& cause) override {
            std::cout << "\nConnection lost" << std::endl;
            if (!cause.empty())
                std::cout << "\tcause: " << cause << std::endl;

            std::cout << "Reconnecting..." << std::endl;
            nretry = 0;
            reconnect();
        }

        // Callback for when a message arrives.
        void message_arrived(mqtt::const_message_ptr msg) override {
            std::cout << "Message arrived" << std::endl;
            std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
            std::cout << "\tpayload: '" << msg->to_string() << "'\n"
                    << std::endl;
        }

        void delivery_complete(mqtt::delivery_token_ptr token) override {
        }
};

int main(int argc, char* argv[]) {
    ArgParser parser(argc, argv);

    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);

    Callback cb(client, connOpts);
    client.set_callback(cb);

    // Start the connection.
    // When completed, the callback will subscribe to topic.

    try {
        std::cout << "Connecting to the MQTT server..." << std::flush;
        client.connect(connOpts, nullptr, cb);
    } catch (const mqtt::exception&) {
        std::cerr << "\nERROR: Unable to connect to MQTT server: '"
                << SERVER_ADDRESS << "'" << std::endl;
        return 1;
    }

    // Just block till user tells us to quit.

    while (std::tolower(std::cin.get()) != 'q')
        ;

    // Disconnect

    try {
        std::cout << "\nDisconnecting from the MQTT server..." << std::flush;
        client.disconnect()->wait();
        std::cout << "OK" << std::endl;
    } catch (const mqtt::exception &exc) {
        std::cerr << exc.what() << std::endl;
        return 1;
    }

    return 0;
}
