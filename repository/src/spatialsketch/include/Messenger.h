#ifndef MESSENGER_H_
#define MESSENGER_H_

#include <string>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>

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
        Messenger(std::string &brokers);
        void sendData(Data d);
        void sendRequest(request rq);
        void dr_cb(RdKafka::Message &message) override;
        
        std::string brokers; //kafka listener

    private:
        void sendKafkaMsg(const std::string &brokers, const std::string &topic_name, const std::string &message);
};

#endif  // MESSENGER_H_