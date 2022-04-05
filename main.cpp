#include <chrono>
#include <csignal>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>

#include "mqtt/async_client.h"

namespace po = boost::program_options;
namespace json = boost::json;
namespace fs = boost::filesystem;

using namespace std;
using namespace std::chrono;

namespace {
    volatile std::sig_atomic_t gSignalStatus;
}

void signal_handler(int signal) { gSignalStatus = signal; }

string get_timestamp() {
    return boost::posix_time::to_iso_extended_string(
               boost::posix_time::microsec_clock::universal_time()) +
           "Z";
}

class Agent {
    shared_ptr<mqtt::async_client> mqtt_client;
    thread heartbeat_publisher;
    bool running = true;
    size_t utterance_window_size = 5;
    queue<json::value> utterance_queue;

  public:
    Agent(string address) {
        // Create an MQTT client using a smart pointer to be shared among
        // threads.
        this->mqtt_client = make_shared<mqtt::async_client>(address, "agent");

        // Connect options for a non-persistent session and automatic
        // reconnects.
        auto connOpts = mqtt::connect_options_builder()
                            .clean_session(true)
                            .automatic_reconnect(seconds(2), seconds(30))
                            .finalize();

        mqtt_client->set_message_callback(
            [&](mqtt::const_message_ptr msg) { process(msg); });

        auto rsp = this->mqtt_client->connect(connOpts)->get_connect_response();
        BOOST_LOG_TRIVIAL(info)
            << "Connected to the MQTT broker at " << address;

        mqtt_client->subscribe("agent/dialog", 2);
    };

    void start() {
        this->heartbeat_publisher = thread(&Agent::publish_heartbeats, this);
    }

    void disconnect() {
        BOOST_LOG_TRIVIAL(info) << "Disconnecting from MQTT broker...";
        this->mqtt_client->disconnect()->wait();
    }

    void look_for_label(string label_1,
                        string label_2,
                        queue<json::value> utterance_queue) {
        // Check first item in the queue for label1
        json::value item_1 = utterance_queue.front();
        auto participant_id = item_1.at("data").at("participant_id");
        auto extractions = item_1.at("data").at("extractions").as_array();
        for (auto x : extractions) {
            cout << item_1.at("data").at("asr_msg_id") << x.at("labels")
                 << endl;
        }

        string timestamp = get_timestamp();

        json::value output_message = {
            {"header",
             {{"timestamp", timestamp},
              {"message_type", "status"},
              {"version", "0.1"}}},
            {"msg",
             {{"timestamp", timestamp},
              {"sub_type", "Event:dialog_coordination_event"},
              {"source", "tomcat-CDC"},
              {"version", "0.0.1"}}}};

        output_message.as_object()["data"] = {{"key", "value"}};

        mqtt_client->publish("agent/tomcat-CDC/coordination_event",
                             json::serialize(output_message));
    }

    void process(mqtt::const_message_ptr msg) {
        json::value jv = json::parse(msg->to_string());

        // Ensure that the participant_id is not Server
        if (jv.at("data").at("participant_id") == "Server") {
            return;
        }

        if (utterance_queue.size() == utterance_window_size) {
            utterance_queue.pop();
        }
        utterance_queue.push(jv);
        // Look for label
        look_for_label("CriticalVictim", "MoveTo", utterance_queue);
    }

    void publish_heartbeats() {
        while (this->running) {
            this_thread::sleep_for(seconds(1));

            string timestamp = get_timestamp();
            json::value jv = {{"header",
                               {{"timestamp", timestamp},
                                {"message_type", "status"},
                                {"version", "0.1"}}},
                              {"msg",
                               {{"timestamp", timestamp},
                                {"sub_type", "heartbeat"},
                                {"source", "tomcat-CDC"},
                                {"version", "0.0.1"}}},
                              {"data", {{"state", "ok"}}}};

            this->mqtt_client
                ->publish("status/tomcat-CDC/heartbeats", json::serialize(jv))
                ->wait();
        }
    }

    ~Agent() {
        this->running = false;
        if (this->heartbeat_publisher.joinable()) {
            BOOST_LOG_TRIVIAL(info)
                << "Shutting down heartbeat_publisher thread...";
            this->heartbeat_publisher.join();
        }
        this->disconnect();
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

    signal(SIGINT, signal_handler);

    Agent agent(address);
    agent.start();
    while (true) {
        if (gSignalStatus == SIGINT) {
            BOOST_LOG_TRIVIAL(info)
                << "Keyboard interrupt detected (Ctrl-C), shutting down.";
            break;
        }
        else {
            this_thread::sleep_for(milliseconds(100));
        }
    }

    return EXIT_SUCCESS;
}
