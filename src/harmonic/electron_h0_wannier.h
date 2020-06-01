#ifndef WANNIERH0_H
#define WANNIERH0_H

#include <math.h>
#include "eigen.h"
#include "harmonic.h"
#include "points.h"
#include "bandstructure.h"
#include "constants.h"

class ElectronH0Wannier : public HarmonicHamiltonian {
public:
	ElectronH0Wannier(const Eigen::Matrix3d & directUnitCell_,
			const Eigen::Matrix<double,3,Eigen::Dynamic> & bravaisVectors_,
			const Eigen::VectorXd & vectorsDegeneracies_,
			const Eigen::Tensor<std::complex<double>,3> & h0R_,
			const Eigen::Tensor<std::complex<double>,4> & rMatrix_);

	template<typename T>
	std::tuple<Eigen::VectorXd,Eigen::MatrixXcd> diagonalize(
			Point<T> & point);

	template<typename T>
	Eigen::Tensor<std::complex<double>,3> diagonalizeVelocity(Point<T> &point);
    const bool hasEigenvectors = true;
    Statistics getStatistics();
    long getNumBands();

    // copy constructor
    ElectronH0Wannier( const ElectronH0Wannier & that );
    // copy assignment
    ElectronH0Wannier & operator = ( const ElectronH0Wannier & that );
    // empty constructor
    ElectronH0Wannier();

    template<typename T>
    FullBandStructure<T> populate(T & fullPoints, bool & withVelocities,
    		bool &withEigenvectors);

    template<typename T>
    std::vector<Eigen::MatrixXcd> getBerryConnection(Point<T> & point);
protected:
    Statistics statistics;
    virtual std::tuple<Eigen::VectorXd, Eigen::MatrixXcd>
    	diagonalizeFromCoords(Eigen::Vector3d & k);

    Eigen::Matrix<double,3,Eigen::Dynamic> bravaisVectors;
    Eigen::VectorXd vectorsDegeneracies;
    Eigen::Matrix3d directUnitCell;
    Eigen::Tensor<std::complex<double>,3> h0R;
    Eigen::Tensor<std::complex<double>,4> rMatrix;

    long numBands;
    long numVectors;
};

template<typename T>
FullBandStructure<T> ElectronH0Wannier::populate(T & fullPoints,
		bool & withVelocities, bool & withEigenvectors) {

	FullBandStructure<T> fullBandStructure(numBands, statistics,
			withVelocities, withEigenvectors, fullPoints);

	for ( long ik=0; ik<fullBandStructure.getNumPoints(); ik++ ) {
		Point<T> point = fullBandStructure.getPoint(ik);
		auto [ens, eigvecs] = diagonalize(point);
		fullBandStructure.setEnergies(point, ens);
		if ( withVelocities ) {
			auto vels = diagonalizeVelocity(point);
			fullBandStructure.setVelocities(point, vels);
		}
		//TODO: I must fix the different shape of eigenvectors w.r.t. phonons
//		if ( withEigenvectors ) {
//			fullBandStructure.setEigenvectors(point, eigvecs);
//		}
	}
	return fullBandStructure;
}

template<typename T>
std::tuple<Eigen::VectorXd, Eigen::MatrixXcd>
		ElectronH0Wannier::diagonalize(Point<T> & point) {
	Eigen::Vector3d k = point.getCoords(Points::cartesianCoords);

	auto [energies,eigenvectors] = diagonalizeFromCoords(k);

	// note: the eigenvector matrix is the unitary transformation matrix U
	// from the Bloch to the Wannier gauge.

	return {energies, eigenvectors};
}

template<typename T>
Eigen::Tensor<std::complex<double>,3> ElectronH0Wannier::diagonalizeVelocity(
		Point<T> & point) {
	Eigen::Vector3d coords = point.getCoords(Points::cartesianCoords);
	double delta = 1.0e-8;
	double threshold = 0.000001 / energyRyToEv; // = 1 micro-eV
	auto velocity = HarmonicHamiltonian::internalDiagonalizeVelocity(coords,
			delta, threshold);
	return velocity;
}

template<typename T>
std::vector<Eigen::MatrixXcd> ElectronH0Wannier::getBerryConnection(
		Point<T> & point) {
	Eigen::Vector3d k = point.getCoords(Points::cartesianCoords);

	// first we diagonalize the hamiltonian
	auto [ens, eigvecs] = diagonalize(point);

	// note: the eigenvector matrix is the unitary transformation matrix U
	// from the Bloch to the Wannier gauge.

	std::vector<Eigen::MatrixXcd> bc;

	for ( long i=0; i<3; i++ ) {

		// now construct the berryConnection in reciprocal space and Wannier gauge
		Eigen::MatrixXcd berryConnectionW(numBands,numBands);
		berryConnectionW.setZero();

		for ( long iR=0; iR<bravaisVectors.cols(); iR++ ) {
			Eigen::Vector3d R = bravaisVectors.col(iR);
			double phase = k.dot(R);
			std::complex<double> phaseFactor = {cos(phase),sin(phase)};
			for ( long m=0; m<numBands; m++ ) {
				for ( long n=0; n<numBands; n++ ) {
					berryConnectionW(m,n) +=
							phaseFactor * rMatrix(i,iR,m,n)
							/ vectorsDegeneracies(iR);
				}
			}
		}

		Eigen::MatrixXcd berryConnection(numBands,numBands);
		berryConnection = eigvecs.adjoint() * berryConnectionW * eigvecs;
		bc.push_back(berryConnection);
	}
	return bc;
}

#endif
