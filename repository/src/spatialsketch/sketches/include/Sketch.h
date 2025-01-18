#ifndef SKETCH_H_
#define SKETCH_H_

#include <stdlib.h>
#include <vector>
#include <DyadicRanges.h>

typedef struct dyadic_cm_precompute {
  std::vector<std::pair<long, long>> intervals;
  std::vector<uint*> hashes;
} dyadic_cm_precompute;

class Sketch {
    public:
        Sketch() {};
        //virtual ~Sketch() = 0;
        virtual void Insert(long id) = 0;
        virtual void Insert(long id, uint* hashes) = 0;
        virtual void Insert(long id, long* hashes) = 0;
        virtual void Insert(long id, int count) = 0;
        virtual void Insert(long id, int count, uint* hashes) = 0;
        virtual void Insert(long id, int count, long* hashes) = 0;
        virtual void Insert(long id, int val, dyadic_cm_precompute* precompute) = 0;
        virtual int QueryItem(long id) = 0;
        virtual int QueryItem(long id, uint* hashes) = 0;
        virtual int QueryItem(long id, long* hashes) = 0;
        virtual int QueryItem(long id, int timestamp) = 0;
        virtual int QueryItem(long id, int timestamp, long* hashes) = 0;
        virtual int Query(dyadic1D dyad) = 0;
        virtual int GetItemHashes(long id, uint* hashes) = 0;  // TODO: you actually want CM to implement this, but non hash sketches to simply return zero
        virtual int GetItemHashes(long id, long* hashes) = 0;
        virtual int** GetHashesCoeff() = 0; // doesnt need to be abstract.
        virtual long** GetHashesCoeffLong()=0; // doesnt need to be abstract.
        
        virtual float GetSize() { return 0; };
        virtual void PrecomputeInsert(long point, dyadic_cm_precompute *precompute) = 0;

        bool NewInsert() { return new_insert_; };

        int repetitions_ = 0;
        int** hashab_ = nullptr;
        std::vector<std::vector<bool> > r_bitmap;
        bool new_insert_ = true; // assume every insertion is new
        int **counters_;
};

#endif  // SKETCH_H_