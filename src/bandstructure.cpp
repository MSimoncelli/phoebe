#include "bandstructure.h"
#include "points.h"
#include "state.h"
#include "particle.h"
#include "exceptions.h"
#include "utilities.h"

FullBandStructure::FullBandStructure(long numBands_, Particle & particle_,
		bool withVelocities, bool withEigenvectors, Points & points_) :
			particle(particle_), points(points_) {

//	I need to crash if I'm using active points

	numBands = numBands_;
	numAtoms = numBands_ / 3;

	if ( withVelocities ) {
		hasVelocities = true;
		velocities = Eigen::MatrixXcd::Zero(numBands*numBands*3,
				getNumPoints());
	}

	if ( withEigenvectors ) {
		hasEigenvectors = true;
		eigenvectors = Eigen::MatrixXcd::Zero(3*numAtoms*numBands,
				getNumPoints());
	}

	energies = Eigen::MatrixXd::Zero(numBands,getNumPoints());

	// now, I want to manipulate the Eigen matrices at lower level
	// I create this pointer to data, so I can move it around
	rawEnergies = energies.data();
	if ( hasVelocities ) {
		rawVelocities = velocities.data();
	}
	if ( hasEigenvectors ) {
		rawEigenvectors = eigenvectors.data();
	}

	energiesCols = numBands;
	velocitiesCols = numBands * numBands * 3;
	eigenvectorsCols = numBands * numAtoms * 3;
}

// copy constructor
FullBandStructure::FullBandStructure(const FullBandStructure & that) :
	particle(that.particle), points(that.points),
	energies(that.energies), velocities(that.velocities),
	eigenvectors(that.eigenvectors), rawEnergies(that.rawEnergies),
	rawVelocities(that.rawVelocities), rawEigenvectors(that.rawEigenvectors),
	energiesCols(that.energiesCols), velocitiesCols(that.velocitiesCols),
	eigenvectorsCols(that.eigenvectorsCols), numBands(that.numBands),
	numAtoms(that.numAtoms),
	hasEigenvectors(that.hasEigenvectors), hasVelocities(that.hasVelocities) {
}

FullBandStructure & FullBandStructure::operator = ( // copy assignment
		const FullBandStructure & that) {
	if ( this != &that ) {
		particle = that.particle;
		points = that.points;
		energies = that.energies;
		velocities = that.velocities;
		eigenvectors = that.eigenvectors;
		rawEnergies = that.rawEnergies;
		rawVelocities = that.rawVelocities;
		rawEigenvectors = that.rawEigenvectors;
		energiesCols = that.energiesCols;
		velocitiesCols = that.velocitiesCols;
		eigenvectorsCols = that.eigenvectorsCols;
		numBands = that.numBands;
		numAtoms = that.numAtoms;
		hasEigenvectors = that.hasEigenvectors;
		hasVelocities = that.hasVelocities;
	}
	return *this;
}

Particle FullBandStructure::getParticle() {
	return particle;
}

long FullBandStructure::getNumBands() {
	return numBands;
}

long FullBandStructure::getNumStates() {
	return numBands*getNumPoints();
}

long FullBandStructure::getNumPoints() {
	return points.getNumPoints();
}

long FullBandStructure::getIndex(Eigen::Vector3d & pointCoords) {
	return points.getIndex(pointCoords);
}

Point FullBandStructure::getPoint(const long & pointIndex) {
	return points.getPoint(pointIndex);
}

const double & FullBandStructure::getEnergy(long & stateIndex) {
	auto [ik,ib] = decompress2Indeces(stateIndex,getNumPoints(),numBands);
	return energies(ib,ik);
}

Eigen::Vector3d FullBandStructure::getGroupVelocity(long & stateIndex) {
	auto [ik,ib] = decompress2Indeces(stateIndex,getNumPoints(),numBands);
	Eigen::Vector3d vel;
	for ( long i=0; i<3; i++ ) {
		long ind = compress3Indeces(ib,ib,i,numBands,numBands,3);
		vel(i) = velocities(ind,ik).real();
	}
	return vel;
}

Eigen::Vector3d FullBandStructure::getWavevector(long & stateIndex) {
	auto [ik,ib] = decompress2Indeces(stateIndex,getNumPoints(),numBands);
	return points.getPoint(ik).getCoords(Points::cartesianCoords);
}

void FullBandStructure::setEnergies(Eigen::Vector3d& coords,
		Eigen::VectorXd& energies_) {
	long ik = getIndex(coords);
	energies.col(ik) = energies_;
}

void FullBandStructure::setEnergies(Point & point,
		Eigen::VectorXd& energies_) {
	long ik = point.getIndex();
	energies.col(ik) = energies_;
}

void FullBandStructure::setVelocities(Point & point,
		Eigen::Tensor<std::complex<double>,3>& velocities_) {
	if ( ! hasVelocities ) {
		Error e("FullBandStructure was initialized without velocities",1);
	}
	// we convert from a tensor to a vector (how it's stored in memory)
	Eigen::VectorXcd tmpVelocities_(numBands*numBands*3);
	for ( long i=0; i<numBands; i++ ) {
		for ( long j=0; j<numBands; j++ ) {
			for ( long k=0; k<3; k++ ) {
				// Note: State must know this order of index compression
				long idx = compress3Indeces(i, j, k, numBands, numBands, 3);
				tmpVelocities_(idx) = velocities_(i,j,k);
			}
		}
	}
	long ik = point.getIndex();
	velocities.col(ik) = tmpVelocities_;
}

//void FullBandStructure::setEigenvectors(Point & point,
//		Eigen::Tensor<std::complex<double>,3> & eigenvectors_) {
//	if ( ! hasEigenvectors ) {
//		Error e("FullBandStructure was initialized without eigvecs",1);
//	}
//	// we convert from a tensor to a vector (how it's stored in memory)
//	Eigen::VectorXcd tmp(numBands*numAtoms*3);
//	for ( long i=0; i<numBands; i++ ) {
//		for ( long j=0; j<numAtoms; j++ ) {
//			for ( long k=0; k<3; k++ ) {
//				// Note: State must know this order of index compression
//				long idx = compress3Indeces(i, j, k, numBands, numAtoms, 3);
//				tmp(idx) = eigenvectors_(k,j,i);
//			}
//		}
//	}
//	long ik = point.getIndex();
//	eigenvectors.col(ik) = tmp;
//}

void FullBandStructure::setEigenvectors(Point & point,
		Eigen::MatrixXcd & eigenvectors_) {
	if ( ! hasEigenvectors ) {
		Error e("FullBandStructure was initialized without eigvecs",1);
	}
	// we convert from a matrix to a vector (how it's stored in memory)
	Eigen::VectorXcd tmp(numBands*numBands);
	for ( long i=0; i<numBands; i++ ) {
		for ( long j=0; j<numBands; j++ ) {
			// Note: State must know this order of index compression
			long idx = compress2Indeces(i, j, numBands, numBands);
			tmp(idx) = eigenvectors_(j,i);
		}
	}
	long ik = point.getIndex();
	eigenvectors.col(ik) = tmp;
}

State FullBandStructure::getState(Point & point) {
	long pointIndex = point.getIndex();
	State state = getState(pointIndex);
	return state;
}

State FullBandStructure::getState(const long & pointIndex) {
	Point point = getPoint(pointIndex);
	// we construct the vector by defining begin() and end()
	// note that rawEnergies points at the start of the matrix
	// and pointIndex*energiesCols skips the first pointIndex-1 wavevectors
	// we are assuming column-major ordering!
	// thisEn points at the location with where the next numBands elements have
	// the desired energies for this wavevector.
	double * thisEn;
	thisEn = rawEnergies + pointIndex * energiesCols;

	// note: in some cases these are nullptr
	std::complex<double> * thisVel = nullptr;
	std::complex<double> * thisEig = nullptr;

	if ( hasVelocities ) {
		thisVel = rawVelocities + pointIndex * velocitiesCols;
	}
	if ( hasEigenvectors ) {
		thisEig = rawEigenvectors + pointIndex * eigenvectorsCols;
	}

	State s(point, thisEn, numAtoms, numBands, thisVel, thisEig);
	return s;
}

Eigen::VectorXd FullBandStructure::getBandEnergies(long & bandIndex) {
	Eigen::VectorXd bandEnergies = energies.row(bandIndex);
	return bandEnergies;
}

Points FullBandStructure::getPoints() {
	return points;
}

long FullBandStructure::getIndex(const WavevectorIndex & ik,
		const BandIndex & ib) {
	return ik.get() * numBands + ib.get();
}
