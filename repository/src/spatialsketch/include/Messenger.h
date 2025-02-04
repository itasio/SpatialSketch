#ifndef MESSENGER_H_
#define MESSENGER_H_

#include <string>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <optional>
#include <future>
#include <memory>

// Synopsis ID for CountMin sketch
#define CM_ID (1)
//Synopsis ID for BloomFilter sketch
#define BF_ID (2)

//add Synopsis with Keyed partitioning 
#define RQ_ID_ADD_SYN (1)   
// delete a Synopsis
#define RQ_ID_DEL_SYN (2)
// request an estimation of a queryable Synopsis
#define RQ_ID_EST_ONE_SYN (3)
// add Synopsis with Random partioning
#define RQ_ID_ADD_SYN_RAND (4) 
// add continuous Synopsis
#define RQ_ID_ADD_SYN_CONT (5)   
// request a more advance estimation
#define RQ_ID_EST_ADV (6)   
// update a Synopsis state
#define RQ_ID_UPD_SYN_STATE (7)   
// request an estimation among multiple Synopses
#define RQ_ID_EST_MANY_SYN (8)


typedef struct request {
	std::string DataSetkey;
	int RequestID;
    int SynopsisID;
	int UID;
    std::string StreamID;
	int NoOfP;
	std::vector<std::string> Param;
} request;

typedef struct Data {
  	std::string DataSetkey;
	std::string StreamID;
	std::string keyFieldName;
	std::string keyToSend;
	std::string valueFieldName;
	std::string valueToSend;
} Data;

class Messenger : public RdKafka::DeliveryReportCb {
    public:
        Messenger(std::string &brokers, std::string &request_topic, std::string &data_topic, std::string &estimation_topic);

        Messenger(Messenger&& other) noexcept   // Move constructor (needed because std::promise is non-copyable)
            :   brokers(std::move(other.brokers)),
                delivery_promise(std::move(other.delivery_promise)),
                request_topic(std::move(other.request_topic)),
                data_topic(std::move(other.data_topic)),
                estimation_topic(std::move(other.estimation_topic)),
                producer(std::move(other.producer)),
                consumer(std::move(other.consumer)) {}

        Messenger(const Messenger&) = delete;   // Delete copy constructor and assignment operator (because of std::promise)
        Messenger& operator=(const Messenger&) = delete;
        
        Messenger& operator=(Messenger&& other) noexcept {  // Move assignment operator (needed for std::optional)
            if (this != &other) {
                brokers = std::move(other.brokers);
                delivery_promise = std::move(other.delivery_promise);
                request_topic = std::move(other.request_topic);
                data_topic = std::move(other.data_topic);
                estimation_topic = std::move(other.estimation_topic);
                producer = std::move(other.producer);
                consumer = std::move(other.consumer);

            }
            return *this;
        }
        
        ~Messenger();   // Cleanup

        bool sendData(Data d);
        bool sendRequest(request rq);
        std::optional<std::pair<long, std::string>> receiveEstimation();
        void dr_cb(RdKafka::Message &message) override;
        
        std::string brokers; //kafka listener
        std::promise<bool> delivery_promise;
        std::shared_ptr<RdKafka::Producer> producer;
        std::shared_ptr<RdKafka::KafkaConsumer> consumer;
        // The topic to send requests
        std::string request_topic;
        // The topic to send data
        std::string data_topic;
        // The topic to receive messages
        std::string estimation_topic;

    private:
        void initConsumer();
        void initProducer();
        bool sendKafkaMsg(const std::string &message, const std::string &topic);
        std::optional<std::pair<long, std::string>> consumeKafkaMsg();
};

#endif  // MESSENGER_H_