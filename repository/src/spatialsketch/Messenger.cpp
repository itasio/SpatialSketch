#include "Messenger.h"

#include <iostream>



void Messenger::sendData(){
    std::cout << "Hello world " <<std::endl;
}

void Messenger::dr_cb(RdKafka::Message &message) {
    if (message.err()) {
        std::cerr << "Message couldn't be delivered: " << message.errstr() << std::endl;
    } 
    /*else {
        std::cout << "Message delivered to topic " << message.topic_name()
                  << " [" << message.partition() << "] at offset "
                  << message.offset() << std::endl;
    }*/
}

void Messenger::sendKafkaMsg(const std::string &brokers, const std::string &topic_name, const std::string &message){
    std::string errstr;

    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK) {
        std::cerr << "Failed to set brokers: " << errstr << std::endl;
        return;
    }

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

