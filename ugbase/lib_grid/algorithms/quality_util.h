/*
 * Copyright (c) 2017:  G-CSC, Goethe University Frankfurt
 * Author: Sebastian Reiter
 * 
 * This file is part of UG4.
 * 
 * UG4 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License version 3 (as published by the
 * Free Software Foundation) with the following additional attribution
 * requirements (according to LGPL/GPL v3 §7):
 * 
 * (1) The following notice must be displayed in the Appropriate Legal Notices
 * of covered and combined works: "Based on UG4 (www.ug4.org/license)".
 * 
 * (2) The following notice must be displayed at a prominent place in the
 * terminal output of covered works: "Based on UG4 (www.ug4.org/license)".
 * 
 * (3) The following bibliography is recommended for citation and must be
 * preserved in all covered files:
 * "Reiter, S., Vogel, A., Heppner, I., Rupp, M., and Wittum, G. A massively
 *   parallel geometric multigrid solver on hierarchically distributed grids.
 *   Computing and visualization in science 16, 4 (2013), 151-164"
 * "Vogel, A., Reiter, S., Rupp, M., Nägel, A., and Wittum, G. UG4 -- a novel
 *   flexible software system for simulating pde based models on high performance
 *   computers. Computing and visualization in science 16, 4 (2013), 165-179"
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 */

#ifndef __H__UG_quality_util
#define __H__UG_quality_util

#include <algorithm>
#include <string>
#include <vector>
#include "common/math/misc/math_util.h"
#include "lib_grid/grid/grid_base_objects.h"
#include "lib_grid/grid/grid.h"

namespace ug {

///	Returns the aspect ratio of a triangle
/**	The aspect ratio is calculated using the formula
 * \code
 * q = aMax * (a0 + a1 + a2) / (4*sqrt(3)*A)
 * \endcode
 */
template <class TAAPos>
number TriangleAspectRatio (FaceVertices* f, TAAPos& aaPos)
{
	// q = aMax * (a0 + a1 + a2) / (4*sqrt(3)*A)
	using std::max;
	
	FaceVertices::ConstVertexArray vrts = f->vertices();
	const number a[3] = {	VecDistance(aaPos[vrts[0]], aaPos[vrts[1]]),
							VecDistance(aaPos[vrts[1]], aaPos[vrts[2]]),
							VecDistance(aaPos[vrts[2]], aaPos[vrts[0]])};
	const number aMax = max(max(a[0], a[1]), a[2]);

	const number A = TriangleArea(aaPos[vrts[0]], aaPos[vrts[1]], aaPos[vrts[2]]);

	if(aMax > 0)
		return (6.9282032302755091741097853660235 * A) / (aMax * (a[0] + a[1] + a[2]));
	return 0;
}

///	Returns the aspect ratio of a quadrilateral
/**	The aspect ratio is calculated using the formula
 * \code
 * q = aMax * (a0 + a1 + a2 + a3) / (4*A)
 * \endcode
 */
template <class TAAPos>
number QuadrilateralAspectRatio (FaceVertices* f, TAAPos& aaPos)
{
	// q = aMax * (a0 + a1 + a2 + a3) / (4*A)
	using std::max;
	
	FaceVertices::ConstVertexArray vrts = f->vertices();
	const number a[4] = {	VecDistance(aaPos[vrts[0]], aaPos[vrts[1]]),
							VecDistance(aaPos[vrts[1]], aaPos[vrts[2]]),
							VecDistance(aaPos[vrts[2]], aaPos[vrts[3]]),
							VecDistance(aaPos[vrts[3]], aaPos[vrts[0]])};
	const number aMax = max(max(max(a[0], a[1]), a[2]), a[3]);

	const number A = QuadrilateralArea(aaPos[vrts[0]], aaPos[vrts[1]],
	                                   aaPos[vrts[2]], aaPos[vrts[3]]);

	if(aMax > 0)
		return (4. * A) / (aMax * (a[0] + a[1] + a[2] + a[3]));
	return 0;
}


///	Returns the aspect ratio of a face
/**	Depending on the number of nodes, either TriangleAspectRatio or
 * QuadrilateralAspectRatio is called.
 */
template <class TAAPos>
number FaceAspectRatio (FaceVertices* f, TAAPos& aaPos)
{
	switch(f->num_vertices()){
		case 3:	return TriangleAspectRatio (f, aaPos);
		case 4:	return QuadrilateralAspectRatio (f, aaPos);
		default: UG_THROW("FaceAspectRatio only supports faces with 3 or 4 vertices.");
	}
}


///	Holds information on the min, max, mean, and standard deviation of a sample
struct AspectRatioInfo {
	number min;
	number max;
	number mean;
	number sd;

	std::string to_string();
};


///	Computes the AspectRatioInfo for a sample of elements
template <class TFaceIter, class TAAPos>
AspectRatioInfo
GetFaceAspectRatioInfo (TFaceIter facesBegin, TFaceIter facesEnd, TAAPos& aaPos)
{
	using std::min;
	using std::max;

	AspectRatioInfo ari;
	ari.mean = 0;
	ari.min = 0;
	ari.max = 0;
	ari.sd = 0;

	if(facesBegin == facesEnd)
		return ari;

	ari.min = 1;
	number counter = 0;

	for(TFaceIter iface = facesBegin; iface != facesEnd; ++iface) {
		number ar = FaceAspectRatio (*iface, aaPos);
		ari.min = min (ari.min, ar);
		ari.max = max (ari.max, ar);
		ari.mean += ar;
		++counter;
	}

	ari.mean /= counter;

	if(counter > 1){
		for(TFaceIter iface = facesBegin; iface != facesEnd; ++iface) {
			number ar = FaceAspectRatio (*iface, aaPos);
			ari.sd += sq(ar - ari.mean);
		}

		ari.sd = sqrt(ari.sd / (counter));
	}

	return ari;
}


template <class TFaceIter, class TAAPos>
void
GetFaceAspectRatioHistogram (
		std::vector<int>& histoOut,
		TFaceIter facesBegin,
		TFaceIter facesEnd,
		int histoSecs,
		TAAPos& aaPos,
		Grid::FaceAttachmentAccessor<Attachment<int> >* paaHistoSec = NULL)
{
	using std::min;
	using std::max;

	if(histoSecs <= 0){
		histoOut.resize(0);
		return;
	}

	histoOut.resize(histoSecs, 0);

	UG_COND_THROW(paaHistoSec && (!paaHistoSec->valid()),
	              "Invalid paaHistoSec supplied to 'GetFaceAspectRatioHistogram'");

	for(TFaceIter iface = facesBegin; iface != facesEnd; ++iface) {
		number ar = FaceAspectRatio (*iface, aaPos);
		int sec = static_cast<int> (ar * histoSecs);
		sec = min (sec, histoSecs - 1);
		sec = max (sec, 0);
		++histoOut[sec];
		if(paaHistoSec)
			(*paaHistoSec)[*iface] = sec;
	}
}

}//	end of namespace

#endif	//__H__UG_quality_util
