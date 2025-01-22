#ifndef MESSENGER_H_
#define MESSENGER_H_

#include <string>
#include <iostream>
#include <librdkafka/rdkafkacpp.h>

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
        Messenger();
        Messenger(std::string &brokers);
        void sendData(Data d);
        void sendRequest(request rq);
        void dr_cb(RdKafka::Message &message) override;
        
        std::string brokers; //kafka listener

    private:
        void sendKafkaMsg(const std::string &brokers, const std::string &topic_name, const std::string &message);
};

#endif  // MESSENGER_H_