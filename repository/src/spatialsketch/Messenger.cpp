#include "Messenger.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

using json = nlohmann::json;

Messenger::Messenger(std::string &brokers, std::string &request_topic, std::string &data_topic, std::string &estimation_topic){
    this->brokers = brokers;
    this->request_topic = request_topic;
    this->data_topic = data_topic;
    this->estimation_topic = estimation_topic;

    initConsumer();
    initProducer();

}

void Messenger::initConsumer(){
    std::string errstr;

    // Create configuration object
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

    conf->set("bootstrap.servers", brokers, errstr);
    conf->set("group.id", "spatialsketch_group", errstr);
    conf->set("enable.auto.commit", "false", errstr); // Disable auto-commit to control offsets
    conf->set("enable.partition.eof", "true", errstr); // emit eof whenever the consumer reaches the end of a partition.

    consumer = std::shared_ptr<RdKafka::KafkaConsumer>(RdKafka::KafkaConsumer::create(conf, errstr));
    if (!consumer) {
        throw std::runtime_error("Can't set up kafka consumer.");
    }

    RdKafka::TopicPartition *tp = RdKafka::TopicPartition::create(estimation_topic, 0);

    RdKafka::ErrorCode err_asgn = consumer->assign({tp});   //assign consumer to the topic+partition 

    if (err_asgn != RdKafka::ERR_NO_ERROR) {
        throw std::runtime_error("Can't set up kafka consumer to topic, partition.");
    }
    delete conf;
}

void Messenger::initProducer(){
    std::string errstr;

    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

    if (conf->set("bootstrap.servers", brokers, errstr) != RdKafka::Conf::CONF_OK) {
        throw std::runtime_error("Failed to set brokers for kafka producer");
    }
    conf->set("dr_cb", this, errstr);
    producer = std::shared_ptr<RdKafka::Producer>(RdKafka::Producer::create(conf, errstr));
    if (!producer) {
        throw std::runtime_error("Failed to create producer");
    }
    delete conf;
}

Messenger::~Messenger(){
    if (producer){
        producer->flush(3000);
        // producer.reset();       //not needed with smart pointers
    }
    if (consumer){
        consumer->unassign();
        consumer->close();
        // consumer.reset();           //not needed with smart pointers
    }

    
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

bool Messenger::sendData(Data d){
    json j = d;
    std::string msg = j.dump(4);    // serialization with pretty printing
    return this->sendKafkaMsg(msg, data_topic);
}

bool Messenger::sendRequest(request rq){
    json j = rq;
    std::string msg = j.dump(4);    // serialization with pretty printing

    return this->sendKafkaMsg(msg, request_topic);
}

std::optional<std::pair<long, std::string>> Messenger::receiveEstimation() {
    return this->consumeKafkaMsg();
}

void Messenger::dr_cb(RdKafka::Message &message) {
    if (message.err()) {
        std::cerr << "Message couldn't be delivered: " << message.errstr() << std::endl;
        delivery_promise.set_value(false);
    } 
    else {
        delivery_promise.set_value(true);
        // std::cout << "Message delivered to topic " << message.topic_name()
        //           << " [" << message.partition() << "] at offset "
        //           << message.offset() << std::endl;
    }
}

bool Messenger::sendKafkaMsg(const std::string &message, const std::string &topic){
    this->delivery_promise = std::promise<bool>();  // Reset promise
    std::future<bool> future = delivery_promise.get_future();  // Get future

    // Produce the message
    RdKafka::ErrorCode resp = producer->produce(
        topic,                           // Topic name
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
    // delete producer;
    return future.get();  // Wait for delivery confirmation
}

std::optional<std::pair<long, std::string>> Messenger::consumeKafkaMsg() {
    // std::this_thread::sleep_for(std::chrono::seconds(1));   //todo wait for offsets to be ready, otherwise it gets RD_KAFKA_OFFSET_INVALID -1001 or message isn't ready at the topic yet
    std::this_thread::sleep_for(std::chrono::milliseconds(500));   //todo wait for offsets to be ready, otherwise it gets RD_KAFKA_OFFSET_INVALID -1001
    std::pair<long, std::string> est_key;

    int64_t low, high;
    if (consumer->get_watermark_offsets(estimation_topic, 0, &low, &high) != RdKafka::ERR_NO_ERROR ) {        // Get partition's offset range
        std::cerr << "Getting offsets failed!" << std::endl;
        return std::nullopt;
    }
    if (high == 0) {
        std::cerr << "Topic "<< estimation_topic <<" is empty, no messages to read!" << std::endl;
        return std::nullopt;
    }
    
    std::vector<RdKafka::TopicPartition *> tp_vector;
    RdKafka::ErrorCode err =  consumer->assignment(tp_vector);

    if (err != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to get assigned partitions: " << RdKafka::err2str(err) << std::endl;
        return std::nullopt;
    }

    RdKafka::TopicPartition *tp = tp_vector[0]; //consumer is assigned only in one topic
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
        if (jmsg.contains("estimation") && jmsg.contains("param")){
            long est ;
            if (jmsg["estimation"].is_number_integer()) {
                est = jmsg["estimation"].get<long>();
            } else if (jmsg["estimation"].is_number_float()) {
                est = static_cast<long>(jmsg["estimation"].get<double>());
            } else if (jmsg["estimation"].is_string()) {
                try {
                    est = std::stol(jmsg["estimation"].get<std::string>());
                } catch (const std::exception& e) {
                    std::cerr << "Error: The string estimation returned from SDE couldn't be converted to long " << e.what() << '\n';
                    return std::nullopt;
                }
            } else {
                std::cerr << "Error: The type of estimation returned from SDE is unsupported \n";
                return std::nullopt;
            }

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
    RdKafka::TopicPartition::destroy(tp_vector);
    
    return est_key;
}
