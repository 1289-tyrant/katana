/*
 * Tier.hxx
 *
 *  Created on: Aug 27, 2013
 *      Author: dgoik
 */

#ifndef TIER_2D_EDGE_HXX_
#define TIER_2D_EDGE_HXX_

#include <stdlib.h>
#include "../Point2D/Element.hxx"
#include "../Point2D/DoubleArgFunction.hxx"
#include "../EquationSystem.h"
#include <stdio.h>
using namespace D2;
namespace D2Edge {

class Tier : public EquationSystem
{

private:
	Element* element;
	IDoubleArgFunction* f;
	double** global_matrix;
	double* global_rhs;
	static const int tier_matrix_size = 9;

public:
	Tier(Element* element, IDoubleArgFunction* f, double** global_matrix, double* global_rhs) :
		EquationSystem(tier_matrix_size), element(element), f(f), global_matrix(global_matrix), global_rhs(global_rhs)
	{}

	void InitTier()
	{

		element->fillMatrices(NULL,global_matrix,NULL,global_rhs,f,0);

		int global_numbers[9];
		element->get_nrs(global_numbers);

		int local_numbers[9];
		for(int i = 0; i<9; i++)
			local_numbers[i] = i;

		element->set_nrs(local_numbers);

		element->fillMatrices(matrix,NULL,rhs,NULL,f,0);
		element->set_nrs(global_numbers);

	}

	virtual ~Tier()
	{
		delete element;
	}

	double** get_tier_matrix(){
		return matrix;
	}

	double* get_tier_rhs(){
		return rhs;
	}
};
}
 /* namespace D2Edge */
#endif /* TIER_2D_EDGE_HXX_ */
