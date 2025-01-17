#include "Partitioner.h"
#include "Utils.h"
#include "Statistics.h"
#include "Postgres.h"
#include "SpatialSketch.h"
#include "ReservoirSampling.h"
#include "MultiDimCM.h"
#ifdef MARQ
#include "marq.h"
#endif

#include <filesystem>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <tuple>


using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;

//#define NO_OFFSET  // define this if shapes should not be offset

// Parameters in same order they are passed via command line
int N = 4096;  // grid size
int query_method = 5;  // 4: postgres, 5: spatial sketch, 6: MARQ, 7: reservoir sampling, 8: 3D CM sketch
int query_sample_size = 1;  // Number of time to perform the same query to increase the sample size and get a better average
int partition_sample_size = 1;  // number of time rectangle partitioning is performed 
int query_pos_sample_size = 100; // number of time same shape is put in different positions and queried, queries per file therefore is query_sample_size * query_pos_sample_size
//std::string folder = cluster_folder;
std::string folder = "../";

std::string region_data_dir = "../experiments/data_in/RegionData/squares/N4096";
std::string ip_data =  "../experiments/data_in/GeoCaida/GeoCaidaN4096L10M.csv"; //GeoCaidaN4096L1M.csv";  // if empty (""), fixed data is inserted for naive, fenwick, dyadic (not postgres)
int postgres_index = 0;  // 0: no index, 1: btree [x,y], 2: btree [ip,x,y], 3: gist [x,y] box, 4: gist [x,y] polygon, 5: spgist [x,y] box, 6: spgist [x,y] polygon
std::string sketch_name = "ElasticSketch";  // CM, dyadicCM, BF, FM, CML2, ECM, ECM_merge, ElasticSketch
long memory_limit = 38797312;
int max_insertions = -1;
bool range_queries = false;

// General sketch parameters
float epsilon = 0.1f;
float delta = 0.05f;
float theta = 0.95f;
int domain_size = 400000;

// MARQ parameters
int marq_insertions = 100*100*1000;
int surplus_qmethod = 2; // 0: one cm query, 1: all cold cells via cm, 2: all cells exact
int marq_insertion_method = 0;  // 0: stream, 1: bulk

bool subtractive_querying = false;  // most subtractive querying removed from experiments

// Other variables
std::string experiments_dir = folder + "repository/experiments/data_out/";  // where to log statistics

// Partition failure handling
int fail_count = 0;
bool log_failed_files = false;
bool remove_failed_files = false;

int ip_limit = -1;
bool force_min_query_answer = true;
int min_query_answer = -1;  // todo set

// Query generation
bool queries_from_file = true; // cached queries
bool log_generate_queries = false; // write queries to file
bool insert_from_file = false; // benchmarking ingestion time: set to true. Otherwise, set to false and insert from memory
bool align_queries = false; // align queries to data structure resolution
int query_resolution = -1; // Option to align queries to some resolution, if not set query_resolution = N

int main(int argc, char* argv[]) {
    if (argc < 8) {
        std::cout << "\nArguments (" << argc << ") not as expected, using default values:" << std::endl;
    } else {
        // Current argument setup:
        N = atoi(argv[1]);
        query_method = atoi(argv[2]);
        query_sample_size = atoi(argv[3]);
        query_pos_sample_size = atoi(argv[4]);
        region_data_dir = "../experiments/data_in/" + std::string(argv[5]);
        ip_data = std::string(argv[6]);
        std::cout << "\nParsed " << argc - 1 << " arguments, running experiments with following settings:" << std::endl;
        if (argc > 7) {
            postgres_index = atoi(argv[7]);
        }
        if (argc > 8) {
            int query_or_generate = atoi(argv[8]);
            if (query_or_generate == 0) {
                queries_from_file = false;
                log_generate_queries = false;
            } else if (query_or_generate == 1) {
                queries_from_file = true;
                log_generate_queries = false;
            } else if (query_or_generate == 2) {
                queries_from_file = false;
                log_generate_queries = true;
            }
        } 
        if (argc > 9) {
            memory_limit = atol(argv[9]);
        }
        if (argc > 10) {
            range_queries = (bool) atoi(argv[10]);
        }
        if (argc > 11) {
            epsilon = atof(argv[11]);
        }
        if (argc > 12) {
            sketch_name = argv[12];
        }
        if (argc > 13) {
            domain_size = atoi(argv[13]);
        }
        if (argc > 14) {
            delta = atof(argv[14]);
        }
    }
    std::cout << "N\t" << N 
                << "\nquery_method\t" << query_method
                << "\nquery_sample_size\t" << query_sample_size
                << "\nquery_pos_sample_size\t" << query_pos_sample_size
                << "\nregion data folder\t" << region_data_dir 
                << "\nip data folder\t" << ip_data 
                << "\npostgres index\t" << postgres_index 
                << "\nquery from file\t" << (int) queries_from_file  << " or generate " << (int) log_generate_queries 
                << "\nmemory limit\t" << memory_limit 
                << "\nrange queries\t" << range_queries 
                << "\nepsilon " << epsilon << ", delta " << delta << ", theta " << theta << ", domain size " << domain_size
                << "\ndomain size " << domain_size
                << "\nspatialsketch sketch " << sketch_name 
                << "\ninsert from file " << insert_from_file 
                << "\nsurplus method " << surplus_qmethod << std::endl << std::endl;

    // Parameter checks
    if (sketch_name == "dyadicCM" && !range_queries) {
        std::cout << "dyadicCM only supports range queries, assuming range queries" << std::endl;
        range_queries = true;
    }
    if (sketch_name == "CM" && range_queries) {
        std::cout << "CM does not support range queries, assuming point queries" << std::endl;
        range_queries = false;
    }

    if (query_resolution == -1) {
        query_resolution = N;
        std::cout << "Query resolution not set, so assuming N = " << N << std::endl;
    }

    // Timing 
    timespec timesp, subtimesp;
    time_t start_time;
    clock_gettime(CLOCK_REALTIME, &timesp);
    start_time = timesp.tv_sec * 1000 + timesp.tv_nsec / 1000000;

    std::string method_name;
    if (query_method == 4) {
        method_name = "postgres" + std::to_string(postgres_index);
    } else if (query_method == 5) {
        method_name = "spatialsketch" + sketch_name;
    } else if (query_method == 6) {
        method_name = "MARQ";
    } else if (query_method == 7) {
        method_name = "reservoir" + sketch_name;
    } else if (query_method == 8) {
        method_name = "MDsketch";
    }

    // time var
    time_t before_time, temp_time, before_subtime;

// --------- Initialization of data structures and insertion of data ---------
    Postgres *pg;
    SpatialSketch *sp;
    RSampling *rs;
    MultiDimCM *mdcm;
#ifdef MARQ
    MARQ *marq;
#endif

    if (query_method == 4) {
        pg = new Postgres(ip_data, postgres_index);  // setup postgres without bulk insert
    } else if (query_method == 5) {
        sp = new SpatialSketch(sketch_name, N, memory_limit, epsilon, delta, domain_size);  // todo: pass memory limit
    } else if (query_method == 6) {
#ifdef MARQ
        int* upper_limits = new int[3];
        int* lower_limits = new int[3];
        // Limits are long [0, N-1], lat [0, N-1], ip [0, 2^32-1] but converted to int
        upper_limits[0] = N - 1; upper_limits[1] = N - 1; upper_limits[2] = INT_MAX;
        lower_limits[0] = 0; lower_limits[1] = 0; lower_limits[2] = INT_MIN;
        if (max_insertions > 0) {
            marq_insertions = max_insertions;
        }
        marq = new MARQ(3, marq_insertions, upper_limits, lower_limits, epsilon, delta, theta, surplus_qmethod, align_queries);
#endif
    } else if (query_method == 7) {
        rs = new RSampling(epsilon, delta, memory_limit);
    } else if (query_method == 8) {
        mdcm = new MultiDimCM(N, epsilon, delta, memory_limit);
    }


// -------- Reading of IP data from file --------
    // Open data file, as data will be inserted immediately
    std::ifstream* data_file = 0;
    int insertion_count = 0;
    std::vector<input_data> data_array;
    if (ip_data != "" && query_method != 4) {
        data_file = new std::ifstream(ip_data, std::ios::in);
        if (!data_file->is_open()) {
            std::cout << "Error opening data file: " << ip_data << std::endl;
            return 1;
        } else {           
            // Beuned data reading
            std::string line, cell;
            std::getline(*data_file,line); // header

            std::cout << "Opened data file: " << ip_data << std::endl;
            long timestamp = 0;
            while (std::getline(*data_file, line)) {
                if (max_insertions > -1 && insertion_count >= max_insertions) {
                    std::cout << "Max insertions of " << max_insertions << " reached" << std::endl;
                    break;
                }
                timestamp++;
                std::stringstream linestream(line);
                input_data tup;
                for (int i = 0; i < 6; i++) {
                    std::getline(linestream, cell, ',');
                    // TODO: This depends on the order of the columns in the dataset, therefore not ideal
                    // Current GeoCaide orders it as follows [timestamp, int_ip, long, lat, points]
                    if (i == 0) tup.timestamp = std::stol(cell); // timestamp
                    if (i == 1) tup.ip = std::stol(cell);  // int_ip (can be 2^32, thus uint/long)
                                                                           // We cast it to int as current fucntion support this, that does imply negative items can occur, but this should not change any result.
                    if (i == 2) tup.x = std::stoi(cell);  // long
                    if (i == 3) tup.y = std::stoi(cell);  // lat
                }

                 // If from file, skip insertions to later
                if (!insert_from_file) {
                    //data.push_back(tup);
                    data_array.push_back(tup);
                    insertion_count++;
                    continue;
                }

                if (query_method == 5) {
                    sp->Update(tup.x, tup.y, tup.ip, 1);
                } else if (query_method == 6) {
#ifdef MARQ
                    if (marq_insertion_method == 0) {
                        marq->update({ tup.x, tup.y,  UnsignedIPToSigned(tup.ip)});  // Offset uint ip to fit in integer
                    } else if (marq_insertion_method == 1) {
                        marq->updateBulk({ tup.x, tup.y,  UnsignedIPToSigned(tup.ip)});  // Offset uint ip to fit in integer
                    }
#endif
                } else if (query_method == 7) {
                    rs->insert(tup);
                } else if (query_method == 8) {
                    mdcm->Update(tup.x, tup.y, tup.ip, 1);
                }
                insertion_count++;
            }
            std::cout << "\nInsertion count " << insertion_count << std::endl;
            if (query_method == 6 && marq_insertion_method == 1) {
#ifdef MARQ
                marq->finalizeBulk();
#endif
            }
        }
    }

    if (query_method != 4 && !queries_from_file) {
        pg = new Postgres(ip_data, postgres_index); 
    }

    // Data inserted, so close file and clear temporary vars
    if (data_file) {
        data_file->close();
        delete data_file;
    }

/*========================== Main memory insertion ===========================*/

    int log_interval = 100000;
    std::vector<time_t> sub_times;
    sub_times.reserve((int) std::ceil(insertion_count / log_interval));
    if (!insert_from_file) {
        // Create stats file
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        std::ostringstream oss2;
        oss2 << experiments_dir << "updates_" << method_name << "_" << std::to_string(N) << "_";
        oss2 << std::put_time(&tm, "%Y_%m_%d %H_%M_%S") << ".csv";
        UpdateStatisticsWriter* insert_writer = new UpdateStatisticsWriter(oss2.str());

        std::cout << "Insertion from main memory start" << std::endl;
        if (query_method == 5) {
            clock_gettime(CLOCK_REALTIME, &timesp);
            clock_gettime(CLOCK_REALTIME, &subtimesp);
            before_time = timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000;
            before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;

            /*for (auto tup : data) {
                sp->Update(std::get<2>(tup), std::get<3>(tup), std::get<1>(tup), 1);
            }*/
            
            for (int i = 0; i < insertion_count; i++) {
                sp->Update(data_array[i].x, data_array[i].y, data_array[i].ip, 1);
                if ((i+1) % log_interval == 0) {
                    clock_gettime(CLOCK_REALTIME, &subtimesp);
                    sub_times.push_back(((subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000) - before_subtime) / 1000);
                    before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;
                }
            }
            
            clock_gettime(CLOCK_REALTIME, &timesp);
            auto mem = ScaleMemory(sp->GetSize());
            insert_writer->WriteInsertTime(((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000, std::to_string(mem.first) + mem.second, sub_times);
            std::cout << "SpatialSketch insertion time: " << ((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000000.0f << " seconds" << std::endl;
            if (memory_limit > 0) {
                sp->PrintCoverage();
            }
        } else if (query_method == 6) {
#ifdef MARQ
            clock_gettime(CLOCK_REALTIME, &timesp);
            clock_gettime(CLOCK_REALTIME, &subtimesp);
            before_time = timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000;
            before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;

            for (int i = 0; i < insertion_count; i++) {
                marq->update({ data_array[i].x, data_array[i].y,  UnsignedIPToSigned(data_array[i].ip)});  // Offset uint ip to fit in integer

                if ((i+1) % log_interval == 0) {
                    clock_gettime(CLOCK_REALTIME, &subtimesp);
                    sub_times.push_back(((subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000) - before_subtime) / 1000);
                    before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;
                }
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            auto mem = ScaleMemory(marq->GetSize());
            insert_writer->WriteInsertTime(((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000, std::to_string(mem.first) + mem.second, sub_times);
            std::cout << "MARQ insertion time: " << ((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000000.0f << " seconds" << std::endl;
#endif
        } else if (query_method == 7) {
            clock_gettime(CLOCK_REALTIME, &timesp);
            clock_gettime(CLOCK_REALTIME, &subtimesp);
            before_time = timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000;
            before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;


            for (int i = 0; i < insertion_count; i++) {
                rs->insert(data_array[i]);
                if ((i+1) % 100000 == 0) {
                    clock_gettime(CLOCK_REALTIME, &subtimesp);
                    sub_times.push_back(((subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000) - before_subtime) / 1000);
                    before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;
                }
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            auto mem = ScaleMemory(rs->getSize());
            insert_writer->WriteInsertTime(((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000, std::to_string(mem.first) + mem.second, sub_times);
            std::cout << "Reservoir insertion time: " << ((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000000.0f << " seconds" << std::endl;
        } else if (query_method == 8) {
            clock_gettime(CLOCK_REALTIME, &timesp);
            clock_gettime(CLOCK_REALTIME, &subtimesp);
            before_time = timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000;
            before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;


            for (int i = 0; i < insertion_count; i++) {
                mdcm->Update(data_array[i].x, data_array[i].y, data_array[i].ip, 1);
                if ((i+1) % 100000 == 0) {
                    clock_gettime(CLOCK_REALTIME, &subtimesp);
                    sub_times.push_back(((subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000) - before_subtime) / 1000);
                    before_subtime = subtimesp.tv_sec * 1000000 + subtimesp.tv_nsec / 1000;
                }
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            auto mem = ScaleMemory(mdcm->GetSize());
            insert_writer->WriteInsertTime(((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000, std::to_string(mem.first) + mem.second, sub_times);
            std::cout << "MDSketch insertion time: " << ((timesp.tv_sec * 1000000 + timesp.tv_nsec / 1000) - before_time) / 1000000.0f << " seconds" << std::endl;
        }
        delete insert_writer;
    }

    clock_gettime(CLOCK_REALTIME, &timesp);
    std::cout << "Setup and data insertion time: " << ((timesp.tv_sec * 1000 + timesp.tv_nsec / 1000000) - start_time)  << "ms" << std::endl;

/*========================================= Memory Indiciation =======================================*/

    std::pair<float, std::string> memory_unit = std::make_pair(-1, "");
    if (query_method == 5) {
        memory_unit = ScaleMemory((float) sp->GetSize());
        sp->PrintCoverage();
    } else if (query_method == 6) {
#ifdef MARQ
        memory_unit = ScaleMemory((float) marq->GetSize());
#endif
    } else if (query_method == 7) {
        memory_unit = ScaleMemory((float) rs->getSize());
    } else if (query_method == 8) {
        memory_unit = ScaleMemory((float) mdcm->GetSize());
    }

    if (memory_unit.first != -1) {
        std::cout << "\nMemory usage of query method " << query_method << ": " << memory_unit.first << " " << memory_unit.second << std::endl << std::endl;
    }
    
/*========================================== experiment start ==========================================*/

    // Create statistics file
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << experiments_dir << "statistics_" << method_name << "_" << std::put_time(&tm, "%Y_%m_%d %H_%M_%S") << ".csv";
    StatisticsWriter* stats_writer = new StatisticsWriter(oss.str());

    // If log_failed_files is defined, open file to log failed files
    std::ofstream failed_files;
    if (log_failed_files) {
        oss.str("");
        oss << experiments_dir << "failed_files_" << std::put_time(&tm, "%Y_%m_%d %H_%M_%S");
        failed_files.open(oss.str());
        std::cout << "Created file " << oss.str() << std::endl;
    }

    // Loop over region data files
    int file_count = 0;
    shape_info shape_info = {};
    for (const auto& entry : recursive_directory_iterator(region_data_dir)) {
        std::string file_name = entry.path().string();

        if (ParseShapeFile(file_name, shape_info, subtractive_querying)) {
            file_count++;
            std::cout << "Processed file " << file_count << ": " << file_name << std::endl;
        }

        // Check data is complete, coordinates are optional
        if (shape_info.grid_size == -1 || shape_info.selection_size == -1 || shape_info.shape == "" || shape_info.vertices.size() == 0) {
            std::cout << "Error: missing data in file " << file_name << std::endl;
            return 1;
        } else if (shape_info.grid_size != N) {
            std::cout << "Initialized grid size " << N << " is not equal to file  specified grid size " << shape_info.grid_size << std::endl;
            return 1;
        }


// -------- Processing of query and collecting statistics --------
        
        float runtime;

        std::vector<range> add_query_ranges, sub_query_ranges;
        float partition_time = 0;
        int nr_of_add_rectangles = 0, nr_of_sub_rectangles = 0;
        int item_to_query = 0;

        // Partition polygon into rectangles, not required for query_method 0: naive
        if (query_method != 0) {
            // -- Partition vertices into rectangles ------
            std::vector<rect> add_rects, sub_rects;

            std::vector<float> samples;
            bool partition_success = true;
            for (int j = 0; j < partition_sample_size; j++) {  // repeat partitioning to get average time
                // we do not measure time of initialization
                Partitioner rp_add = Partitioner(shape_info.vertices);
                Partitioner rp_sub = Partitioner(shape_info.hole_vertices);

                // Check if setup went correct, and don't continue if it didn't
                if (!rp_add.PartitioningSuccess() || !rp_sub.PartitioningSuccess()) {
                    fail_count++;
                    break;
                }

                clock_gettime(CLOCK_REALTIME, &timesp); // measure start time
                before_time = timesp.tv_sec * 1000000000 + timesp.tv_nsec;

                add_rects = rp_add.ComputePartitioning(); // do partitiioning
                sub_rects = rp_sub.ComputePartitioning();

                // Check if partitioning went correct
                if (!rp_add.PartitioningSuccess() || !rp_sub.PartitioningSuccess()) {
                    partition_success = false;
                    fail_count++;
                    if (log_failed_files) {
                        failed_files << file_name << std::endl;
                    }
                    break;
                }

                // get end time calculate difference, before and after in nanoseconds, converted to milliseconds float
                clock_gettime(CLOCK_REALTIME, &timesp);
                samples.push_back(((timesp.tv_sec * 1000000000 + timesp.tv_nsec) - before_time) / 1000000.0f);
            }
            if (!partition_success) {
                std::cout << "Error: partitioning failed, skipping.." << std::endl;
                if (remove_failed_files) {
                    remove(file_name.c_str());
                }
                continue;
            }

            // Compute average partition time and log of rectangles
            int sum_part_time = 0;
            for (auto sample : samples) {
                sum_part_time += sample;
            }
            partition_time = sum_part_time / partition_sample_size;
            nr_of_add_rectangles = add_rects.size();
            nr_of_sub_rectangles = sub_rects.size();

            // compute exact query ranges, rectangles are with coordinates of .5
            for (auto r : add_rects) {
                range rang = RectToRange(r);
                RangeBoundsCheck(rang, N); // make sure range is not out of bounds to prevent exception
                add_query_ranges.push_back(rang);
            }
            for (auto r : sub_rects) {
                range rang = RectToRange(r);
                RangeBoundsCheck(rang, N);
                sub_query_ranges.push_back(rang);
            }
        } else {
            nr_of_add_rectangles = 0;
            nr_of_sub_rectangles = 0;
            partition_time = 0;
        }

        // Early stopping if wrong dataset is passed for marq
        if (query_method == 6 && nr_of_add_rectangles > 1) {
            std::cout << "MARQ only supports one rectangle, skipping ..." << std::endl;
            break;
        }

        // Statistics line per query file, not per sample
        std::shared_ptr<statistics> stats = std::make_shared<statistics>();
        stats->grid_size = shape_info.grid_size;
        stats->selection_size = shape_info.selection_size;
        stats->shape = file_name.substr(file_name.find_last_of("/") + 1);
        stats->nr_of_vertices = shape_info.vertices.size();
        stats->partition_time = partition_time;
        stats->nr_of_rectangles = nr_of_add_rectangles + nr_of_sub_rectangles;
        stats->epsilon = epsilon;
        if (memory_limit > 0) {
            auto mem = ScaleMemory((float) memory_limit);
            stats->memory_limit = std::to_string(mem.first) + mem.second;
        }

        clock_gettime(CLOCK_REALTIME, &timesp);
        before_time = timesp.tv_sec * 1000000000 + timesp.tv_nsec;

        // Obtaining of the query set
        std::vector<QuerySet> query_sets;
        if (queries_from_file) {
            // Try to read corresponding query file
            std::string query_folder;
            query_folder = "/queries/";
            
            std::string qfile_name = entry.path().parent_path().parent_path().string() + query_folder + entry.path().filename().string() + "_queries_sketch=" +sketch_name ;
            if (sketch_name == "ElasticSketch") {
                qfile_name = entry.path().parent_path().parent_path().string() + query_folder + entry.path().filename().string() + "_queries_sketch=" + "CM";
            }
            std::ifstream qfile(qfile_name, std::ios::out);
            if (qfile.is_open()) {
                std::cout << "Using supplied query file: " << qfile_name << std::endl;

                std::string line, cell;
                std::getline(qfile,line); // header

                int qcount = 0;
                while (std::getline(qfile, line)) {
                    if (qcount > query_pos_sample_size) {
                        std::cout << "query_pos_sample size reached from query file\n";
                        break;
                    }
                    std::stringstream linestream(line);
                    QuerySet query = {};
                    for (int i = 0; i < 7; i++) {
                        std::getline(linestream, cell, ',');
                        if (i == 0) query.offset_x = std::stoi(cell);
                        if (i == 1) query.offset_y = std::stoi(cell);
                        if (i == 2) query.item = std::stol(cell);
                        if (i == 3) query.item_end = std::stol(cell);
                        if (i == 4) query.groundtruth = std::stol(cell);
                        if (i == 5) query.L1 = std::stoi(cell);
                        if (i == 6) query.N = std::stoi(cell);
                    }
                    qcount++;

                    std::vector<range> offset_add_ranges = add_query_ranges;
                    std::vector<std::pair<float, float>> offset_vertices = shape_info.vertices;
                    bool within_bounds = true;
                    for (size_t j = 0; j < offset_add_ranges.size(); j++) {
                        offset_add_ranges[j].x1 += query.offset_x;
                        offset_add_ranges[j].x2 += query.offset_x;
                        offset_add_ranges[j].y1 += query.offset_y;
                        offset_add_ranges[j].y2 += query.offset_y;
                        if (!RangeBoundsCheck(offset_add_ranges[j], N)) {
                            within_bounds = false;
                        }
                    }
                    if (!within_bounds) {
                        std::cout << "Query set " << qcount << " out of bounds, skipping\n";
                        continue;
                    }

                    for (size_t j = 0; j < offset_vertices.size(); j++) {
                        offset_vertices[j].first += query.offset_x;
                        offset_vertices[j].second += query.offset_y;
                    }

                    query.ranges = offset_add_ranges;
                    query.vertices = offset_vertices;

                    query_sets.push_back(query);
                }

            } else {
                std::cout << "Could not open query file " << qfile_name << std::endl;
                continue;
            }
        } else {

            // Get query set and ground truth via postgres                
            if (sketch_name == "CM" || sketch_name == "dyadicCM" || sketch_name == "ElasticSketch") {
                query_sets = pg->GetIPRangeQueriesFrequency(add_query_ranges, shape_info.vertices, N, query_resolution,
                                                    shape_info.max_x_offset, shape_info.max_y_offset,
                                                    query_pos_sample_size, range_queries,
                                                    min_query_answer);    
            } else if (sketch_name == "FM") { // method is FM sketch
                query_sets = pg->GetIPRangeQueriesCountDistinct(add_query_ranges, shape_info.vertices, N,
                                                    shape_info.max_x_offset, shape_info.max_y_offset,
                                                    query_pos_sample_size, range_queries,
                                                    min_query_answer);                    
            } else if (sketch_name == "BF") {
                query_sets = pg-> GetIPRangeQueriesMembership(add_query_ranges, shape_info.vertices, N, 
                                                    shape_info.max_x_offset, shape_info.max_y_offset, query_pos_sample_size, 
                                                    range_queries, min_query_answer);            
            } else if (sketch_name == "CML2") {
                query_sets = pg->GetIPRangeQueriesL2(add_query_ranges, shape_info.vertices, N, 
                                                    shape_info.max_x_offset, shape_info.max_y_offset, 
                                                    query_pos_sample_size, min_query_answer);
            } else if (sketch_name.find(std::string("ECM")) != std::string::npos) {
                    query_sets = pg->GetIPRangeQueriesFrequency(add_query_ranges, shape_info.vertices, N, query_resolution,
                                                    shape_info.max_x_offset, shape_info.max_y_offset,
                                                    query_pos_sample_size, range_queries,
                                                    min_query_answer, true);    
            }

            if (log_generate_queries) {
                std::ofstream qfile(std::filesystem::path(region_data_dir).parent_path().string() + "/queries/" + entry.path().filename().string() + "_queries_sketch=" + sketch_name, std::ios::out);
                if (qfile.is_open()) {
                    qfile << "x_offset,y_offset,item,item_end,groundtruth,L1,N\n";
                    for (auto q : query_sets) {
                        qfile << q.offset_x << "," << q.offset_y << "," << q.item << "," << q.item_end << "," << q.groundtruth << "," << q.L1 << "," << q.N << "\n";
                    }
                    qfile.close();
                } else {
                    std::cout << "Error creating queries file: " << experiments_dir + file_name + "_queries skipping" << std::endl;
                }
            }
        }

        clock_gettime(CLOCK_REALTIME, &timesp);
        // time tracked to nanoseconds, but stored as ms
        std::cout << "IPRangeQ time " << ((timesp.tv_sec * 1000000000 + timesp.tv_nsec) - before_time) / 1000000.f << " ms\n";


        // Execute all query sets sequantially and measure their runtime
        // Keep track of the query answers for error tracking
        std::vector<long> query_answers;
        query_answers.reserve(query_sets.size());
        if (query_method == 4) {
            stats->query_method = "postgres_index" + std::to_string(postgres_index);

            float qtime = 0;
            query_answers = pg->QuerySetRangeCountIP(query_sets, postgres_index, qtime, query_sample_size);
            stats->query_time = qtime;

        } else if (query_method == 5) {
            stats->query_method = "spatialsketch_" + sketch_name;
            long answer = -1;

            clock_gettime(CLOCK_REALTIME, &timesp);
            before_time = timesp.tv_sec * 1000000000 + timesp.tv_nsec;

            for (QuerySet q : query_sets) {
                for (int i = 0; i < query_sample_size; i++) {
                    if (sketch_name == "CML2") {
                        answer = sp->QueryRangesL2(q.ranges);
                    } else {
                        answer = sp->QueryRanges(q.ranges, q.item, q.item_end);
                    }
                }
                /*if (answer < q.groundtruth && insertion_count == 100*1000*1000) {
                    std::cout << "Query spatialsketch answer " << answer << " is smaller than groundtruth " << q.groundtruth << std::endl;
                }*/
                query_answers.push_back(answer);
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            // time tracked to nanoseconds, but stored as ms
            stats->query_time = (((timesp.tv_sec * 1000000000 + timesp.tv_nsec) - before_time) / 1000000.f / (float) query_sample_size / (float) query_sets.size());
            if (query_sets[0].ranges.size() > 1 && (postgres_index != 4 && postgres_index != 6)) {
                stats->query_time += stats->partition_time;
            }
            auto mem = ScaleMemory(sp->GetSize());
            stats->memory = std::to_string(mem.first) + mem.second;
            stats->resolution = N / sp->GetResolution();
        } else if (query_method == 6) {
#ifdef MARQ
            stats->query_method = "marq";
            if (align_queries) {
                stats->query_method = "cdarq";
            }
            int answer = -1;

             // Preprocess data to boxes as required by marq
            std::vector<std::pair<std::vector<int>, std::vector<int>>> marq_boxes;  // marq boxes are defined by vector of lower corner coords, and vector of upper corner
            marq_boxes.reserve(query_sets.size());
            for (size_t i = 0; i < query_sets.size(); i++) {
                // TODO: Check +-1 if required
                std::vector<int> ll_corner = {query_sets[i].ranges[0].x1, query_sets[i].ranges[0].y1, UnsignedIPToSigned(query_sets[i].item)};
                std::vector<int> ur_corner = {query_sets[i].ranges[0].x2, query_sets[i].ranges[0].y2, UnsignedIPToSigned(query_sets[i].item_end)};
                marq_boxes.push_back(std::make_pair(ll_corner, ur_corner));
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            before_time = timesp.tv_sec * 1000000000 + timesp.tv_nsec;

            for (int i = 0; i < (int) marq_boxes.size(); i++) {
                for (int j = 0; j < query_sample_size; j++) {
                    answer = marq->countQuery(marq_boxes[i].first, marq_boxes[i].second);
                    if (answer < query_sets[i].groundtruth && insertion_count == 100*1000*1000) {
                        std::cout << "Query " << i << " marq answer " << answer << " is smaller than groundtruth " << query_sets[i].groundtruth << std::endl;
                    }
                }
                query_answers.push_back(answer);
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            // time tracked to nanoseconds, but stored as ms
            stats->query_time = (((timesp.tv_sec * 1000000000 + timesp.tv_nsec) - before_time) / 1000000.f / (float) query_sample_size / (float) query_sets.size());
            auto mem = ScaleMemory(marq->GetSize());
            stats->memory = std::to_string(mem.first) + mem.second;
            stats->resolution = marq->GetResolution();
#endif
        } else if (query_method == 7) {
            stats->query_method = "ReservoirSampling";
            int answer = -1;

            clock_gettime(CLOCK_REALTIME, &timesp);
            before_time = timesp.tv_sec * 1000000000 + timesp.tv_nsec;

            for (QuerySet q : query_sets) {
                if (q.ranges.size() > 1) {
                    std::cout << "Only rectangle queries supported" << std::endl;
                    continue;
                }
                for (int i = 0; i < query_sample_size; i++) {
                    if (sketch_name == "BF") {
                        answer = rs->getMembership(q.ranges[0], q.item);
                    } else if (sketch_name == "FM") {
                        answer = rs->getCountDistinct(q.ranges[0]);
                    } else if (sketch_name == "CML2") {
                        answer = rs->getL2Square(q.ranges[0]);
                    } else {
                        answer = rs->getFrequency(q.ranges[0], q.item, q.item_end);
                    }
                }
                query_answers.push_back(answer);
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            // time tracked to nanoseconds, but stored as ms
            stats->query_time = (((timesp.tv_sec * 1000000000 + timesp.tv_nsec) - before_time) / 1000000.f / (float) query_sample_size / (float) query_sets.size());
        } else if (query_method == 8) {
            stats->query_method = "MDSketch" + sketch_name;
            long answer = -1;

            clock_gettime(CLOCK_REALTIME, &timesp);
            before_time = timesp.tv_sec * 1000000000 + timesp.tv_nsec;

            for (QuerySet q : query_sets) {
                for (int i = 0; i < query_sample_size; i++) {
                    answer = mdcm->Query(q.ranges, q.item, q.item_end);
                }
                query_answers.push_back(answer);
            }

            clock_gettime(CLOCK_REALTIME, &timesp);
            // time tracked to nanoseconds, but stored as ms
            stats->query_time = (((timesp.tv_sec * 1000000000 + timesp.tv_nsec) - before_time) / 1000000.f / (float) query_sample_size / (float) query_sets.size());
            if (query_sets[0].ranges.size() > 1) {
                stats->query_time += stats->partition_time;
            }
            auto mem = ScaleMemory(mdcm->GetSize());
            stats->memory = std::to_string(mem.first) + mem.second;
        } 

        // Add partition time
        if (stats->query_time > 0 && query_sets[0].ranges.size() > 1) {
            stats->query_time += stats->partition_time;
        }

        if (query_answers.size() > 0) {
            stats->query_answer = query_answers[0];
            stats->groundtruth = query_sets[0].groundtruth;
        }

        if (query_sets.size() > 0 && (query_method >= 5)) {
            if (insertion_count != query_sets[0].N) {
                std::cout << "Note, computed error likely wrong, as query sets have N of " << query_sets[0].N << ", but only " << insertion_count << " items inserted" << std::endl;
            }

            std::vector<double> errorsN, errorsL1, errorsRel, errorsL2;
            
            if (sketch_name != "BF") {
                for (size_t i = 0; i < query_sets.size(); i++) {
                    // Comput error per answer in vector
                    errorsRel.push_back(std::abs(query_answers[i] - query_sets[i].groundtruth)/ ((double) query_sets[i].groundtruth));
                    errorsN.push_back(std::abs(query_answers[i] - query_sets[i].groundtruth) / ((double) query_sets[i].N));
                    errorsL1.push_back(std::abs(query_answers[i] - query_sets[i].groundtruth) / ((double) query_sets[i].L1));
                    errorsL2.push_back(std::abs(query_answers[i] - query_sets[i].groundtruth) / ((double) std::pow(query_sets[i].L1, 2)));
                }
                // Compute average
                float sumN = 0, sumL1 = 0, sumRel = 0, sumL2 = 0;
                for (size_t j = 0; j < errorsN.size(); j++) {
                    sumRel += errorsRel[j];
                    sumN += errorsN[j];
                    sumL1 += errorsL1[j];
                    sumL2 += errorsL2[j];
                }
                stats->avg_error_N = sumN / (float) errorsN.size();
                stats->avg_error_L1 = sumL1 / (float) errorsL1.size();
                stats->avg_error_Rel = sumRel / (float) errorsRel.size();
                stats->avg_error_L2 = sumL2 / (float) errorsL2.size();
            } else {
                // Compute FPR and FNR
                int TP = 0, FP = 0, TN = 0, FN = 0;
                for (size_t i = 0; i < query_sets.size(); i++) {
                    // Comput error per answer in vector
                    if (query_sets[i].groundtruth == 1) {
                        // Positive
                        if (query_answers[i] > 0) {
                            TP++;
                        } else {
                            FN++;
                        }
                    } else {
                        // Negative
                        if (query_answers[i] > 0) {
                            FP++;
                        } else {
                            TN++;
                        }
                    }
                }
                std::cout<< "TP: " << TP << ", FP: " << FP << ", TN: " << TN << ", FN: " << FN << std::endl;
                stats->false_positive_rate = (float) FP / (float) (FP + TN);
                stats->false_negative_rate = (float) FN / (float) (FN + TP);
                stats->precision = (float) TP / (float) (TP + FP);
                stats->recall = (float) TP / (float) (TP + FN);
                stats->f1_score = 2 * (stats->precision * stats->recall) / (stats->precision + stats->recall);
            }
        }

        // Write statistics struct to file, note atomic operation if run in parallel
        stats_writer->WriteStatistics(*(stats.get()));
    }

// --- End memory indication
    memory_unit = std::make_pair(-1, "");
    if (query_method == 5) {
        memory_unit = ScaleMemory(sp->GetSize());
    } else if (query_method == 6) {
#ifdef MARQ
        memory_unit = ScaleMemory(marq->GetSize());
        marq->PrintStatistics();
#endif
    } else if (query_method == 7) {
        memory_unit = ScaleMemory(rs->getSize());
    } else if (query_method == 8) {
        memory_unit = ScaleMemory(mdcm->GetSize());
    }

    if (memory_unit.first != -1 && query_method != 4) {
        std::cout << "\nMemory usage of query method " << query_method << ": " << memory_unit.first << " " << memory_unit.second << std::endl << std::endl;
    } else if (query_method == 4) {
        std::cout << "\nMemory usage of query method " << query_method << ": " << pg->GetDBSize() << std::endl;
        std::cout << "With a table of " << pg->GetNrRecords() << " rows " << std::endl << std::endl;
    }
// --- 

    // cleanup
    if ((query_method == 4 || queries_from_file == false)) {
        delete pg;
    } else if (query_method == 5) {
        delete sp;
    } else if (query_method == 6) {
#ifdef MARQ
        delete marq;
#endif
    } else if (query_method == 7) {
        delete rs;
    } else if (query_method == 8) {
        delete mdcm;
    }

    if (log_failed_files) {
        failed_files.close();
    }

    delete stats_writer;

    std::cout << "\nPartitioned " << (file_count - fail_count) << " / " << file_count << " (" << (file_count - fail_count) / (float) file_count << ") successfully" << std::endl;

    return 0;
}
