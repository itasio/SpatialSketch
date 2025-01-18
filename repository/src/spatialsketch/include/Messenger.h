#ifndef MESSENGER_H_
#define MESSENGER_H_

#include <string>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>

class Messenger : public RdKafka::DeliveryReportCb {
    public:
        Messenger(){};
        void sendData();
        void sendRequest();
        void dr_cb(RdKafka::Message &message) override;
        std::string datasetKey;
        std::string streamID;
        std::string params;
        int uID;
        int synopsisID;
        int numOfParall;

    private:
        void sendKafkaMsg(const std::string &brokers, const std::string &topic_name, const std::string &message);
};

#endif  // MESSENGER_H_