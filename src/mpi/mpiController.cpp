#include <vector>
#include <complex>
#include <chrono>
#include <iostream>
#include "mpiController.h"
#include "Blacs.h"
#include "exceptions.h"
#include "eigen.h"

#ifdef MPI_AVAIL 
#include <mpi.h>
#endif

// default constructor
MPIcontroller::MPIcontroller(){

	#ifdef MPI_AVAIL
        int errCode; 
	// start the MPI environment
        #ifdef OMP_AVAIL
                // need to specify threading style -- 
                // MPI_THREAD_MULTIPLE might be better, but FUNNELED is simpler. 
                int provided;
                errCode = MPI_Init_thread(0, 0, MPI_THREAD_SINGLE, &provided);
                if(errCode != MPI_SUCCESS) {  errorReport(errCode); }
                //std::cout << "Provided level of threading: " << provided << std::endl;  
        #else 
                // simple mpi init with MPI_THREAD_SINGLE
                errCode = MPI_Init(NULL, NULL);
        #endif  
        // set this so that MPI returns errors and lets us handle them, rather
        // than using the default, MPI_ERRORS_ARE_FATAL
        MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);
        // If there was an init problem, kill the code
        if(errCode != MPI_SUCCESS) {  errorReport(errCode); }

	// get the number of processes
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	
	// get rank of current process
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
	// start a timer
	startTime = MPI_Wtime();

	#else
        // To maintain consistency when running in serial
        size = 1;
        rank = 0;  
	startTime = std::chrono::steady_clock::now();
	#endif
	
  // BLACS

#ifdef MPI_AVAIL
  blacs_pinfo_(&blasRank_, &size);  // BLACS rank and world size
  int zero = 0;
  blacs_get_(&zero, &zero, &blacsContext_);  // -> Create context
  // Context -> Initialize the grid

  numBlasRows_ = (int)(sqrt(size)); // int does rounding down (intentional!)
  numBlasCols_ = numBlasRows_; // scalapack requires square grid

  // we "pause" the processes that fall outside the blas grid
  if ( size > numBlasRows_*numBlasCols_ ) {
      Error e("Phoebe needs a square number of MPI processes");
      // TODO: stop the extra MPI processes and continue with the rest.
  }

  blacs_gridinit_(&blacsContext_, &blacsLayout_, &numBlasRows_, &numBlasCols_);
  // Context -> Context grid info (# procs row/col, current procs row/col)
  blacs_gridinfo_(&blacsContext_, &numBlasRows_, &numBlasCols_, &myBlasRow_,
                  &myBlasCol_);
#else
  blasRank_ = 0;
  blasContext_ = 0;
  numBlasRows_ = 1;
  numBlasCols_ = 1;
  myBlasRow_ = 0;
  myBlasCol_ = 0;
#endif
}

// default destructor
MPIcontroller::~MPIcontroller(){
	#ifdef MPI_AVAIL
	if (!MPI::Is_finalized()) finalize();
	#endif
}

// TODO: any other stats would like to output here?
void MPIcontroller::finalize() const {
#ifdef MPI_AVAIL
  barrier();
  if (mpiHead()) {
    fprintf(stdout, "Final time: %3f\n ", MPI_Wtime() - startTime);
  }
  blacs_gridexit_(&blacsContext_);
  MPI_Finalize();
#else
  std::cout << "Total runtime: "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now() - startTime)
                       .count() * 1e-6
            << " secs" << std::endl;
#endif
}

// Utility functions  -----------------------------------

// get the error string and print it to stderr before returning
void MPIcontroller::errorReport(int errCode) const{
	#ifdef MPI_AVAIL
	char errString[BUFSIZ];
	int lengthOfString;
	
	MPI_Error_string(errCode, errString, &lengthOfString);
	fprintf(stderr, "Error from rank %3d: %s\n", rank, errString);
	MPI_Abort(MPI_COMM_WORLD, errCode);
	#else 
	// TODO: how are we throwing non-mpi errors? 
	#endif
}

void MPIcontroller::time() const{
	#ifdef MPI_AVAIL
	fprintf(stdout, "Time for rank %3d : %3f\n", rank, MPI_Wtime() - startTime );
	#else
	std::cout << "Time for rank 0 :" << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count() << " secs" << std::endl;
	#endif
}
// Asynchronous support functions -----------------------------------------
void MPIcontroller::barrier() const{ 
        #ifdef MPI_AVAIL
        int errCode; 
        errCode = MPI_Barrier(MPI_COMM_WORLD); 
        if(errCode != MPI_SUCCESS) {  errorReport(errCode); }
        #endif
}

// Labor division functions -----------------------------------------
void MPIcontroller::divideWork(size_t numTasks) {
        // clear just in case there was a prior call
        workDivisionHeads.clear(); 
        workDivisionTails.clear(); 

        // each should be nranks long
        workDivisionHeads.resize(size);  
        workDivisionTails.resize(size); 
        //size_t numDivisons = numTasks/size + (numTasks % size != 0); // ceiling of work/ranks
        for(int r = 0; r<size; r++) {
                workDivisionHeads[r] = (numTasks * r)/size;
                workDivisionTails[r] = (numTasks * (r+1))/size;
                // if(rank==0) fprintf(stdout, "rank %3d : %3d %3d \n", rank,  workDivisionHeads[r], workDivisionTails[r]); // for debugging work division
        }
}

int MPIcontroller::workHead() { return workDivisionHeads[rank]; }

int MPIcontroller::workTail() { return workDivisionTails[rank]; } 

int MPIcontroller::getNumBlasRows() { return numBlasRows_; }

int MPIcontroller::getNumBlasCols() { return numBlasCols_; }

int MPIcontroller::getMyBlasRow() { return myBlasRow_; }

int MPIcontroller::getMyBlasCol() { return myBlasCol_; }

int MPIcontroller::getBlacsContext() { return blacsContext_; }

// template specialization
template <>
void MPIcontroller::reduceSum(
    Eigen::Matrix<double, -1, -1, 0, -1, -1>* data) const {
  using namespace mpiContainer;
#ifdef MPI_AVAIL
  if (size == 1) return;
  // NOTE: this requires 2 copies, while I could make it with one (in theory).
  std::vector<double> y(data->data(), data->data() + data->size());
  std::vector<double> y2(data->size());
  reduceSum(&y, &y2);
  for (int i = 0; i < data->size(); i++) {
    *(data->data() + i) = y2[i];
  }
#endif
}

// template specialization
template <>
void MPIcontroller::allReduceSum(
    Eigen::Matrix<double, -1, -1, 0, -1, -1>* data) const {
  using namespace mpiContainer;
#ifdef MPI_AVAIL
  if (size == 1) return;
  // NOTE: this requires 2 copies, while I could make it with one (in theory).
  std::vector<double> y(data->data(), data->data() + data->size());
  std::vector<double> y2(data->size());
  allReduceSum(&y, &y2);
  for (int i = 0; i < data->size(); i++) {
    *(data->data() + i) = y2[i];
  }
#endif
}
