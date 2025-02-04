#include "SpatialSketch.h"
#include "ECM.h"
#include "Messenger.h"

#include <iostream>

int main() {
    // Initialize SpatialSketch with desired sketch, grid size, and optionally a memory limit, epsilon, delta, and domain size
    std::string brokers = "192.168.43.155:9092";
    // std::string brokers = "192.168.1.9:9092";
    std::string request_topic = "request_topic";
    std::string data_topic = "data_topic";
    std::string estimation_topic = "estimation_topic";

    std::shared_ptr<Messenger> m = std::make_shared<Messenger>(brokers, request_topic, data_topic, estimation_topic);
    
    int n = 4;
    SpatialSketch sp = SpatialSketch(m,"CM", n, 3350000, 0.5, 0.01);
    
    std::cout << "Memory usage: " << sp.GetSize() << std::endl;

    // Insert some ip address at given x, y
    long ip1 = 103;
    long ip2 = 555;
    // for (int i = 0; i < n; i++)
    // {
    //     for (int j = 0; j < n; j++)
    //     {
    //         sp.Update(i, j, ip1, 1);
    //     }
        
    // }
    
    sp.Update(0, 0, ip1, 1);
    // sp.Update(1, 1, ip1, 1);

    // sp.Update(0, 0, ip2, 1);
    // sp.Update(0, 0, ip2, 4);
    sp.PrintCoverage(); // prints th number of initialized sketches in the grid that has max resolution
    
    // Determine range to query
    range r1 = range(0, 0, 0, 0);        // Meaning cells, not the point in greed e.g. 0,0,1,1 -> cells 0,0 0,1 1,0 1,1
    range r2 = range(0, 0, 1, 1);        // Meaning cells, not the point in greed e.g. 0,0,1,1 -> cells 0,0 0,1 1,0 1,1
    std::vector<range> ranges1 = {r1};    // SpatialSketch will handle multiple orthogonal ranges if given
    std::vector<range> ranges2 = {r2};    // SpatialSketch will handle multiple orthogonal ranges if given
    
    std::this_thread::sleep_for(std::chrono::seconds(1));   //wait to get the latest result from data sent

    long query_answer = sp.QueryRanges(ranges1, ip1, ip1, 0);
    std::cout << "\nQuery answer to range " << r1.x1 << ", " << r1.y1 << ", " << r1.x2 << ", " << r1.y2 << " is " << query_answer << std::endl;

    query_answer = sp.QueryRanges(ranges2, ip1, ip1, 0);
    std::cout << "\nQuery answer to range " << r2.x1 << ", " << r2.y1 << ", " << r2.x2 << ", " << r2.y2 << " is " << query_answer << std::endl;

    query_answer = sp.QueryRanges(ranges2, ip1, ip1, 0);
    std::cout << "\nQuery answer to range " << r2.x1 << ", " << r2.y1 << ", " << r2.x2 << ", " << r2.y2 << " is " << query_answer << std::endl;

    query_answer = sp.QueryRanges(ranges1, ip1, ip1, 0);
    std::cout << "\nQuery answer to range " << r1.x1 << ", " << r1.y1 << ", " << r1.x2 << ", " << r1.y2 << " is " << query_answer << std::endl;
    return 0;
}