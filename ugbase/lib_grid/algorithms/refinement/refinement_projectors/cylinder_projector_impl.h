// created by Sebastian Reiter
// s.b.reiter@gmail.com
// Feb 20, 2014

#ifndef __H__UG__cylinder_projector_impl__
#define __H__UG__cylinder_projector_impl__

#include "cylinder_projector.h"

namespace ug{

template <class TAPosition>
CylinderProjector<TAPosition>::
CylinderProjector() :
	m_pGrid(NULL)
{
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
CylinderProjector<TAPosition>::
CylinderProjector(Grid& grid, TAPosition& aPos,
						 const typename TAPosition::ValueType& center,
						 const typename TAPosition::ValueType& axis) :
	m_pGrid(&grid),
	m_center(center),
	m_axis(axis)
{
//	we have to make sure that aPos is attached at the grid.
//	This is important to avoid crashes later on.
	if(!grid.has_vertex_attachment(aPos))
		grid.attach_to_vertices(aPos);
	m_aaPos.access(grid, aPos);
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
CylinderProjector<TAPosition>::
~CylinderProjector()
{
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
void CylinderProjector<TAPosition>::
new_vertex(Vertex* vrt, Vertex* parent)
{
	assert(m_aaPos.valid() && "make sure to initialise the refiner-callback correctly.");
	m_aaPos[vrt] = m_aaPos[parent];
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
void CylinderProjector<TAPosition>::
new_vertex(Vertex* vrt, Edge* parent)
{
	perform_projection(vrt, parent);
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
void CylinderProjector<TAPosition>::
new_vertex(Vertex* vrt, Face* parent)
{
	perform_projection(vrt, parent);
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
void CylinderProjector<TAPosition>::
new_vertex(Vertex* vrt, Volume* parent)
{
	perform_projection(vrt, parent);
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
int CylinderProjector<TAPosition>::
current_pos(number* coordsOut, Vertex* vrt, int maxCoords)
{
	return IRefinementCallback::current_pos_helper(coordsOut, vrt, maxCoords, m_aaPos);
}

////////////////////////////////////////////////////////////////////////
template <class TAPosition>
template <class TElem>
void CylinderProjector<TAPosition>::
perform_projection(Vertex* vrt, TElem* parent)
{
	assert(m_aaPos.valid() && "make sure to initialise the refiner-callback correctly.");

//	calculate the new position by linear interpolation and project that point
//	onto the cylinder.
	typename TElem::ConstVertexArray vrts = parent->vertices();
	size_t numVrts = parent->num_vertices();
	number avDist = 0;
	pos_type v;
	VecSet(v, 0);

	for(size_t i = 0; i < numVrts; ++i){
		const pos_type& p = m_aaPos[vrts[i]];
		avDist += DistancePointToRay(p, m_center, m_axis);
		VecAdd(v, v, p);
	}

	avDist /= (number)numVrts;
	VecScale(v, v, 1. / (number)numVrts);

	pos_type proj;
	ProjectPointToRay(proj, v, m_center, m_axis);
	VecSubtract(v, v, proj);
	VecNormalize(v, v);
	VecScale(v, v, avDist);
	VecAdd(m_aaPos[vrt], proj, v);
}

}// end of namespace

#endif