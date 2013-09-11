#ifndef TASKDESCRIPTION_H
#define TASKDESCRIPTION_H

enum Singularities
{
	POINT,
	CENTRAL_POINT,
	EDGE,
	FACE,
	ANISOTROPIC
};

struct TaskDescription {
	int dimensions;
	int polynomialDegree;
	int nrOfTiers;

	double size;
	bool quad;

	double x;
	double y;
	double z;

	double (*function)(int, ...);

	bool performTests;
	Singularities singularity;
};

#endif
