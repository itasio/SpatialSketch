#include "SpatialSketch.h"

#include <cmath>
#include <string>
#include <iostream>
#include <unistd.h>


SpatialSketch::SpatialSketch(std::string sketch_name, int n, long memory_lim, float epsilon, float delta, int domain_size) {
    n_ = n;
    memory_limit_ = memory_lim;
    epsilon_ = epsilon;
    delta_ = delta;
    levels_ = std::floor(std::log2(n_)) + 1;
    sketch_name_ = sketch_name;
    domain_size_ = domain_size;

    if(isMessengerUsed){    //handle sketches to SDE
        if(sketch_name != "CM" && sketch_name != "BF"){
            throw invalid_argument("Not implemented yet");
        }
    }else{  //handle sketches locally
        // sketch setup
        if (sketch_name == "CM" || sketch_name == "CML2") { // cm
            sketch_ = new CountMin(epsilon_, delta_);
            hash_coeffs_ = sketch_->GetHashesCoeff();
            hashes_ = new uint[sketch_->repetitions_];
        } else if (sketch_name_ == "dyadicCM") { // dyadic cm 
            sketch_ = new DyadCountMin(epsilon_, delta_);
            precompute_ = new dyadic_cm_precompute();
        } else if (sketch_name_ == "FM") {
            sketch_ = new FM(epsilon_, delta_);
            hash_coeffs_long_ = sketch_->GetHashesCoeffLong();
            hashes_long_ = new long[sketch_->repetitions_];
            //precompute_ = new fm_precompute();
        } else if (sketch_name_ == "BF") {
            sketch_ = new BloomFilter(delta_, domain_size);
            hash_coeffs_long_ = sketch_->GetHashesCoeffLong();
            hashes_ = new uint[sketch_->repetitions_];
            //precompute_ = new bloom_precompute();
        } else if (sketch_name_.find(std::string("ECM")) != std::string::npos) {
            sketch_ = new ECM(epsilon_, delta_);
            hash_coeffs_long_ = sketch_->GetHashesCoeffLong();
            hashes_long_ = new long[sketch_->repetitions_];
        } else if (sketch_name_ == "ElasticSketch") {
            elastic_sketch_ = true; // indication wether to use elastic sketch in grid implementation
            hashes_32_ = new uint32_t[2];
            es_sketch_ = new Elastic();
        } else {
            throw "SpatialSketch::SpatialSketch: sketch name not recognized";
        }

        if (elastic_sketch_) {
            // elastic sketch size does not change <- It can be compressed but not necessary in our experiments.
            sketch_size_ = TOT_MEM_IN_BYTES;
        } else {
            sketch_size_ = sketch_->GetSize();
            std::cout << "Sketch size " << sketch_size_ / 1024 << "KB" << std::endl;
            std::cout << "Sketch repetitions " << sketch_->repetitions_ << std::endl;
        }
    }



    // Create list of #levels_ hash maps, where the end of the list contains the highest resolution and the last element contains the single cell grid
    if (elastic_sketch_) {
        es_grids_.reserve(levels_*levels_);
        for (int x_pow = 0; x_pow < levels_; x_pow++) {
            for (int y_pow = 0; y_pow < levels_; y_pow++) {
                int x_dim = std::pow(2,x_pow);
                int y_dim = std::pow(2,y_pow);
                es_grid *g = new es_grid(x_dim, y_dim);
                es_grids_[DimToKey(x_dim, y_dim)] = g;

                // Grid without initialized sketches is 2d array of null pointers
                current_memory_ += (x_dim * y_dim * sizeof(void*)); // data list
            }
        }
    } else {
        grids_.reserve(levels_*levels_);
// TODO what to do with grids when using SDE ???
        for (int x_pow = 0; x_pow < levels_; x_pow++) {
            for (int y_pow = 0; y_pow < levels_; y_pow++) {
                int x_dim = std::pow(2,x_pow);
                int y_dim = std::pow(2,y_pow);
                grid *g = new grid(x_dim, y_dim);
                grids_[DimToKey(x_dim, y_dim)] = g;
                
                std::cout << "Grid created with dims: " << x_dim << "," << y_dim <<std::endl;

                // Grid without initialized sketches is 2d array of null pointers
                current_memory_ += (x_dim * y_dim * sizeof(void*)); // data list
            }
        }
        std::cout << grids_.size() <<" grids created  and levels are: " << levels_ <<std::endl;
    }

    //SetupTopLevelIntervals(n_);
    top_level_intervals_ = {dyadic1D(1, n_)};  // simply replace for SetupTopLevelIntervals() when using grids fo powers of 2
    if (memory_limit_ > 0) {
        MemoryCheck();
    }

    // Rehash for efficiency and add hash map size
    if (elastic_sketch_) {
        es_grids_.rehash(es_grids_.size());
        current_memory_ += es_grids_.size() * (sizeof(void*)) + // data list
                                es_grids_.bucket_count() * (sizeof(void*) + sizeof(size_t)); // bucket index;        
    } else {
        grids_.rehash(grids_.size());
        current_memory_ += grids_.size() * (sizeof(void*)) + // data list
                                grids_.bucket_count() * (sizeof(void*) + sizeof(size_t)); // bucket index;
    }
}

// SpatialSketch::SpatialSketch(Messenger mes, std::string sketch_name, int n, long memory_lim, float epsilon, float delta, int domain_size){
//     mes = mes;
//     n_ = n;
//     memory_limit_ = memory_lim;
//     epsilon_ = epsilon;
//     delta_ = delta;
//     levels_ = std::floor(std::log2(n_)) + 1;
//     sketch_name_ = sketch_name;
//     domain_size_ = domain_size;

//     if (sketch_name != "CM" && sketch_name != "BF"){
//          throw std::invalid_argument("Not implemented yet");
//     }

//     grids_.reserve(levels_*levels_);

//     for (int x_pow = 0; x_pow < levels_; x_pow++) {
//         for (int y_pow = 0; y_pow < levels_; y_pow++) {
//             int x_dim = std::pow(2,x_pow);
//             int y_dim = std::pow(2,y_pow);
//             grid *g = new grid(x_dim, y_dim);
//             grids_[DimToKey(x_dim, y_dim)] = g;
            
//             std::cout << "Grid created with dims: " << x_dim << "," << y_dim <<std::endl;

//             // Grid without initialized sketches is 2d array of null pointers
//             current_memory_ += (x_dim * y_dim * sizeof(void*)); // data list
//         }
//     }
//     std::cout << grids_.size() <<" grids created  and levels are: " << levels_ <<std::endl;
//     grids_.rehash(grids_.size());
//     current_memory_ += grids_.size() * (sizeof(void*)) + // data list
//                             grids_.bucket_count() * (sizeof(void*) + sizeof(size_t)); // bucket index;
// }

SpatialSketch::~SpatialSketch() {
    if (elastic_sketch_) {
        es_grids_.clear();
    } else {
        grids_.clear();
    
    }

    //delete cm_;
    if (sketch_name_ == "CM" || sketch_name_ == "CML2") {
        delete hashes_;
    } else if (sketch_name_ == "dyadicCM") {
        delete precompute_;
    } else if (sketch_name_ == "FM") {
        delete hashes_;
    } else if (sketch_name_ == "BF") {
        delete hashes_;
    } else if (sketch_name_ == "ElasticSketch"){
        delete hashes_32_;
    }
}

void SpatialSketch::PrintCoverage() {
    if (resolution_ == 1) {
        std::cout << "Spatialsketch nr of initialized sketches " << grids_[DimToKey(n_,n_)]->nr_init_sketches << std::endl;
    }
}

// OUTDATED, BUT REQUIRED WHEN HANDLING NON POWER OF TWO GRIDS
/* To obtain the next interval (either up or down the hierarchy) it is useful to know the highest interval
   This function obtains the largest dyadic interval for some range [1, end], function assumes square grids
   For example, for [1, 13], the top level intervals are [1,8], [9, 12], [13, 13]
   Note that under any interval representation is done starting from 1, but under the hood (i.e., the actual grid) starts from 0.
   Additionally, a resolution is specified where all intervals should be atleast as large as the resolution
*/
/*void SpatialSketch::SetupTopLevelIntervals(int end, int resolution) {
    std::list<std::pair<int, int>> intervals;
    std::pair<int, int> current_interval = std::make_pair(1, end);

    // Recruse from the largest [1, 2^x] interval until [?, end]
    while (current_interval.first <= current_interval.second && current_interval.second - current_interval.first + 1 > resolution) {
        int start = current_interval.first;
        int power = (int) std::floor(std::log2((double) end - start + 1));
        intervals.push_back(std::make_pair(start, start + (int) std::pow(2, power) - 1));
        current_interval = std::make_pair(start + std::pow(2, power), end);
    }
    top_level_intervals_ = intervals;
}*/


// Drop grid and update memory
bool SpatialSketch::DropGrid(int grid_key) {
    if (grids_.find(grid_key) == grids_.end()) {
        // std::cout << "Trying to drop grid " << KeyToDimString(grid_key) << " that does not exist" << std::endl;
        return false;
    }

    grid* temp_grid = grids_[grid_key];
    std::pair<int, int> dim = KeyToDimInt(grid_key);

    // For ECM we have to iterate over the cells to check what memory they use and has to be subtracted
    if (sketch_name_.find(std::string("ECM")) != std::string::npos) {
        for (int i = 0; i < dim.first; i++) {
            for (int j = 0; j < dim.second; j++) {
                if (temp_grid->cells[i][j] != NULL) {
                    current_memory_ -= temp_grid->cells[i][j]->GetSize();
                }
            }
        }
    } else {
        // For fixed size sketches we can simply subtract the memory of the grid
        current_memory_ -= temp_grid->nr_init_sketches * sketch_size_;  // sketch memory
    }
    current_memory_ -= (dim.first * dim.second * sizeof(void*));  // pointer memory
    current_memory_ -= (sizeof(void*));  // ptr to grid, note that buckets stays until rehash
    grids_.erase(grid_key);

    if (grid_key == DimToKey(n_/resolution_, n_/resolution_)) {
        resolution_ = resolution_ * 2;
        levels_ = levels_ - 1;
        std::cout << "Resolution reduced to " << n_/resolution_ << std::endl;
        //grids_.rehash(grids_.size());
    }

    //std::cout << "Dropped grid " << KeyToDimString(grid_key) << ", " << grid_key << "\n";
    return true;
}


bool SpatialSketch::DropESGrid(int grid_key) {
    if (es_grids_.find(grid_key) == es_grids_.end()) {
        // std::cout << "Trying to drop grid " << KeyToDimString(grid_key) << " that does not exist" << std::endl;
        return false;
    }

    es_grid* temp_grid = es_grids_[grid_key];
    std::pair<int, int> dim = KeyToDimInt(grid_key);

    // For fixed size sketches we can simply subtract the memory of the grid
    // TODO: Verify this is correct for ES
    current_memory_ -= temp_grid->nr_init_sketches * sketch_size_;  // sketch memory

    current_memory_ -= (dim.first * dim.second * sizeof(void*));  // pointer memory
    current_memory_ -= (sizeof(void*));  // ptr to grid, note that buckets stays until rehash
    es_grids_.erase(grid_key);

    if (grid_key == DimToKey(n_/resolution_, n_/resolution_)) {
        resolution_ = resolution_ * 2;
        levels_ = levels_ - 1;
        std::cout << "Resolution reduced to " << n_/resolution_ << std::endl;
        //grids_.rehash(grids_.size());
    }

    //std::cout << "Dropped grid " << KeyToDimString(grid_key) << ", " << grid_key << "\n";
    return true;
}

// Generate all grid keys of the grids that lay on the diagonal of a given exponent sum
// i.e., for some grid g(2^i, 2^j), the exponent sum is i + j
std::list<int> SpatialSketch::GenDiagonalGridKeys(int exponent_sum, int max_exponent) {
    std::list<int> grid_keys;
    int i, j = 0;
    for (i = 0; i <= exponent_sum && i+j <= exponent_sum && i < max_exponent; i++) {
        for (; j <= exponent_sum && i+j <= exponent_sum && j < max_exponent; j++) {
            if (i + j == exponent_sum) {
                grid_keys.push_back(DimToKey(std::pow(2,i), std::pow(2,j)));
            }
        }
        j = 0;
    }
    return grid_keys;
}

// Generate all grid keys of the grids that lay on the L-shape, i.e., have atleast one axis with highest resolution
std::list<int> SpatialSketch::GenHighestResolutionGridKeys() {
    std::cout << "Phase 2 resolution " << n_/resolution_ << std::endl;
    std::list<int> grid_keys;
    
    // TODO, drop all with x or y == resolution_, increase resolution, regenerate top level
    // check for exponent sum == 2??

    std::list<int> x_grids = {};  // grids with x dimension max res
    for (int y = 0; y < levels_; y += 1) { // steps of 2 to skip odd sized exponent sums which should already been dropped
        x_grids.push_back(DimToKey(std::pow(2, levels_ - 1), std::pow(2, y)));
    }

    std::list<int> y_grids = {};  // grids with y dimension max res
    for (int x = 0; x < levels_; x += 1) {
        y_grids.push_back(DimToKey(std::pow(2, x), std::pow(2, levels_ - 1)));
    }

    // Enter them the final list in a matter such that the grids with fewest cells are dropped first, i.e., [g(2^4, 2^0), g(2^0, 2^4), g(2^4, 2^2), g(2^2, 2^4), g(2^4, 2^4)]
    bool swap = true;
    while (x_grids.size() > 0 && y_grids.size() > 0) {
        if (swap) {
            grid_keys.push_back(x_grids.front());
            x_grids.pop_front();
        } else {
            grid_keys.push_back(y_grids.front());
            y_grids.pop_front();
        }
        swap = !swap;
    }

    return grid_keys;
}



void SpatialSketch::DropNextGrid() {
    // There are still grids queued to be dropped next, so simply drop these
    if (dropping_list_.size() > 0) {
        // Drop next grid and remove from list
    } else if (diag_exponent_ < (levels_-1)*2) {  // phase 1
        // Still diagonal grids to drop
        dropping_list_ = GenDiagonalGridKeys(diag_exponent_, levels_);
        diag_exponent_ += 2;  // Increment exponent by 2 (has to stay odd)

    } else {  // phase 2, since all diagonal grids have been dropped
        dropping_list_ = GenHighestResolutionGridKeys();
    }

    // Drop next grid and remove from list
    bool dropped = false;
    while (!dropped) {
        if (es_sketch_) {
            dropped = DropESGrid(dropping_list_.front());
        } else {
            dropped = DropGrid(dropping_list_.front());
        }
        dropping_list_.pop_front();
    }
}

// Convert any 2d range to a string to be used as the key in hash maps
inline std::string SpatialSketch::CoordinatesToString(int x1, int y1, int x2, int y2) {
    // Given coordinates append them into one string
    //return std::to_string(x1) + "," + std::to_string(y1) + "," + std::to_string(x2) + "," + std::to_string(y2);
    //return fmt::format("{0},{1},{2},{3}", x1, y1, x2, y2);

    // Futur work, Unique key determined by only math operators will be faster
    std::string sd(47, '\0');
    sprintf(const_cast<char*>(sd.c_str()), "%d,%d,%d,%d", x1, y1, x2, y2);
    return sd; //std::string(sprintf("%d,%d,%d,%d", x1, y1, x2, y2));
}


void SpatialSketch::MemoryCheck() {
    // compute memory of layers_
    while (GetSize() > memory_limit_) {
        DropNextGrid();
    }
}


// --------------- Update functionalities ---------------

// Update dyadic interval
void SpatialSketch::UpdateInterval(int x1, int y1, int x2, int y2, long item, int value, uint* hashes, long* hashes_long, uint32_t* hashes_32) {
    // Check if sketch is initialized and do so if not

    int key = DimToKey(n_ / (x2 - x1 + 1), n_ / (y2 - y1 + 1));

    if (elastic_sketch_) {
        std::unordered_map<int, es_grid*>::iterator grid_ptr = es_grids_.find(key);
        if (grid_ptr != es_grids_.end()) {
            int x_cell = x1/(x2-x1+1);
            int y_cell = y1/(y2-y1+1);
            if (grid_ptr->second->cells[x_cell][y_cell] == NULL) {
                grid_ptr->second->cells[x_cell][y_cell] = new Elastic();
                //grid_ptr->second->cells[x_cell][y_cell]->insert((uint8_t*) &item, value);
                if (nr_hashes_ > 0 ) {
                    grid_ptr->second->cells[x_cell][y_cell]->insert((uint8_t*) &item, value, hashes_32);
                } else {
                    grid_ptr->second->cells[x_cell][y_cell]->insert((uint8_t*) &item, value);
                }
                // Increment counters
                grid_ptr->second->nr_init_sketches += 1;
                current_memory_ += sketch_size_;

                // Initializing of new sketch implies an increase in memory usage
                if (memory_limit_ > 0) {
                    MemoryCheck();
                }
            } else {
                if (nr_hashes_ > 0 ) {
                    grid_ptr->second->cells[x_cell][y_cell]->insert((uint8_t*) &item, value, hashes_32);
                } else {
                    grid_ptr->second->cells[x_cell][y_cell]->insert((uint8_t*) &item, value);
                }
                // grid_ptr->second->cells[x_cell][y_cell]->insert((uint8_t*) &item, value);
            }
        }
    } else {
        // Check if grid exists, if not, then it has dropped 
        std::unordered_map<int, grid*>::iterator grid_ptr = grids_.find(key);
        if (grid_ptr != grids_.end()) {
            int x_cell = x1/(x2-x1+1);
            int y_cell = y1/(y2-y1+1);

            std::cout << "Now update grid with key: " << key << " that has dims: " << KeyToDimString(key) <<
             " in position: [" << x_cell << "," << y_cell << "]"<< " for item: " << item <<std::endl;

            if (grid_ptr->second->cells[x_cell][y_cell] == NULL) {
                if (sketch_name_ == "CM" || sketch_name_ == "CML2") {
                    grid_ptr->second->cells[x_cell][y_cell] = new CountMin(epsilon_, delta_, hash_coeffs_);
                } else if (sketch_name_ == "dyadicCM") {
                    grid_ptr->second->cells[x_cell][y_cell] = new DyadCountMin(epsilon_, delta_);
                } else if (sketch_name_ == "FM") {
                    grid_ptr->second->cells[x_cell][y_cell] = new FM(epsilon_, delta_, hash_coeffs_long_);
                } else if (sketch_name_ == "BF") {
                    grid_ptr->second->cells[x_cell][y_cell] = new BloomFilter(delta_, domain_size_, hash_coeffs_long_);//, hash_coeffs_long_);  
                } else if (sketch_name_.find(std::string("ECM")) != std::string::npos) {
                    grid_ptr->second->cells[x_cell][y_cell] = new ECM(epsilon_, delta_, hash_coeffs_long_);
                }

                if (nr_hashes_ > 0 ) {
                    if (sketch_name_ == "CM" || sketch_name_ == "CML2") {
                        grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value, hashes_);
                    } else if (sketch_name_ == "FM") {
                        grid_ptr->second->cells[x_cell][y_cell]->Insert(item, hashes_long_);
                    } else if (sketch_name_.find(std::string("ECM")) != std::string::npos) {
                        grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value, hashes_long_);
                    }
                } else {
                    grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value);
                }
                new_insert_ = grid_ptr->second->cells[x_cell][y_cell]->NewInsert();    


                // Increment counters
                grid_ptr->second->nr_init_sketches += 1;
                current_memory_ += sketch_size_;

                // Initializing of new sketch implies an increase in memory usage
                if (memory_limit_ > 0) {
                    MemoryCheck();
                }
            } else {
                //std::cout << DimToKey(n_ / (x2 - x1 + 1), n_ / (y2 - y1 + 1)) << " cell " << x1/(x2-x1+1) << ", " << y1/(y2-y1+1) << " already initialized" << std::endl;
                if (nr_hashes_ > 0 ) {
                    if (sketch_name_ == "CM" || sketch_name_ == "CML2") {
                        grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value, hashes_);
                    } else if (sketch_name_ == "FM") {
                        grid_ptr->second->cells[x_cell][y_cell]->Insert(item, hashes_long_);
                    } else if (sketch_name_.find(std::string("ECM")) != std::string::npos) {
                        current_memory_ -= grid_ptr->second->cells[x_cell][y_cell]->GetSize();
                        grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value, hashes_long_);
                        current_memory_ += grid_ptr->second->cells[x_cell][y_cell]->GetSize();
                    }
                } else {
                    grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value);
                }
                new_insert_ = grid_ptr->second->cells[x_cell][y_cell]->NewInsert();            
            }
        }
    }
}

inline void SpatialSketch::UpdateInterval(int x1, int y1, int x2, int y2, long item, int value, dyadic_cm_precompute* precompute) {
    // Check if sketch is initialized and do so if not
    if (sketch_name_ != "dyadicCM") {
        throw "SpatialSketch::UpdateInterval: sketch is not dyadicCM";
    }
    int key = DimToKey(n_ / (x2 - x1 + 1), n_ / (y2 - y1 + 1));
    // Check if grid exists, if not, then it has dropped
    std::unordered_map<int, grid*>::iterator grid_ptr = grids_.find(key);
    if (grid_ptr != grids_.end()) {
        int x_cell = x1/(x2-x1+1);
        int y_cell = y1/(y2-y1+1);
        if (grid_ptr->second->cells[x_cell][y_cell] == NULL) {
            grid_ptr->second->cells[x_cell][y_cell] = new DyadCountMin(epsilon_, delta_);

            grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value, precompute);

            // Increment counters
            grid_ptr->second->nr_init_sketches += 1;
            current_memory_ += sketch_size_;

            // Initializing of new sketch implies an increase in memory usage
            if (memory_limit_ > 0) {
                MemoryCheck();
            }
        } else {
            grid_ptr->second->cells[x_cell][y_cell]->Insert(item, value, precompute);
        }
    }
}

// Verify if target lays within interval, for example target = 5, interval = [3, 7] returns true
inline bool IsWithin(int target, std::pair<int, int> interval) {
    return target >= interval.first && target <= interval.second;
}

inline bool IsWithin(int target, int start, int end) {
    return target >= start && target <= end;
}

// Given a target index and top-level interval, find the set of dyadic intervals that contain the target
// For example, for a grid of [1,13], target = 5, which resides in top level interval [1,8], returns the intervals: [1,8], [5,8], [5,6], [5,5]
std::list<std::pair<int, int>> SpatialSketch::FindChildIntervalRecursive(int target, std::pair<int, int> current_interval) {
    if (current_interval.second - current_interval.first + 1 < resolution_) {
        // Check if interval is large enough to be considered for given resolution
        return std::list<std::pair<int, int>>();
    } else if (target == current_interval.first && target == current_interval.second) {
        // base case (target, target)
        std::list<std::pair<int, int>> base;
        base.push_back(std::make_pair(target, target));
        return base;
    } else if (current_interval.first == current_interval.second && target != current_interval.first) {
        std::cout << "Error in base case " << target << ", (" << current_interval.first << ", " << current_interval.second << ")" << std::endl;
        return std::list<std::pair<int, int>>();
    } else {
        // Recursion
        int power = (int) std::log2(current_interval.second - current_interval.first + 1);

        // Split interval
        std::pair<int, int> lower_interval = std::make_pair(current_interval.first, current_interval.second - (int) std::pow(2, power-1));
        std::pair<int, int> upper_interval = std::make_pair(current_interval.first + (int) std::pow(2, power-1), current_interval.second);
        if (IsWithin(target, lower_interval)) {
            std::list<std::pair<int, int>> temp_res = FindChildIntervalRecursive(target, lower_interval);
            temp_res.push_back(current_interval);
            return temp_res;
        } else if (IsWithin(target, upper_interval)) {
            std::list<std::pair<int, int>> temp_res = FindChildIntervalRecursive(target, upper_interval);
            temp_res.push_back(current_interval);
            return temp_res;        
        } else {
            std::cout << "Error in recursion " << target << ", (" << current_interval.first << ", " << current_interval.second << ")" << std::endl;
            return std::list<std::pair<int, int>>();
        }
    }
}

std::vector<std::pair<int, int>> SpatialSketch::FindChildInterval(int target, int int_start, int int_end) {
    int start = int_start;
    int end = int_end;
    std::vector<std::pair<int,int>> intervals(levels_, std::make_pair(-1,-1));
    int diff = end - start + 1;
    for (int i = 0; i < levels_; i++) {
        if (diff < resolution_) {
            // Check if interval is large enough to be considered for given resolution
            break;
        }
        intervals[i].first = start;
        intervals[i].second = end;
        if (target == start && target == end) {
            // base case (target, target)
            break;
        } else if (start == end && target != start) {
            std::cout << "Error in base case " << target << ", (" << start << ", " << end << ")" << std::endl;
            break;
        } else {
            // Split interval
            if (IsWithin(target, start, start + diff/2 - 1)) {
                end = start + diff/2 - 1;
                //intervals[i].first = start;
                //intervals[i].second = end;
            } else if (IsWithin(target, start + diff/2, end)) {
                start = start + diff/2;
                //intervals[i].first = start;
                //intervals[i].second = end;   
            } else {
                std::cout << "Error in recursion " << target << ", (" << start << ", " << end << ")" << std::endl;
                break;
            }
            diff = diff/2;
        }
    }
    return intervals;
}


// Update the dyadic intervals that contain the given point with the given value
// X and y are done seperately and joined at the end
void SpatialSketch::Update(int x, int y, long item, int value) {
    if (sketch_name_ == "CM" || sketch_name_ == "CML2") {
        nr_hashes_ = sketch_->GetItemHashes(item, hashes_);
    } else if (sketch_name_== "dyadicCM") {
        sketch_->PrecomputeInsert(item, precompute_);
    } else if(sketch_name_ == "FM") {
        nr_hashes_ = sketch_->GetItemHashes(item, hashes_long_);
    } else if (sketch_name_ == "BF") {
        nr_hashes_ = 0; //sketch_->repetitions_;//GetItemHashes(item, hashes_);
    } else if (sketch_name_.find(std::string("ECM")) != std::string::npos) {
        nr_hashes_ = sketch_->GetItemHashes(item, hashes_long_);
    } else if (sketch_name_ == "ElasticSketch") {
        nr_hashes_ = es_sketch_->GetItemHashes((uint8_t*) &item, hashes_32_);
    }

    std::vector<std::pair<int, int>> x_intervals, y_intervals;
    // Find the set of dyadic intervals for the given x,y by recursng on their respective top level interval
    if (x == prev_x_) {
        x_intervals = prev_x_interval_;
    } else {
        x_intervals = FindChildInterval(x + 1, 1, n_);
        prev_x_interval_ = x_intervals;
        prev_x_ = x;
    }
    if (y == prev_y_) {
        y_intervals = prev_y_interval_;
    } else {
        y_intervals = FindChildInterval(y + 1, 1, n_);
        prev_y_interval_ = y_intervals;
        prev_y_ = y;
    }   

    // Combine them into 2d intervals and update them
    int x_dim = n_;
    int y_dim = n_;
    for (int i = x_intervals.size() - 1; i >= 0; i--) {  // go over intervals from smallest to largest
        bool flag = false;
        for (int j = y_intervals.size() - 1; j >= 0; j--) {
            if (x_intervals[i].second - x_intervals[i].first + 1 > x_dim && y_intervals[j].second - y_intervals[j].first + 1 > y_dim) {
                continue;
            }
            // Update the individual intervals
            if (sketch_name_ == "CM" || sketch_name_ == "CML2" || sketch_name_ == "FM" || (sketch_name_.find(std::string("ECM")) != std::string::npos || sketch_name_ == "ElasticSketch")) {
                UpdateInterval(x_intervals[i].first - 1, y_intervals[j].first - 1, x_intervals[i].second - 1, y_intervals[j].second - 1, item, value, hashes_, hashes_long_, hashes_32_);
            } else if (sketch_name_ == "dyadicCM") {
                UpdateInterval(x_intervals[i].first - 1, y_intervals[j].first - 1, x_intervals[i].second - 1, y_intervals[j].second - 1, item, value, precompute_);
            } else if (sketch_name_ == "BF") {
                UpdateInterval(x_intervals[i].first - 1, y_intervals[j].first - 1, x_intervals[i].second - 1, y_intervals[j].second - 1, item);//item
            }
            if (!new_insert_) {
                x_dim = x_intervals[i].second - x_intervals[i].first + 1;
                y_dim = y_intervals[j].second - y_intervals[j].first + 1;
            }
            if (!new_insert_ && i == (int) x_intervals.size() - 1 && j == (int) y_intervals.size() - 1) {
                flag = true;
                break;
            }
        }
        // For FM we can break if insert did not change the sketch
        if (flag) {
            break;
        }
    }
    nr_hashes_ = 0;  // reset
}


// --------- Query functionalities -----------

// Concatenate vector b to vector a
void SpatialSketch::ConcatVectors(std::vector<dyadic1D> &a, std::vector<dyadic1D> &b) {
    a.reserve(a.size() + b.size());
    for (auto i : b) {
        a.push_back(i);
    }
}

// How does interval [start1, end1] overlap in [start2, end2]
int SpatialSketch::IntervalOverlap(int start1, int end1, int start2, int end2) {
    //std::cout << "IntervalOverlap [" << start1 << ", " << end1 << "] in [" << start2 << ", " << end2 << "]: ";
    if ((start2 >= start1 && end2 <= end1)) { 
        // Covers
        return 1;
    } else if (start1 >= start2 && end1 <= end2) {
        // Contained within
        return 2;
    } else if (start1 <= start2 && end1 >= start2 && end1 <= end2) {
        // Lower contained
        return 3;
    } else if (start1 >= start2 && start1 <= end2 && end1 >= end2) {
        // Upper contained
        return 4;
    } else {
        return 0;
    }
}

// Given a target 1D range that overlaps with a higher level dyadic interval in some manner, 
// obtain the dyadic intervals that together compose the target part that overlaps with the base
std::vector<dyadic1D> SpatialSketch::ObtainIntervals(dyadic1D target, dyadic1D base) {
    // Check for resolution compliance
    if (base.end - base.start + 1 < (uint) resolution_) {
        std::vector<dyadic1D> res = {};
        return res;
    } else if (target.start == base.start && target.end == base.end) {
        // If exactly overlap, return
        // split interval, if overlap lower, recurse lower, if overlap upper, recurse upper
        std::vector<dyadic1D> res = {target};
        return res;
    } else {
        // Recursion
        int power = (int) std::log2(base.end - base.start + 1);

        // Split interval
        dyadic1D lower_base = dyadic1D(base.start, base.end - (int) std::pow(2, power-1));
        dyadic1D upper_base = dyadic1D(base.start + (int) std::pow(2, power-1), base.end);

        std::vector<dyadic1D> lower_res = {}, upper_res = {};
        if (IntervalOverlap(target.start, target.end, lower_base.start, lower_base.end)) {
            //std::cout << "Lower recursion with " << std::max(target.first, lower_base.first) << ", " << std::min(target.second, lower_base.second) << std::endl;
            /*if (target.end - target.start + 1 == resolution_) {
                lower_res = {target};
            } else {*/
            lower_res = ObtainIntervals(dyadic1D(std::max(target.start, lower_base.start), std::min(target.end, lower_base.end)), lower_base);
            if (lower_res.size() == 0) {
                dyadic1D partial = base;
                partial.coverage = (float) (target.end - target.start + 1) / (float) (base.end - base.start + 1);
                lower_res = {partial};
            }
        }
        if (IntervalOverlap(target.start, target.end, upper_base.start, upper_base.end)) {
            //std::cout << "Upper recursion with " << std::max(target.first, upper_base.first) << ", " << std::min(target.second, upper_base.second) << std::endl;
            //std::cout << "base " << upper_base.first << ", " << upper_base.second << std::endl;
            /*if (target.end - target.start + 1 == resolution_) {
                upper_res = {target};
            } else {*/
            upper_res = ObtainIntervals(dyadic1D(std::max(target.start, upper_base.start), std::min(target.end, upper_base.end)), upper_base);
            if (upper_res.size() == 0) {
                dyadic1D partial = base;
                partial.coverage = (float) (target.end - target.start + 1) / (float) (base.end - base.start + 1);
                upper_res = {partial}; 
            }
        }
        ConcatVectors(lower_res, upper_res);
        return lower_res;
    }
}

// Given a rectangular range, obtain the minimum number dyadic intervals composing it
std::vector<dyadic2D> SpatialSketch::GetDyadicIntervals(int x1, int y1, int x2, int y2) {
    std::vector<dyadic1D> x_intervals, y_intervals;

    // Sanity check
    if (x1 > x2 || y1 > y2) {
        return std::vector<dyadic2D>();
    }

    // X 
    for (auto interval : top_level_intervals_) {
        int overlapx = IntervalOverlap(x1+1, x2+1, interval.start, interval.end);
        if (overlapx == 1) {
            // Exact overlap, thus this interval is required, but continue the loop for potential other intervals
            x_intervals.push_back(interval);
        } else if (overlapx == 2) {
            // interval completely contained, therefore the other top level intervals do not have to be considered
            x_intervals = ObtainIntervals(dyadic1D(x1+1, x2+1), interval);
            break;
        } else if (overlapx == 3) {
            // Lower overlap, implying upper part is out of range, therefore we can break afterwards
            std::vector<dyadic1D> sub_intervals = ObtainIntervals(dyadic1D(x1+1, x2+1), interval);
            ConcatVectors(x_intervals, sub_intervals);
            break;
        } else if (overlapx == 4) {
            // Upper overlap
            std::vector<dyadic1D> sub_intervals = ObtainIntervals(dyadic1D(x1+1, x2+1), interval);
            ConcatVectors(x_intervals, sub_intervals);
        }
    }

    // Y
    for (auto interval : top_level_intervals_) {
        int overlapy = IntervalOverlap(y1+1, y2+1, interval.start, interval.end);
        if (overlapy == 1) {
            // Exact overlap, thus this interval is required, but continue the loop for potential other intervals
            y_intervals.push_back(interval);
        } else if (overlapy == 2) {
            // interval completely contained, therefore the other top level intervals do not have to be considered
            y_intervals = ObtainIntervals(dyadic1D(y1+1, y2+1), interval);
            break;
        } else if (overlapy == 3) {
            // Lower overlap, implying upper part is out of range, therefore we can break afterwards
            std::vector<dyadic1D> sub_intervals = ObtainIntervals(dyadic1D(y1+1, y2+1), interval);
            ConcatVectors(y_intervals, sub_intervals);
            break;
        } else if (overlapy == 4) {
            // Upper overlap
            std::vector<dyadic1D> sub_intervals = ObtainIntervals(dyadic1D(y1+1, y2+1), interval);
            ConcatVectors(y_intervals, sub_intervals);
        }
    }

    // Combine x and y intervals into 2d intervals
    std::vector<dyadic2D> dyadic_intervals;
    dyadic_intervals.reserve(x_intervals.size() * y_intervals.size());
    for (auto int_x : x_intervals) {
        for (auto int_y : y_intervals) {
            dyadic2D r;
            r.x1 = int_x.start;
            r.y1 = int_y.start;
            r.x2 = int_x.end;
            r.y2 = int_y.end;
            r.coverage = int_x.coverage * int_y.coverage;
            dyadic_intervals.push_back(r);
        }
    }

    return dyadic_intervals;
}


bool SpatialSketch::QueryDyadicInterval(dyadic2D di, long item, long item_end, int &query_sum, int timestamp) {
    /*std::string*/ int key = DimToKey(n_ / (di.x2 - di.x1 + 1), n_ / (di.y2 - di.y1 + 1));

    if (elastic_sketch_) {       
        auto grid_ptr = es_grids_.find(key);
        if (grid_ptr != es_grids_.end()) {
            
            int x_cell = di.x1/(di.x2-di.x1+1);
            int y_cell = di.y1/(di.y2-di.y1+1);
            // Check if the actual sketch is initialized, if it isn't then the value is simply zero
            if (grid_ptr->second->cells[x_cell][y_cell] != NULL) {
                query_sum += (int) (di.coverage * grid_ptr->second->cells[x_cell][y_cell]->query((uint8_t*) &item));
            }
 
            return true;  // Grid exists, thus query was success
        }
    } else {
        auto grid_ptr = grids_.find(key);
        if (grid_ptr != grids_.end()) {
            
            int x_cell = di.x1/(di.x2-di.x1+1);
            int y_cell = di.y1/(di.y2-di.y1+1);

        
            // Check if the actual sketch is initialized, if it isn't then the value is simply zero
            if (grid_ptr->second->cells[x_cell][y_cell] != NULL) {
                std::cout << "Now querying grid with key: " << key << " that has dims: " << KeyToDimString(key) << 
                    " in position: [" << x_cell << "," << y_cell << "]"<< " for item: " << item <<std::endl;
                if (sketch_name_ == "CM" || sketch_name_ == "CML2") {
                    if (nr_hashes_ > 0) {
                        query_sum += (int) (di.coverage * grid_ptr->second->cells[x_cell][y_cell]->QueryItem(item, hashes_));
                    } else {
                        query_sum += (int) (di.coverage * grid_ptr->second->cells[x_cell][y_cell]->QueryItem(item));
                    }
                } else if (sketch_name_ == "dyadicCM") {
                    query_sum += (int) (di.coverage * grid_ptr->second->cells[x_cell][y_cell]->Query(dyadic1D(item, item_end))); 
                } else if (sketch_name_ == "ECM") {
                    if (nr_hashes_ > 0) {
                        query_sum += (int) (di.coverage * grid_ptr->second->cells[x_cell][y_cell]->QueryItem(item, timestamp, hashes_long_));
                    } else {
                        query_sum += (int) (di.coverage * grid_ptr->second->cells[x_cell][y_cell]->QueryItem(item, timestamp));
                    }
                }
                std::cout << "Query result so far: " << query_sum << std::endl;
            } 
            return true;  // Grid exists, thus query was success
        }
    }
    return false;  // Grid doesn't exist
}


int SpatialSketch::RecurseQueryDyadicInterval(dyadic2D d_interval, long item, long item_end, int &query_sum, int timestamp) {
    dyadic2D di1, di2; // copy
    di1 = di2 = d_interval;
    int x_dim = d_interval.x2 - d_interval.x1 + 1;
    int y_dim = d_interval.y2 - d_interval.y1 + 1;

    // Nothing to break, return
    if (x_dim == resolution_ && y_dim == resolution_) {
        return 0;
    // Otherwise break largest dimension
    } else if (x_dim >= y_dim) {
        di1.x2 = di1.x1 + (x_dim / 2) - 1;
        di2.x1 = di2.x1 + (x_dim / 2);
    } else {
        di1.y2 = di1.y1 + (y_dim / 2) - 1; //(di1.y1 + di1.y2 + 1) / 2 - 1;
        di2.y1 = di2.y1 + (y_dim / 2); // (di1.y1 + di1.y2 + 1) / 2;//
    }

    int subqueries = 0;
    if (!QueryDyadicInterval(di1, item, item_end, query_sum)) {
        subqueries += RecurseQueryDyadicInterval(di1, item, item_end, query_sum, timestamp);
    } else {
        subqueries++;
    }
    if (!QueryDyadicInterval(di2, item, item_end, query_sum)) {
        subqueries += RecurseQueryDyadicInterval(di2, item, item_end, query_sum, timestamp);
    } else {
        subqueries++;
    }
    return subqueries;
}


// Given a set of ranges query the dyadic intervals and return the result
// Note, function kinda sketch specific has to be adapted for different sketches
int SpatialSketch::QueryRanges(std::vector<range> ranges, long item, long item_end, int timestamp) {
    if (sketch_name_ == "FM") {
        return QueryCountDistinct(ranges);
    } else if (sketch_name_ == "BF") {
        if (item < 0) {
            throw "SpatialSketch::QueryRanges: item is negative";
        }
        int mem =  QueryMembership(ranges,item);
        if (mem > 0) {
            return 1;
        } else {
            return 0;
        }
    } else if (sketch_name_ == "ECM_merge") {
        return QueryECMMerge(ranges, item, item_end, timestamp);
    } else {
        return QueryFrequency(ranges, item, item_end, timestamp);
    }
}

int SpatialSketch::QueryFrequency(std::vector<range> ranges, long item, long item_end, int timestamp) {
    int sum = 0, subqueries = 0;
    std::pair<int, int> index;
    std::vector<dyadic2D> dyadic_intervals;
    dyadic_intervals.reserve(levels_*levels_);

    if (sketch_name_.find(std::string("ECM")) != std::string::npos) {
        nr_hashes_ = sketch_->GetItemHashes(item, hashes_long_);
    } else if (sketch_name_ == "ElasticSketch") {
        nr_hashes_ = 0;
    } else {
        nr_hashes_ = sketch_->GetItemHashes(item, hashes_);
    }

    // Query the sketch of every dyadic interval and accumulate the sum
    int count = 0;
    for (range r : ranges) {
        dyadic_intervals = GetDyadicIntervals(r.x1, r.y1, r.x2, r.y2);
        for (dyadic2D di : dyadic_intervals) {
            di.x1--;
            di.x2--;
            di.y1--;
            di.y2--;
            // Query 1 cell dyadic intervals on the grid, larger intervals via the hash map
            bool query_success = QueryDyadicInterval(di, item, item_end, sum, timestamp);

            // If query was not success due to the grid not existing, the current interval has to be broken up again
            if (!query_success) {
                subqueries += RecurseQueryDyadicInterval(di, item, item_end, sum, timestamp); 
            } else {
                subqueries++;
            }
        }
        count++;
    }

    nr_hashes_ = 0;  // reset for next query/update

    return sum;
}

bool SpatialSketch::QueryDyadicIntervalCountDistinct(dyadic2D di, FM &merged_fm) {
    int key = DimToKey(n_ / (di.x2 - di.x1 + 1), n_ / (di.y2 - di.y1 + 1));
    auto grid_ptr = grids_.find(key);
    if (grid_ptr != grids_.end()) {
        
        int x_cell = di.x1/(di.x2-di.x1+1);
        int y_cell = di.y1/(di.y2-di.y1+1);
        // Check if the actual sketch is initialized, if it isn't then the value is simply zero
        if (grid_ptr->second->cells[x_cell][y_cell] != NULL) {
            //std::cout << grid_ptr->second->cells[x_cell][y_cell] << std::endl;
            MergeFM(merged_fm, grid_ptr->second->cells[x_cell][y_cell]);
        }
        return true;
    }
    return false;
}

int SpatialSketch::RecurseQueryDyadicIntervalCountDistinct(dyadic2D d_interval, FM &merged_fm) {
    dyadic2D di1, di2; // copy
    di1 = di2 = d_interval;
    int x_dim = d_interval.x2 - d_interval.x1 + 1;
    int y_dim = d_interval.y2 - d_interval.y1 + 1;

    // Nothing to break, return
    if (x_dim == resolution_ && y_dim == resolution_) {
        return 0;
    // Otherwise break largest dimension
    } else if (x_dim >= y_dim) {
        di1.x2 = di1.x1 + (x_dim / 2) - 1;
        di2.x1 = di2.x1 + (x_dim / 2);
    } else {
        di1.y2 = di1.y1 + (y_dim / 2) - 1; //(di1.y1 + di1.y2 + 1) / 2 - 1;
        di2.y1 = di2.y1 + (y_dim / 2); // (di1.y1 + di1.y2 + 1) / 2;//
    }

    int subqueries = 0;
    if (!QueryDyadicIntervalCountDistinct(di1, merged_fm)) {
        subqueries += RecurseQueryDyadicIntervalCountDistinct(di1,  merged_fm);
    } else {
        subqueries++;
    }
    if (!QueryDyadicIntervalCountDistinct(di2,  merged_fm)) {
        subqueries += RecurseQueryDyadicIntervalCountDistinct(di2, merged_fm);
    } else {
        subqueries++;
    }
    return subqueries;
}

int SpatialSketch::QueryCountDistinct(std::vector<range> ranges) {
    FM merged_fm = FM(epsilon_, delta_, hash_coeffs_long_);
    nr_hashes_ = 0; //merged_fm->GetItemHashes(item, hashes_long_);

    int est_distinct_ips= 0, subqueries = 0;
    std::pair<int, int> index;
    std::vector<dyadic2D> dyadic_intervals;
    dyadic_intervals.reserve(levels_*levels_);


    // Query the sketch of every dyadic interval and accumulate the sum
    int count = 0;

    // Merge all FM sketches into one.
    for (range r : ranges) {
        std::vector<dyadic2D> dyadic_intervals = GetDyadicIntervals(r.x1, r.y1, r.x2, r.y2);
        for (dyadic2D di : dyadic_intervals) {
            di.x1--;
            di.x2--;
            di.y1--;
            di.y2--;
            // Query 1 cell dyadic intervals on the grid, larger intervals via the hash map
            bool query_success = QueryDyadicIntervalCountDistinct(di, merged_fm);
            /*int key = DimToKey(n_ / (di.x2 - di.x1 + 1), n_ / (di.y2 - di.y1 + 1));
            auto grid_ptr = grids_.find(key);
            if (grid_ptr != grids_.end()) {
                int x_cell = di.x1/(di.x2-di.x1+1);
                int y_cell = di.y1/(di.y2-di.y1+1);
                // Check if the actual sketch is initialized, if it isn't then the value is simply zero
                if (grid_ptr->second->cells[x_cell][y_cell] != NULL) {
                    //std::cout << grid_ptr->second->cells[x_cell][y_cell] << std::endl;
                    merged_fm = MergeFM(merged_fm, grid_ptr->second->cells[x_cell][y_cell]);
                }
            } else {*/
            if (!query_success) {
                subqueries += RecurseQueryDyadicIntervalCountDistinct(di, merged_fm); 
            }
            subqueries++;
        }
        count++;
    }
    
    nr_hashes_ = 0;  // reset for next query/update

    est_distinct_ips= merged_fm.QueryItem(0);
    //std::cout << "Estimate distinct ips: " << est_distinct_ips << std::endl;
    return est_distinct_ips;
}



bool SpatialSketch::QueryDyadicIntervalMembership(dyadic2D di, long item, int &query_sum) {
    int key = DimToKey(n_ / (di.x2 - di.x1 + 1), n_ / (di.y2 - di.y1 + 1));
    auto grid_ptr = grids_.find(key);
    if (grid_ptr != grids_.end()) {
        
        int x_cell = di.x1/(di.x2-di.x1+1);
        int y_cell = di.y1/(di.y2-di.y1+1);
        // Check if the actual sketch is initialized, if it isn't then the value is simply zero
        if (grid_ptr->second->cells[x_cell][y_cell] != NULL) {
            query_sum += (int) (grid_ptr->second->cells[x_cell][y_cell]->QueryItem(item));
        }
        return true;
    }
    return false;
}


int SpatialSketch::RecurseQueryDyadicIntervalMembership(dyadic2D d_interval, long item, int &query_sum) {
    dyadic2D di1, di2; // copy
    di1 = di2 = d_interval;
    int x_dim = d_interval.x2 - d_interval.x1 + 1;
    int y_dim = d_interval.y2 - d_interval.y1 + 1;

    // Nothing to break, return
    if (x_dim == resolution_ && y_dim == resolution_) {
        return 0;
    // Otherwise break largest dimension
    } else if (x_dim >= y_dim) {
        di1.x2 = di1.x1 + (x_dim / 2) - 1;
        di2.x1 = di2.x1 + (x_dim / 2);
    } else {
        di1.y2 = di1.y1 + (y_dim / 2) - 1; //(di1.y1 + di1.y2 + 1) / 2 - 1;
        di2.y1 = di2.y1 + (y_dim / 2); // (di1.y1 + di1.y2 + 1) / 2;//
    }

    int subqueries = 0;
    if (!QueryDyadicIntervalMembership(di1, item, query_sum)) {
        subqueries += RecurseQueryDyadicIntervalMembership(di1, item, query_sum);
    } else {
        subqueries++;
    }
    if (query_sum > 0) {
        return subqueries;
    }
    if (!QueryDyadicIntervalMembership(di2, item, query_sum)) {
        subqueries += RecurseQueryDyadicIntervalMembership(di2, item, query_sum);
    } else {
        subqueries++;
    }
    return subqueries;
}

int SpatialSketch::QueryMembership(std::vector<range> ranges, long item) {
    int sum = 0, subqueries = 0;
    std::pair<int, int> index;
    std::vector<dyadic2D> dyadic_intervals;
    dyadic_intervals.reserve(levels_*levels_);

    nr_hashes_ = sketch_->repetitions_; //GetItemHashes(item, hashes_);

    // Query the sketch of every dyadic interval and accumulate the sum
    int count = 0;
    for (range r : ranges) {
        std::vector<dyadic2D> dyadic_intervals = GetDyadicIntervals(r.x1, r.y1, r.x2, r.y2);
        for (dyadic2D di : dyadic_intervals) {
            di.x1--;
            di.x2--;
            di.y1--;
            di.y2--;
            // Query 1 cell dyadic intervals on the grid, larger intervals via the hash map
            bool query_success = QueryDyadicIntervalMembership(di, item, sum);
            if (sum > 0) {
                return sum;
            }
            // If query was not success due to the grid not existing, the current interval has to be broken up again
            if (!query_success) {
                //throw "SpatialSketch::QueryMembership: this is not possible with this config.";
                subqueries += RecurseQueryDyadicIntervalMembership(di, item, sum); 
            } else {
                subqueries++;
            }
            if (sum > 0) {
                return sum;
            }
        }
        count++;
    }

    nr_hashes_ = 0;  // reset for next query/update

    return sum;
}


bool SpatialSketch::QueryDyadicInterval(dyadic2D di, CountMin &cm) {
    /*std::string*/ int key = DimToKey(n_ / (di.x2 - di.x1 + 1), n_ / (di.y2 - di.y1 + 1));
    auto grid_ptr = grids_.find(key);
    if (grid_ptr != grids_.end()) {
        
        int x_cell = di.x1/(di.x2-di.x1+1);
        int y_cell = di.y1/(di.y2-di.y1+1);
        // Check if the actual sketch is initialized, if it isn't then the value is simply zero
        if (grid_ptr->second->cells[x_cell][y_cell] != NULL) {
            cm.Merge(grid_ptr->second->cells[x_cell][y_cell]);
        } 
        return true;  // Grid exists, thus query was success
    }
    return false;  // Grid doesn't exist
}


int SpatialSketch::RecurseQueryDyadicInterval(dyadic2D d_interval, CountMin &cm) {
    dyadic2D di1, di2; // copy
    di1 = di2 = d_interval;
    int x_dim = d_interval.x2 - d_interval.x1 + 1;
    int y_dim = d_interval.y2 - d_interval.y1 + 1;

    // Nothing to break, return
    if (x_dim == resolution_ && y_dim == resolution_) {
        return 0;
    // Otherwise break largest dimension
    } else if (x_dim >= y_dim) {
        di1.x2 = di1.x1 + (x_dim / 2) - 1;
        di2.x1 = di2.x1 + (x_dim / 2);
    } else {
        di1.y2 = di1.y1 + (y_dim / 2) - 1; //(di1.y1 + di1.y2 + 1) / 2 - 1;
        di2.y1 = di2.y1 + (y_dim / 2); // (di1.y1 + di1.y2 + 1) / 2;//
    }

    int subqueries = 0;
    if (!QueryDyadicInterval(di1, cm)) {
        subqueries += RecurseQueryDyadicInterval(di1, cm);
    } else {
        subqueries++;
    }
    if (!QueryDyadicInterval(di2, cm)) {
        subqueries += RecurseQueryDyadicInterval(di2, cm);
    } else {
        subqueries++;
    }
    return subqueries;
}


long SpatialSketch::QueryRangesL2(std::vector<range> ranges) {
    int subqueries = 0;
    std::pair<int, int> index;
    std::vector<dyadic2D> dyadic_intervals;
    dyadic_intervals.reserve(levels_*levels_);

    CountMin merged_cm = CountMin(epsilon_, delta_, hash_coeffs_);

    // Query the sketch of every dyadic interval and accumulate the sum
    int count = 0;
    for (range r : ranges) {
        dyadic_intervals = GetDyadicIntervals(r.x1, r.y1, r.x2, r.y2);
        for (dyadic2D di : dyadic_intervals) {
            di.x1--;
            di.x2--;
            di.y1--;
            di.y2--;
            // Query 1 cell dyadic intervals on the grid, larger intervals via the hash map
            bool query_success = QueryDyadicInterval(di, merged_cm);

            // If query was not success due to the grid not existing, the current interval has to be broken up again
            if (!query_success) {
                subqueries += RecurseQueryDyadicInterval(di, merged_cm); 
            } else {
                subqueries++;
            }
        }
        count++;
    }

    return merged_cm.L2Estimate();
}

 int SpatialSketch::QueryECMMerge(std::vector<range> ranges, long item, long item_end, int timestamp) {
    std::pair<int, int> index;
    std::vector<dyadic2D> dyadic_intervals;
    dyadic_intervals.reserve(levels_*levels_);

    // Keep vector of hists per repitition
    std::vector<ExpHist> hists[sketch_->repetitions_];
    for (int i = 0; i < sketch_->repetitions_; i++) {
        hists[i] = {};
    }

    nr_hashes_ = sketch_->GetItemHashes(item, hashes_long_);

    // Query the sketch of every dyadic interval and accumulate the sum
    int count = 0;
    for (range r : ranges) {
        dyadic_intervals = GetDyadicIntervals(r.x1, r.y1, r.x2, r.y2);
        for (dyadic2D di : dyadic_intervals) {
            di.x1--;
            di.x2--;
            di.y1--;
            di.y2--;

            bool query_success = QueryDyadicIntervalECM(di, hists, item, item_end);
            // If query was not success due to the grid not existing, the current interval has to be broken up again
            if (!query_success) {
                RecurseQueryDyadicIntervalECM(di, hists, item, item_end); 
            }
        }
        count++;
    }

    // Compute sum
    int sum = INT_MAX;
    int k_ = std::ceil(1.0f/epsilon_);
    for (int i = 0; i < sketch_->repetitions_; i++) {
        ExpHist merged = MergeECM(hists[i], k_);
        int temp_sum = HistSum(merged, timestamp);
        if (temp_sum < sum) {
            sum = temp_sum;
        }
    }

    nr_hashes_ = 0;  // reset for next query/update

    return sum;
 }



bool SpatialSketch::QueryDyadicIntervalECM(dyadic2D di, std::vector<ExpHist> *hists, long item, long item_end) {
    int key = DimToKey(n_ / (di.x2 - di.x1 + 1), n_ / (di.y2 - di.y1 + 1));
    auto grid_ptr = grids_.find(key);
    if (grid_ptr != grids_.end()) {
        
        int x_cell = di.x1/(di.x2-di.x1+1);
        int y_cell = di.y1/(di.y2-di.y1+1);
        // Check if the actual sketch is initialized, if it isn't then the value is simply zero
        if (grid_ptr->second->cells[x_cell][y_cell] != NULL) {
            std::vector<ExpHist> rep_hists = dynamic_cast<ECM*>(grid_ptr->second->cells[x_cell][y_cell])->GetHists(item, hashes_long_);
            for (int i = 0; i < (int) rep_hists.size(); i++) {
                hists[i].push_back(rep_hists[i]);
            }            
        } 
        return true;  // Grid exists, thus query was success
    }
    return false;  // Grid doesn't exist
}

int SpatialSketch::RecurseQueryDyadicIntervalECM(dyadic2D d_interval, std::vector<ExpHist> *hists, long item, long item_end) {
    dyadic2D di1, di2; // copy
    di1 = di2 = d_interval;
    int x_dim = d_interval.x2 - d_interval.x1 + 1;
    int y_dim = d_interval.y2 - d_interval.y1 + 1;

    // Nothing to break, return
    if (x_dim == resolution_ && y_dim == resolution_) {
        return 0;
    // Otherwise break largest dimension
    } else if (x_dim >= y_dim) {
        di1.x2 = di1.x1 + (x_dim / 2) - 1;
        di2.x1 = di2.x1 + (x_dim / 2);
    } else {
        di1.y2 = di1.y1 + (y_dim / 2) - 1; //(di1.y1 + di1.y2 + 1) / 2 - 1;
        di2.y1 = di2.y1 + (y_dim / 2); // (di1.y1 + di1.y2 + 1) / 2;//
    }

    int subqueries = 0;
    if (!QueryDyadicIntervalECM(di1, hists, item, item_end)) {
        subqueries += RecurseQueryDyadicIntervalECM(di1, hists, item, item_end);
    } else {
        subqueries++;
    }
    if (!QueryDyadicIntervalECM(di2, hists, item, item_end)) {
        subqueries += RecurseQueryDyadicIntervalECM(di2, hists, item, item_end);
    } else {
        subqueries++;
    }
    return subqueries;
}