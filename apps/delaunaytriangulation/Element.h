/*
 * Element.h
 *
 *  Created on: Jan 25, 2011
 *      Author: xinsui
 */

#ifndef DTELEMENT_H_
#define DTELEMENT_H_

#include "Tuple.h"
#include <vector>
using namespace std;
class DTElement{
	DTTuple coords[3];
	bool bDim; // true == 3, false == 2
	bool processed;
	std::vector<DTTuple>* _tuples;

public:
	const bool isProcessed() const { return processed;};
	void setProcessed(){ processed = true;}
	const DTTuple& getPoint(int i) const { return coords[i];};
	bool getBDim(){ return bDim;}
	void addTuple(DTTuple& newTuple){
		_tuples->push_back(newTuple);
	};

	void setTuples(vector<DTTuple>* tuples){  _tuples = tuples;};

	std::vector<DTTuple>* getTuples(){ return _tuples; };

	explicit DTElement(const DTTuple& a, const DTTuple& b, const DTTuple& c):bDim(true), processed(false){
		coords[0] = a;
		coords[1] = b;
		coords[2] = c;
		_tuples = NULL;
	}

	explicit DTElement(const DTTuple& a, const DTTuple& b)
	:bDim(false), processed(false){
		coords[0] = a;
		coords[1] = b;
		_tuples=NULL;
	}
	
	
/*
	 bool operator==(const DTElement& rhs) const {
	    for (int x = 0; x < 3; ++x)
	      if (coords[x] != rhs.coords[x])
		return false;
    	    return true;
  	}
*/
	/**
	 * determine if a tuple is inside the triangle
	 */
	bool elementContains(DTTuple& p) {
		DTTuple p1 = coords[0];
		DTTuple p2 = coords[1];
		DTTuple p3 = coords[2];

		if ((p1 == p) || (p2 == p) || (p3 == p)) {
			return false;
		}

		int count = 0;
		double px = p.getX();
		double py = p.getY();
		double p1x = p1.getX();
		double p1y = p1.getY();
		double p2x = p2.getX();
		double p2y = p2.getY();
		double p3x = p3.getX();
		double p3y = p3.getY();

		if (p2x < p1x) {
			if ((p2x < px) && (p1x >= px)) {
				if (((py - p2y) * (p1x - p2x)) < ((px - p2x) * (p1y - p2y))) {
					count = 1;
				}
			}
		} else {
			if ((p1x < px) && (p2x >= px)) {
				if (((py - p1y) * (p2x - p1x)) < ((px - p1x) * (p2y - p1y))) {
					count = 1;
				}
			}
		}

		if (p3x < p2x) {
			if ((p3x < px) && (p2x >= px)) {
				if (((py - p3y) * (p2x - p3x)) < ((px - p3x) * (p2y - p3y))) {
					if (count == 1) {
						return false;
					}
					count++;
				}
			}
		} else {
			if ((p2x < px) && (p3x >= px)) {
				if (((py - p2y) * (p3x - p2x)) < ((px - p2x) * (p3y - p2y))) {
					if (count == 1) {
						return false;
					}
					count++;
				}
			}
		}

		if (p1x < p3x) {
			if ((p1x < px) && (p3x >= px)) {
				if (((py - p1y) * (p3x - p1x)) < ((px - p1x) * (p3y - p1y))) {
					if (count == 1) {
						return false;
					}
					count++;
				}
			}
		} else {
			if ((p3x < px) && (p1x >= px)) {
				if (((py - p3y) * (p1x - p3x)) < ((px - p3x) * (p1y - p3y))) {
					if (count == 1) {
						return false;
					}
					count++;
				}
			}
		}

		return count == 1;
	}

	/**
	 * determine if the circumcircle of the triangle contains the tuple
	 */
	bool inCircle(DTTuple& p) {
		// This version computes the determinant of a matrix including the
		// coordinates of each points + distance of these points to the origin
		// in order to check if a point is inside a triangle or not
		double t1_x = coords[0].getX();
		double t1_y = coords[0].getY();

		double t2_x = coords[1].getX();
		double t2_y = coords[1].getY();

		double t3_x = coords[2].getX();
		double t3_y = coords[2].getY();

		double p_x = p.getX();
		double p_y = p.getY();

		// Check if the points (t1,t2,t3) are sorted clockwise or
		// counter-clockwise:
		// -> counter_clockwise > 0 => counter clockwise
		// -> counter_clockwise = 0 => degenerated triangle
		// -> counter_clockwise < 0 => clockwise
		double counter_clockwise = (t2_x - t1_x) * (t3_y - t1_y) - (t3_x - t1_x) * (t2_y - t1_y);

		// If the triangle is degenerated, then the triangle should be updated
		if (counter_clockwise == 0.0) {
			return true;
		}

		// Compute the following determinant:
		// | t1_x-p_x  t1_y-p_y  (t1_x-p_x)^2+(t1_y-p_y)^2 |
		// | t2_x-p_x  t2_y-p_y  (t2_x-p_x)^2+(t2_y-p_y)^2 |
		// | t3_x-p_x  t3_y-p_y  (t3_x-p_x)^2+(t3_y-p_y)^2 |
		//
		// If the determinant is >0 then the point (p_x,p_y) is inside the
		// circumcircle of the triangle (t1,t2,t3).

		// Value of columns 1 and 2 of the matrix
		double t1_p_x, t1_p_y, t2_p_x, t2_p_y, t3_p_x, t3_p_y;
		// Determinant of minors extracted from columns 1 and 2
		// (det_t3_t1_m corresponds to the opposite)
		double det_t1_t2, det_t2_t3, det_t3_t1_m;
		// Values of the column 3 of the matrix
		double t1_col3, t2_col3, t3_col3;

		t1_p_x = t1_x - p_x;
		t1_p_y = t1_y - p_y;
		t2_p_x = t2_x - p_x;
		t2_p_y = t2_y - p_y;
		t3_p_x = t3_x - p_x;
		t3_p_y = t3_y - p_y;

		det_t1_t2 = t1_p_x * t2_p_y - t2_p_x * t1_p_y;
		det_t2_t3 = t2_p_x * t3_p_y - t3_p_x * t2_p_y;
		det_t3_t1_m = t3_p_x * t1_p_y - t1_p_x * t3_p_y;
		t1_col3 = t1_p_x * t1_p_x + t1_p_y * t1_p_y;
		t2_col3 = t2_p_x * t2_p_x + t2_p_y * t2_p_y;
		t3_col3 = t3_p_x * t3_p_x + t3_p_y * t3_p_y;

		double det = t1_col3 * det_t2_t3 + t2_col3 * det_t3_t1_m + t3_col3 * det_t1_t2;

		// If the points are enumerated in clockwise, then negate the result
		if (counter_clockwise < 0) {
			return det < 0;
		}
		return det > 0;
	}

};

#endif /* ELEMENT_H_ */
