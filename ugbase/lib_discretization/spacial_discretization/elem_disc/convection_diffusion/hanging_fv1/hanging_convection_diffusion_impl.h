/*
 * hanging_convection_diffusion_impl.h
 *
 *  Created on: 26.02.2010
 *      Author: andreasvogel
 */

#ifndef __H__LIB_DISCRETIZATION__SPACIAL_DISCRETIZATION__ELEM_DISC__CONVECTION_DIFFUSION__HANGING_FV1__HANGING_CONVECTION_DIFFUSION_IMPL__
#define __H__LIB_DISCRETIZATION__SPACIAL_DISCRETIZATION__ELEM_DISC__CONVECTION_DIFFUSION__HANGING_FV1__HANGING_CONVECTION_DIFFUSION_IMPL__

#include "hanging_convection_diffusion.h"

namespace ug{


////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
//  Provide a generic implementation for all elements
//  (since this discretization can be implemented in a generic way)
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////


template<typename TDomain, typename TAlgebra>
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
HangingConvectionDiffusionElemDisc(TDomain& domain, number upwind_amount,
							Diff_Tensor_fct diff, Conv_Vel_fct vel, Reaction_fct reac, Rhs_fct rhs)
	: 	m_domain(domain), m_upwind_amount(upwind_amount),
		m_Diff_Tensor(diff), m_Conv_Vel(vel), m_Reaction(reac), m_Rhs(rhs)
{
	register_assemble_functions();
};



template<typename TDomain, typename TAlgebra>
template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
prepare_element_loop()
{
	// all this will be performed outside of the loop over the elements.
	// Therefore it is not time critical.

	typedef typename reference_element_traits<TElem>::reference_element_type ref_elem_type;
	m_corners = new position_type[ref_elem_type::num_corners];

	// remember position attachement
	m_aaPos = m_domain.get_position_accessor();

	return true;
}

template<typename TDomain, typename TAlgebra>
template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
finish_element_loop()
{
	// all this will be performed outside of the loop over the elements.
	// Therefore it is not time critical.
	delete[] m_corners;

	return true;
}

template<typename TDomain, typename TAlgebra>

template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
prepare_element(TElem* elem, const local_vector_type& u, const local_index_type& glob_ind)
{
	// this loop will be performed inside the loop over the elements.
	// Therefore, it is TIME CRITICAL
	typedef typename reference_element_traits<TElem>::reference_element_type ref_elem_type;

	// load corners of this element
	for(int i = 0; i < ref_elem_type::num_corners; ++i)
	{
		VertexBase* vert = elem->vertex(i);
		m_corners[i] = m_aaPos[vert];
	}

	// update Geometry for this element
	if(!get_fvgeom<TElem>().update(elem, m_domain.get_grid(), m_corners))
		{UG_LOG("HangingConvectionDiffusionElemDisc::prepare_element: Cannot update Finite Volume Geometry.\n"); return false;}

	return true;
}

template<typename TDomain, typename TAlgebra>
template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
assemble_JA(local_matrix_type& J, const local_vector_type& u, number time)
{
	static const int dim = TDomain::dim;

	number flux;
	MathMatrix<dim,dim> D;
	MathVector<dim> v, lin_Defect;
	MathVector<dim> Dgrad;
	for(size_t i = 0; i < get_fvgeom<TElem>().num_scvf(); ++i)
	{
		const typename HFVGeometry<TElem, TDomain::dim>::SCVF& scvf = get_fvgeom<TElem>().scvf(i);

		m_Diff_Tensor(D, scvf.global_ip(), time);
		m_Conv_Vel(v, scvf.global_ip(), time);

		for(size_t j = 0; j < scvf.num_sh(); ++j)
		{
			////////////////////////////////////
			// diffusiv term (central discretization)
			MatVecMult(Dgrad, D, scvf.global_grad(j));

			flux = VecDot(Dgrad, scvf.normal());

			J(_C_, scvf.from(), _C_, j) -= flux;
			J(_C_, scvf.to()  , _C_, j) += flux;

			////////////////////////////////////
			// convective term
			// (upwinding_amount == 1.0 -> full upwind;
			//  upwinding_amount == 0.0 -> full central disc)

			// central part convection
			if(m_upwind_amount != 1.0)
			{
				flux = (1.- m_upwind_amount) * scvf.shape(j) * VecDot(v, scvf.normal());

				// coupling 'from' with j  (i.e. A[from][j]) and 'to' with j (i.e. A[to][j])
				J(_C_, scvf.from(), _C_, j) += flux;
				J(_C_, scvf.to()  , _C_, j) -= flux;

			}
		}
		// upwind part convection
		if(m_upwind_amount != 0.0)
		{
			int up;
			flux = m_upwind_amount * VecDot(v, scvf.normal());
			if(flux >= 0.0) up = scvf.from(); else up = scvf.to();
			J(_C_, scvf.from(), _C_, up) += flux;
			J(_C_, scvf.to()  , _C_, up) -= flux;
		}
	}
	int co;
	number reac_val;
	for(size_t i = 0; i < get_fvgeom<TElem>().num_scv(); ++i)
	{
		const typename HFVGeometry<TElem, TDomain::dim>::SCV& scv = get_fvgeom<TElem>().scv(i);

		co = scv.node_id();

		m_Reaction(reac_val, scv.global_ip(), time);

		J(_C_, co, _C_, co) += reac_val * scv.volume();
	}

	return true;
}


template<typename TDomain, typename TAlgebra>
template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
assemble_JM(local_matrix_type& J, const local_vector_type& u, number time)
{
	int co;
	for(size_t i = 0; i < get_fvgeom<TElem>().num_scv(); ++i)
	{
		const typename HFVGeometry<TElem, TDomain::dim>::SCV& scv = get_fvgeom<TElem>().scv(i);

		co = scv.node_id();

		J(_C_, co, _C_, co) += 1.0 * scv.volume();
	}

	return true;
}


template<typename TDomain, typename TAlgebra>
template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
assemble_A(local_vector_type& d, const local_vector_type& u, number time)
{
	static const int dim = TDomain::dim;

	number flux;
	MathVector<dim> grad_u;
	number shape_u;
	MathMatrix<dim,dim> D;
	MathVector<dim> v;
	MathVector<dim> Dgrad_u;
	for(size_t i = 0; i < get_fvgeom<TElem>().num_scvf(); ++i)
	{
		const typename HFVGeometry<TElem, TDomain::dim>::SCVF& scvf = get_fvgeom<TElem>().scvf(i);

		VecSet(grad_u, 0.0);
		shape_u = 0.0;
		for(size_t j = 0; j < scvf.num_sh(); ++j)
		{
			VecScaleAppend(grad_u, u(_C_,j), scvf.global_grad(j));
			shape_u += u(_C_,j) * scvf.shape(j);
		}

		m_Diff_Tensor(D, scvf.global_ip(), time);
		m_Conv_Vel(v, scvf.global_ip(), time);

		////////////////////////////////////
		// diffusiv term (central discretization)
		MatVecMult(Dgrad_u, D, grad_u);
		flux = VecDot(Dgrad_u, scvf.normal());

		d(_C_, scvf.from()) -= flux;
		d(_C_, scvf.to()  ) += flux;

		////////////////////////////////////
		// convective term
		// (upwinding_amount == 1.0 -> full upwind;
		//  upwinding_amount == 0.0 -> full central disc)

		// central part convection
		if(m_upwind_amount != 1.0)
		{
			flux = (1.- m_upwind_amount) * shape_u * VecDot(v, scvf.normal());

			d(_C_, scvf.from()) += flux;
			d(_C_, scvf.to()  ) -= flux;
		}

		// upwind part convection
		if(m_upwind_amount != 0.0)
		{
			flux = m_upwind_amount * VecDot(v, scvf.normal());
			if(flux >= 0.0) flux *= u(_C_, scvf.from()); else flux *= u(_C_, scvf.to());
			d(_C_, scvf.from()) += flux;
			d(_C_, scvf.to()  ) -= flux;
		}
	}
	int co;
	number reac_val;
	for(size_t i = 0; i < get_fvgeom<TElem>().num_scv(); ++i)
	{
		const typename HFVGeometry<TElem, TDomain::dim>::SCV& scv = get_fvgeom<TElem>().scv(i);

		co = scv.node_id();

		m_Reaction(reac_val, scv.global_ip(), time);

		d(_C_, co) += reac_val * u(_C_, co) * scv.volume();
	}

	return true;
}


template<typename TDomain, typename TAlgebra>
template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
assemble_M(local_vector_type& d, const local_vector_type& u, number time)
{
	int co;
	for(size_t i = 0; i < get_fvgeom<TElem>().num_scv(); ++i)
	{
		const typename HFVGeometry<TElem, TDomain::dim>::SCV& scv = get_fvgeom<TElem>().scv(i);

		co = scv.node_id();

		d(_C_, co) += u(_C_, co) * scv.volume();
	}

	return true;
}


template<typename TDomain, typename TAlgebra>
template<typename TElem >
inline
bool
HangingConvectionDiffusionElemDisc<TDomain, TAlgebra>::
assemble_f(local_vector_type& d, number time)
{
	number fvalue = 0.0;
	for(size_t i = 0; i < get_fvgeom<TElem>().num_scv(); ++i)
	{
		const typename HFVGeometry<TElem, TDomain::dim>::SCV& scv = get_fvgeom<TElem>().scv(i);

		m_Rhs(fvalue, scv.global_ip(), time);
		d(_C_, scv.node_id()) += fvalue * scv.volume();
	}

	return true;
}


} // namespace ug


#endif /*__H__LIB_DISCRETIZATION__SPACIAL_DISCRETIZATION__ELEM_DISC__CONVECTION_DIFFUSION__HANGING_FV1__HANGING_CONVECTION_DIFFUSION_IMPL__*/
