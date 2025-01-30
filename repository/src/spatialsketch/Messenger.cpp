#include "Messenger.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

using json = nlohmann::json;

Messenger::Messenger(std::string &brokers){
    this->brokers = brokers;
}

// Define how to convert request  to JSON
void to_json(json& j, const request& rq) {
    j = json{
            {"key", rq.DataSetkey},
            {"noOfP", rq.NoOfP},
            {"streamID", rq.StreamID},
            {"param", rq.Param},
            {"dataSetkey", rq.DataSetkey},
            {"requestID", rq.RequestID},
            {"synopsisID", rq.SynopsisID},
            {"uid", rq.UID}
            };
}

// Define how to convert data to JSON
void to_json(json& j, const Data& d) {
    json dataSent;
    dataSent[d.keyFieldName] = d.keyToSend;
    dataSent[d.valueFieldName] = d.valueToSend;

    j = json{
        {"dataSetkey", d.DataSetkey},
        {"streamID", d.StreamID},
        {"values", dataSent}
    };
}

void Messenger::sendData(Data d)
{
    std::string topic_name = "data_topic";
    json j = d;
    std::string msg = j.dump(4);    // serialization with pretty printing
    this->sendKafkaMsg(brokers, topic_name, msg);
}

void Messenger::sendRequest(request rq){
    std::string topic_name = "request_topic";

    json j = rq;

    std::string msg = j.dump(4);    // serialization with pretty printing

    this->sendKafkaMsg(brokers, topic_name, msg);


}

std::optional<std::pair<long, std::string>> Messenger::receiveEstimation() {
    std::string topic_name = "estimation_topic";

    return this->consumeKafkaMsg(brokers, topic_name);
}

void Messenger::dr_cb(RdKafka::Message &message) {
    if (message.err()) {
        std::cout << "Message couldn't be delivered: " << message.errstr() << std::endl;
    } 
    else {
        // std::cout << "Message delivered to topic " << message.topic_name()
        //           << " [" << message.partition() << "] at offset "
        //           << message.offset() << std::endl;
    }
}

void Messenger::sendKafkaMsg(const std::string &brokers, const std::string &topic_name, const std::string &message){
    std::string errstr;

    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    
    if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK) {
        std::cerr << "Failed to set brokers: " << errstr << std::endl;
        return;
    }

    conf->set("dr_cb", this, errstr);
    RdKafka::Producer *producer = RdKafka::Producer::create(conf, errstr);
    if (!producer) {
        std::cerr << "Failed to create producer: " << errstr << std::endl;
        return;
    }
    
    // Produce the message
    RdKafka::ErrorCode resp = producer->produce(
        topic_name,                      // Topic name
        RdKafka::Topic::PARTITION_UA,    // Unassigned partition
        RdKafka::Producer::RK_MSG_COPY,  // Copy payload flag
        const_cast<char *>(message.c_str()), // Message payload
        message.size(),                  // Payload size
        nullptr,                         // Message key
        0,                               // Key size
        0,                               // Timestamp
        nullptr                          // Opaque pointer
    );
    
    if (resp != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to produce message: " << RdKafka::err2str(resp) << std::endl;
    }

    producer->flush(5000);
    delete producer;
    delete conf;
}

std::optional<std::pair<long, std::string>> Messenger::consumeKafkaMsg(const std::string &brokers, const std::string &topic_name) {

    std::pair<long, std::string> est_key;
    std::string errstr;

    // Create configuration object
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

    conf->set("bootstrap.servers", brokers, errstr);
    conf->set("group.id", "spatialsketch_group", errstr);
    conf->set("enable.auto.commit", "false", errstr); // Disable auto-commit to control offsets
    conf->set("enable.partition.eof", "true", errstr); // emit eof whenever the consumer reaches the end of a partition.


    // Create Kafka consumer
    RdKafka::KafkaConsumer *consumer = RdKafka::KafkaConsumer::create(conf, errstr);
    if (!consumer) {
        std::cerr << "Failed to create consumer: " << errstr << std::endl;
        return std::nullopt;
    }

    RdKafka::TopicPartition *tp = RdKafka::TopicPartition::create(topic_name, 0);

    RdKafka::ErrorCode err_asgn = consumer->assign({tp});   //assign consumer to the topic+partition 

    if (err_asgn != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Assignment failed: " << RdKafka::err2str(err_asgn) << std::endl;
        return std::nullopt; 
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));   //wait for offsets to be ready, otherwise it gets RD_KAFKA_OFFSET_INVALID -1001

    int64_t low, high;
    if (consumer->get_watermark_offsets(topic_name, 0, &low, &high) != RdKafka::ERR_NO_ERROR ) {        // Get partition's offset range
        std::cerr << "Getting offsets failed!" << std::endl;
        return std::nullopt;
    }
    
    if (high == 0) {
        std::cerr << "Topic is empty, no messages to read!" << std::endl;
        return std::nullopt;
    }
    
    tp->set_offset(high - 1);   // seek to the last message
   
    RdKafka::ErrorCode err_seek = consumer->seek(*tp, 3000);  //find the last message of the topic

    if (err_seek != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Seek failed: " << RdKafka::err2str(err_seek) << std::endl;
        return std::nullopt;
    }

    RdKafka::Message *msg = consumer->consume(3000);    //consume the last message
    
    if (msg->err() == RdKafka::ERR_NO_ERROR) {

        std::string payload = std::string((char *)msg->payload(), msg->len());
        std::cout << "Received: " << payload << std::endl;
        // consumer->commitSync();
        json jmsg = json::parse(payload);
        long est = jmsg["estimation"];
        if (jmsg.contains("estimation") && jmsg.contains("param")){
            long est = jmsg["estimation"];
            std::string key_queried = jmsg["param"][0];
            est_key = {est, key_queried};
        }else{
            return std::nullopt;
        }
    } else if (msg->err() == RdKafka::ERR__PARTITION_EOF) {
        std::cout << "End of partition reached." << std::endl;
        return std::nullopt;
    } else {
        std::cerr << "Error: " << msg->errstr() << std::endl;
        return std::nullopt;
    }

    delete msg;
    delete tp;
    consumer->unassign();
    consumer->close();
    delete consumer;
    delete conf;

    RdKafka::wait_destroyed(5000);  // Wait max 5 seconds

    return est_key;
}
