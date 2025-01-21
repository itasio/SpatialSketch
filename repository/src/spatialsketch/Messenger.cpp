#include "Messenger.h"

#include <iostream>
#include <nlohmann/json.hpp>

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

void Messenger::dr_cb(RdKafka::Message &message) {
    if (message.err()) {
        std::cout << "Message couldn't be delivered: " << message.errstr() << std::endl;
    } 
    else {
        std::cout << "Message delivered to topic " << message.topic_name()
                  << " [" << message.partition() << "] at offset "
                  << message.offset() << std::endl;
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
    } else {
        std::cout << "Produced message to " << topic_name << std::endl;
    }

    producer->flush(5000);
    delete producer;
    delete conf;
}

