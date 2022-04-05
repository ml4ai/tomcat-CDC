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

/** Get current UTC timestamp in ISO-8601 format. */
string get_timestamp() {
    return boost::posix_time::to_iso_extended_string(
               boost::posix_time::microsec_clock::universal_time()) +
           "Z";
}

/** Class that represents our agent/AC */
class Agent {
    shared_ptr<mqtt::async_client> mqtt_client;
    thread heartbeat_publisher;
    bool running = true;
    size_t utterance_window_size = 5;

    // We use a deque instead of a queue since we will want to iterate over it.
    deque<json::object> utterance_queue;

    /** Disconnect from the MQTT broker */
    void disconnect() {
        BOOST_LOG_TRIVIAL(info) << "Disconnecting from MQTT broker...";
        this->mqtt_client->disconnect()->wait();
    }

    /** Simple function that allows you to look for a simple label */
    bool look_for_label(const json::array extractions, string label) {
        for (auto extraction : extractions) {
            json::array labels = extraction.at("labels").as_array();
            if (find(labels.begin(),
                     labels.end(),
                     json::string(label.c_str()))) {
                return true;
            }
        }
        return false;
    }

    void publish_coordination_message() {
        string timestamp = get_timestamp();

        json::object output_message = {
            {"header",
             {{"timestamp", timestamp},
              {"message_type", "status"},
              {"version", "0.1"}}},
            {"msg",
             {{"timestamp", timestamp},
              {"sub_type", "Event:dialog_coordination_event"},
              {"source", "tomcat-CDC"},
              {"version", "0.0.1"}}}};

        // To modify values, we must view them as objects using the as_object()
        // method.
        output_message["data"] = {{"key", "value"}};

        mqtt_client->publish("agent/tomcat-CDC/coordination_event",
                             json::serialize(output_message));
    }
    /** This method takes 3 args, a Queue and 2 labels. It checks if the first
     * item in the queue is the label. If it is, it also checks if any of the
     * other items in the queue have this label  */
    void check_label_seq_2(const string& label_1,
                           const string& label_2,
                           deque<json::object> utterance_queue) {

        // Check first item in the queue for label1
        json::object item_1 = utterance_queue.front();

        json::string id1 = item_1.at("data").at("participant_id").as_string();

        json::array item_1_extractions =
            item_1.at("data").at("extractions").as_array();

        if (look_for_label(item_1_extractions, label_1)) {
            BOOST_LOG_TRIVIAL(info) << "First label detected";
            for (size_t i = 1; i < utterance_queue.size(); i++) {
                json::array extractions = utterance_queue.at(i)
                                              .at("data")
                                              .at("extractions")
                                              .as_array();
                json::string id2 = utterance_queue.at(i)
                                       .at("data")
                                       .at("participant_id")
                                       .as_string();
                if (look_for_label(extractions, label_2) and (id1 != id2)) {
                    BOOST_LOG_TRIVIAL(info) << label_1 << " and " << label_2
                                            << " sequence detected.";
                    publish_coordination_message();
                };
            }
        };
    }

    /** Function that processes incoming messages */
    void process(mqtt::const_message_ptr msg) {
        json::object jv = json::parse(msg->to_string()).as_object();

        // Uncomment the line below to print the message
        // cout << jv << endl;

        // Ensure that the participant_id is not Server
        if (jv.at("data").at("participant_id") == "Server") {
            return;
        }

        if (utterance_queue.size() == utterance_window_size) {
            utterance_queue.pop_front();
        }

        utterance_queue.push_back(jv);
        // Look for label
        check_label_seq_2("CriticalVictim", "MoveTo", utterance_queue);
    }

    /** Function that publishes heartbeat messages while the agent is running */
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

        /** Start publishing heartbeat messages */
        this->heartbeat_publisher = thread(&Agent::publish_heartbeats, this);
    };

    /** Destructor for the class that cleans up threads and disconnects from
     * the broker. */
    ~Agent() {
        this->running = false;
        if (this->heartbeat_publisher.joinable()) {
            BOOST_LOG_TRIVIAL(info) << "Shutting down heartbeat thread...";
            this->heartbeat_publisher.join();
        }
        this->disconnect();
    }
};

int main(int argc, char* argv[]) {

    // Setting up program options
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

    // We run notify this first time to pick up the -c/--config option
    po::notify(vm);

    // Print a help message
    if (vm.count("help")) {
        cout << cmdline_options;
        return 1;
    }

    // If the -c/--config option is passed to the program on the command line,
    // we check for the existence of the specified config file and load the
    // options from it.
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

    // We run the notify function a second time in order to process the config
    // file
    po::notify(vm);

    string address = "tcp://" + vm["mqtt.host"].as<string>() + ":" +
                     to_string(vm["mqtt.port"].as<int>());

    signal(SIGINT, signal_handler);

    Agent agent(address);
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
