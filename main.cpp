#include "mqtt/async_client.h"
#include <chrono>
#include <csignal>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

namespace po = boost::program_options;
namespace json = boost::json;
namespace fs = boost::filesystem;

using namespace std;
using namespace std::chrono;


void process_dialog_agent_message(mqtt::const_message_ptr msg) {
    json::value jv = json::parse(msg->to_string());
    cout << jv << endl;
}

class Agent {
    shared_ptr<mqtt::async_client> mqtt_client;
    thread publisher, subscriber;
    bool agent_stopped = false;

  public:
    Agent(string address) {
        // Create an MQTT client using a smart pointer to be shared among
        // threads.
        this->mqtt_client = make_shared<mqtt::async_client>(address, "agent");

        // Connect options for a persistent session and automatic reconnects.
        auto connOpts = mqtt::connect_options_builder()
                            .clean_session(true)
                            .automatic_reconnect(seconds(2), seconds(30))
                            .finalize();

        auto topics = mqtt::string_collection::create({"agent/dialog"});
        const vector<int> QOS{2};
        // Start consuming _before_ connecting, because we could get a flood
        // of stored messages as soon as the connection completes since
        // we're using a persistent (non-clean) session with the broker.
        this->mqtt_client->start_consuming();

        BOOST_LOG_TRIVIAL(info)
            << "Connecting to the MQTT broker at " << address << "...";
        auto rsp = this->mqtt_client->connect(connOpts)->get_connect_response();
    };

    void start() {
        this->publisher = thread(&Agent::heartbeat_publisher_func, this);
        // this->subscriber = jthread(subscriber_func, this->mqtt_client);
        // this->publisher.join();
        // this->subscriber.join();
    }

    void disconnect() {
        BOOST_LOG_TRIVIAL(info) << "Disconnecting...";
        this->mqtt_client->disconnect();
    }

    void subscriber_func() {
        while (true) {
            mqtt::const_message_ptr msg = this->mqtt_client->consume_message();

            if (!msg) {
                continue;
            }

            string topic = msg->get_topic();
            if (topic == "agent/dialog") {
                process_dialog_agent_message(msg);
            }
        }
    }

    void heartbeat_publisher_func() {
        while (!this->agent_stopped) {
            this_thread::sleep_for(seconds(1));
            this->mqtt_client->publish("status/agent", "ok")->wait();
        }
    }

    ~Agent() {
        this->agent_stopped = true;
        if (this->publisher.joinable()) {
            BOOST_LOG_TRIVIAL(info) << "Shutting down publisher thread";
            this->publisher.join();
        }
    }
};

int main(int argc, char* argv[]) {

    po::options_description generic("Generic options");

    string config_path;
    generic.add_options()("help,h", "Display this help message")(
        "version,v",
        "Display the version number")("config,c",
                                      po::value<string>(&config_path),
                                      "Path to (optional) config file.");

    po::options_description config("Configuration");

    config.add_options()("mqtt.host",
                         po::value<string>()->default_value("localhost"),
                         "MQTT broker host")(
        "mqtt.port", po::value<int>()->default_value(1883), "MQTT broker port");

    po::options_description cmdline_options;
    cmdline_options.add(generic).add(config);

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, cmdline_options), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << cmdline_options;
        return 1;
    }

    if (vm.count("config")) {
        if (fs::exists(config_path)) {
            po::store(po::parse_config_file(config_path.c_str(), config), vm);
        }
        else {
            BOOST_LOG_TRIVIAL(error) << "Specified config file '" << config_path
                                     << "' does not exist!";
            return EXIT_FAILURE;
        }
    }

    po::notify(vm);

    string address = "tcp://" + vm["mqtt.host"].as<string>() + ":" +
                     to_string(vm["mqtt.port"].as<int>());

    Agent agent(address);
    agent.start();
    while(true) {
        cout << "Agent is running" << endl;
        this_thread::sleep_for(seconds(1));
    }

    // Disconnect

    return EXIT_SUCCESS;
}
