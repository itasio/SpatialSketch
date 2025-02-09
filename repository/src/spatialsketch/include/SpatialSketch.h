#ifndef SPATIAL_SKETCH_H_
#define SPATIAL_SKETCH_H_

#include "Utils.h"
#include "Statistics.h"
#include "DyadicRanges.h"


#include "Messenger.h"
#include "Sketch.h"
#include "CountMin.h"
#include "DyadCountMin.h"
#include "FM.h"
#include "BloomFilter.h"
#include "ECM.h"
#include "ElasticSketch.h"

#include <vector>
#include <list>
#include <unordered_map>
#include <string>

// ElasticSketch parameters
// Same budget as one CM sketch with epsilon=0.1 and delta=0.05.
#define HEAVY_MEM (168)
#define BUCKET_NUM (HEAVY_MEM / 64)
//#define BUCKET_NUM (1)
#define TOT_MEM_IN_BYTES (336)
typedef ElasticSketch<BUCKET_NUM,TOT_MEM_IN_BYTES> Elastic;


typedef struct es_grid {
    Elastic*** cells = 0;
    int x_dim = 0;
    int nr_init_sketches = 0;

    es_grid(int x_dim, int y_dim) : x_dim(x_dim) {
        cells = new Elastic**[x_dim];
        for (int i = 0; i < x_dim; i++) {
            cells[i] = new Elastic*[y_dim];
            // Enforce null pointers
            for (int j = 0; j < y_dim; j++) {
                cells[i][j] = NULL;
            }
        }
    }

    ~es_grid() {
        for (int i = 0; i < x_dim; i++) {
            delete[] cells[i];
        }
        delete[] cells;
    }
} es_grid;

typedef struct sde_sketch {
    // represents the type of Synopsis that is used
    int SynopsisID;
    // unique for each Synopsis. Will be the concatenated digits of grid's key, x coordinate, y coordinate
    int uID;
    // the parallelism of the sketch
	int NoOfP;;
    std::vector<std::string> Param;
    std::string DataSetkey;
    std::string StreamID;
    std::string keyIndex = "key";
    std::string valueIndex = "value";
    std::string operationMode = "Queryable";

    size_t hashCode() const {
        return std::hash<const void*>()(this); // Hash only the memory address
    }
} sde_sketch;


typedef struct sde_grid {
    sde_sketch*** cells = 0;
    int x_dim = 0;
    int nr_init_sketches = 0;

    sde_grid(int x_dim, int y_dim) : x_dim(x_dim) {
        cells = new sde_sketch**[x_dim];
        for (int i = 0; i < x_dim; i++) {
            cells[i] = new sde_sketch*[y_dim];
            // Enforce null pointers
            for (int j = 0; j < y_dim; j++) {
                cells[i][j] = NULL;
            }
        }
    }
    
    ~sde_grid() {
        for (int i = 0; i < x_dim; i++) {
            delete[] cells[i];
        }
        delete[] cells;
    }

} sde_grid;

typedef struct grid {
    Sketch*** cells = 0;
    int x_dim = 0;
    int nr_init_sketches = 0;

    grid(int x_dim, int y_dim) : x_dim(x_dim) {
        cells = new Sketch**[x_dim];
        for (int i = 0; i < x_dim; i++) {
            cells[i] = new Sketch*[y_dim];
            // Enforce null pointers
            for (int j = 0; j < y_dim; j++) {
                cells[i][j] = NULL;
            }
        }
    }

    ~grid() {
        for (int i = 0; i < x_dim; i++) {
            delete[] cells[i];
        }
        delete[] cells;
    }
} grid;



class SpatialSketch {
    public:

        /**
         * @brief initialize spatialsketch
         * @param n highest resolution is grid of n x n
         * @param memory_lim available memory, should not be exceeded and high resolution layers will be thrown away for this
         *                   default is -1 which implies there is no limit
         * @note Exception handling for very small memory limits is not implemented
        */
        SpatialSketch(std::string sketch, int n, long memory_lim=-1, float epsilon=2.8f, float delta=0.5f, int domain_size=50000);

        /**
         * @brief initialize spatialsketch with Messenger included to be used for handling sketches in SDE
         * @param n highest resolution is grid of n x n
         * @param memory_lim available memory, should not be exceeded and high resolution layers will be thrown away for this
         *                   default is -1 which implies there is no limit
         * @note Exception handling for very small memory limits is not implemented
        */
        SpatialSketch(std::shared_ptr<Messenger> mes, std::string sketch, int n, long memory_lim=-1, float epsilon=2.8f, float delta=0.5f, int domain_size=50000);

        // Cleanup
        ~SpatialSketch();

        /**
         * @brief Insert item with id and value at location (x,y), updating all cells containing (x,y) in the dyadic hierarchy
        */
        void Update(int x, int y, long id=0, int value=1);

        /**
         * @brief Query SpatialSketch at the given ranges with the specified item
         * @param stats If statistics pointer is supplied this function will log any relevant statistics to the struct
         * @return Query answer, i.e., the aggregate of all ranges
        */
        int QueryRanges(std::vector<range> ranges, long item=0, long item_end=0, int timestamp=0);

        long QueryRangesL2(std::vector<range> ranges);

        // Obtain the memory usage of SpatialSketch in bytes
        inline uint GetSize() {
            return current_memory_;
        }

        int GetResolution() {
            return resolution_;
        }

        void PrintCoverage();

    private:

        // The kafka client to send messages to kafka
        std::shared_ptr<Messenger> mes_;
        // Hash map that points to the sde grids
        std::unordered_map<int, sde_grid*> sde_grids_;
        // The number of requests sent to SDE for estimating multiple synopses. Must be different for every request.
        int num_of_req_send = 0;    


        // Hash map that points to the grids
        std::unordered_map<int, grid*> grids_;
        int n_ = 0; // Current largest resolution n x n grid
        int levels_ = 0;  // Current height of the hierarchy equivalent to the length of layers_
        int resolution_ = 1; // Default resolution, but can increase dynamically
        std::list<dyadic1D> top_level_intervals_;  // The set of largest non-overlapping intervals, for n that are power of 2, this is simply [1, n], for n=11, this is [1,8], [9,10], [11,11]
        int* nr_of_interval_cum_sum_ = nullptr;

        // Elastic sketch
        std::unordered_map<int, es_grid*> es_grids_;
        Elastic* es_sketch_ = nullptr;
        bool elastic_sketch_ = false;

        // Sketch related
        //int sketch_type_ = 0;
        std::string sketch_name_;// = "BF";//= "CM";
        Sketch* sketch_ = nullptr;
        int nr_hashes_ = 0;
        uint *hashes_ = nullptr;
        long *hashes_long_ = nullptr;
        uint32_t *hashes_32_ = nullptr;
        int **hash_coeffs_ = nullptr;
        long **hash_coeffs_long_ = nullptr;
        int domain_size_;

        dyadic_cm_precompute* precompute_;
        bool new_insert_ = true;


        inline bool isMessengerUsed() {
            if(mes_)
                return true;
            else
                return false;
        }

        void setMessenger(std::shared_ptr<Messenger> m){
            mes_ = m;
        }

        sde_sketch* InitSdeSketch(int key, int x_cell, int y_cell);
        request CreateRequest(std::vector<sde_sketch> sketches, int type_of_rq, std::string key_to_query = "");

        // Grid / layer dropping variables
        int diag_exponent_ = 1;  // The combined exponent of the diagonal layer resolution to be dropped, first grids to drop are g(2^1, 2^0) and g(2^0, 2^1), where exponent sum is odd
        int dropping_phase_ = 1; // 1: dropping grids of diagonal layers sequentially, 2: dropping lowest resolution grids
        std::list<int> dropping_list_ = {}; // List of grids to drop, where once a grid is dropped it is removed from the list, once the list is empty we continue to the next layer or phase

        // Sketch param
        float epsilon_ = 0;
        float delta_ = 0;

        // Memory
        long memory_limit_ = 0;
        int sketch_size_ = 0;  // Size of one embedded sketch
        unsigned long current_memory_ = 0; // running count of current memory of spatialsketch

        //
        int prev_x_ = -1;
        int prev_y_ = -1;
        std::vector<std::pair<int, int>> prev_x_interval_, prev_y_interval_;

        // Setup
        //void SetupTopLevelIntervals(int end, int resolution = 1);

        // --- Dropping functionality ---
        bool DropGrid(int grid_key);
        bool DropESGrid(int grid_key); // ElasticSketch variants
        std::list<int> GenDiagonalGridKeys(int exponent_sum, int max_exponent);
        std::list<int> GenHighestResolutionGridKeys();
        void DropNextGrid();
        

        // --- Generic function  ---
        // Convert any grid dimension to string to be used as the key in hash maps 
        inline std::string DimToString(int x, int y) {
            // return std::to_string(x) + "," + std::to_string(y);
            std::string sd(21, '\0');
            sprintf(const_cast<char*>(sd.c_str()), "%d,%d", x, y);
            return sd; //std::string(sprintf("%d,%d,%d,%d", x1, y1, x2, y2));
        }

        inline int DimToKey(int x, int y) {
            return x + y * (n_+1);
        }

        std::pair<int, int> KeyToDimInt(int key) {
            return std::make_pair<int,int>(key % (n_+1), key / (n_+1));
        }

        std::string KeyToDimString(int key) {
            return std::to_string(key % (n_+1)) + "x" + std::to_string(key / (n_+1));
        }

        // Convert any 2d range to a string to be used as the key in hash maps
        inline std::string CoordinatesToString(int x1, int y1, int x2, int y2);

        // Verify memory is within limits and drop highest resolution layer if necessary
        void MemoryCheck();

        /*inline CountMin DyadicIntervalToCell(int x1, int y1, int x2, int y2) {
            //std::cout << DimToString(n_ / (x2 - x1 + 1), n_ / (y2 - y1 + 1)) << std::endl;
            return grids_[DimToString(n_ / (x2 - x1 + 1), n_ / (y2 - y1 + 1))].cells[x1/(x2-x1+1)][y1/(y2-y1+1)];
        }*/

        // Get dyadic intervals of some 2d rectangle
        std::vector<dyadic2D> GetDyadicIntervals(int x1, int y1, int x2, int y2); 

        // --- Update functionality ---
        // Update the dyadic interval of specified dimensions with given item id and value
        // Optional passing of the level the dyadic interval is at to speed up computation
        void UpdateInterval(int x1, int y1, int x2, int y2, long item=0, int value=1, uint* hashes=NULL, long* hashes_long=nullptr,
        uint32_t* hashes_32=nullptr);
        void UpdateInterval(int x1, int y1, int x2, int y2, long item, int value, dyadic_cm_precompute* precompute);
        
        // Given a target index and top-level interval, find the set of dyadic intervals that contain the target
        std::list<std::pair<int, int>> FindChildIntervalRecursive(int target, std::pair<int, int> current_interval);

        // Given a target index and top-level interval, find the set of dyadic intervals that contain the target, non-recursively
        std::vector<std::pair<int, int>> FindChildInterval(int target, int int_start, int int_end);

        // --- Query functionality
        // Concatenate vector b to vector a
        void ConcatVectors(std::vector<dyadic1D> &a, std::vector<dyadic1D> &b);

        // How does interval [start1, end1] overlap in [start2, end2]
        int IntervalOverlap(int start1, int end1, int start2, int end2);

        // Given a target 1D range that overlaps with a higher level dyadic interval in some manner, 
        // obtain the dyadic intervals that together compose the target part that overlaps with the base
        std::vector<dyadic1D> ObtainIntervals(dyadic1D target, dyadic1D base);

        int RecurseQueryDyadicInterval(dyadic2D d_interval, long item, long item_end, int &query_sum, int timestamp=0);

        // Query individual dyadic interval and accumulate result
        bool QueryDyadicInterval(dyadic2D di, long item, long item_end, int &query_sum, int timestamp=0);

        int QueryFrequency(std::vector<range> ranges, long item=0, long item_end=0, int timestamp=0);
        
        int RecurseQueryDyadicIntervalCountDistinct(dyadic2D d_interval, FM &fm);
        bool QueryDyadicIntervalCountDistinct(dyadic2D di, FM &fm);

        int QueryCountDistinct(std::vector<range> ranges);

        int RecurseQueryDyadicIntervalMembership(dyadic2D d_interval, long item, int &query_sum);
        bool QueryDyadicIntervalMembership(dyadic2D di, long item, int &query_sum);

        int QueryMembership(std::vector<range> ranges, long item);
        bool QueryDyadicInterval(dyadic2D di, CountMin &cm);
        int RecurseQueryDyadicInterval(dyadic2D d_interval, CountMin &cm);        

        int QueryECMMerge(std::vector<range> ranges, long item=0, long item_end=0, int timestamp=0);
        bool QueryDyadicIntervalECM(dyadic2D di, std::vector<ExpHist> *hists, long item, long item_end);
        int RecurseQueryDyadicIntervalECM(dyadic2D d_interval, std::vector<ExpHist> *hists, long item, long item_end);
};

#endif   // SPATIAL_SKETCH_H_