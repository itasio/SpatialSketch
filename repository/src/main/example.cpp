#include "SpatialSketch.h"
#include "ECM.h"
#include "Messenger.h"

#include <iostream>

int main() {
    // Initialize SpatialSketch with desired sketch, grid size, and optionally a memory limit, epsilon, delta, and domain size
    // SpatialSketch sp = SpatialSketch("ECM", 1024, 3350000);
    // std::cout << "Memory usage: " << sp.GetSize() << std::endl;

    // // Insert some ip address at given x, y
    // long ip = 101;
    // sp.Update(0, 0, ip, 0);
    // sp.Update(0, 0, ip, 1);
    // sp.Update(0, 0, ip, 2);
    // sp.Update(0, 0, ip, 3);
    // sp.Update(0, 0, ip, 4);

    // // Determine range to query
    // range r = range(0, 0, 3, 3);
    // std::vector<range> ranges = {r};  // SpatialSketch will handle multiple orthogonal ranges if given
    // long query_answer = sp.QueryRanges(ranges, ip, ip, 0);

    // std::cout << "\nQuery answer to range " << r.x1 << ", " << r.y1 << ", " << r.x2 << ", " << r.y2 << " is " << query_answer << std::endl;

    int n = 4;
    SpatialSketch sp = SpatialSketch("CM", n, 3350000, 0.5, 0.01);
    std::cout << "Memory usage: " << sp.GetSize() << std::endl;

    // Insert some ip address at given x, y
    long ip1 = 101;
    long ip2 = 555;
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            sp.Update(i, j, ip1, 1);
        }
        
    }
    
    // sp.Update(0, 0, ip1, 1);
    // sp.Update(1, 1, ip1, 1);

    // sp.Update(0, 0, ip2, 1);
    // sp.Update(0, 0, ip2, 4);
    sp.PrintCoverage(); // prints th number of initialized sketches in the grid that has max resolution

    // Determine range to query
    range r = range(0, 0, 1, 1);        // Meaning cells, not the point in greed e.g. 0,0,1,1 -> cells 0,0 0,1 1,0 1,1
    std::vector<range> ranges = {r};    // SpatialSketch will handle multiple orthogonal ranges if given
    long query_answer = sp.QueryRanges(ranges, ip1, ip1, 0);

    std::cout << "\nQuery answer to range " << r.x1 << ", " << r.y1 << ", " << r.x2 << ", " << r.y2 << " is " << query_answer << std::endl;


    std::string brokers = "192.168.43.155:9092";
    Messenger m = Messenger(brokers);
    request rq;
    rq.DataSetkey = "Poly_Data";

    rq.RequestID = 3;
    rq.SynopsisID = 1;
    rq.UID = 45;
    rq.StreamID = "Poly_Data";
    // rq.Param = {"Poly_Data","label","Queryable","0.0002", "0.99", "4"};
    rq.Param = {"4"};
    rq.NoOfP = 3;
    // m.sendRequest(rq);

    Data d;
    d.DataSetkey = "Poly_Data";
    d.StreamID = "Poly_Data";
    d.keyFieldName = "Poly_Data";
    d.keyToSend = "5";
    d.valueFieldName = "label";
    d.valueToSend = "60";
    m.sendData(d);

    return 0;
}